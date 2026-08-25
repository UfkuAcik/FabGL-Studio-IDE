# ADR 0008: Keep authoring formats readable and runtime representations bounded

- Status: Accepted
- Date: 2026-08-09

## Context

Editor data needs stable diffs and migration, while ESP32 runtime data and visual scripts need
predictable memory and execution limits. Loading permissive or partially understood data would
hide corruption and create different PC/target behavior.

## Options considered

1. Use one permissive JSON representation on host and target.
2. Store only opaque binary authoring files.
3. Use canonical versioned authoring text and compile validated bounded runtime data.

## Decision

Authoring serializers emit deterministic UTF-8 versioned text with stable GUIDs. Readers impose
input/count limits, reject duplicate IDs, non-finite values, bad references, unknown required
types and trailing data, and only perform explicit tested migrations. Visual graphs compile to a
validated compact bytecode with instruction/stack/variable/host-call limits; runtime host effects
go through an explicit callback table. Generated `.fglpack` data is indexed and checksummed.

## Rationale

Option 3 gives authors meaningful diffs and recovery while moving target-specific size, alignment,
and execution constraints to a deterministic compile step.

## Positive consequences

- Corruption and unsupported schema changes fail with contextual diagnostics.
- Runtime memory and instruction budgets can be checked before upload.
- Canonical output supports stable cache keys and reproducible tests.

## Negative consequences

New source-format versions require frozen migration fixtures. Unsupported source types fail with
actionable errors rather than best-effort guessing. Editor-only layout/comments never inflate the
runtime bytecode.

## Reconsider when

Change concrete encodings only when measured project size or load time justifies it; retain schema
versions, canonical output, bounded readers, migration fixtures, and deterministic packs.
