PLUGIN_WASM_COMPONENT := 1
PLUGIN_WIT_WORLD := gams:image/image-plugin@1.0.0
PLUGIN_COMPONENT_NAME := image_plugin
PLUGIN_COMPONENT_SOURCES := $(PLUGIN_PATH)/component.c
PLUGIN_COMPONENT_EXTRA_DEPS := \
	$(PLUGIN_PATH)/qoi.h \
	$(PLUGIN_PATH)/stb_image.h \
	$(PLUGIN_PATH)/stb_image_resize2.h \
	$(PLUGIN_PATH)/stb_image_write.h
