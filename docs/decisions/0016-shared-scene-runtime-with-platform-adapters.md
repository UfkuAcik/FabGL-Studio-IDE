# ADR 0016: Share scene gameplay and runtime state behind platform adapters

- Status: Accepted
- Date: 2026-08-13

## Context

PC preview and ESP32 firmware must run project content rather than unrelated demos, while window,
audio, storage, input devices, and FabGL APIs are necessarily platform specific.

## Options considered

1. Maintain separate desktop and firmware game implementations.
2. Put all platform APIs behind preprocessor branches throughout gameplay.
3. Share serialization, scene lifecycle, visual/native script semantics, framework state, and
   renderer inputs; inject bounded platform adapters and asset resolvers.

## Decision

Choose option 3. Host and target load the same stable IDs and scene/component meaning. Target
export compiles a bounded project payload; adapters provide input, presentation, audio, storage,
and diagnostics without redefining gameplay state.

## Rationale

Parity is testable only when meaningful state transitions are shared. Adapters isolate unavoidable
hardware differences and let headless tests exercise the same contracts.

## Positive consequences

- Project behavior is not a hard-coded preview demo.
- Runtime tests can substitute deterministic adapters.
- Hardware telemetry can identify adapter failures separately from project parsing.

## Negative consequences

- Target caps intentionally reject some desktop content.
- Every adapter needs conformance evidence.

## Reconsider when

Split a system only after hardware measurements prove a shared implementation cannot meet target
budgets; retain compatible serialized meaning and observable behavior.
