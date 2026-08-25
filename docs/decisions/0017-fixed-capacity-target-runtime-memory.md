# ADR 0017: Use fixed-capacity ownership in target runtime paths

- Status: Accepted
- Date: 2026-08-13

## Context

ESP32 has constrained, fragmented memory and time-sensitive VGA/audio behavior. Unbounded parsing,
entity growth, particle allocation, visual bytecode, or asset caching can fail long after upload.

## Options considered

1. Use general heap allocation everywhere and handle failure opportunistically.
2. Forbid dynamic behavior entirely.
3. Give target-facing systems explicit capacities, validate them during export/load, and use fixed
   pools/arenas or caller-owned buffers during steady state.

## Decision

Choose option 3. Scene/project readers, entities, assets, bindings, visual VM state, particles,
renderer queues, and firmware payload parsing have hard limits. Host authoring may allocate, but
target compilation rejects content that cannot fit its selected profile.

## Rationale

Early bounded failure is diagnosable and reproducible; late heap fragmentation during a frame is
not. Profile-specific limits still permit richer PC authoring.

## Positive consequences

- Target peak memory is reviewable before upload.
- Steady-state update/render avoids unbounded allocation.
- Limit failures include the resource and selected profile.

## Negative consequences

- Capacities constrain content and require author-facing diagnostics.
- Some containers/code paths differ between authoring and firmware.

## Reconsider when

Adjust capacities from measured hardware telemetry or introduce a bounded arena allocator when it
reduces waste; never replace explicit limits with unchecked heap growth.
