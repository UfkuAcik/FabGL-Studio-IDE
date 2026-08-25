# Building FabGL Studio

## Host requirements

- Windows 10/11 x64 or a current Linux x86-64 distribution
- CMake 3.24 or newer
- Ninja 1.10 or newer
- A C++20 compiler (MSVC 2022, GCC 12+, or Clang 15+ recommended)
- Qt 6 Widgets for the graphical Studio target
- SDL is optional until the SDL presentation backend is enabled; the reference software
  renderer and headless player do not require it

GNU host builds require GCC 10 or newer. The older CodeBlocks MinGW 8 runtime is intentionally
rejected because its Windows C++20 filesystem implementation is incomplete. This host requirement
does not change the separately pinned ESP32 Arduino compiler used by `scripts/build_esp32.ps1`.
When invoking a MinGW 13-built FabGL Studio CLI directly from Windows PowerShell, its matching
runtime directory must also precede older MinGW installations on `PATH`:

```powershell
$env:PATH = (Resolve-Path '.toolchains\Qt\Tools\mingw1310_64\bin').Path + ';' + $env:PATH
```

The managed desktop workflow sets this environment itself. A mismatched older `libstdc++-6.dll`
can otherwise terminate the CLI before it prints a diagnostic, which looks like an empty ESP32
export failure.

## Locked Windows desktop build

The supported Windows release path is repository-scoped and does not depend on a
machine-wide Qt or MinGW installation. Its machine-readable lock is
`toolchains/desktop-manifest.json`: Qt 6.8.3 `win64_mingw`, MinGW 13.1.0, and
aqtinstall 3.3.0. Bootstrap validates the existing installation before downloading
anything and keeps both the SDK and download cache outside the source tree's tracked
files:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/bootstrap_desktop.ps1
```

The complete release gate configures, explicitly builds the `fabgl_studio` target,
builds every other target, runs CTest with Qt's offscreen platform, stages the install,
deploys Qt, creates a portable ZIP, and runs package smoke tests:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_desktop.ps1 `
  -Configuration Release -Clean -RunGuiSmoke -Jobs 2
```

The command fails if Qt is missing or CMake skips the graphical Studio target. Output
is written to `out/build/desktop-release`, `out/install/desktop-release`, and
`out/packages/desktop`. Pass `-RequireInstaller` to bootstrap the manifest-pinned,
repository-local NSIS 3.12 generator and produce/checksum an installer. Release automation
uses this gate, so a tag cannot
publish only a partial artifact set.

The engine and tests intentionally configure without Qt. When `FGL_BUILD_STUDIO=ON` but Qt is
not found, CMake reports that the graphical target was skipped. Set `CMAKE_PREFIX_PATH` to the
Qt installation or use the managed bootstrap described in `TOOLCHAIN.md`.

## Configure, build, and test

```powershell
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

For a non-release, system-toolchain package on another supported host:

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

## One-command project validation and build

The same orchestrator used by Studio's **Build** action performs project/scene validation, local
package lock verification, native script-module compilation when managed script glue is present,
and a real data-driven PC runtime smoke test:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_project.ps1 `
  -ProjectPath "C:\Games\My Game\My Game.fglproject" `
  -Target Pc -Configuration Release
```

For ESP32 it exports the canonical Scene v2 and manifest asset table, creates the deterministic
asset pack, stages any guarded `Scripts/ESP32` portable gameplay module, invokes the locked
compiler, and retains binary/payload/script counts plus size and hash evidence:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_project.ps1 `
  -ProjectPath examples\pseudo3d_racer\Racer.fglproject `
  -Target Esp32 -Esp32BuildProfile SizeOptimized -Clean
```

The desktop C++20 gameplay module and ESP32 companion are intentionally separate implementations;
the exporter never attempts to compile arbitrary desktop sources with Xtensa. Generate both
starters with `fabgl_project_cli new-script`, then implement the bounded ESP32 `Start`/`Update`
callbacks described in `SCRIPTING_API.md`.

The result is `out/project-builds/<projectGuid>/<target>/project-build-result.json`. Upload is
never implicit. `-Upload` is accepted only for the ESP32 target together with an explicit safe
serial port and `-ConfirmBoardProfile olimex-esp32-sbc-fabgl-revb`; `-DryRun` records the plan but
cannot open the port. `-Monitor` is intentionally interactive and starts only after the completed
result has been atomically written. Programs and arguments remain separate arrays throughout.

## Build a project's C++ gameplay scripts

`fabgl_project_cli new` creates deterministic managed CMake glue, and `new-script` refreshes that
glue while adding a reflected C++ component. After installing the SDK, compile and link every
source below the project's `Scripts` directory against the exported engine target:

```powershell
fabgl_project_cli new-script "C:\Games\My Game" PlayerController
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_project_scripts.ps1 `
  -ProjectPath "C:\Games\My Game\My Game.fglproject" `
  -SdkRoot out\install\desktop-release
```

The build uses strict warnings-as-errors, produces a shared gameplay module, and writes
`out/project-scripts/<projectGuid>/build/<configuration>/compile_commands.json` in a developer
checkout. An installed copy uses the current user's local application-data directory. Compiler
syntax and build diagnostics retain their native `file:line:column` locations. Use `-CxxCompiler`
to select the compiler ABI that matches the SDK, `-OutputRoot` for another owned build directory, or
`-DryRun` for validation only. The driver refuses an empty source set, project-internal output, and
all source-tree symlinks/junctions. It neither overwrites nor executes a custom project
`CMakeLists.txt`.

The successful schema-2 result records the module and its SHA-256. Studio and the PC player
validate that result and load the explicit module before scene lifecycle begins. This is not
in-process hot reload: rebuilding restarts the preview so native objects are destroyed before the
library is unloaded.

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
libraries and their license texts are distributed alongside the application. The locked
Windows workflow additionally stages `qwindows` and `qoffscreen`; package smoke tests
launch the deployed Studio with `QT_QPA_PLATFORM=offscreen` and reject missing runtime,
documentation, examples, player, CLI tools, or SHA-256 files.

## ESP32 firmware

The managed profile, exact core/library pins, board identifier, and checksums are recorded in
`toolchains/manifest.json` and explained in `TOOLCHAIN.md`. Bootstrap the repository-scoped
toolchain, then create the locked compile-only artifact:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/bootstrap_toolchain.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_esp32.ps1 -Clean
```

Compile an actual project by adding `-ProjectPath`; the exporter stages a
canonical scene-v2/asset-pack Arduino sketch before invoking the same locked
toolchain:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_esp32.ps1 `
  -ProjectPath examples\empty\Empty.fglproject `
  -BuildProfile SizeOptimized -Clean -OutputRoot out\esp32\empty
```

Available ESP32 profiles are `Debug`, `Release`, `SizeOptimized`, and
`PerformanceOptimized`; each passes different optimization/debug properties to
Arduino. `-DryRun` prints the validated profile plan and performs no export,
compile, upload, or serial operation. `build-result.json` schema 2 records the
effective flags, verified 3 MiB partition, parsed flash/RAM usage, map/ELF/BIN
hashes, and project payload metadata.

The allocation-free reference runtime currently accepts at most 48 entities, 64 assets and 128
live particles. The host capability check rejects larger projects before firmware compilation;
these are deliberate internal-DRAM limits, not limits of the desktop authoring formats.

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
