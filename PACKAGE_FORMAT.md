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

A local engine package source is a directory containing a root `fabgl.package` key/value
manifest. Schema 2 records a stable ID, display name, SemVer, engine requirement, author, SPDX
license identifier, dependencies, and typed entry points. The complete grammar, schema-1
migration defaults, and entry-point type list are documented in `PLUGIN_DEVELOPMENT.md`.

Install a local directory with:

```powershell
fabgl_project_cli package install game.fglproject C:\PackageSources\example
fabgl_project_cli package validate game.fglproject
fabgl_project_cli package list game.fglproject
fabgl_project_cli package remove game.fglproject org.example.package
```

Installed packages use this project layout:

```text
Packages/
  fabgl-packages.lock
  .fabgl-package-trust
  org.example.package/
    fabgl.package
    .fabgl-package-owned
    ...ordinary package files...
```

`fabgl.package` is canonical schema 2 after installation. `.fabgl-package-owned` binds the
directory to its ID, version, content SHA-256, file count, and byte count so removal can refuse
unowned or changed data. The deterministic lockfile records the engine version, sorted package
versions and content digests, dependencies, executable/trust state, and dependency-first load
order. `.fabgl-package-trust` is a separate, canonical project-local allow-list whose entries are
bound to package ID, version, and content digest. A manifest's own `trust` value is never an
authorization.

Directory contents are staged, verified, and promoted by rename; metadata uses atomic
replacement and rollback. Traversal, links/junctions/reparse points, special files, unsafe names,
case-folding collisions, configured limits, missing dependencies, incompatible versions, cycles,
and changed locks/content fail closed.

There is no standardized `.fglpackage` archive, archive extraction, remote registry protocol,
dependency download, signature/certificate format, or binary plugin ABI yet. Do not label an
arbitrary ZIP as an installable FabGL package. Local directory installation does not build,
dynamically load, sandbox, or activate source entry points.

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
