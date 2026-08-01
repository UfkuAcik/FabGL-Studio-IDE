# Milestone 2 report

## Milestone

**M2 — Engine core. Status: complete for the implemented v1 core.**

## Completed work

Implemented the fixed/variable engine loop, strong GUIDs, `Result/Error`, structured logging,
scene/entity/component lifecycle, transform hierarchy with cycle prevention and dirty propagation,
reflection, strict scene v1 serialization, and a budgeted resource cache.

## Changed files

`engine/include/fabgl/`, `engine/src/`, `engine/CMakeLists.txt`, and engine test sources in `tests/`.

## Architecture decisions

ADRs 0001–0003 define portability, ECS shape, and versioned serialization.

## Commands run

`cmake --build --preset dev --target fabgl_engine_tests` and CTest were run; the engine test
program passed. Scene round-trip, corrupt input, lifecycle, hierarchy cycle, transform, reflection,
resource, runtime, and advanced subsystem assertions are included.

## Test results

- Passed: engine test executable and full 6-program release CTest suite.
- Failed: 0 CTest programs.
- Skipped: arbitrary component-block round-trip because v1 does not support it.

## Remaining work

Scene v1 intentionally does not serialize arbitrary component blocks. Component serializer
registration and migrations are required before calling the broader authoring format complete.
