# Plugin and local package development

FabGL Studio currently implements a parser and dependency/trust validator for local package
manifests. It does not implement a package archive installer, online registry, dynamic plugin
loader, editor extension ABI, sandbox, signature verification, or GUI Package Manager. This
document defines the usable local manifest contract and the boundary future plugins must keep.

## Local package layout

Place a package below a project's `Packages/` directory. The `path` in its manifest is a safe
project-relative directory; absolute paths, drive prefixes, empty segments, and `..` are rejected.
Package names are lowercase ASCII letters/digits plus `-`, `_`, or `.`, begin with an
alphanumeric character, and are at most 80 characters.

Example manifest text:

```text
name=org.example.camera-shake
version=1.2.0
path=Packages/org.example.camera-shake
trust=untrusted
executable=false
dependency=org.fabgl.math@^1.1.0
dependency=org.fabgl.effects@>=2.0.0
```

Required fields are `name`, `version`, and `path`. Optional `trust` values are `untrusted`,
`trusted`, and `builtin`; optional `executable` is `true` or `false`. Repeat `dependency` once per
dependency. Unknown and duplicate singleton fields are errors.

## Version requirements

- `*` accepts any installed version.
- `1.2.3` requires exactly that version.
- `>=1.2.3` sets a minimum.
- `^1.2.3` accepts compatible SemVer versions within major 1.

For major zero, compatible ranges remain within the same nonzero minor; `^0.0.3` matches only
patch 3. Prerelease comparison follows SemVer ordering. Build metadata is ignored for matching.

The registry rejects duplicate packages, self/duplicate dependencies, missing dependencies,
version mismatches, and cycles, then returns dependency-first load order.

## Trust model

New external packages are untrusted. An untrusted package declaring executable code is blocked
unless the caller obtains an explicit project-level authorization. Manifest text is not a
signature and `trust=trusted` must never be accepted merely because a downloaded package asks
for it. The host application must store the user's trust decision separately.

Until process isolation and a stable ABI exist, native editor/runtime extensions run with the
user's permissions and are suitable only for reviewed source code compiled with the project.
Data-only assets and visual graphs still require strict schema, path, size, and dependency
validation.

## Source compatibility

Public portable headers live below `engine/include/fabgl`. Qt types must not cross into engine or
runtime plugin interfaces. Use stable GUIDs for exported component types, semantic versions for
package contracts, `Result<T>` for expected failures, and explicit ownership. Avoid relying on
class layout or binary ABI before 1.0; source-based packages are the supported development model.

## Testing a package today

Add parser/registry tests against `fabgl::PackageManifestParser` and `fabgl::PackageRegistry`,
then build the package source as an explicit CMake target linked to `FabGLStudio::Engine`. Verify
missing/version/cycle/trust failures as well as the happy path. There is no `fabgl package`
command yet and no `.fglpackage` archive should be published as if the installer exists.

