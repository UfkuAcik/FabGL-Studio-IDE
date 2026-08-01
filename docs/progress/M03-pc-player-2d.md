# Milestone 3 report

## Milestone

**M3 — PC player and 2D renderer. Status: partial.**

## Completed work

Implemented deterministic framebuffer drawing, 2D sprites/tilemap primitives, input-driven demo
logic, a native Win32 window, and a cross-platform headless mode. Golden framebuffer/demo tests
pass; interactive Windows controls are wired.

## Changed files

`renderers/`, `apps/player_pc/`, `RENDERERS.md`, and `tests/rendering_tests.cpp`.

## Architecture decisions

ADR 0001 shares engine/renderer logic across hosts; an SDL backend decision is still pending.

## Commands run

All ten demos were run headless during release integration. The rendering CTest passed with exact
checksums; the release suite passed 8/8 CTest programs.

## Test results

- Passed: headless demos, renderer goldens, and ten-example integration replay.
- Failed: 0 release CTest programs.
- Skipped: SDL/audio-device and recorded interactive playtest.

## Remaining work

SDL portability, a real host audio output backend, richer tilemap/camera content, and a recorded
interactive playtest remain. No PC result is presented as ESP32 parity or performance.
