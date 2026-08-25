# ADR 0012: Resolve assets by stable GUID through project mappings

- Status: Accepted
- Date: 2026-08-13

## Context

Scenes, prefabs, animation, maps, scripts, and packages must survive file renames without storing
absolute machine paths. ESP32 packs also need compact deterministic lookup keys.

## Options considered

1. Persist relative paths in every consumer.
2. Hash file contents and use the hash as identity.
3. Persist stable GUIDs and keep validated GUID-to-path/type mappings in the project.

## Decision

Choose option 3. Consumers store non-nil `AssetGuid` values. The project manifest owns a unique,
portable, project-relative mapping; import metadata and packs preserve the same identity.

## Rationale

Paths are locations and content hashes are versions, not identities. A GUID remains stable across
rename and reimport while a separate digest can validate content and cache output.

## Positive consequences

- Asset moves do not rewrite every scene.
- Missing references are distinguishable from moved files.
- Host and target resolve the same logical asset deterministically.

## Negative consequences

- Duplicate or lost metadata needs repair tooling.
- Mapping validation is required at every project boundary.

## Reconsider when

Add signed or globally namespaced identities for remote repositories if required; retain a stable
logical identity distinct from path and content version.
