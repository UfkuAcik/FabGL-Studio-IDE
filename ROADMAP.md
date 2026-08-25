# FabGL Studio roadmap

> **2026-08-25 checkpoint:** [docs/HANDOFF.md](docs/HANDOFF.md) is the current continuation
> record. The next release gates are the four named asset/project test failures, firmware adoption
> of the shared lifecycle/SD-streaming foundations, all-profile ESP32 rebuilds and current HIL.

This roadmap records product state, not aspirations. **Complete** means the repository contains a
working implementation with automated evidence. **Software complete / HIL pending** means the
safe build and diagnostic path is implemented but a physical board is still required. Features
labelled **Experimental** stay outside stable compatibility promises.

| Milestone | Scope | Status | Current evidence |
|---|---|---|---|
| 0 | Discovery, architecture, licensing, pinned tools, risks, CI | Complete | Environment manifests, ADRs, license inventory, source/document checks |
| 1 | Qt editor shell, project lifecycle, docks, layouts, themes, settings | Complete | Managed Qt 6.8.3 build plus offscreen Qt smoke tests |
| 2 | Loop, ECS, transforms, reflection, scene I/O, resources, logging | Complete | Strict C++20 lifecycle, hierarchy, v1 migration and v2 round-trip tests |
| 3 | PC player, input/audio platform, software 2D renderer | Complete | Native Win32 window/audio, deterministic headless replay and renderer goldens |
| 4 | Hierarchy, reflected Inspector, Scene/Game views, gizmos, play, undo | Complete | Real editor actions and Qt integration tests; play uses an isolated scene copy |
| 5 | Asset database/import/cache, optimized assets and `.fglpack` | Complete for supported sources | Image/audio/font/tilemap/OBJ import, atlas/thumbnail, cache and pack tests |
| 6 | Managed ESP32/FabGL toolchain, firmware, upload, serial diagnostics | Software complete / HIL pending | Locked profiles compile real firmware; upload remains explicit-confirm only |
| 7 | C++ component scripting, IDE diagnostics and project build glue | Complete with restart fallback | Generated CMake integration and external SDK compile/link tests; unreliable hot reload is intentionally replaced by restart |
| 8 | Prefab hierarchy/overrides and animation/controller authoring | Complete | Prefab v1→v2 plus `.fglanim`/`.fglcontroller` strict round-trip/runtime tests |
| 9 | Visual node editor, validation, source format and bounded VM | Complete | `.fglvisual` canonical round-trip, corruption tests, Qt editor and typed VM tests |
| 10 | Raycast renderer, FPS framework and demo | Complete runtime path | Exact PC replay/goldens and framework tests; no physical ESP32 playtest yet |
| 11 | Pseudo-3D racer renderer/framework/demo | Complete runtime path | Exact PC replay/goldens and lap/vehicle tests; specialist track UI is not a release gate |
| 12 | Experimental low-poly renderer, TPS framework and profiling | Complete as Experimental | Software tests and deterministic demo; hardware performance is unmeasured |
| 13 | Advanced editor tools, budgets, save and local packages | Alpha checkpoint | Animator, specialist editors, profiler/memory, package trust/install, save and bounded extension hooks are implemented |
| 14 | Recovery, examples, portable ZIP and Windows installer | Alpha checkpoint | Recovery/trust tests and ten examples exist; current clean package/installer run is pending |
| 15 | Full build/test/package/documentation quality pass | In progress | Windows core/Qt Release and ESP32 Release build; four current test failures and cross-platform/HIL gates remain |

## Release boundary

The stable Windows software path targets managed Qt 6.8.3 and MinGW 13.1. The ESP32 compiler is
managed separately and is never used as the host compiler. The Release, SizeOptimized and
PerformanceOptimized firmware profiles are real builds, not simulated artifacts.

The following evidence cannot be manufactured by software-only tests and therefore remains an
explicit release note rather than a hidden failure:

- physical Olimex ESP32-SBC-FabGL Rev B upload and serial observation;
- VGA, PS/2 keyboard/mouse, audio, SD and optional PSRAM checks;
- on-device FPS/memory measurements and 30-minute/2-hour/8-hour soak evidence;
- code signing and independent Windows 10/11 clean-machine certification.

Low-poly 3D, TPS and Physics3D are labelled Experimental. Linux raster source decoding, TTF and
glTF source import are rejected with clear diagnostics instead of silently producing incomplete
assets. Automatic gameplay hot reload is not promised; the supported workflow rebuilds and
restarts the PC player while preserving the authoring scene.
