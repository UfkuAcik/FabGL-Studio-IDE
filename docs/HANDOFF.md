# FabGL Studio development handoff

Evidence date: **2026-08-25**. This document is the continuation source of truth for the current
alpha checkpoint. It intentionally distinguishes compiled software, targeted smoke evidence,
older/stale evidence and observations that still require a human with the physical board.

## Project State

FabGL Studio is a substantial working alpha rather than a scaffold: the Qt editor and PC player
build, the central project/scene/entity/asset/play/build workflow is connected, ten example
projects exist, and a locked ESP32 Release build produces a real Olimex/FabGL binary. The codebase
also contains broad engine, renderer, framework, authoring, package, recovery and security work.

The final 2026-08-25 pass deliberately stopped feature expansion to preserve a buildable,
publishable checkpoint. The most important remaining engineering gap is target parity: a shared
lifecycle contract and external-SD pack/binder foundation exist, but the firmware does not yet use
the shared scheduler or the bounded SD reader in its live loop.

Branch at handoff: `main`. Intended remote:
`https://github.com/UfkuAcik/FabGL-Studio-IDE.git`.

## Completed Features

### Editor

- Qt 6 Widgets main window, menus/toolbars, dock panels, themes and named/custom layouts.
- Project create/open/save, project trust, atomic scene save and recovery/safe-mode paths.
- Hierarchy, selection synchronization, entity/component editing and reflected typed Inspector.
- Mixed-value multi-selection editing with one transactional undo command and reverse rollback on
  partial writer failure.
- Scene/Game views, transform tools, drag/drop assets and isolated Play/Pause/Step/Stop sessions.
- Multi-tab C++ editor, highlighting, search/replace, symbols, file change detection, build-source
  navigation and optional external clangd client.
- Visual Script and Animator authoring, prefab editor, input map editor and material/particle/
  tilemap/raycast-map/racer-track/UI/audio/profiler specialist panels.
- Severity-aware build output, real process argument arrays, unified PC/ESP32 build action,
  guarded deploy action and individual Hardware Diagnostics actions.
- Serial console with explicit connect/disconnect, baud, timestamps, color/filter/search, save,
  input and line-ending controls.

### Engine

- Qt-independent C++20 engine, result/error propagation and structured logging.
- Entity/component lifecycle, hierarchy/cycle prevention, reflection and Scene v2 with migration.
- Prefab hierarchy, nested dependencies, overrides, apply/revert/unpack and strict serializers.
- Input maps, animation/controller assets, 2D physics, limited Physics3D queries/controllers,
  runtime UI, particles, A*/behaviors, audio mixer/streaming and PC save storage.
- Native gameplay module ABI with verified rebuild/restart fallback.
- Typed visual graph, strict `.fglvisual`, validation/compiler and bounded callback-based VM.
- Shared allocation-free lifecycle scheduler used by the desktop `EngineLoop`.

### Renderer

- Software framebuffer and Renderer2D/tilemap paths.
- Raycast FPS and pseudo-3D racer renderers/frameworks.
- Experimental bounded low-poly renderer and TPS framework/demo.
- Scene presenter/resource resolver used by PC preview rather than separate fake demo state.

### Toolchain and build

- Project/scene validation, canonical asset compilation/packing, native script build and PC smoke in
  `scripts/build_project.ps1`.
- Locked Arduino CLI 1.5.1, Arduino-ESP32 2.0.11 and Olimex FabGL 1.0.9 commit
  `04f328a10573297dd554f13be7f369cdee0f7a2b`.
- Four real ESP32 compiler profiles, map/size/hash result schema, read-only port detection,
  exact-profile guarded upload and bounded diagnostic capture.
- Managed Qt 6.8.3/MinGW 13.1 bootstrap and portable/NSIS packaging scripts.
- Safe local package install/list/validate/remove with lock, ownership, dependency and trust data.
- Source-package workflow that refuses a dirty HEAD-only archive and can explicitly include the
  bounded working tree without losing uncommitted sources.

### Samples

Ten real projects: Empty, Platformer, Top-Down, Raycast FPS, Pseudo-3D Racer, experimental TPS,
UI, Audio, Animation and Asset Streaming. They have canonical project/scene inputs and generated
assets; target capability checks intentionally reject unsupported ESP32 content.

## Partially Implemented

### ESP32/PC lifecycle parity

- Working: `platforms/common/LifecycleScheduler.h` defines the allocation-free phase/fixed-step
  contract, and desktop `EngineLoop` uses it. ESP32 export copies the same header into the sketch.
- Missing: `platforms/fabgl/firmware/firmware.ino` still runs its hand-written loop and does not
  invoke `fabgl_lifecycle::initialize/tick/shutdown`. Phase order therefore is not yet shared in the
  live firmware.

### ESP32 SD asset streaming

- Working: deterministic `ProjectAssets.fglpak` export metadata, checksum/GUID/path fail-closed
  binder, `BoundedStorageReader.h` double-window reader and `ProjectAssetSdAdapter.h` exist.
- Missing: the live firmware does not include/bind `ProjectAssetSdAdapter`, open the external pack,
  prefetch during an AssetStreaming phase, or route renderer/audio reads through the bounded
  reader. External assets should remain capability-gated until this is wired and compiled.

### ESP32 runtime component subset

- Working: bounded Collider2D/Rigidbody2D, particle and runtime-UI parsing/update/render foundations
  are present in `ProjectRuntime.h`; host capability code now accepts their supported subset.
- Missing: capability fixtures still assert that Collider2D is unported, and hardware behavior has
  not been revalidated. Fixed internal-DRAM gates are 48 entities, 64 assets and 128 live particles.

### Extension product hooks

- Working: trusted bounded `AssetImporter`, `CustomInspector` and `CustomWindow` service paths are
  connected to the Asset Browser, Inspector and real dock lifecycle. Response size/time/schema
  failures disable the service fail-closed.
- Missing: a dedicated CustomWindow open/refresh/hide/unload Qt test was not completed. Treat the
  service schema as alpha, source-extension API—not a stable binary plugin ABI.

### Platform/release coverage

- Working: local Windows MinGW Release builds and packaging scripts.
- Missing: current Linux, MSVC, macOS, signed installer and independent clean-machine evidence.
  Full portable/NSIS packaging was not rerun in the final short pass.

## Not Implemented

- Stable native binary plugin ABI, signed packages or an online package repository.
- Bundled clangd distribution and integrated source-level breakpoint/step/call-stack debugger.
- In-process state-preserving C++ hot reload; the supported fallback is full rebuild/restart.
- Production 3D physics, skeletal animation or modern GPU/shader features.
- Stable TTF/glTF import and cross-platform raster source decoding.
- Automatic proof of physical VGA appearance, audible output, input-device behavior or board
  identity; software must not manufacture these claims.

## Build Status

| Target | Configuration | 2026-08-25 result |
| --- | --- | --- |
| Windows portable core/player/tools | MinGW 13.1 Release, warnings-as-errors | **Built** (`out/build/root-gcc13`) |
| Windows Qt editor | Qt 6.8.3 + MinGW 13.1 Release, warnings-as-errors | **Built** (`FabGLStudio.exe`, `out/build/root-streaming-qt`) |
| Linux | Not run in this checkpoint | **Not validated** |
| Windows MSVC | Not run | **Not validated** |
| ESP32 Pseudo-3D Racer | Managed Release, PSRAM disabled | **Built** (`out/esp32-final-release-handoff-20260825`) |
| ESP32 Debug/Size/Performance | Not rebuilt after final capacity change | **Current evidence required** |
| Portable ZIP / NSIS installer | Scripts exist; final package gate not rerun | **Required before tagged release** |

Final ESP32 Release evidence:

- project: `examples/pseudo3d_racer/Racer.fglproject` (3 entities, 2 assets);
- payload: 7,121 bytes, SHA-256
  `7755fd0ea6430c7be0cf36ccfe3988d10b43c5b7b574c158b5c860981b7a8721`;
- program storage: 594,689 bytes / 3,145,728 bytes (18.905%);
- global static RAM: 108,208 bytes; reported dynamic RAM remaining: 219,472 bytes;
- binary: 595,056 bytes, SHA-256
  `5e0b320f6ace2bf0491f49575eb83181e6e02f16390935159420ef4a77ac253e`;
- `uploadPerformed=false`.

The first final compile exposed a 20,624-byte DRAM overflow caused by unfinished runtime expansion.
The target/host capacity gates were reduced together; the next managed compile passed with the
numbers above.

## Tests Actually Run

On 2026-08-25:

1. Incremental full Qt-independent Release build: exit 0.
2. Incremental full Qt Release build: exit 0 after fixing one `-Wshadow` diagnostic in the new
   CustomWindow refresh path.
3. Eight targeted CTest cases, all passed in 31.42 seconds:
   - `fabgl_engine_tests`
   - `fabgl_project_build_pipeline_tests`
   - `fabgl_hardware_diagnostic_contract_tests`
   - `fabgl_source_package_contract_tests`
   - `fabgl_project_extension_module_tests`
   - `fabgl_extension_service_panel_tests`
   - `fabgl_studio_play_session_tests`
   - `fabgl_studio_smoke_tests`
4. `fabgl_asset_pipeline_tests`: **49/53 passed, 4 failed**. Do not call this suite passed.
5. Managed ESP32 Release export/compile: passed after the DRAM-capacity correction.
6. Read-only serial detection: one `COM5` CH340 (`VID_1A86&PID_7523`) candidate; the port was not
   opened by detection.

The four current asset/project test failures are:

- `esp32_capability_contract_matches_the_allocation_free_runtime_limits`: stale expectation that
  `fabgl.Collider2D` is `KnownButNotPorted`;
- `esp32_capability_contract_distinguishes_unported_and_unknown_components`: constructs
  Collider2D and expects rejection although the bounded subset is now accepted;
- `esp32_export_rejects_unported_scene_components_and_visual_script_assets`: same stale Collider2D
  rejection assumption (visual-script rejection remains required);
- `project_prepare_and_esp32_export_share_canonical_crop_resize_and_storage`: expected exported
  image dimensions 2×2 but the canonical prepared/exported artifact no longer matches that fixture.

## Tests Still Required

- Update the three capability fixtures to exercise an actually unported component and add positive
  tests for the newly accepted bounded Collider/Rigidbody/Particle/UI subset.
- Diagnose the canonical crop/resize fixture; compare prepared and exported FGLI metadata before
  changing either implementation or expectation.
- Run the current full 28-test Qt CTest matrix after those fixes.
- Rebuild current ESP32 Debug, SizeOptimized and PerformanceOptimized profiles.
- Run Linux/GCC+Qt and Windows/MSVC CI jobs, package smoke, installer/portable checks and source
  archive verification from the committed tree.
- Run renderer golden/replay and performance suites only after functional regressions are closed.
- Execute bounded hardware diagnostics and soak separately; no long soak was run in this final pass.

## Hardware Validation Required

The user previously confirmed `COM5` and `olimex-esp32-sbc-fabgl-revb`; 2026-08-25 read-only
detection again saw a high-confidence CH340 candidate. The new final binary was **not uploaded**.
Evidence under `evidence/hardware/2026-08-13-com5-pseudo3d-racer-runtime/` belongs to an older
firmware checkpoint and must not certify this commit.

For the current commit, review the exact build result, run uploader `-DryRun`, then (with the board
still physically confirmed) upload and capture structured serial output. A human must separately
record:

- board label/revision and correct COM port;
- visible VGA initialization/image and frame stability;
- PS/2 keyboard and mouse events;
- audible audio output and quality;
- microSD mount/read/write/save/restore behavior;
- PSRAM presence only with the experimental PSRAM profile;
- reset/boot/upload reliability, FPS, free heap/largest block;
- cold boots and an appropriately bounded soak.

All parsers intentionally keep `hardwareVerified=false` where visual/audible/manual proof remains.

## Known Bugs

1. The four current `fabgl_asset_pipeline_tests` failures listed above.
2. ESP32 export can appear to fail with no CLI output if an old CodeBlocks/MinGW
   `libstdc++-6.dll` precedes MinGW 13 on `PATH`; use the environment command in `BUILDING.md`.
3. External-SD asset packs are not consumed by the live firmware yet.
4. Shared lifecycle phases are not used by the live firmware yet.
5. Current extension CustomWindow behavior compiles and generic extension tests pass, but its
   refresh/unload product path lacks a dedicated Qt regression.

## Technical Debt

- `apps/studio/src/MainWindow.cpp` and `platforms/fabgl/firmware/ProjectRuntime.h` have accumulated
  too many responsibilities; split only after current behavior is covered, not via a broad rewrite.
- `RuntimeProject` stores every supported component field per entity and is expensive in internal
  RAM. Introduce typed fixed pools/union-like storage before raising target capacities.
- Several progress/final-audit documents predate this handoff and overstate a few parity paths;
  this document and the root README take precedence until they are regenerated from evidence.
- Upstream FabGL 1.0.9 emits known return/narrowing/macro warnings under the pinned compiler.
- Generated assets and authoring source helpers need a single documented regeneration contract to
  avoid fixture/source drift.

## Experimental Features

- low-poly software 3D, TPS and Physics3D;
- advanced visual scripting debugging beyond validation/bytecode execution;
- trusted source extension services and custom windows;
- PSRAM-enabled ESP32 profile;
- external-SD project asset streaming until firmware integration is complete;
- live target parity for the newly added physics/particle/UI subset.

## Important Architecture Entry Points

- `CMakeLists.txt`, `CMakePresets.json`, `scripts/build_desktop.ps1`: host build/release surface.
- `apps/studio/src/MainWindow.cpp`: editor composition and workflow wiring.
- `apps/studio/src/ProjectDocument.cpp`, `SceneDocument.cpp`, `ComponentInspector.cpp`: authoring
  documents and reflected editing.
- `engine/include/fabgl/` and `engine/src/`: portable engine APIs/implementations.
- `tools/project_runtime/`: PC project runtime, native/visual modules and extension service host.
- `tools/project_cli/esp32_capabilities.*`, `esp32_export.*`: target gate/export contract.
- `platforms/common/LifecycleScheduler.h`: intended shared PC/ESP32 loop contract.
- `platforms/fabgl/firmware/firmware.ino`, `ProjectRuntime.h`: live firmware/runtime.
- `scripts/build_project.ps1`, `build_esp32.ps1`, `upload_esp32.ps1`: end-to-end build/deploy.
- `tests/CMakeLists.txt` and `tests/studio/`: test inventory and Qt smoke entry points.
- `docs/decisions/`: architectural rationale; especially ADRs 0004, 0006–0009 and 0016–0019.

## Recommended Next Steps

1. Fix the four current asset/project test failures without reverting the accepted bounded runtime
   subset; run the full 28-test Qt matrix.
2. Wire `LifecycleScheduler` into `firmware.ino`, with host tests proving identical phase order,
   fixed-step clamping and failure behavior.
3. Wire `ProjectAssetSdAdapter`/`BoundedStorageReader` into firmware setup and AssetStreaming phase;
   prove that render/audio hot paths perform zero physical filesystem reads.
4. Rebuild all ESP32 profiles, perform guarded COM5 upload/serial diagnostics, then complete manual
   VGA/input/audio/SD/PSRAM observations.
5. Run the managed portable+NSIS release gate and remote Windows/Linux CI; publish a pre-release,
   not a stable 1.0.

## Resume Instructions

1. Start from remote `main`; read `README.md`, this file, `ARCHITECTURE.md`, `BUILDING.md` and ADR
   0016–0019 before editing.
2. Confirm a clean worktree and run `git log -1 --oneline`. Do not delete ignored `out/` evidence
   until hashes/results needed for comparison are recorded.
3. On this Windows machine prepend `.toolchains\Qt\Tools\mingw1310_64\bin` to `PATH`, then build
   `out/build/root-gcc13` and `out/build/root-streaming-qt` incrementally.
4. Reproduce `fabgl_asset_pipeline_tests` and fix its four named failures first.
5. Keep COM5 closed until the current firmware is rebuilt, `upload_esp32.ps1 -DryRun` matches its
   hash, and the Olimex Rev B identity is physically reconfirmed.
