# Distribution packaging

The release package is produced from CMake install rules. ZIP is the baseline generator on every
host. On Windows, NSIS is added only when `makensis` is found during configure and
`FGL_PACKAGE_NSIS=ON`.

```powershell
cmake --preset release
cmake --build --preset release
ctest --preset release
powershell -ExecutionPolicy Bypass -File packaging/build-packages.ps1
powershell -ExecutionPolicy Bypass -File packaging/smoke-test.ps1
```

Outputs go below `out/packages`; the staging tree is `out/install/release`. Toolchain download
caches and build products are deliberately excluded. If Qt was not found at configure time, the
package remains useful for the PC player and CLI tools but contains no `FabGLStudio.exe`.

When a Qt-enabled Windows build is configured and `windeployqt` is discoverable beside the Qt
installation, the install step deploys the Qt runtime. Absence of `windeployqt` is a packaging
warning and a graphical clean-machine smoke test must fail rather than shipping an incomplete
editor bundle.

The staged `share/fabgl-studio/` tree preserves the repository-relative `scripts/`,
`toolchains/`, `platforms/fabgl/firmware/`, and `tests/hardware/` layout so the managed firmware
workflow remains usable from an extracted portable ZIP. Managed downloads/builds are written
beside that shared tree by default; an installed read-only location should pass explicit writable
cache/install/output paths as documented in `TOOLCHAIN.md`.
