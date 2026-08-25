# Autonomous specification completion audit

> **Historical snapshot.** This audit was written on 2026-08-13 and is not the current release
> verdict. The 2026-08-25 [HANDOFF.md](HANDOFF.md) records the latest builds, four current failing
> tests, incomplete firmware lifecycle/SD-streaming integration and hardware boundaries. Where the
> two documents differ, the handoff takes precedence.

Evidence date: 2026-08-13. This audit maps the 53-section autonomous-development instruction to
repository behavior. It distinguishes implementation from hardware observation and does not count
mock screens, offline logs or estimated ESP32 timings as physical evidence.

## Status vocabulary

- **Implemented and tested**: executable code exists and its important failure paths have tests.
- **Implemented; final integration pending**: the feature is present, while the final clean package
  run is still required before release hand-off.
- **Experimental**: usable, bounded and tested in software, but deliberately outside stable parity
  or performance promises.
- **External evidence required**: completion needs a confirmed device, peripheral, remote runner,
  signing identity or clean machine and cannot be fabricated inside the repository.
- **Explicit fallback**: the requested outcome is met through the safe fallback allowed by the
  specification, with the limitation visible to users.

## Findings and remediation

| Specification area | Gap found in the previous repository state | Remediation and current evidence | Remaining boundary |
|---|---|---|---|
| Repository/toolchain (1–6, 31–33) | Host Qt/installer tooling was absent and old GCC 8 claims were stale | Managed Qt 6.8.3, MinGW 13.1 and NSIS 3.12 bootstrap/build/package paths; GCC 10+ host gate; locked ESP32/FabGL manifests | Independent clean-machine certification |
| Source formats (7) | Scene v1 dropped components; prefab, material, animation/controller and visual formats were only names in documentation | Scene v2 with all reflected property types and v1 migration; strict `.fglprefab`, `.fglmaterial`, `.fglanim`, `.fglcontroller` and `.fglvisual`; bounded parse/corruption/canonical round-trip tests | Track format is in final integration; tileset source metadata remains part of the asset pipeline rather than a standalone stable editor format |
| Engine core/ECS (8) | Transform-centric serialization and incomplete runtime binding | Lifecycle-safe ECS, hierarchy/cycle checks, reflected scene v2, rollback-safe loop and `SceneRuntime` bindings | None for documented 2D runtime scope |
| Reflection/Inspector (9) | Inspector edited only a small hard-coded subset | All reflected property kinds have typed Qt editors; add/remove component and enabled state are real mutations | Custom third-party editors require a trusted plugin |
| Prefab (10) | Flat in-memory components; no hierarchy persistence or migration | Nested dependency resolution, hierarchy validation, overrides/apply/revert/unpack, stable GUIDs, `.fglprefab` v1→v2 and malformed-input tests | Scene-side bespoke prefab-authoring UX is intentionally smaller than Unity’s |
| C++ scripting/code editor (11) | Generated scripts were not automatically compiled; diagnostics/editor tooling was incomplete | Deterministic project CMake glue, installed SDK validation, strict external compile/link, safe diagnostics; real multi-tab Qt editor/build-line navigation; automatic save/reload-triggered verified full restart for Studio Playing/Paused and external PC player with build coalescing | In-process object-preserving hot reload is replaced by the safer full restart; clangd is not bundled |
| Visual scripting (12) | Minimal arithmetic graph with no source format or real editor persistence | Typed registry/categories/pins, comments/layout/GUID metadata, strict `.fglvisual`, validation/compiler/bounded VM, explicit host callback table and Qt New/Open/Save editor | Remote breakpoint debugger is not claimed |
| Asset database/pipeline (13–15) | No cache-key registry, structured importers, atlas/thumbnail or compressed audio path | GUID-preserving DB, importer registry/cache, image crop/grid/atlas, thumbnail, WAV target conversion/compression, CSV/JSON tilemap, OBJ, BDF and deterministic `.fglpack` | TTF/glTF and non-Windows raster source decode fail explicitly |
| Audio (15) | Mixer had no PC device backend | Bounded buses/voices/resampling/priority plus native WinMM output; Master/Music/SFX/UI buses | Physical ESP32 audio quality requires HIL |
| Input (16) | Runtime action/axis map existed without complete project authoring persistence | Context/action/axis/rebinding runtime is tested; project v2 input/packages/targets/assets persistence is lossless in CLI and Qt | Physical PS/2/adapter behavior requires manual HIL |
| Scene/editor UI (17–18) | Qt target had only a partial shell and several decorative actions | Scene/Game/Hierarchy/Inspector/Project/Asset/Console/Code/Visual/Animator/Profiler/Build/Memory docks; required menus/toolbars/layouts/themes have real slots | Human DPI/accessibility polish remains release QA |
| Play mode/Game View (19) | State buttons did not execute the complete runtime workflow | Isolated runtime scene, Play/Pause/Step/Stop, target resolution/aspect/integer/palette/FPS/speed/pixel/fullscreen controls | ESP32 simulation is an estimate, visibly labelled |
| Rendering (20) | Limited software paths and weak parity evidence | 2D/tilemap, raycast, racer and bounded low-poly renderers; exact golden checksums for ten modes | Low-poly hardware performance is Experimental/HIL |
| Materials (21) | Runtime properties existed without durable asset format | Validation diagnostics, renderer compatibility, deterministic cost estimate, renderer application and strict `.fglmaterial` source format | Dedicated visual material preview panel is not a stable acceptance gate |
| Animation (22) | Basic bool transition runtime and in-memory Qt panel | Typed bool/trigger/int/float conditions, exit time/blending/events, strict clip/controller formats with explicit GUID resolver, runtime binding and state/parameter/transition/preview/timeline Qt panel | None for the documented v1 scope |
| Physics (23) | Narrow collision support | Deterministic AABB/circle dynamics, triggers/layers/gravity/mass/restitution, ray/point/overlap queries; bounded Physics3D overlap/raycast | Physics3D is explicitly Experimental, not a full solver |
| Runtime UI/particles/AI (24–26) | Foundation-only structures | Runtime widgets/layout/focus/theme/input, fixed-pool particle emitters, A*, state machine, waypoint/LOS/racing behaviors and scene bindings | Specialist visual authoring panels are future UX expansion |
| Frameworks/examples (27–28) | Demos were procedural previews without full project/scene replay | Platformer, top-down, FPS, racer and TPS frameworks; ten versioned projects with exact deterministic replay checksums | TPS remains Experimental |
| Profiler/budgets (29–30) | Estimated and measured values were easy to conflate | Separate measured PC and estimated ESP32 channels, histories, budgets and Qt memory analyzer | Real ESP32 measurements require HIL |
| Build pipeline/toolchain (31–34) | Qt did not invoke complete real workflows; no robust profiles/port gate | Unified project validation/package/script/runtime or ESP export/pack/compile pipeline; PC Debug/Release and four ESP profiles; program+argv execution; size/map/hash result; upload confirmation and serial controls | Cancel is process-boundary termination; full serial monitor remains interactive |
| Save system (35) | Memory-only slots and locale-sensitive header parsing | Portable PC parser/checksums/migrations and atomic `FileSaveStorage`; allocation-free ESP32 v1 codec, stable-GUID scene/entity/player capture, explicit SD temp/backup adapter, multiple bounded slots and rollback/corruption tests | FAT/power-loss/card-wear durability requires physical testing |
| Packages/plugins (36) | Manifest-only registry; no filesystem install/remove or durable trust | Schema-2 typed manifest, deterministic lock, SHA-256 ownership, content-bound trust and safe local install/list/validate/remove CLI with hostile fixtures | Archive registry, signatures and dynamic loading are not claimed |
| Undo/recovery (37–38) | Recovery/autosave/safe mode absent | Command-based undo plus atomic editor saves, rotating recovery snapshots, unclean-session restore/discard, last-project recovery, corruption handling and safe mode | OS-kill behavior still benefits from manual destructive-process QA |
| Security (39) | Project build trust and package filesystem boundaries were incomplete | External-project trust gate, build/play disable until trusted, argument arrays, traversal/reparse checks, owned output roots, separate plugin/package trust and telemetry-off default | No sandbox can make arbitrary trusted native C++ harmless |
| Licensing/docs/ADRs (40, 43–45) | Reports and guides contradicted the implementation | GPL-3.0-or-later license set, notices/dependency inventory, API/user/build/hardware guides, ADRs and refreshed evidence index | Third-party inventory must be refreshed when dependencies change |
| Testing/CI/release (41–42, 46–53) | Qt, installer, current firmware profiles and many negative paths were unverified | Strict warnings, unit/integration/golden/replay/CLI/security tests, managed Qt job, Windows ESP32 job, portable ZIP and NSIS pipelines | Remote CI execution, signing, clean-machine QA and physical HIL/soak are external evidence |

## Explicitly unsupported or experimental behavior

The following are visible product limits, not silent stubs:

- in-process/state-preserving native C++ hot reload; the supported automatic PC fallback performs a verified full rebuild and restarts the same preview kind;
- bundled clangd/LSP and source-level debugger integration;
- TTF and glTF import, and non-Windows raster source decoding;
- online package registry, signed package verification and automatic native plugin loading;
- production 3D physics, skeletal animation and low-poly/TPS hardware parity;
- claims of physical VGA, PS/2, audio, SD, PSRAM, FPS or soak success without a confirmed board.

## Hardware evidence state

The user explicitly confirmed `COM5` and `olimex-esp32-sbc-fabgl-revb`. The final managed Racer
artifact was uploaded with the guarded uploader. Serial evidence proves the euler-tagged Scene v2
payload parsed, the project runtime loaded 3 entities/1 asset/6 bindings, and the real racer
update/render loop advanced with zero firmware `FAIL` records. Exact hashes and logs are indexed by
`evidence/hardware/2026-08-13-com5-pseudo3d-racer-runtime/result-final.json`. The generic parser
still reports `hardwareVerified=false`: no assistant claim substitutes for a human confirming the
VGA picture, audible output, physical peripherals/board label, or 30-minute soak.
