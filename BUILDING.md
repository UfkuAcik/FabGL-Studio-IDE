# Building FabGL Studio

## Host requirements

- Windows 10/11 x64 or a current Linux x86-64 distribution
- CMake 3.24 or newer
- Ninja 1.10 or newer
- A C++20 compiler (MSVC 2022, GCC 12+, or Clang 15+ recommended)
- Qt 6 Widgets for the graphical Studio target
- SDL is optional until the SDL presentation backend is enabled; the reference software
  renderer and headless player do not require it

The engine and tests intentionally configure without Qt. When `FGL_BUILD_STUDIO=ON` but Qt is
not found, CMake reports that the graphical target was skipped. Set `CMAKE_PREFIX_PATH` to the
Qt installation or use the managed bootstrap described in `TOOLCHAIN.md`.

## Configure, build, and test

```powershell
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

For a release package:

```powershell
cmake --preset release
cmake --build --preset release
ctest --preset release
cmake --install out/build/release --prefix out/install/release
cpack --config out/build/release/CPackConfig.cmake -B out/packages
```

To validate only the dependency-free engine on a machine without Qt:

```powershell
cmake --preset core-only
cmake --build --preset core-only
ctest --preset core-only
```

Build output is confined to `out/` and is ignored by Git. Paths containing spaces and Unicode
are supported; do not move the source tree merely to simplify quoting.

## Qt discovery

Use a Qt build matching the desktop compiler ABI. Examples:

```powershell
$env:CMAKE_PREFIX_PATH = 'C:\Qt\6.8.3\mingw_64'
cmake --preset dev
```

```bash
cmake --preset dev -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.3/gcc_64
```

The Windows package runs `windeployqt` during staging when it is available. Dynamic Qt
libraries and their license texts are distributed alongside the application.

## ESP32 firmware

The managed profile, exact core/library pins, board identifier, and checksums are recorded in
`toolchains/manifest.json` and explained in `TOOLCHAIN.md`. Bootstrap the repository-scoped
toolchain, then create the locked compile-only artifact:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/bootstrap_toolchain.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_esp32.ps1 -Clean
```

The build never selects a port or uploads. Only after a human has confirmed the Rev B board,
selected its port, and reviewed `out/esp32/build-result.json`, run the separate uploader with
the exact profile confirmation token. Previewing the argv is hardware-safe:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/upload_esp32.ps1 `
  -Port COM9 -ConfirmBoardProfile olimex-esp32-sbc-fabgl-revb -DryRun

# Remove -DryRun only after completing HARDWARE_TESTING.md's safety gate.
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/upload_esp32.ps1 `
  -Port COM9 -ConfirmBoardProfile olimex-esp32-sbc-fabgl-revb
```

Serial monitoring is another explicit operation and requires both port and baud:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/serial_monitor.ps1 `
  -Port COM9 -Baud 115200 -ConfirmBoardProfile olimex-esp32-sbc-fabgl-revb
```

Do not infer board identity from a CH340 USB/serial adapter alone. Run upload or hardware
diagnostics only after confirming the target and port.

## Troubleshooting

- A compiler that accepts only experimental `-std=c++2a` is not a supported release
  toolchain, even if a subset of the core happens to compile.
- Qt compiler and application compiler ABIs must match (for example MinGW Qt with MinGW).
- Keep at least the free-space threshold reported by the bootstrap tool before installing a
  managed desktop toolchain.
- Use `cmake --fresh --preset dev` to discard CMake cache state; it does not remove source or
  project data.
