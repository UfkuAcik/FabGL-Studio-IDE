# FabGL Studio 0.1.0 final engineering report

Evidence date: 2026-08-01. This is an engineering preview, not a stable 1.0 release.

## Product summary

FabGL Studio now has a portable C++20 engine, deterministic PC player, Qt 6 editor source,
asset/project/toolchain CLIs, a pinned Olimex ESP32-SBC-FabGL firmware path, ten example projects,
automated tests, CI definitions, documentation, and release packaging. The dependency-free host
targets and firmware compile were exercised. The graphical editor and physical board were not.

## Completed features

- Engine: loop, ECS/lifecycle, hierarchy, reflection, strict scene I/O, resources, input, 2D
  physics, animation, prefab overrides, audio mixer, save migrations/checksums, UI layout,
  particles, A* navigation, profiler/budgets, local package trust/dependencies, and visual-graph
  validation/bytecode VM.
- Scripting: versioned reflected `ScriptComponent` API and safe `new-script` generator; generated
  source compiled with strict warnings.
- Rendering/player: native Windows and headless framebuffer, sprite/tilemap 2D, raycast FPS,
  pseudo-3D racer, and experimental flat-shaded low-poly modes.
- Editor source: project/scene documents, docks, Hierarchy, Inspector transforms, Scene/Game views,
  selection/snap/move, asset browser/drop, clone-based Play/Pause/Step/Stop, undo/redo, code tabs,
  line numbers/highlighting/find/replace/go-to-line, build diagnostics, layouts/themes, and a
  profiler separating measured PC values from estimated ESP32 values.
- Assets/tools: Windows WIC PNG/JPEG/BMP import, WAV conversion, palette/dither/RLE, GUID-preserving
  asset moves/dependencies, deterministic `.fglpack`, Unicode paths, project migration/validation,
  and safe process argument models.
- FabGL: pinned manifest/checksums, repository-local bootstrap, compile-only build, explicit-confirm
  upload/monitor scripts, Rev B pin profile, diagnostic firmware, and offline log-fixture parser.

## Working example projects

All ten manifests validate and replay 180 deterministic frames against exact checksums: Empty,
Platformer, Top-Down, Raycast FPS, Pseudo-3D Racer, experimental TPS/Low-Poly, UI, Audio Mixer
Visualization, Animation, and Asset Streaming. The showcase projects use real procedural render
paths; the audio showcase visualizes mixer activity but has no host audio-device backend.

## PC build results

- Windows local Debug and Release: passed with GCC 8.1's experimental C++20 mode and warnings as
  errors.
- Native/headless player and four CLI executables: built and staged successfully.
- Qt 6 SDK was unavailable, so `FabGLStudio.exe`, GUI launch/visual QA, and Qt deployment were
  skipped. The local ZIP therefore contains the working player/CLIs, not the graphical editor.
- Linux was not run locally. CI defines Ubuntu 24.04 GCC + Qt 6 and Windows Server 2022 MSVC 2022
  + Qt 6.8.3 full build/test jobs; those remote jobs have not yet executed.

## ESP32 build results

- Arduino CLI 1.5.1, Arduino-ESP32 2.0.11, Olimex FabGL commit
  `04f328a10573297dd554f13be7f369cdee0f7a2b`.
- Reference profile: 4 MiB flash, huge-app partition, 240 MHz, DIO, PSRAM disabled.
- Program storage: 448,757 bytes. Global data: 25,920 bytes.
- Primary binary: 449,120 bytes; SHA-256
  `87137e737da22ad4e686a7974f8ac35edae881e58c1944c3f0ff794a5ab08a56`.
- This was exact FabGL/CLI integration validation using the user's installed 2.0.11 core, not a
  clean fully managed-core release attestation. `uploadPerformed=false`.

## Hardware tests

No board was positively identified. No upload, flash, erase, serial monitor, or COM operation was
performed. VGA, keyboard, mouse, audio, SD, PSRAM, FPS, reset, save/load, and soak checks remain
unverified. Synthetic offline PASS and expected-FAIL log fixtures test only the protocol parser and
always report `hardwareVerified=false`.

## Test results

- Debug CTest: 8 passed, 0 failed, 0 skipped.
- Release CTest: 8 passed, 0 failed, 0 skipped.
- Test binaries: 63 passing first-party assertions; integration additionally validates/replays ten
  projects and checks two offline hardware-log branches.
- Renderer goldens cover all ten preview modes.
- clang-format 18.1.8: 132 first-party files passed.
- Documentation contract: 44 required files and 5 ADRs passed.
- Workflow YAML/actionlint passed; action references were verified against their upstream tags.
- Full clang-tidy, sanitizer/leak, GUI automation, clean-machine, performance-regression, and
  long-duration soak runs were not executed locally.

## Packages

- Portable host ZIP: `out/packages/FabGL-Studio-0.1.0-Windows-AMD64.zip`, 530,546 bytes,
  SHA-256 `cb6b20ee0dec56b05609b5929de2acf536e8129b353f84b5c63b184542b3aa18`.
- Compile-only firmware evidence ZIP:
  `out/packages/FabGL-Studio-0.1.0-ESP32-SBC-FabGL-compile-only.zip`, 6,583,195 bytes,
  SHA-256 `b533c6ef11a91bac047ab97c847efa354512c38ba8c467bd056d3217888b1bb4`.
- Staged CLI smoke checks passed. NSIS was unavailable, so no installer was produced. A Qt-enabled
  portable package remains a CI/clean-machine deliverable.

## Known limitations

Component blocks beyond Transform are not yet serialized into scene v1. C++ scripts require
explicit target integration; clangd/hot reload are absent. The visual node editor, animation and
specialist material/particle/UI/track/map editors are incomplete. Linux raster source import uses
an explicit unsupported stub. There is no PC audio-device backend, 3D physics solver, crash
recovery/backup rotation, signed installer, or online package repository.

## Experimental features

Low-poly 3D and TPS are technology paths only. They use bounded painter-style rendering and
framework logic, but have no physical-board performance evidence. PSRAM-enabled firmware is an
explicit experimental profile and was not used for the reference compile.

## License

First-party code is GPL-3.0-or-later. The Olimex FabGL runtime is GPL-3.0-or-later; firmware
distribution must meet corresponding-source obligations. Exact dependency versions, licenses,
source URLs, and redistribution notes are in `THIRD_PARTY_LICENSES.md` and `docs/LICENSING.md`.

## Repository status

Branch `main` contains a local, milestone-oriented commit series. No remote is configured, so
nothing was pushed. Generated build, package, download, and managed-toolchain paths are ignored.
Approximately 1.385 GiB of ignored validation cache/intermediate data remains because the verified
recursive cleanup command was rejected by execution policy; no workaround was attempted.

## Next-version recommendations

1. Run the CI matrix and fix any Qt/MSVC/Linux-only compile findings.
2. Install a matching Qt SDK, complete GUI tests/visual QA, and produce a Qt-deployed clean-machine
   ZIP plus NSIS installer.
3. Run a fully managed-core firmware build, then execute the explicit HIL checklist on a confirmed
   Olimex Rev B board.
4. Add component serialization v2/migrations, automatic script build registration and clangd.
5. Complete specialist editors, host audio, autosave/recovery, recorded-input replay, sanitizers,
   leak checks, regression budgets, and soak testing before declaring 1.0.
