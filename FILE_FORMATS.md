# FabGL Studio file formats

All authoring files are UTF-8, use `/` in stored relative paths, carry an explicit integer
format version, and identify durable objects with lowercase canonical UUIDs. Readers reject
non-finite numeric values, duplicate IDs, invalid references, hierarchy cycles, unknown
required fields, and trailing non-comment data. An error includes the file, line/field where
available, error code, and recovery hint.

## Extensions

| Extension | Purpose | Source or generated |
|---|---|---|
| `.fglproject` | Project identity, settings, input map, scenes, packages, target profiles | Source |
| `.fglscene` | Entity hierarchy and serialized components | Source |
| `.fglprefab` | Reusable entity hierarchy plus nested dependencies and defaults | Source |
| `.fglmaterial` | Bounded renderer material | Source |
| `.fglanim` | Animation curves, events, and clip settings | Source |
| `.fglcontroller` | Animator parameters, states, and transitions | Source |
| `.fglvisual` | Typed visual-script graph | Source |
| `.fgltrack` | Racer segment/checkpoint/object data | Source |
| `.fgltileset` | Tile sources, collision shapes, and import metadata | Source |
| `.fglpack` | Indexed, aligned, checksummed target asset payloads | Generated |
| `.fglsave` | Versioned game save slot with checksum; separate from scenes | Runtime data |

## `.fglscene` version 1

The initial engine serializer uses a deliberately strict line format. Whitespace surrounds
tokens; names are quoted; `\\`, `\n`, `\r`, and `\t` are escaped. Blank lines and lines whose
first non-space character is `#` are ignored.

```text
fglscene 1
scene_guid 5ab82f06-69e7-4d1d-b636-004796aba050
scene_name "Demo"
entity_begin
guid 45c55a3b-730e-49e5-8234-b5e853c42127
name "Oyuncu"
active 1
parent nil
position 16 24 0
rotation 0 0 0
scale 1 1 1
entity_end
scene_end
```

Version 1 losslessly supports entities, active state, and Transform. It intentionally fails
serialization when an entity contains a component without a registered serializer; it never
silently drops custom data. The version 2 component-block registry will be introduced only
with migration fixtures.

## Project and editor saves

The version 1 project document stores `kind`, `formatVersion`, project GUID, display name,
`projectRoot`, a separate `startupScene` path, an optional preview demo, and a process/argument
build model. Early v1 files may contain an inline `scene` summary; readers ignore it and writers
do not emit it. Qt uses `QSaveFile` and the portable CLI uses a same-directory `.part` file plus
atomic replacement. Bounded backup rotation and crash-recovery autosaves are planned and are not
implemented in this snapshot.

## Asset metadata and GUIDs

An asset's sidecar metadata stores its GUID, importer ID/version, source fingerprint, import
settings, dependency GUIDs, and target outputs. Moving source and sidecar together preserves
references. Missing sidecars receive a new GUID and a visible warning; reusing a GUID for two
paths is a hard error.

## `.fglpack`

All integers use little-endian byte order. A pack begins with magic, format version, target
profile ID, build fingerprint, entry count, and index checksum. Each sorted index entry holds
asset GUID, type ID, flags/storage class, aligned offset, stored size, decoded size, and payload
checksum. Payloads are ordered by GUID for deterministic output. A reader verifies ranges and
integer overflow before allocating or reading.

## Migration policy

Readers support the current version and explicitly registered older versions. Migration is a
pure step from one version to the next, validated after every step and covered by a frozen
fixture. Files newer than the reader are opened read-only or rejected; fields are not guessed.
Saving a migrated file uses atomic replacement and retains the original backup.
