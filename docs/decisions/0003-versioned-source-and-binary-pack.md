# ADR-0003: Versioned readable source formats and deterministic binary packs

- **Status:** Accepted
- **Date:** 2026-08-01

## Context

Authors need mergeable, recoverable project files, while ESP32 needs compact aligned data and
bounded loading. Using one format for both creates either poor authoring or wasteful runtime
behavior.

## Options considered

1. Human-readable data on both host and target.
2. Binary data for authoring and target.
3. Versioned UTF-8 source formats compiled into `.fglpack` artifacts.

## Decision

Use explicit schema versions and GUID references in readable source files. Validate and
migrate them on the host, then produce a deterministic indexed binary pack for the selected
target profile.

## Rationale

This separates usability and recovery concerns from storage/alignment/streaming constraints.

## Positive consequences

- Meaningful diffs and recoverable corruption errors.
- Reproducible packs support cache keys and CI comparisons.
- The compiler can reject target budget violations before firmware build.

## Negative consequences

- Source and pack readers must both be maintained.
- Every schema change needs migration fixtures.

## Reconsider when

Reconsider the concrete source encoding if real projects show unacceptable parse time or diff
quality, while preserving explicit versions, GUIDs, atomic writes, and deterministic packs.
