# ADR-0002: Editor-friendly object ECS with packed hot-path views

- **Status:** Accepted
- **Date:** 2026-08-01

## Context

Scenes require stable identity, reflection, prefab overrides, understandable diagnostics, and
ESP32 execution. A full archetype ECS would optimize desktop throughput but complicate
authoring and migration before performance evidence exists.

## Options considered

1. Deep inheritance game-object tree.
2. Archetype/chunk ECS.
3. Entities with owned components plus optional packed system views.

## Decision

Use strong entity IDs, component type IDs, a component lifecycle, reflected properties, and a
separate transform hierarchy. Systems may build preallocated packed views during scene load
for measured hot paths.

## Rationale

This model is straightforward to serialize and expose in Inspector while still allowing the
asset compiler to emit compact target data.

## Positive consequences

- Predictable lifecycle and editor integration.
- Stable references survive list reordering.
- Target optimization can remain internal to systems.

## Negative consequences

- General component lookup is slower than an archetype column.
- Extra validation is required when entities/components are destroyed.

## Reconsider when

Adopt a different storage strategy if profiler data shows component traversal, not rendering
or I/O, is a dominant target cost in representative scenes.
