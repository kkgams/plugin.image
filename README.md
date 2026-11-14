# plugin.image

A [GAMS](https://github.com/kkgams/gams) WASM plugin for reading immutable image processing.

| Unit | Source / artifact | Screenshot | License | Original work |
| --- | --- | --- | --- | --- |
| Image processor | `plugins/image.comp/component.c` / `plugin.image.wasm` | Not applicable | [Apache-2.0](LICENSE), [third-party terms](LICENSING.md) | GAMS; [stb by Sean Barrett](https://github.com/nothings/stb), [QOI by Dominic Szablewski](https://github.com/phoboslab/qoi) |

## API

`plugins/image.comp/wit/package.wit` defines `gams:image/image-plugin@1.0.0`: create/open resource images, crop, resize, transform, blit, read/write pixels, export PNG/QOI and convert Base64. Path access uses Host-provided WASI preopens; operations return WIT results for errors.

## Development

Use `nix develop` (or direnv) for pinned WASI SDK 33, wit-bindgen, wasm-tools and documentation tools. No sibling checkout is required.

| Command | Purpose |
| --- | --- |
| `make build` | Compile and validate the component with embedded notices. |
| `make package` | Prepare `build.nosync/plugin.image.wasm`, `LICENSES.zip` and `SHA256SUMS` incrementally. |
| `make check` | Check Python tooling syntax; no API runtime tests are provided. |
| `make open` | Build and open this local Project in the released macOS Apple Silicon Host. |
| `make docs` | Build the wiki to `build.nosync/site`. |
| `make serve-docs` | Preview the wiki; `DOCS_PORT` defaults to 8080. |
| `make validate-docs` | Validate wiki content. |
| `make clean` | Remove `build.nosync` and identified Host/Unit downloads; refuse tracked files, symlinks and unidentified supplied tools/caches. |

The local Project browses files with pinned published Units; it is not an image editor or runtime test. Downloads preserve accompanying notices and existing Host tools. Packaging does not verify native Host/API behavior.

Distribute the readable `LICENSES.zip` terms together with the raw WASM.
