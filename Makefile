SHELL := bash
.SHELLFLAGS := -eu -o pipefail -c
.DEFAULT_GOAL := build
.DELETE_ON_ERROR:
PYTHON ?= python3
CURL ?= curl
GAMS_RELEASE_API ?= https://api.github.com/repos/kkgams/gams/releases/latest
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)
DOCS_PORT ?= 8080
WIKI_VERSION := v1.4.0
WIKI_REPOSITORY := https://github.com/justgook/wiki
WIKI_MAKEFILE := build.nosync/wiki-$(WIKI_VERSION).mk
WIKI_ENGINE := $(abspath build.nosync/wiki-$(WIKI_VERSION))
export TMPDIR := $(abspath build.nosync/tmp)
export PYTHONPYCACHEPREFIX := $(abspath build.nosync/pycache)
SOURCE := plugins/image.comp
WIT := $(shell find $(SOURCE)/wit -type f)
WIT_DIRS := $(shell find $(SOURCE)/wit -type d)
LEGAL_DIRS := $(shell find LICENSES -type d)
LEGAL := LICENSE NOTICE LICENSING.md $(sort $(shell find LICENSES -type f \( -iname '*license*' -o -name 'W3C-Community-CLA.html' -o -path 'LICENSES/*' \)))
ARTIFACT := build.nosync/plugin.image.wasm
.PHONY: all build package check clean open docs serve-docs validate-docs
all: build
build: $(ARTIFACT)

build.nosync/tmp:
	@mkdir -p "$@"

BINDINGS := build.nosync/bindings/image_plugin.c build.nosync/bindings/image_plugin.h build.nosync/bindings/image_plugin_component_type.o

$(BINDINGS) &: $(WIT) $(WIT_DIRS) Makefile flake.nix flake.lock | build.nosync/tmp
	mkdir -p build.nosync/bindings
	cd build.nosync/bindings && wit-bindgen c ../../$(SOURCE)/wit -w gams:image/image-plugin@1.0.0

$(ARTIFACT): $(shell find $(SOURCE) -type f -o -type d) $(BINDINGS) $(LEGAL) $(LEGAL_DIRS) scripts/wasm-notices.py Makefile flake.nix flake.lock
	wasm32-wasip2-clang -o "$@" -mexec-model=reactor -Ibuild.nosync/bindings -O2 -DNDEBUG build.nosync/bindings/image_plugin.c $(SOURCE)/component.c build.nosync/bindings/image_plugin_component_type.o -Wl,--strip-all
	wasm-tools strip -a "$@" -o "$@.stripped"
	mv "$@.stripped" "$@"
	$(PYTHON) scripts/wasm-notices.py embed "$@" --license LICENSE --notice NOTICE
	wasm-tools validate "$@"

check: | build.nosync/tmp
	$(PYTHON) -c 'import ast; from pathlib import Path; [ast.parse(p.read_text()) for p in Path("scripts").glob("*.py")]'

package: build.nosync/SHA256SUMS $(ARTIFACT) build.nosync/LICENSES.zip

build.nosync/LICENSES.zip: $(LEGAL) $(LEGAL_DIRS) Makefile | build.nosync/tmp
	rm -f "$@"
	zip -q -X "$@" $(LEGAL)

build.nosync/SHA256SUMS: $(ARTIFACT) build.nosync/LICENSES.zip
	cd build.nosync && shasum -a 256 plugin.image.wasm LICENSES.zip > SHA256SUMS

clean:
	$(PYTHON) -B scripts/clean.py

# Retain an existing app; never update it implicitly.
tools/gams.app: | build.nosync/tmp
	@test "$(UNAME_S)" = Darwin && test "$(UNAME_M)" = arm64 || \
		{ echo 'GAMS release download requires macOS Apple Silicon' >&2; exit 1; }
	@test ! -e tools/gams-release-notices || \
		{ echo 'tools/gams-release-notices already exists; move it aside before reinstalling GAMS' >&2; exit 1; }
	@mkdir -p tools
	@set -eu; stage=$$(mktemp -d "$(TMPDIR)/gams-download.XXXXXX"); \
		trap 'rm -rf "$$stage"' EXIT; \
		$(CURL) --fail --location --silent --show-error "$(GAMS_RELEASE_API)" --output "$$stage/release.json"; \
		url=$$($(PYTHON) -c 'import json, sys; release = json.load(open(sys.argv[1])); assets = [a for a in release["assets"] if a["name"].startswith("GAMS-") and a["name"].endswith("-macos-aarch64.zip")]; assert len(assets) == 1, "Expected exactly one GAMS macOS Apple Silicon ZIP asset"; print(assets[0]["browser_download_url"])' "$$stage/release.json"); \
		$(CURL) --fail --location --silent --show-error "$$url" --output "$$stage/gams.zip"; \
		unzip -q "$$stage/gams.zip" -d "$$stage/unpacked"; \
		test -f "$$stage/unpacked/GAMS.app/Contents/Info.plist"; \
		test -x "$$stage/unpacked/GAMS.app/Contents/MacOS/gams"; \
		mv "$$stage/unpacked/GAMS.app" "$$stage/GAMS.app"; \
		mv "$$stage/unpacked" tools/gams-release-notices; \
		mv "$$stage/GAMS.app" "$@"

open: build tools/gams.app
	GAMS_APP_CWD="$(CURDIR)" GAMS_CONFIG="gams.json" ./tools/gams.app/Contents/MacOS/gams

$(WIKI_MAKEFILE): | build.nosync/tmp
	$(CURL) --fail --location --retry 3 "$(WIKI_REPOSITORY)/raw/$(WIKI_VERSION)/Makefile" -o "$@.tmp"
	mv "$@.tmp" "$@"

docs serve-docs validate-docs: $(WIKI_MAKEFILE) | build.nosync/tmp
	$(MAKE) -f "$(WIKI_MAKEFILE)" "$(if $(filter docs,$@),build,$(patsubst %-docs,%,$@))" WIKI_REPOSITORY="$(WIKI_REPOSITORY)" WIKI_VERSION="$(WIKI_VERSION)" WIKI_ENGINE="$(WIKI_ENGINE)" WIKI_SOURCE="$(abspath docs)" WIKI_OUTPUT="$(abspath build.nosync/site)" PORT="$(DOCS_PORT)"
