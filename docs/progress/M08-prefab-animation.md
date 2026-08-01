# Milestone 8 report

## Milestone

**M8 — Prefab and animation. Status: partial.**

## Completed work

Runtime code supports prefab assets/instances, property overrides, apply/revert/unpack-oriented
data operations, animation curves/events, clips, controller states, transitions, and deterministic
evaluation. Unit coverage passes.

## Changed files

`engine/include/fabgl/prefab/`, `engine/src/prefab.cpp`, `engine/include/fabgl/animation/`,
`engine/src/animation.cpp`, and associated tests.

## Architecture decisions

Prefab identity and dependencies use stable GUIDs; runtime structures are independent of Qt.

## Commands run

The engine CTest program containing prefab/animation assertions passed.

## Test results

- Passed: prefab and animation runtime assertions.
- Failed: 0 release CTest programs.
- Skipped: Qt prefab/Animator/Timeline editing and integrated animated-game acceptance.

## Remaining work

Full hierarchy serialization, nested-prefab dependency loading, added/removed component overrides,
missing-prefab UX, Animator/Timeline editors, preview/scrub, and an integrated animated example are
not complete.
