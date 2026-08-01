# FabGL Studio roadmap

Status values are evidence based: **complete** means its acceptance checks passed; **active**
means implementation is in progress; **planned** means it is not advertised as stable.

| Milestone | Scope | Status | Exit evidence |
|---|---|---|---|
| 0 | Discovery, architecture, licensing, pinned toolchains, risk register, CI | complete | Reproducible configure, manifest checks, and documented decisions |
| 1 | Qt editor shell, projects, docks, layouts, themes, console, settings | partial | Source implemented; Qt unavailable for local build/run |
| 2 | Loop, ECS, transform, reflection, scene I/O, resources, logging | complete | Headless lifecycle, hierarchy, and round-trip tests pass |
| 3 | PC platform, input/audio foundation, 2D software renderer, demo | partial | Native Windows/headless deterministic demo; no audio device backend |
| 4 | Hierarchy, Inspector, Scene/Game views, gizmos, play state, undo | partial | Integrated Qt source; local graphical acceptance blocked by missing Qt |
| 5 | Asset DB/import/cache, image/audio/font/tilemap, packer, budgets | partial | Image/WAV/GUID/pack tests pass; font/thumbnail automation incomplete |
| 6 | Managed ESP32/FabGL toolchain, firmware, upload, monitor, diagnostics | partial / hardware blocked | Locked smoke compile passes; no identified-board upload/HIL |
| 7 | C++ component scripts, code editor, build diagnostics, clangd | partial | Versioned reflected script API/generator compiles; clangd/build glue incomplete |
| 8 | Prefabs, overrides, animation clips/controller/timeline | partial | Runtime override/animation tests pass; visual editor incomplete |
| 9 | Validated visual graph and compact bytecode | partial | Validator/compiler/VM tests pass; node editor integration incomplete |
| 10 | Raycast renderer/editor/FPS framework and demo | partial | Deterministic PC FPS replay passes; map editor/ESP32 playtest absent |
| 11 | Racer renderer/track editor/framework and demo | partial | Deterministic PC racer/framework pass; track editor/opponent AI incomplete |
| 12 | Experimental low-poly/TPS renderer and profiler | partial | PC technology demo passes; hardware performance unmeasured |
| 13 | Material, particle, UI, memory/profiler, profiles, local packages | partial | Runtime foundations tested; specialist editor panels incomplete |
| 14 | Recovery, first-run setup, examples, portable ZIP and installer | partial | Ten examples and staged ZIP smoke pass; NSIS/clean-machine test unavailable |
| 15 | Full test/static/performance/doc/release pass | partial | Debug/Release tests green; Qt/HIL/soak/static host runs remain |

## Delivery rules

Each milestone keeps the main build green, adds automated checks in proportion to risk, and
writes a report under `docs/progress/`. Hardware checks may be skipped only when no board is
positively identified; the firmware and procedure remain deliverables. Experimental features
remain visibly labeled in code, UI, examples, and documentation.

## Cross-cutting backlog

- Windows 10/11 is the first packaged desktop target; Linux stays build-tested.
- All source formats need migration, corruption, missing-reference, Unicode, spaces, and
  relative-path tests.
- Build profiles cover PC Debug/Release and ESP32 Debug/Release/size/performance variants.
- Demos cover empty, platformer, top-down shooter, raycast FPS, racer, TPS technology, UI,
  audio, animation, and streaming.
- Release evidence includes binary sizes, test counts, checksums, dependency licenses, and a
  clean repository status.
