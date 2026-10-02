#include "image_plugin.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define STBI_ASSERT(x) ((void)0)
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_NO_LINEAR
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define QOI_NO_STDIO
#define QOI_IMPLEMENTATION
#include "qoi.h"

#define STBIR_ASSERT(x) ((void)0)
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

#define STBIW_ASSERT(x) ((void)0)
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

struct exports_gams_image_image_image_t {
  uint32_t width;
  uint32_t height;
  uint8_t *pixels;
};

static void set_error(image_plugin_string_t *err, const char *message) {
  image_plugin_string_dup(err, message);
}

typedef struct image_buffer_t {
  uint8_t *bytes;
  size_t len;
  size_t cap;
} image_buffer_t;

static bool is_qoi_bytes(const uint8_t *bytes, size_t len) {
  static const char magic[] = {'q', 'o', 'i', 'f'};
  return len >= 4 && memcmp(bytes, magic, 4) == 0;
}

static bool is_png_bytes(const uint8_t *bytes, size_t len) {
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  return len >= 8 && memcmp(bytes, sig, 8) == 0;
}

static void set_wasi_error(image_plugin_string_t *err, const char *prefix,
                           wasi_filesystem_types_error_code_t code) {
  char buffer[96];
  const char *name = "unknown";
  switch (code) {
  case WASI_FILESYSTEM_TYPES_ERROR_CODE_ACCESS: name = "access"; break;
  case WASI_FILESYSTEM_TYPES_ERROR_CODE_NO_ENTRY: name = "no-entry"; break;
  case WASI_FILESYSTEM_TYPES_ERROR_CODE_NOT_DIRECTORY: name = "not-directory"; break;
  case WASI_FILESYSTEM_TYPES_ERROR_CODE_IS_DIRECTORY: name = "is-directory"; break;
  case WASI_FILESYSTEM_TYPES_ERROR_CODE_NOT_PERMITTED: name = "not-permitted"; break;
  case WASI_FILESYSTEM_TYPES_ERROR_CODE_READ_ONLY: name = "read-only"; break;
  case WASI_FILESYSTEM_TYPES_ERROR_CODE_EXIST: name = "exist"; break;
  case WASI_FILESYSTEM_TYPES_ERROR_CODE_IO: name = "io"; break;
  default: break;
  }
  size_t prefix_len = strlen(prefix);
  size_t name_len = strlen(name);
  size_t pos = 0;
  if (prefix_len > sizeof(buffer) - 1) prefix_len = sizeof(buffer) - 1;
  memcpy(buffer + pos, prefix, prefix_len);
  pos += prefix_len;
  if (pos + 2 < sizeof(buffer)) {
    buffer[pos++] = ':';
    buffer[pos++] = ' ';
  }
  if (name_len > sizeof(buffer) - 1 - pos) name_len = sizeof(buffer) - 1 - pos;
  memcpy(buffer + pos, name, name_len);
  pos += name_len;
  buffer[pos] = '\0';
  set_error(err, buffer);
}

static bool string_eq_bytes(const uint8_t *ptr, size_t len, const char *literal) {
  size_t literal_len = strlen(literal);
  return len == literal_len && memcmp(ptr, literal, len) == 0;
}

static bool preopen_matches_path(const image_plugin_string_t *preopen_path,
                                 const image_plugin_string_t *path,
                                 size_t *relative_offset) {
  if (string_eq_bytes(preopen_path->ptr, preopen_path->len, "/")) {
    if (path->len == 0 || path->ptr[0] != '/') return false;
    *relative_offset = 1;
    return true;
  }
  if (path->len < preopen_path->len ||
      memcmp(path->ptr, preopen_path->ptr, preopen_path->len) != 0) {
    return false;
  }
  if (path->len == preopen_path->len) {
    *relative_offset = path->len;
    return true;
  }
  if (path->ptr[preopen_path->len] != '/') return false;
  *relative_offset = preopen_path->len + 1;
  return true;
}

static bool resolve_wasi_path(
    image_plugin_string_t *path,
    wasi_filesystem_preopens_list_tuple2_own_descriptor_string_t *preopens,
    wasi_filesystem_types_borrow_descriptor_t *ret_base,
    image_plugin_string_t *ret_relative,
    image_plugin_string_t *err) {
  wasi_filesystem_preopens_get_directories(preopens);
  if (preopens->len == 0) {
    set_error(err, "no wasi filesystem preopens");
    return false;
  }
  if (path->len == 0) {
    wasi_filesystem_preopens_list_tuple2_own_descriptor_string_free(preopens);
    set_error(err, "invalid image path");
    return false;
  }

  bool is_absolute = path->ptr[0] == '/';
  bool found = false;
  size_t index = 0;
  size_t offset = 0;
  size_t best_len = 0;

  for (size_t i = 0; i < preopens->len; i++) {
    image_plugin_string_t *guest = &preopens->ptr[i].f1;
    bool guest_is_absolute = guest->len > 0 && guest->ptr[0] == '/';
    if (string_eq_bytes(guest->ptr, guest->len, ".") ||
        guest_is_absolute != is_absolute) {
      continue;
    }
    size_t candidate_offset = 0;
    if (!preopen_matches_path(guest, path, &candidate_offset)) continue;
    if (!found || guest->len > best_len) {
      found = true;
      index = i;
      offset = candidate_offset;
      best_len = guest->len;
    }
  }

  if (!found && !is_absolute) {
    for (size_t i = 0; i < preopens->len; i++) {
      image_plugin_string_t *guest = &preopens->ptr[i].f1;
      if (string_eq_bytes(guest->ptr, guest->len, ".")) {
        found = true;
        index = i;
        offset = 0;
        break;
      }
    }
  }

  if (!found || path->len <= offset) {
    wasi_filesystem_preopens_list_tuple2_own_descriptor_string_free(preopens);
    set_error(err, found ? "invalid image path" : "image path is not under a WASI preopen");
    return false;
  }

  *ret_base = wasi_filesystem_types_borrow_descriptor(preopens->ptr[index].f0);
  ret_relative->ptr = path->ptr + offset;
  ret_relative->len = path->len - offset;
  return true;
}

static bool append_bytes(image_plugin_list_u8_t *buffer, image_plugin_list_u8_t *chunk) {
  if (chunk->len == 0) return true;
  if (buffer->len > SIZE_MAX - chunk->len) return false;
  uint8_t *next = realloc(buffer->ptr, buffer->len + chunk->len);
  if (!next) return false;
  memcpy(next + buffer->len, chunk->ptr, chunk->len);
  buffer->ptr = next;
  buffer->len += chunk->len;
  return true;
}

static bool read_all_bytes(image_plugin_string_t *path, image_plugin_list_u8_t *ret, image_plugin_string_t *err) {
  ret->ptr = NULL;
  ret->len = 0;

  wasi_filesystem_preopens_list_tuple2_own_descriptor_string_t preopens;
  wasi_filesystem_types_borrow_descriptor_t base;
  image_plugin_string_t relative;
  if (!resolve_wasi_path(path, &preopens, &base, &relative, err)) return false;

  wasi_filesystem_types_own_descriptor_t file;
  wasi_filesystem_types_error_code_t code = 0;
  if (!wasi_filesystem_types_method_descriptor_open_at(
          base, 0, &relative, 0, WASI_FILESYSTEM_TYPES_DESCRIPTOR_FLAGS_READ,
          &file, &code)) {
    wasi_filesystem_preopens_list_tuple2_own_descriptor_string_free(&preopens);
    set_wasi_error(err, "failed to open image file", code);
    return false;
  }

  wasi_filesystem_types_borrow_descriptor_t borrowed = wasi_filesystem_types_borrow_descriptor(file);
  uint64_t offset = 0;
  bool eof = false;
  while (!eof) {
    image_plugin_tuple2_list_u8_bool_t chunk;
    code = 0;
    if (!wasi_filesystem_types_method_descriptor_read(borrowed, 65536u, offset, &chunk, &code)) {
      wasi_filesystem_types_descriptor_drop_own(file);
      wasi_filesystem_preopens_list_tuple2_own_descriptor_string_free(&preopens);
      free(ret->ptr);
      ret->ptr = NULL;
      ret->len = 0;
      set_wasi_error(err, "failed to read image file", code);
      return false;
    }
    if (!append_bytes(ret, &chunk.f0)) {
      image_plugin_list_u8_free(&chunk.f0);
      wasi_filesystem_types_descriptor_drop_own(file);
      wasi_filesystem_preopens_list_tuple2_own_descriptor_string_free(&preopens);
      free(ret->ptr);
      ret->ptr = NULL;
      ret->len = 0;
      set_error(err, "out of memory");
      return false;
    }
    offset += chunk.f0.len;
    eof = chunk.f1;
    image_plugin_list_u8_free(&chunk.f0);
  }

  wasi_filesystem_types_descriptor_drop_own(file);
  wasi_filesystem_preopens_list_tuple2_own_descriptor_string_free(&preopens);
  return true;
}

static bool write_all_bytes(image_plugin_string_t *path, const uint8_t *data, size_t len, image_plugin_string_t *err) {
  wasi_filesystem_preopens_list_tuple2_own_descriptor_string_t preopens;
  wasi_filesystem_types_borrow_descriptor_t base;
  image_plugin_string_t relative;
  if (!resolve_wasi_path(path, &preopens, &base, &relative, err)) return false;

  wasi_filesystem_types_own_descriptor_t file;
  wasi_filesystem_types_error_code_t code = 0;
  if (!wasi_filesystem_types_method_descriptor_open_at(
          base, 0, &relative,
          WASI_FILESYSTEM_TYPES_OPEN_FLAGS_CREATE | WASI_FILESYSTEM_TYPES_OPEN_FLAGS_TRUNCATE,
          WASI_FILESYSTEM_TYPES_DESCRIPTOR_FLAGS_WRITE, &file, &code)) {
    wasi_filesystem_preopens_list_tuple2_own_descriptor_string_free(&preopens);
    set_wasi_error(err, "failed to open output image file", code);
    return false;
  }

  wasi_filesystem_types_borrow_descriptor_t borrowed = wasi_filesystem_types_borrow_descriptor(file);
  size_t offset = 0;
  while (offset < len) {
    image_plugin_list_u8_t chunk = {
      .ptr = (uint8_t *)data + offset,
      .len = len - offset,
    };
    wasi_filesystem_types_filesize_t written = 0;
    code = 0;
    if (!wasi_filesystem_types_method_descriptor_write(borrowed, &chunk, offset, &written, &code)) {
      wasi_filesystem_types_descriptor_drop_own(file);
      wasi_filesystem_preopens_list_tuple2_own_descriptor_string_free(&preopens);
      set_wasi_error(err, "failed to write image file", code);
      return false;
    }
    if (written == 0) {
      wasi_filesystem_types_descriptor_drop_own(file);
      wasi_filesystem_preopens_list_tuple2_own_descriptor_string_free(&preopens);
      set_error(err, "write returned zero bytes");
      return false;
    }
    offset += (size_t)written;
  }

  code = 0;
  if (!wasi_filesystem_types_method_descriptor_sync(borrowed, &code)) {
    wasi_filesystem_types_descriptor_drop_own(file);
    wasi_filesystem_preopens_list_tuple2_own_descriptor_string_free(&preopens);
    set_wasi_error(err, "failed to sync image file", code);
    return false;
  }

  wasi_filesystem_types_descriptor_drop_own(file);
  wasi_filesystem_preopens_list_tuple2_own_descriptor_string_free(&preopens);
  return true;
}

static bool image_buffer_reserve(image_buffer_t *buffer, size_t need) {
  if (need <= buffer->cap) return true;
  size_t cap = buffer->cap == 0 ? 256u : buffer->cap;
  while (cap < need) {
    if (cap > SIZE_MAX / 2u) return false;
    cap *= 2u;
  }
  uint8_t *next = realloc(buffer->bytes, cap);
  if (!next) return false;
  buffer->bytes = next;
  buffer->cap = cap;
  return true;
}

static void image_stbi_write_func(void *context, void *data, int size) {
  image_buffer_t *buffer = context;
  if (!buffer || size <= 0) return;
  if (!image_buffer_reserve(buffer, buffer->len + (size_t)size)) {
    buffer->len = SIZE_MAX;
    return;
  }
  memcpy(buffer->bytes + buffer->len, data, (size_t)size);
  buffer->len += (size_t)size;
}

static bool checked_len(uint32_t width, uint32_t height, size_t *len, image_plugin_string_t *err) {
  if (width == 0 || height == 0) {
    set_error(err, "image dimensions must be non-zero");
    return false;
  }
  size_t pixels = (size_t)width * (size_t)height;
  if (pixels / width != height || pixels > SIZE_MAX / 4u) {
    set_error(err, "image dimensions are too large");
    return false;
  }
  *len = pixels * 4u;
  return true;
}

static exports_gams_image_image_image_t *alloc_image(uint32_t width, uint32_t height, image_plugin_string_t *err) {
  size_t len = 0;
  if (!checked_len(width, height, &len, err)) return NULL;

  exports_gams_image_image_image_t *image = calloc(1, sizeof(*image));
  if (!image) {
    set_error(err, "out of memory");
    return NULL;
  }
  image->pixels = len == 0 ? NULL : malloc(len);
  if (!image->pixels) {
    free(image);
    set_error(err, "out of memory");
    return NULL;
  }
  image->width = width;
  image->height = height;
  return image;
}

static exports_gams_image_image_image_t *image_from_pixels(uint8_t *pixels, uint32_t width, uint32_t height, image_plugin_string_t *err) {
  size_t len = 0;
  if (!checked_len(width, height, &len, err)) return NULL;
  exports_gams_image_image_image_t *image = calloc(1, sizeof(*image));
  if (!image) {
    free(pixels);
    set_error(err, "out of memory");
    return NULL;
  }
  image->width = width;
  image->height = height;
  image->pixels = pixels;
  return image;
}

static bool return_image(exports_gams_image_image_image_t *image, exports_gams_image_image_own_image_t *ret, image_plugin_string_t *err) {
  if (!image) return false;
  *ret = exports_gams_image_image_image_new(image);
  if (ret->__handle == 0) {
    exports_gams_image_image_image_destructor(image);
    set_error(err, "failed to create image resource");
    return false;
  }
  return true;
}

static size_t pixel_offset(const exports_gams_image_image_image_t *image, uint32_t x, uint32_t y) {
  return (((size_t)y * image->width) + x) * 4u;
}

static void load_pixel(const exports_gams_image_image_image_t *image, uint32_t x, uint32_t y, exports_gams_image_image_rgba8_t *out) {
  const uint8_t *p = image->pixels + pixel_offset(image, x, y);
  out->r = p[0];
  out->g = p[1];
  out->b = p[2];
  out->a = p[3];
}

static void store_pixel(exports_gams_image_image_image_t *image, uint32_t x, uint32_t y, const exports_gams_image_image_rgba8_t *color) {
  uint8_t *p = image->pixels + pixel_offset(image, x, y);
  p[0] = color->r;
  p[1] = color->g;
  p[2] = color->b;
  p[3] = color->a;
}

static exports_gams_image_image_image_t *clone_image(exports_gams_image_image_borrow_image_t src, image_plugin_string_t *err) {
  exports_gams_image_image_image_t *out = alloc_image(src->width, src->height, err);
  if (!out) return NULL;
  memcpy(out->pixels, src->pixels, (size_t)src->width * src->height * 4u);
  return out;
}

static exports_gams_image_image_image_t *decode_image_bytes(const uint8_t *bytes, size_t len, image_plugin_string_t *err) {
  if (is_qoi_bytes(bytes, len)) {
    qoi_desc desc;
    void *pixels = qoi_decode(bytes, (int)len, &desc, 4);
    if (!pixels) {
      set_error(err, "failed to decode qoi image");
      return NULL;
    }
    return image_from_pixels(pixels, desc.width, desc.height, err);
  }

  if (is_png_bytes(bytes, len)) {
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc *pixels = stbi_load_from_memory(bytes, (int)len, &width, &height, &channels, 4);
    (void)channels;
    if (!pixels) {
      set_error(err, "failed to decode png image");
      return NULL;
    }
    return image_from_pixels(pixels, (uint32_t)width, (uint32_t)height, err);
  }

  set_error(err, "unsupported image format");
  return NULL;
}

static int base64_value(uint8_t c) {
  if (c >= 'A' && c <= 'Z') return (int)(c - 'A');
  if (c >= 'a' && c <= 'z') return (int)(c - 'a' + 26);
  if (c >= '0' && c <= '9') return (int)(c - '0' + 52);
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

static bool is_base64_space(uint8_t c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static bool base64_encode_bytes(const uint8_t *bytes, size_t len, image_plugin_string_t *ret, image_plugin_string_t *err) {
  static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  ret->ptr = NULL;
  ret->len = 0;

  if (len > ((SIZE_MAX / 4u) * 3u) - 2u) {
    set_error(err, "base64 input is too large");
    return false;
  }
  size_t out_len = ((len + 2u) / 3u) * 4u;
  char *out = out_len == 0 ? NULL : malloc(out_len);
  if (out_len > 0 && !out) {
    set_error(err, "out of memory");
    return false;
  }

  size_t in = 0;
  size_t pos = 0;
  while (in < len) {
    size_t remaining = len - in;
    uint32_t a = bytes[in++];
    uint32_t b = remaining > 1u ? bytes[in++] : 0;
    uint32_t c = remaining > 2u ? bytes[in++] : 0;
    uint32_t triple = (a << 16) | (b << 8) | c;
    out[pos++] = alphabet[(triple >> 18) & 0x3f];
    out[pos++] = alphabet[(triple >> 12) & 0x3f];
    out[pos++] = remaining > 1u ? alphabet[(triple >> 6) & 0x3f] : '=';
    out[pos++] = remaining > 2u ? alphabet[triple & 0x3f] : '=';
  }

  image_plugin_string_dup_n(ret, out, out_len);
  free(out);
  return true;
}

static bool base64_decode_string(image_plugin_string_t *encoded, image_plugin_list_u8_t *ret, image_plugin_string_t *err) {
  ret->ptr = NULL;
  ret->len = 0;

  size_t clean_len = 0;
  for (size_t i = 0; i < encoded->len; i++) {
    if (!is_base64_space(encoded->ptr[i])) clean_len++;
  }
  if (clean_len % 4u != 0) {
    set_error(err, "invalid base64 length");
    return false;
  }

  uint8_t *clean = clean_len == 0 ? NULL : malloc(clean_len);
  if (clean_len > 0 && !clean) {
    set_error(err, "out of memory");
    return false;
  }
  size_t pos = 0;
  for (size_t i = 0; i < encoded->len; i++) {
    if (!is_base64_space(encoded->ptr[i])) clean[pos++] = encoded->ptr[i];
  }

  size_t padding = 0;
  if (clean_len >= 1 && clean[clean_len - 1] == '=') padding++;
  if (clean_len >= 2 && clean[clean_len - 2] == '=') padding++;
  for (size_t i = 0; i + padding < clean_len; i++) {
    if (clean[i] == '=') {
      free(clean);
      set_error(err, "invalid base64 padding");
      return false;
    }
  }
  if (padding > 2) {
    free(clean);
    set_error(err, "invalid base64 padding");
    return false;
  }

  size_t out_len = (clean_len / 4u) * 3u;
  if (out_len < padding) {
    free(clean);
    set_error(err, "invalid base64 padding");
    return false;
  }
  out_len -= padding;
  uint8_t *out = out_len == 0 ? NULL : malloc(out_len);
  if (out_len > 0 && !out) {
    free(clean);
    set_error(err, "out of memory");
    return false;
  }

  size_t out_pos = 0;
  for (size_t i = 0; i < clean_len; i += 4u) {
    int v0 = base64_value(clean[i]);
    int v1 = base64_value(clean[i + 1]);
    int v2 = clean[i + 2] == '=' ? 0 : base64_value(clean[i + 2]);
    int v3 = clean[i + 3] == '=' ? 0 : base64_value(clean[i + 3]);
    if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) {
      free(out);
      free(clean);
      set_error(err, "invalid base64 character");
      return false;
    }
    uint32_t triple = ((uint32_t)v0 << 18) | ((uint32_t)v1 << 12) | ((uint32_t)v2 << 6) | (uint32_t)v3;
    if (out_pos < out_len) out[out_pos++] = (uint8_t)((triple >> 16) & 0xff);
    if (out_pos < out_len) out[out_pos++] = (uint8_t)((triple >> 8) & 0xff);
    if (out_pos < out_len) out[out_pos++] = (uint8_t)(triple & 0xff);
  }

  free(clean);
  ret->ptr = out;
  ret->len = out_len;
  return true;
}

static bool encode_image(exports_gams_image_image_borrow_image_t src, exports_gams_image_image_image_format_t format, image_plugin_list_u8_t *ret, image_plugin_string_t *err) {
  ret->ptr = NULL;
  ret->len = 0;

  if (format == EXPORTS_GAMS_IMAGE_IMAGE_IMAGE_FORMAT_QOI) {
    qoi_desc desc;
    int encoded_len = 0;
    desc.width = src->width;
    desc.height = src->height;
    desc.channels = 4;
    desc.colorspace = QOI_SRGB;
    void *encoded = qoi_encode(src->pixels, &desc, &encoded_len);
    if (!encoded || encoded_len <= 0) {
      set_error(err, "failed to encode qoi image");
      return false;
    }
    ret->ptr = encoded;
    ret->len = (size_t)encoded_len;
    return true;
  }

  if (format == EXPORTS_GAMS_IMAGE_IMAGE_IMAGE_FORMAT_PNG) {
    image_buffer_t buffer = {0};
    if (!stbi_write_png_to_func(image_stbi_write_func, &buffer, (int)src->width, (int)src->height, 4, src->pixels, (int)src->width * 4)) {
      free(buffer.bytes);
      set_error(err, "failed to encode png image");
      return false;
    }
    if (!buffer.bytes || buffer.len == 0 || buffer.len == SIZE_MAX) {
      free(buffer.bytes);
      set_error(err, "failed to encode png image");
      return false;
    }
    ret->ptr = buffer.bytes;
    ret->len = buffer.len;
    return true;
  }

  set_error(err, "unsupported image format");
  return false;
}

bool exports_gams_image_image_open(image_plugin_string_t *path, exports_gams_image_image_own_image_t *ret, image_plugin_string_t *err) {
  image_plugin_list_u8_t bytes;
  if (!read_all_bytes(path, &bytes, err)) return false;
  exports_gams_image_image_image_t *image = decode_image_bytes(bytes.ptr, bytes.len, err);
  free(bytes.ptr);
  return return_image(image, ret, err);
}

bool exports_gams_image_image_create(uint32_t width, uint32_t height, exports_gams_image_image_rgba8_t *maybe_fill, exports_gams_image_image_own_image_t *ret, image_plugin_string_t *err) {
  exports_gams_image_image_image_t *image = alloc_image(width, height, err);
  if (!image) return false;

  exports_gams_image_image_rgba8_t fill = {0, 0, 0, 0};
  if (maybe_fill) fill = *maybe_fill;
  for (uint32_t y = 0; y < height; y++) {
    for (uint32_t x = 0; x < width; x++) {
      store_pixel(image, x, y, &fill);
    }
  }
  return return_image(image, ret, err);
}

bool exports_gams_image_image_info(exports_gams_image_image_borrow_image_t src, exports_gams_image_image_image_info_t *ret, image_plugin_string_t *err) {
  (void)err;
  ret->width = src->width;
  ret->height = src->height;
  ret->pixel_format = EXPORTS_GAMS_IMAGE_IMAGE_PIXEL_FORMAT_RGBA8;
  return true;
}

bool exports_gams_image_image_clone(exports_gams_image_image_borrow_image_t src, exports_gams_image_image_own_image_t *ret, image_plugin_string_t *err) {
  return return_image(clone_image(src, err), ret, err);
}

bool exports_gams_image_image_crop(exports_gams_image_image_borrow_image_t src, exports_gams_image_image_rect_t *rect, exports_gams_image_image_own_image_t *ret, image_plugin_string_t *err) {
  if (rect->x0 == rect->x1 || rect->y0 == rect->y1) {
    set_error(err, "crop rectangle must have non-zero width and height");
    return false;
  }

  uint32_t width = (uint32_t)(rect->x1 > rect->x0 ? rect->x1 - rect->x0 : rect->x0 - rect->x1);
  uint32_t height = (uint32_t)(rect->y1 > rect->y0 ? rect->y1 - rect->y0 : rect->y0 - rect->y1);
  exports_gams_image_image_image_t *out = alloc_image(width, height, err);
  if (!out) return false;

  for (uint32_t y = 0; y < height; y++) {
    int32_t sy = rect->y1 > rect->y0 ? rect->y0 + (int32_t)y : rect->y0 - 1 - (int32_t)y;
    for (uint32_t x = 0; x < width; x++) {
      int32_t sx = rect->x1 > rect->x0 ? rect->x0 + (int32_t)x : rect->x0 - 1 - (int32_t)x;
      if (sx < 0 || sy < 0 || sx >= (int32_t)src->width || sy >= (int32_t)src->height) {
        exports_gams_image_image_image_destructor(out);
        set_error(err, "crop rectangle is out of bounds");
        return false;
      }
      exports_gams_image_image_rgba8_t color;
      load_pixel(src, (uint32_t)sx, (uint32_t)sy, &color);
      store_pixel(out, x, y, &color);
    }
  }
  return return_image(out, ret, err);
}

bool exports_gams_image_image_resize(exports_gams_image_image_borrow_image_t src, uint32_t width, uint32_t height, exports_gams_image_image_resize_filter_t filter, exports_gams_image_image_own_image_t *ret, image_plugin_string_t *err) {
  exports_gams_image_image_image_t *out = alloc_image(width, height, err);
  if (!out) return false;

  stbir_filter stbir_filter = STBIR_FILTER_TRIANGLE;
  if (filter == EXPORTS_GAMS_IMAGE_IMAGE_RESIZE_FILTER_NEAREST) {
    stbir_filter = STBIR_FILTER_POINT_SAMPLE;
  } else if (filter == EXPORTS_GAMS_IMAGE_IMAGE_RESIZE_FILTER_CATMULLROM) {
    stbir_filter = STBIR_FILTER_CATMULLROM;
  }

  void *resize_result = stbir_resize(src->pixels, (int)src->width, (int)src->height, 0,
                                     out->pixels, (int)width, (int)height, 0,
                                     STBIR_RGBA, STBIR_TYPE_UINT8,
                                     STBIR_EDGE_CLAMP, stbir_filter);
  if (!resize_result) {
    exports_gams_image_image_image_destructor(out);
    set_error(err, "resize operation failed");
    return false;
  }
  return return_image(out, ret, err);
}

bool exports_gams_image_image_transform(exports_gams_image_image_borrow_image_t src, exports_gams_image_image_transform_flags_t flip, exports_gams_image_image_own_image_t *ret, image_plugin_string_t *err) {
  bool diagonal = (flip & EXPORTS_GAMS_IMAGE_IMAGE_TRANSFORM_FLAGS_DIAGONAL) != 0;
  bool horizontal = (flip & EXPORTS_GAMS_IMAGE_IMAGE_TRANSFORM_FLAGS_HORIZONTAL) != 0;
  bool vertical = (flip & EXPORTS_GAMS_IMAGE_IMAGE_TRANSFORM_FLAGS_VERTICAL) != 0;
  uint32_t width = diagonal ? src->height : src->width;
  uint32_t height = diagonal ? src->width : src->height;
  exports_gams_image_image_image_t *out = alloc_image(width, height, err);
  if (!out) return false;

  for (uint32_t y = 0; y < height; y++) {
    for (uint32_t x = 0; x < width; x++) {
      uint32_t tx = horizontal ? (width - 1 - x) : x;
      uint32_t ty = vertical ? (height - 1 - y) : y;
      uint32_t sx = diagonal ? ty : tx;
      uint32_t sy = diagonal ? tx : ty;
      exports_gams_image_image_rgba8_t color;
      load_pixel(src, sx, sy, &color);
      store_pixel(out, x, y, &color);
    }
  }
  return return_image(out, ret, err);
}

static void blit_image(exports_gams_image_image_image_t *dst,
                       exports_gams_image_image_borrow_image_t src,
                       const exports_gams_image_image_point_t *at) {
  for (uint32_t sy = 0; sy < src->height; sy++) {
    int32_t dy = at->y + (int32_t)sy;
    if (dy < 0 || dy >= (int32_t)dst->height) continue;
    for (uint32_t sx = 0; sx < src->width; sx++) {
      int32_t dx = at->x + (int32_t)sx;
      if (dx < 0 || dx >= (int32_t)dst->width) continue;
      exports_gams_image_image_rgba8_t s;
      exports_gams_image_image_rgba8_t d;
      load_pixel(src, sx, sy, &s);
      load_pixel(dst, (uint32_t)dx, (uint32_t)dy, &d);
      uint32_t inv_a = 255u - s.a;
      exports_gams_image_image_rgba8_t blended = {
          .r = (uint8_t)((s.r * s.a + d.r * inv_a) / 255u),
          .g = (uint8_t)((s.g * s.a + d.g * inv_a) / 255u),
          .b = (uint8_t)((s.b * s.a + d.b * inv_a) / 255u),
          .a = (uint8_t)(s.a + (d.a * inv_a) / 255u),
      };
      store_pixel(dst, (uint32_t)dx, (uint32_t)dy, &blended);
    }
  }
}

bool exports_gams_image_image_blit(exports_gams_image_image_borrow_image_t dst, exports_gams_image_image_borrow_image_t src, exports_gams_image_image_point_t *at, exports_gams_image_image_own_image_t *ret, image_plugin_string_t *err) {
  exports_gams_image_image_image_t *out = clone_image(dst, err);
  if (!out) return false;
  blit_image(out, src, at);
  return return_image(out, ret, err);
}

bool exports_gams_image_image_blit_many(exports_gams_image_image_borrow_image_t dst, exports_gams_image_image_list_blit_operation_t *operations, exports_gams_image_image_own_image_t *ret, image_plugin_string_t *err) {
  exports_gams_image_image_image_t *out = clone_image(dst, err);
  if (!out) return false;
  for (size_t index = 0; index < operations->len; index++) {
    exports_gams_image_image_blit_operation_t *operation = &operations->ptr[index];
    blit_image(out, operation->src, &operation->at);
  }
  return return_image(out, ret, err);
}

bool exports_gams_image_image_write_pixel(exports_gams_image_image_borrow_image_t src, exports_gams_image_image_point_t *at, exports_gams_image_image_rgba8_t *color, exports_gams_image_image_own_image_t *ret, image_plugin_string_t *err) {
  if (at->x < 0 || at->y < 0 || at->x >= (int32_t)src->width || at->y >= (int32_t)src->height) {
    set_error(err, "pixel coordinate is out of bounds");
    return false;
  }
  exports_gams_image_image_image_t *out = clone_image(src, err);
  if (!out) return false;
  store_pixel(out, (uint32_t)at->x, (uint32_t)at->y, color);
  return return_image(out, ret, err);
}

bool exports_gams_image_image_from_pixels(uint32_t width, uint32_t height, exports_gams_image_image_pixel_format_t pixel_format, image_plugin_list_u8_t *data, exports_gams_image_image_own_image_t *ret, image_plugin_string_t *err) {
  if (pixel_format != EXPORTS_GAMS_IMAGE_IMAGE_PIXEL_FORMAT_RGBA8) {
    set_error(err, "only rgba8 pixel data is supported");
    return false;
  }
  size_t len = 0;
  if (!checked_len(width, height, &len, err)) return false;
  if (data->len != len) {
    set_error(err, "pixel data length must equal width * height * 4");
    return false;
  }
  exports_gams_image_image_image_t *image = alloc_image(width, height, err);
  if (!image) return false;
  memcpy(image->pixels, data->ptr, len);
  return return_image(image, ret, err);
}

bool exports_gams_image_image_read_pixel(exports_gams_image_image_borrow_image_t src, exports_gams_image_image_point_t *at, exports_gams_image_image_rgba8_t *ret, image_plugin_string_t *err) {
  if (at->x < 0 || at->y < 0 || at->x >= (int32_t)src->width || at->y >= (int32_t)src->height) {
    set_error(err, "pixel coordinate is out of bounds");
    return false;
  }
  load_pixel(src, (uint32_t)at->x, (uint32_t)at->y, ret);
  return true;
}

bool exports_gams_image_image_read_pixels(exports_gams_image_image_borrow_image_t src, image_plugin_list_u8_t *ret, image_plugin_string_t *err) {
  size_t len = 0;
  if (!checked_len(src->width, src->height, &len, err)) return false;
  ret->ptr = len == 0 ? NULL : malloc(len);
  if (!ret->ptr) {
    ret->len = 0;
    set_error(err, "out of memory");
    return false;
  }
  memcpy(ret->ptr, src->pixels, len);
  ret->len = len;
  return true;
}

bool exports_gams_image_image_export(exports_gams_image_image_borrow_image_t src, exports_gams_image_image_image_format_t format, image_plugin_list_u8_t *ret, image_plugin_string_t *err) {
  return encode_image(src, format, ret, err);
}

bool exports_gams_image_image_decode_base64(image_plugin_string_t *encoded, exports_gams_image_image_own_image_t *ret, image_plugin_string_t *err) {
  image_plugin_list_u8_t bytes;
  if (!base64_decode_string(encoded, &bytes, err)) return false;
  exports_gams_image_image_image_t *image = decode_image_bytes(bytes.ptr, bytes.len, err);
  free(bytes.ptr);
  return return_image(image, ret, err);
}

bool exports_gams_image_image_encode_base64(exports_gams_image_image_borrow_image_t src, exports_gams_image_image_image_format_t format, image_plugin_string_t *ret, image_plugin_string_t *err) {
  image_plugin_list_u8_t bytes;
  if (!encode_image(src, format, &bytes, err)) return false;
  bool ok = base64_encode_bytes(bytes.ptr, bytes.len, ret, err);
  free(bytes.ptr);
  return ok;
}

bool exports_gams_image_image_save(exports_gams_image_image_borrow_image_t src, image_plugin_string_t *path, exports_gams_image_image_image_format_t format, uint64_t *ret, image_plugin_string_t *err) {
  image_plugin_list_u8_t bytes;
  if (!encode_image(src, format, &bytes, err)) return false;
  bool ok = write_all_bytes(path, bytes.ptr, bytes.len, err);
  if (ok) *ret = bytes.len;
  free(bytes.ptr);
  return ok;
}

void exports_gams_image_image_image_destructor(exports_gams_image_image_image_t *rep) {
  if (!rep) return;
  free(rep->pixels);
  free(rep);
}
