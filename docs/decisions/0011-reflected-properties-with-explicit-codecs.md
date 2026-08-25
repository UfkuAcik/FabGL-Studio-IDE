# ADR 0011: Use registered reflected properties with explicit codecs

- Status: Accepted
- Date: 2026-08-13

## Context

Inspector editing, scene migration, prefab overrides, scripts, and target validation need the same
property vocabulary. Compiler RTTI or raw object memory cannot provide stable IDs, portable
serialization, editor hints, or bounded validation.

## Options considered

1. Serialize component memory and rely on C++ RTTI.
2. Hand-code every editor and serializer path independently.
3. Register stable component/property IDs, typed values, hints, validators, and explicit codecs.

## Decision

Choose option 3. Reflection metadata is registered explicitly; serializers switch over the closed
property type set; references, lists, curves, finite numeric values, and editor hints are validated
centrally before mutation.

## Rationale

A closed value model makes schema behavior reviewable and keeps editor changes from silently
changing persisted layout or target memory.

## Positive consequences

- Inspector, scripts, prefab overrides, and scene codecs share one contract.
- Unknown or invalid values fail before partially mutating a scene.
- Stable IDs survive C++ names and field layout changes.

## Negative consequences

- New property kinds require coordinated registry, codec, Inspector, and migration changes.
- Registration boilerplate is explicit.

## Reconsider when

Generate registration code only after the output is deterministic, reviewable, and covered by
compatibility fixtures; do not replace stable IDs with compiler-specific RTTI.
