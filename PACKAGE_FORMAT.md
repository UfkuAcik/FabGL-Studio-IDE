# Package and distribution formats

FabGL Studio uses three different packaging concepts. They are intentionally not interchangeable.

## Product distribution

CPack stages installed host binaries, documentation, examples, manifests, and source notices.
The always-available Windows artifact is:

```text
FabGL-Studio-<version>-Windows-<architecture>.zip
```

Extract the ZIP to a normal user-writable directory and run programs from `bin/`. The ZIP does
not contain cached downloads, compiler build trees, test output, or a preinstalled Arduino/ESP32
toolchain. The pinned manifest and firmware source are included so toolchain setup remains
reproducible.

An NSIS `.exe` installer is generated only when Windows packaging is configured with a usable
`makensis`. It installs the same staged files and is not promised on machines without NSIS.
Neither artifact is code-signed in this development snapshot.

## Runtime asset pack (`.fglpack`)

`.fglpack` is generated game data, not an installable plugin. Version 1 is little-endian and
starts with magic `FGLP`, version, entry count, index size, and build checksum. The sorted fixed-
size index records GUID, type ID, storage class, flags, payload offset, stored/decoded sizes, and
payload checksum. Payload offsets are aligned. The reader validates magic, version, integer
ranges, sort order, GUID uniqueness, alignment, and FNV-1a checksums before exposing entries.

Create and verify one with:

```powershell
fabgl_asset_compiler pack build-assets.txt game.fglpack
fabgl_asset_compiler inspect game.fglpack
```

Each manifest line is `GUID type-id storage-class "payload path"`; storage is `flash`, `ram`,
`psram`, or `sd`. Rebuild packs from source instead of editing binary bytes.

## Local development package

A local engine package is currently a directory plus the key/value manifest documented in
`PLUGIN_DEVELOPMENT.md`. It has no standardized archive extension. The implemented registry
performs semantic-version, dependency-cycle, path, and executable-trust validation in memory.
Installation, removal, archive extraction, signatures, lockfiles, and online resolution are
not implemented.

## Release production

```powershell
cmake --preset release
cmake --build --preset release
ctest --preset release
powershell -ExecutionPolicy Bypass -File packaging/build-packages.ps1
```

`packaging/build-packages.ps1` stages into `out/install/release`, always asks CPack for ZIP, and
requests NSIS only when `makensis` is present. `packaging/smoke-test.ps1` checks the staged CLI
programs and validates one bundled example. A clean-machine graphical smoke test still requires
the Qt runtime to be staged and is a release gate rather than an achieved result.

Release checksums should be produced after packaging, published beside artifacts, and recorded
with the Git tag. They are not embedded in this source document because artifacts are rebuilds.

