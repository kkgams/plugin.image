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
Changed inputs require refreshed exact evidence; checksum consistency alone is
not legal or publication approval. On 2026-10-12 the owner confirmed checksum
and licensing preparation complete, dropped pending owner reviews, and authorized
0.2.1 preparation, local commits and local annotated tags. No new legal audit or
hosted validation is claimed; publication safeguards remain enforced.

Distribution is through GitHub Releases, not npm or OCI. Before publishing:

1. Owner checksum/license review is complete per the 2026-10-12 instruction.
   Hosted candidate checks and runtime coverage are separate technical evidence;
   local preparation does not claim they passed.
2. Set repository-scoped `LICENSE_SHA256` and `NOTICE_SHA256` Actions variables
   to the exact reviewed texts. Local rehearsal uses
   `APPROVED_LICENSE_SHA256` and `APPROVED_NOTICE_SHA256`.
3. Push the reviewed `release` branch and inspect its hosted candidate. Without
   approved digests, branch checks do not distribute a candidate. Manual release
   workflow dispatch verifies but does not publish.
4. The owner authorized preparing `v<version.txt>` locally at the tested commit.
   Before pushing it, satisfy the release-branch and hosted-candidate gates above.
   Never reuse or move a published or failed tag. Publication requires immutable-release policy,
   canonical repository/version identity, exact artifacts and checksums, embedded
   notices, and complete readable legal assets. An existing release blocks creation.

The publication workflow rehearses a private draft's bytes before publication
and checks the anonymous public bytes afterward. Keep LICENSE, NOTICE, evidence,
review documents, and applicable LICENSES with distributed artifacts. See the
repository's release workflow and scripts for the enforced gates.
