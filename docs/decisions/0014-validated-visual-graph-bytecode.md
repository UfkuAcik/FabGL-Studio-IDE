# ADR 0014: Compile visual graphs to validated bounded bytecode

- Status: Accepted
- Date: 2026-08-13

## Context

Visual scripting needs readable graphs in source control, useful editor metadata, predictable event
dispatch, and a runtime representation that cannot allocate or execute without bounds on ESP32.

## Options considered

1. Walk editor graph objects directly at runtime.
2. Generate and compile C++ for every edit.
3. Validate typed graphs and compile each event root to compact bytecode for a bounded VM.

## Decision

Choose option 3. Typed pins, required connections, cycles, references, and host calls are validated
before compilation. The VM enforces program, stack, variable, call, and per-phase instruction
limits and exposes effects only through an explicit callback table.

## Rationale

Bytecode removes layout/comments from runtime state, gives deterministic diagnostics, and supports
the same execution semantics on PC and ESP32 without a target compiler in the edit loop.

## Positive consequences

- Corrupt or oversized graphs fail before execution.
- Event programs and persistent variables have explicit lifetimes.
- Host capabilities are auditable and testable.

## Negative consequences

- The node/type registry and VM must evolve together.
- Debug information needs an explicit source mapping.

## Reconsider when

Add JIT or native code generation only if representative profiling shows the bounded VM is a
dominant cost and source mapping, validation, and target limits remain equivalent.
