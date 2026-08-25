# Distribution packaging

The supported Windows package is produced by the locked Qt 6.8.3 / MinGW 13.1
workflow. One command bootstraps or validates the repository-scoped SDK, configures
with Studio required, builds, tests, installs, packages, and validates the result:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_desktop.ps1 `
  -Configuration Release -Clean -RunGuiSmoke -RequireInstaller -Jobs 2
```

`packaging/build-packages.ps1` is a compatibility entry point for the same command;
it no longer permits packaging an untested or Qt-less build.

Outputs are written below:

- `out/install/desktop-release`: staged clean-machine tree
- `out/packages/desktop/FabGL-Studio-*-Windows-*.zip`: portable application
- the matching `.zip.sha256`
- the NSIS `.exe` and matching `.exe.sha256` from the pinned repository-local generator
- `desktop-build-result.json`: paths, hashes, profile, and smoke-test status

`packaging/smoke-test.ps1` is a hard gate. It requires `FabGLStudio.exe`, the player
and every CLI, Qt Core/Gui/Widgets, both `qwindows` and `qoffscreen`, public
documentation, all ten example projects and scenes, and a matching SHA-256 file. It
also executes each CLI, validates the bundled Empty project, inspects the ZIP without
extracting it, compiles and runs an external consumer against the staged
`find_package(FabGLStudio)` SDK, and can keep the deployed GUI alive in offscreen mode
for a bounded smoke window. Missing Studio, Qt, or the exported SDK is an error, never
a warning.

Tag releases run the same gate with `-RequireInstaller`. They also create a Git-based
source archive with `packaging/build-source-package.ps1`; the release job refuses to
publish unless exactly one portable ZIP, one NSIS installer, one source ZIP, and a
valid checksum for each are present.

The source packager refuses a dirty tree in its default release mode, so a tag cannot
silently omit local changes. For an explicit local engineering snapshot, pass
`-IncludeWorkingTree`; this packages the current tracked modifications and non-ignored
untracked sources, rejects links/reparse points and bounded-size violations, and adds
`SOURCE_PACKAGE.json` recording the HEAD revision plus `workingTreeIncluded/sourceDirty`
flags. Build caches, `.git`, managed SDKs, and ignored outputs remain excluded.

CPack's normal install rules deploy the Qt runtime. The packaging-only
`install-offscreen-plugin.cmake` adds the locked `qoffscreen` plugin to both ZIP and
installer staging so CI can test the artifact without a display server. Download
caches, managed SDKs, and local build products are never included.
