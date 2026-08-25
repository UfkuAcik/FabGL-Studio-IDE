# FabGL Studio

FabGL Studio is an open-source game engine and desktop development environment for the
[Olimex ESP32-SBC-FabGL](https://www.olimex.com/Products/Retro-Computers/ESP32-SBC-FabGL/open-source-hardware).
It adapts a Unity/Godot-style workflow—projects, scenes, entities, components, assets, play mode,
profiling and one build surface—to ESP32-class hardware. It does **not** promise desktop-GPU
graphics on a microcontroller: target runtimes are bounded, software-rendered and checked against
explicit memory/capability limits.

## Project status

**Alpha / active development.** The Windows editor, portable engine, PC player, project and asset
tools, and guarded ESP32 build/upload path are usable development checkpoints. Raycast, racer,
TPS, low-poly 3D, advanced extension services and some ESP32 parity paths remain experimental or
partial. Physical display, audio and peripheral behavior must not be inferred from a successful
compile or serial connection.

For an exact continuation checklist, see [docs/HANDOFF.md](docs/HANDOFF.md).

## Highlights

- Qt 6 dockable editor with project/scene lifecycle, Hierarchy, reflected Inspector, Scene/Game
  views, code and visual-script editors, Animator, specialist asset editors, profiler, memory and
  build panels.
- C++20 engine core independent of Qt: versioned scenes, entity/component lifecycle, transform
  hierarchy, reflection, prefabs, animation, input, audio mixing, 2D physics, save data and
  bounded visual bytecode.
- Deterministic software renderers for 2D/tilemaps, raycast FPS and pseudo-3D racing, plus an
  explicitly experimental low-poly path.
- Stable GUID asset database and deterministic image/audio/font/tilemap/mesh/pack tooling.
- PC preview with isolated play-state copies and a rebuild/restart fallback for native gameplay
  modules.
- Locked Arduino-ESP32/FabGL toolchain, target capability gate, firmware size evidence, guarded
  upload and structured serial diagnostics.
- Explicit project/package trust boundaries, path/reparse protection, atomic saves and recovery.

## Editor

The main authoring path is connected end to end:

`Create/Open Project → Scene → Entity/Component → Hierarchy/Inspector → Assets → Save → Play → Build`

The Inspector uses reflected property metadata and supports mixed values for multi-selection with
transactional undo. Project assets are imported through canonical settings rather than copied as
opaque editor-only data. Build diagnostics retain severity and source locations. Upload and serial
operations require an explicit port and exact board-profile confirmation; they are never triggered
by opening or previewing a project.

Advanced package entry points expose bounded trusted `AssetImporter`, `CustomInspector` and
`CustomWindow` services. Their current schema is intentionally smaller than a stable native plugin
ABI.

## Engine

The portable engine provides:

- entity/component ownership and lifecycle callbacks;
- parent/child transforms with cycle prevention and dirty propagation;
- reflection and typed, versioned scene serialization with migration;
- prefab hierarchy, overrides, apply/revert/unpack and missing-reference handling;
- action/axis input maps, animation controllers, runtime UI, particles and lightweight AI;
- deterministic 2D physics plus a limited experimental 3D query/controller layer;
- audio buses/voices/streaming and native Windows output;
- separate versioned gameplay save storage with checksums and migrations;
- native C++ gameplay modules and validated visual-graph bytecode.

## Renderers

| Renderer | Status | Notes |
| --- | --- | --- |
| Renderer2D / tilemap | ✅ Working on PC | Sprites, layers, camera, palette, culling, UI and particles |
| Raycast FPS | 🟡 Working PC path | Textured walls, doors, sprites, weapon/HUD; ESP32 playtest pending |
| Pseudo-3D racer | 🟡 Working PC path | Segment tracks, curves/hills, vehicle/framework and track editor |
| Low-poly 3D | 🧪 Experimental | Bounded indexed triangles, flat lighting, fog and simple materials |
| TPS | 🧪 Experimental | Technology demo and framework; no hardware performance claim |

## PC preview

The PC player and Studio play session consume the same versioned scene/component data and portable
runtime systems. Play mode clones the authoring scene, so Stop does not mutate edit-time data.
Native gameplay changes use a verified rebuild and full preview restart instead of unsafe
state-preserving in-process hot reload. The reference Windows backend is native Win32 plus the
software framebuffer; SDL remains an architectural option rather than a required dependency.

## ESP32 target

The primary target is `olimex-esp32-sbc-fabgl-revb`: an ESP32-WROVER-class board with FabGL VGA,
PS/2 keyboard/mouse, audio, microSD and optional PSRAM. The reference release profile currently
uses the 3 MiB `huge_app` partition and keeps PSRAM disabled unless the experimental profile is
selected. The allocation-free project runtime currently gates projects to 48 entities, 64 assets
and 128 live particles so FabGL and task stacks retain internal-DRAM headroom.

Software checks cannot verify the visible VGA image, audible output, attached PS/2 devices, SD
electrical behavior, PSRAM hardware or board identity. Follow [HARDWARE_TESTING.md](HARDWARE_TESTING.md).

## Screenshot

The image below was captured from the real Qt editor running the Platformer project in the local
offscreen GUI smoke workflow.

![FabGL Studio with the Platformer project open](docs/images/studio-overview.png)

## Examples

The repository contains ten versioned projects under `examples/`:

- Empty Project
- 2D Platformer
- Top-Down Shooter
- Raycast FPS
- Pseudo-3D Racer
- TPS / Low-Poly Technology Demo (Experimental)
- UI Showcase
- Audio Showcase
- Animation Showcase
- Asset Streaming Showcase

They are real project/scene inputs for the PC player and build tools. ESP32 export is intentionally
fail-closed when a project exceeds the target subset or capacity.

## Quick start

On Windows, the managed path installs repository-local Qt 6.8.3 and MinGW 13.1, then performs a
release build:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/bootstrap_desktop.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_desktop.ps1 `
  -Configuration Release -RunGuiSmoke -Jobs 2
```

For an already configured development toolchain:

```powershell
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Run an example with `scripts/run_example.ps1`, or open its `.fglproject` file from Studio.

## Building

### Windows

The supported release ABI is Qt 6.8.3 `win64_mingw` with MinGW 13.1. The complete portable/NSIS
workflow is documented in [BUILDING.md](BUILDING.md). A shorter core-only build is:

```powershell
cmake --preset core-only
cmake --build --preset core-only
ctest --preset core-only
```

### Linux

Linux is an architectural target and CI configuration exists, but the 2026-08-25 handoff was not
locally validated on Linux. With CMake, Ninja, GCC 12+ (or Clang 15+) and Qt 6 Widgets:

```bash
cmake --preset dev -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.3/gcc_64
cmake --build --preset dev
ctest --preset dev
```

Non-Windows source raster decoding has explicit limitations; see [ASSET_PIPELINE.md](ASSET_PIPELINE.md).

## ESP32 toolchain

`toolchains/manifest.json` pins:

- Arduino CLI 1.5.1
- Arduino-ESP32 core 2.0.11
- Olimex FabGL 1.0.9 source at commit `04f328a10573297dd554f13be7f369cdee0f7a2b`
- the `olimex-esp32-sbc-fabgl-revb` board/profile contract

Bootstrap verifies artifact checksums and keeps managed downloads/toolchains outside tracked source
files. Details and compatibility rationale are in [TOOLCHAIN.md](TOOLCHAIN.md) and ADR
[0004](docs/decisions/0004-toolchain-and-fabgl.md).

## Building for ESP32

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/bootstrap_toolchain.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_esp32.ps1 `
  -ProjectPath examples\pseudo3d_racer\Racer.fglproject `
  -BuildProfile Release -Clean -OutputRoot out\esp32\racer
```

The command exports canonical Scene v2/project data, compiles assets and any bounded
`Scripts/ESP32` companion, invokes the locked compiler, and writes `build-result.json` with binary,
map, size and SHA-256 evidence. It never selects or opens a serial port.

## Uploading to Olimex ESP32-SBC-FabGL

First list ports without opening them:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/detect_serial_ports.ps1
```

After physically confirming the board and reviewing the build result, preview the exact upload:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/upload_esp32.ps1 `
  -Port COM5 -ConfirmBoardProfile olimex-esp32-sbc-fabgl-revb `
  -BuildResultPath out\esp32\racer\build-result.json -DryRun
```

Remove `-DryRun` only for the confirmed device. The Studio Build and Hardware Diagnostics actions
use the same guarded scripts. The Serial Monitor has explicit connect/disconnect, baud, filtering,
search, timestamps, save and send controls.

## Repository structure

| Path | Responsibility |
| --- | --- |
| `apps/studio/` | Qt Widgets editor |
| `apps/player_pc/` | Native/headless PC preview |
| `engine/` | Qt-independent engine and formats |
| `renderers/` | 2D, raycast, racer and experimental low-poly software renderers |
| `frameworks/` | Reusable platformer/top-down/FPS/racer/TPS gameplay modules |
| `platforms/common/` | Portable target/desktop contracts |
| `platforms/fabgl/` | Olimex/FabGL firmware and adapters |
| `tools/` | Asset, project, package and toolchain CLIs |
| `examples/` | Ten example projects and generated target assets |
| `tests/` | Unit, integration, Qt, rendering and hardware-safe harnesses |
| `packaging/` | Portable, installer and source-package workflows |
| `docs/` | Guides, ADRs, progress evidence and handoff |

## Current feature status

| Feature | Status |
| --- | --- |
| Windows Qt editor and PC player | ✅ Builds and short smoke passes |
| Project/scene/entity/component authoring | ✅ Working |
| Hierarchy, reflected Inspector and multi-selection undo | ✅ Working |
| Asset pipeline and deterministic packs | 🟡 Working supported formats; one fixture regression is documented |
| PC play/rebuild/restart workflow | ✅ Working |
| ESP32 export and managed compile | 🟡 Working subset; current result in HANDOFF |
| Port detection/upload/serial monitor | 🟡 Guarded workflow implemented; physical revalidation pending |
| Extension importer/inspector/window services | 🟡 Bounded trusted alpha API |
| ESP32 SD asset streaming | 🟡 Export/binder foundation; firmware adapter integration incomplete |
| ESP32/PC lifecycle parity | 🟡 Shared scheduler exists; firmware adoption incomplete |
| Raycast and racer | 🟡 PC working, hardware gameplay validation pending |
| Low-poly 3D, TPS and Physics3D | 🧪 Experimental |
| Source-level debugger / bundled clangd | ❌ Not shipped |

## Known limitations

- No claim of complete Unity/Godot feature or rendering parity.
- Linux/macOS, MSVC and independent clean-machine release validation remain outstanding.
- TTF/glTF import and non-Windows raster source decoding are not stable supported paths.
- Native scripts run trusted machine code; the trust boundary is explicit, not a sandbox.
- In-process state-preserving native hot reload is replaced by a safer full restart.
- Online/signed package repositories, a stable binary plugin ABI and a source debugger are absent.
- Hardware VGA/audio/input/SD/PSRAM/FPS/soak results require a confirmed physical test session.

## Validation status

The 2026-08-25 finalization checkpoint built the Qt-independent Release tree and the real Qt
6.8.3/MinGW 13 Studio tree with warnings as errors. Eight targeted engine/Studio/build/security
smoke tests passed. A broader asset/project test executable reported 49/53 passing; its four known
failures and the final ESP32 result are recorded in [docs/HANDOFF.md](docs/HANDOFF.md). No unrun
test or manual hardware observation is represented as passed.

## Roadmap

Near-term work is to close the documented target-runtime parity/streaming gaps, update stale
capability fixtures, execute full CI and perform a controlled Olimex HIL pass. See
[ROADMAP.md](ROADMAP.md) and the prioritized [handoff](docs/HANDOFF.md).

## Documentation

- [Building](BUILDING.md)
- [User guide](USER_GUIDE.md)
- [Editor guide](EDITOR_GUIDE.md)
- [Architecture](ARCHITECTURE.md)
- [Engine API](ENGINE_API.md) and [scripting API](SCRIPTING_API.md)
- [File formats](FILE_FORMATS.md) and [asset pipeline](ASSET_PIPELINE.md)
- [Hardware testing](HARDWARE_TESTING.md) and [toolchain](TOOLCHAIN.md)
- [Plugin development](PLUGIN_DEVELOPMENT.md) and [package format](PACKAGE_FORMAT.md)
- [Architecture decisions](docs/decisions/) and [project handoff](docs/HANDOFF.md)

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md), keep engine code Qt-independent, preserve fail-closed
target validation, and do not weaken path/trust/upload gates to make a test pass. Contributions
should state exactly which host and hardware checks were actually run.

## License

First-party code is licensed under **GPL-3.0-or-later**. See [LICENSE](LICENSE), [NOTICE](NOTICE),
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) and [docs/LICENSING.md](docs/LICENSING.md).

## Credits

FabGL Studio depends on Fabrizio Di Vittorio's FabGL project and Olimex's open hardware/software
work, plus Qt, CMake, Ninja, MinGW, Arduino CLI and Arduino-ESP32. Exact versions, licenses, source
URLs and redistribution notes are maintained in [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
