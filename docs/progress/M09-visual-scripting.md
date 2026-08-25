# Milestone 9 report

## Milestone

**M9 — Visual scripting. Status: complete for v1.**

## Completed work

The engine exposes typed event/flow/control/variable/math/reference/gameplay node categories,
required-pin metadata, layout/comment data and stable graph identity. Validation rejects type,
flow ambiguity, cycle, duplicate, missing-reference and capacity faults. The compiler emits bounded
deterministic bytecode; host operations require an explicit validated callback table.

The Qt Visual Script panel creates/opens/saves `.fglvisual`, edits nodes/connections, validates and
compiles the real engine graph. Typed host nodes are no longer hidden: callback/payload and
asset/entity/component GUID references are authorable, with the same bounded callback schema used
by Studio Play and the PC player. `ProjectVisualHost` provides real InputMap, reflected
scene/entity/component, streaming audio, Animator, RuntimeUI, and non-blocking delay integration
with bounded diagnostics and retained-resource lifetimes. The strict v1 reader/writer is canonical
and rejects unknown versions/types, malformed schemas, trailing data and target-limit violations.

## Test results

Registry, source round-trip/corruption, compiler and VM tests pass under strict MinGW 13.1. This is
a bounded desktop v1 graph runtime, not an unrestricted general-purpose language or remote
debugger. The current ESP32 capability gate rejects visual-script assets because the firmware does
not execute visual bytecode; its gameplay path is the explicit portable C++ companion ABI.
