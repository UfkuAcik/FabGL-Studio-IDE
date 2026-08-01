# Milestone 11 report

## Milestone

**M11 — Pseudo-3D racer. Status: partial.**

## Completed work

Implemented projected road scanlines with curve/hill/rumble/lane data, vehicle dynamics, ordered
checkpoints and lap state. The deterministic PC racer demo and framework tests pass.

## Changed files

`renderers/*racer*`, `frameworks/*racer*`, `apps/player_pc/demo.*`, racer example, and tests.

## Architecture decisions

Track segments and race rules are data/framework concerns rather than hard-coded renderer state.

## Commands run

The racer demo was run headless; rendering and framework CTest programs passed.

## Test results

- Passed: racer golden rendering, project validation/replay, and race framework assertions.
- Failed: 0 release CTest programs.
- Skipped: track editor, opponent game loop, and ESP32 play/performance.

## Remaining work

Track editor, object placement, opponents/AI, collisions, start countdown, results/restart UI,
weather/traffic, asset-backed demo content, and ESP32 validation are absent.
