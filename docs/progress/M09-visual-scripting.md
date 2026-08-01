# Milestone 9 report

## Milestone

**M9 — Visual scripting. Status: partial.**

## Completed work

Implemented a typed graph model, validation issues, reference resolver hooks, deterministic compact
bytecode generation, and a bounded bytecode VM. Validation/compiler/VM unit assertions pass.

## Changed files

`engine/include/fabgl/visual/visual_graph.h`, `engine/src/visual_graph.cpp`, and engine tests.

## Architecture decisions

Invalid graphs do not compile; runtime executes validated compact bytecode instead of editor nodes.

## Commands run

The engine CTest program passed. No node-editor interaction or gameplay-scene integration test was
run.

## Test results

- Passed: graph validation, bytecode compiler, and VM assertions.
- Failed: 0 release CTest programs.
- Skipped: node editor, scene binding, and no-code playable mechanic.

## Remaining work

Node palette/canvas, context filtering, variables/functions/events, breakpoints/watch view,
runtime component binding, save format, and a no-code playable mechanic remain unimplemented.
