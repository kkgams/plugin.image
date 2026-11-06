# plugin.image

GAMS WASM Project Unit for immutable image processing.
Distribution and WIT interface versions are independent.

## API

Exports `gams:image/image@1.0.0` through `image-plugin`.
The API creates/opens images, crops/resizes/transforms/blits them, reads/writes
pixels, and exports/saves PNG or QOI data, including Base64 conversion.
Image values are resources; transformations return image results rather than
requiring callers to edit shared pixel buffers. The Host supplies filesystem
preopens for path access.

The contract is in `plugins/image.comp/wit/package.wit`.
Plugin Manager resolves imports; distribution filenames do not rename WIT identities.

## Build and test

All source/build inputs are owned by this repository; no sibling checkout is
required for its supported build/test commands. Use the pinned Nix environment:

```sh
nix develop --command make build
nix develop --command make test
```

Output: `dist/plugin.image.wasm`. Install it as
`plugins/image.comp.wasm` in an external GAMS Project. Configure that Project separately.

The flake and lockfiles pin the toolchain. Network access may be required to
fetch tools/modules. `.envrc` supports `direnv allow`.

`make test` runs offline integrity/publication regression tests, builds the
components, validates WASM, extracts WIT, and checks the exact input inventory.
It does not exercise the Image API in a standalone WASM runtime.
Build success does not establish operation behavior or Host integration coverage.

## Licensing and releases

GAMS-authored contributions are Apache-2.0. Third-party code retains its own
terms; see `LICENSING.md`, `THIRD-PARTY-REVIEW.md`, and `LICENSES/`.
Source inventory and notice evidence are checked before candidate packaging.
Changed inputs require separately authorized evidence refreshes; source snapshot
verification remains strict and is not refreshed by release tooling.

Distribution is through GitHub Releases, not npm or OCI. Pushing a matching
`v<version.txt>` tag authorizes publication from that commit; no approval digest
variables or required publication branch are used. Manual workflow dispatch
verifies without publishing. Build/tests, canonical repository and tag/version
identity, legal texts and embedded notices, evidence/source inventory, artifact
checksums, candidate integrity, and no-overwrite checks must pass. Never reuse
or move a published or failed tag. An existing release blocks creation.

Enable immutable releases in the repository settings before publication. The
automatic GitHub token is sufficient; no administrative settings API or extra
policy token is used. Public release verification requires `immutable: true`.

The publication workflow rehearses a private draft's bytes before publication
and checks the anonymous public bytes afterward. Keep LICENSE, NOTICE, evidence,
review documents, and applicable LICENSES with distributed artifacts. See the
repository's release workflow and scripts for the enforced gates.

## Local Project and documentation

```sh
nix develop --command make open
make serve-docs                 # http://localhost:8080; override DOCS_PORT
make validate-docs
make docs                       # portable site under build/site/
```

`make open` builds the component and opens this repository's `gams.json` with
explicit `GAMS_APP_CWD`. If absent, `tools/gams.app` is downloaded from the latest
GAMS macOS Apple Silicon release, with its accompanying notices retained.
An existing download is never updated implicitly; Gatekeeper is not bypassed.
No neighboring checkout is required. First launch needs network access for the
Project's pinned published dependencies.

The local Project loads the built plugin and the published Files View to browse
Project files. It is not a plugin-specific editor or an API/runtime test.
The wiki uses pinned `justgook/wiki` v1.4.0, cached under ignored `build/`.
GitHub Pages builds with `make docs` on every push to `release`, independently of
component releases. Set Pages source to **GitHub Actions** in repository settings.
