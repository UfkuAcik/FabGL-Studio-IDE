# Milestone 12 report

## Milestone

**M12 — TPS and low-poly 3D. Status: partial and experimental.**

## Completed work

The PC technology renderer transforms indexed triangles, clips/culls, applies flat lighting,
painter-sorts, and fills a framebuffer. TPS framework logic includes camera-relative movement and
target selection. Profiling foundations record sourced measurements and budgets.

## Changed files

`renderers/*lowpoly*`, `frameworks/*tps*`, `engine/*profiler*`, TPS example, and tests.

## Architecture decisions

All low-poly/TPS surfaces are labeled Experimental; PC throughput is not evidence of ESP32 speed.

## Commands run

The low-poly demo was run headless and its golden checksum passed. Framework/profiler assertions
passed in CTest.

## Test results

- Passed: experimental low-poly golden, TPS framework, and profiler assertions.
- Failed: 0 release CTest programs.
- Skipped: integrated TPS game and all ESP32 performance/visual checks.

## Remaining work

Character/world collision, third-person camera collision, animated character, combat, LOD/texture
limits, scene integration, profiler capture UI, and measured hardware budgets remain. No board
performance claim is made.
