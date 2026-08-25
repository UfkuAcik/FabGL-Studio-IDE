# Plugin and local package development

FabGL Studio supports deterministic, project-local directory packages. The command-line package
manager copies ordinary files into a project's `Packages/` directory, validates dependencies and
engine compatibility, records executable trust outside the manifest, and maintains a lockfile.
The engine SDK also provides a source extension host in
`fabgl/extensions/extension_registry.h`. It supports all eight manifest entry-point kinds,
deterministic dependency ordering, bounded hook registration, explicit activation/deactivation,
and rollback on partial activation failure. Studio and the PC player can load explicitly declared
native modules from a content-locked, locally approved package when the module was built with the
same FabGL Studio distribution/toolchain. Package archives, an online registry, publisher
signature verification, a stable cross-version/cross-compiler ABI, and process sandboxing remain
explicit future boundaries.

## Manifest schema 2

A source package is a directory with `fabgl.package` at its root. New packages should use schema
2:

```text
schema=2
id=org.example.camera-shake
displayName=Camera Shake
version=1.2.0
engine=^0.1.0
author=Example Studio
license=MIT
path=Packages/org.example.camera-shake
executable=true
dependency=org.fabgl.math@^1.1.0
entry=editor-plugin:src/editor_plugin.cpp
entry=runtime-module:src/camera_shake.cpp
```

The singleton fields are:

- `schema`: `2` for the current schema.
- `id`: stable package identity. It starts with an alphanumeric character, is at most 80
  characters, and uses lowercase ASCII letters, digits, `.`, `_`, or `-`.
- `displayName`: user-facing name, independent from the stable ID.
- `version`: three-part semantic version, optionally with a SemVer prerelease.
- `engine`: supported engine-version requirement.
- `author`: package author or organization.
- `license`: an SPDX license identifier, or `NOASSERTION` when it is genuinely unknown.
- `path`: optional in source schema 2. Installation always canonicalizes it to
  `Packages/<id>`.
- `executable`: `true` or `false`. Entry points imply executable content regardless of this
  declaration.

Repeat `dependency=<id>@<requirement>` for each dependency and
`entry=<type>:<relative-path>` for each typed entry point. Supported entry types are:

- `editor-plugin`
- `runtime-module`
- `asset-importer`
- `custom-inspector`
- `custom-window`
- `build-step`
- `renderer-extension`
- `framework`

Entry paths must identify normal files inside the package. Duplicate dependencies, duplicate
typed entries, self-dependencies, unknown fields, unsafe paths, and missing entry files are hard
errors.

## Legacy manifests

The schema-1 form remains readable for existing local sources:

```text
name=org.example.legacy
version=1.0.0
path=Packages/org.example.legacy
trust=untrusted
executable=false
```

It receives safe defaults (`displayName=<name>`, `engine=*`, `author=Unknown`, and
`license=NOASSERTION`) and is written into the project as canonical schema 2. A legacy or schema-2
`trust=trusted` claim never grants trust and is omitted from the installed manifest.

## Version requirements and load order

- `*` accepts any installed version.
- `1.2.3` requires exactly that version.
- `>=1.2.3` sets a minimum.
- `^1.2.3` accepts compatible SemVer versions within major 1.

For major zero, compatible ranges remain within the same nonzero minor; `^0.0.3` matches only
patch 3. Prerelease comparison follows SemVer ordering. Build metadata is accepted but ignored
for matching and canonical output.

The package registry rejects duplicate IDs, self/duplicate dependencies, missing dependencies,
version mismatches, engine mismatches, and dependency cycles. Its successful result is a stable,
dependency-first load order recorded in `Packages/fabgl-packages.lock`.

## Install, inspect, validate, and remove

Use a source directory outside the project root and its `Packages/` tree:

```powershell
fabgl_project_cli package install "C:\Games\My Game\My Game.fglproject" `
  "C:\PackageSources\camera-shake"
fabgl_project_cli package list "C:\Games\My Game\My Game.fglproject"
fabgl_project_cli package validate "C:\Games\My Game\My Game.fglproject"
fabgl_project_cli package remove "C:\Games\My Game\My Game.fglproject" `
  org.example.camera-shake
```

Installation recursively copies normal files, canonicalizes the manifest, computes a content
SHA-256, stages and verifies the complete package, then promotes it with one directory rename. It
also writes a matching `.fabgl-package-owned` marker. Lock and trust records use atomic file
replacement and rollback paths; removal first verifies the marker and content digest, refuses to
break installed dependents, and deletes only the verified owned directory.

The lockfile is deterministic: it records the engine version, sorted identities and digests,
dependency requirements, executable/trust state, and dependency-first load order. Commit
`fabgl-packages.lock` with the project. Do not hand-edit it or the ownership marker; validation
fails closed when either installed content or metadata changes.

## Executable trust and input limits

Native code, build scripts, dynamic libraries, WebAssembly, known script extensions, executable
file signatures, shebang files, and every typed entry point are treated as executable. The
manager performs this detection itself, so `executable=false` cannot hide code. After reviewing
the exact source, opt in explicitly:

```powershell
fabgl_project_cli package install "C:\Games\My Game\My Game.fglproject" `
  "C:\PackageSources\camera-shake" --allow-executable
```

That flag creates a separate project-local record in `Packages/.fabgl-package-trust`, bound to
the package ID, version, and installed content SHA-256. Changing any installed byte invalidates
both trust and lock validation. Trust is not global and is not inherited by a later version.

Package traversal is bounded to 1,024 files, 16 MiB per file, 64 MiB total, and 32 path segments
by default. Absolute/traversal paths, Windows drive prefixes, reserved `.fabgl-*` source files,
case-insensitive path collisions, special files, symbolic links, junctions, and other reparse
points are rejected. `Packages/` is managed as a closed set: unexpected files or unowned
directories stop package operations instead of being overwritten or removed.

Executable packages run with the user's permissions. `fabgl::ExtensionHost` refuses non-built-in
activation in safe mode, while extensions are disabled, or when the host's content-bound trust
evaluator rejects the exact identity/hash. Native discovery is limited to an explicit typed entry
in the validated package lock; the host never scans for libraries. The package manager does not
compile modules and the host does not sandbox them. Only approve code you have reviewed and rebuild
it against the exact Studio SDK/toolchain you distribute with it.

## Native module and product-service contract

A native entry path ends in the platform library extension (`.dll`, `.so`, or `.dylib`) and exports
`fabglStudioGetExtensionModuleV1` from
`fabgl/extensions/extension_module.h`. The returned bounded descriptor table creates
`IExtension` instances. Activation registers named hooks; the product then invokes the one
type-specific registration hook for that entry:

| Entry kind | Registration capability | Product dispatch operations |
| --- | --- | --- |
| `editor-plugin` | `fabgl.editor-plugin.register` | `activate`, `execute`, `deactivate` |
| `runtime-module` | `fabgl.runtime-module.register` | `startup`, `update`, `shutdown` |
| `asset-importer` | `fabgl.asset-importer.register` | `probe`, `import`, `reimport` |
| `custom-inspector` | `fabgl.custom-inspector.register` | `inspect`, `apply` |
| `custom-window` | `fabgl.custom-window.register` | `show`, `hide`, `refresh` |
| `build-step` | `fabgl.build-step.register` | `pre-build`, `post-build` |
| `renderer-extension` | `fabgl.renderer-extension.register` | `startup`, `update`, `shutdown` |
| `framework` | `fabgl.framework.register` | `startup`, `update`, `shutdown` |

The registration hook receives a temporary `ProjectExtensionHostContext`. Its `services` table is
valid only during that call. Register a `ProjectExtensionServiceDescriptor` whose `kind` matches
the package entry and whose `dispatchCapability` names a hook registered during activation. IDs,
labels, counts, priorities, structure sizes, API versions, and dispatch-hook existence are all
validated before the descriptor is copied into host ownership. Never retain context, scene,
runtime, service-table, string, or host-state pointers.

Studio exposes registered descriptors in the **Extensions** dock. Editor, custom-window,
custom-inspector, and asset-importer services can be invoked there; build-step services run before
and after PC/ESP32 project builds. Studio Play and the PC player dispatch runtime/framework/
renderer `startup`, one `update` per engine frame, and `shutdown`. The player filters out all
editor-only entry kinds before opening a library. Project `open`/`close` and runtime `start`/`stop`
lifecycle hooks remain separate module-level notifications.

All service payloads and responses are UTF-8. Product-generated payloads are compact JSON with
`"schema":1` and are capped at 256 KiB. Runtime update carries `deltaSeconds`; build steps carry
`target`, `configuration`, `processSucceeded`, and `exitCode`. Interactive Studio dispatch carries
only the current project path plus the selected in-project asset or entity identity needed by that
service. A throwing hook is converted to an error at the host boundary. Any service that returns
an error is disabled for the rest of that project session, shown as disabled in the Extensions
dock, and reported in the Console/Build Output or player stderr; unrelated services continue.

Safe Mode, `--disable-plugins`, an untrusted Studio project path, a missing executable-package
approval, a lock/content digest mismatch, an unsafe path, or an unsupported player entry kind all
prevent native service dispatch. Safe Mode and disabled-plugin tests assert that the fixture module
is never opened and produces no lifecycle or service trace.

## Source compatibility and testing

Portable engine headers live below `engine/include/fabgl`; the product registration/dispatch
surface is in `tools/project_runtime/include/fabgl/project`. Qt types must not cross into engine or
runtime extension interfaces. Use stable GUIDs for exported component types, semantic versions
for package contracts, `Result<T>` for expected failures, and explicit ownership. Module ABI v1
detects structure/API mismatches, but does not promise compatibility between compilers, standard
libraries, build modes, SDK revisions, or Studio releases before 1.0.

Test manifests with `fabgl::PackageManifestParser`, package dependency graphs with
`fabgl::PackageRegistry`, compiled source hooks with `fabgl::ExtensionRegistry`, and the actual
project flow through `fabgl_project_cli package`. Cover
missing/version/cycle/engine failures, explicit executable approval, traversal and link rejection,
limits, deterministic locks, tampered installed content, dependency-order activation, safe-mode
denial, failure rollback, and reverse-order deactivation. Compile source entry points as explicit
shared-library CMake targets linked to the exact FabGL Studio engine/runtime SDK used by the
product. Test descriptor registration, every supported operation, a deliberate dispatch
failure/disable, Player startup-update-shutdown order, Studio build pre/post order, and zero
execution in Safe Mode. Package installation alone does not build or activate source entries.
