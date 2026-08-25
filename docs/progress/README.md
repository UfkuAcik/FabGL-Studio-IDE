# Milestone evidence index

Evidence was refreshed on 2026-08-09. A status is based on executable repository evidence:

- **complete**: the software exit criteria have direct build/test coverage;
- **active**: final integration or packaging evidence is still being collected;
- **software complete / HIL pending**: the implementation and non-destructive tests pass, while a
  real board or peripheral is required for the remaining observation;
- **experimental**: implemented and tested, but intentionally outside stable compatibility or
  hardware-performance promises.

| Milestone | Status | Primary evidence |
|---:|---|---|
| 0 | complete | ADRs, risks, licensing, manifests, CI and documentation contracts |
| 1 | complete | Managed Qt editor build and offscreen interaction/settings tests |
| 2 | complete | ECS/lifecycle/reflection, strict scene v2 and frozen v1 migration tests |
| 3 | complete | Native PC player/audio plus deterministic renderer goldens |
| 4 | complete | Reflected Inspector, gizmos, play isolation and undo/redo Qt tests |
| 5 | complete for supported inputs | Asset DB/cache, image/audio/font/tilemap/OBJ/thumbnail and pack tests |
| 6 | software complete / HIL pending | Locked real firmware profiles and safe detection/upload/monitor contracts |
| 7 | complete with restart fallback | Generated script glue and installed-SDK external compile/link diagnostics |
| 8 | complete | Nested prefab hierarchy plus strict prefab/clip/controller formats and Animator runtime/editor |
| 9 | complete | Typed node registry/editor, strict `.fglvisual`, compiler and bounded VM |
| 10 | complete runtime path | Raycast renderer/FPS framework deterministic replay |
| 11 | complete runtime path | Racer renderer/framework deterministic replay |
| 12 | experimental | Low-poly/TPS/Physics3D software tests; no hardware budget claim |
| 13 | active | Animator, memory/profiler, save persistence and local package manager integration |
| 14 | active | Recovery/trust, ten examples, portable/NSIS pipelines; final artifact pass pending |
| 15 | active | Strict suites and labeled PC performance gate are green; final package run pending |

Physical upload is intentionally not performed from detection alone. It requires the user to
confirm both the exact serial port and the exact board profile. Consequently, HIL and soak entries
remain unverified until that boundary is satisfied; offline fixtures are never reported as
hardware evidence.
