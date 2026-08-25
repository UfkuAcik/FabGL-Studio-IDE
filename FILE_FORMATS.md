# FabGL Studio file formats

Text authoring files are UTF-8 and use `/` in stored relative paths. Binary authoring and
compiled files carry a magic value, explicit integer version, exact byte length and bounded
record counts. Durable objects use lowercase canonical UUIDs. Readers reject non-finite numeric
values, duplicate IDs, invalid references, hierarchy cycles, unknown required fields and
trailing data. An error includes the file or field where available, error code and recovery hint.

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
| `.fgltilemap` | Tilemap layers, objects, chunks, animations, and tileset GUIDs | Source |
| `.fgltileset` | Source-image GUID, tile slicing metadata, and collision tile IDs | Source |
| `.fglt` | Legacy `FGLT` v1 single-layer tilemap; accepted for migration only | Legacy generated |
| `.fglpack` | Indexed, aligned, checksummed target asset payloads | Generated |
| `.fglsave` | Versioned game save slot with checksum; separate from scenes | Runtime data |
| `.fglreplay` | Deterministic PC input recording | Runtime/test data |

## `.fglscene` version 2

The current engine serializer uses a deliberately strict line format. Names are quoted;
`\\`, `\n`, `\r`, and `\t` are escaped. Blank lines and lines whose first non-space character
is `#` are ignored. Component and property order follows the registered metadata so the same
scene produces deterministic text.

```text
fglscene 2
scene_guid 5ab82f06-69e7-4d1d-b636-004796aba050
scene_name "Demo"
entity_begin
guid 45c55a3b-730e-49e5-8234-b5e853c42127
name "Oyuncu"
active 1
parent nil
component_count 1
component_begin
type_id 3f0c300d-0dcd-5ba3-9e9e-b0ee0fd1661a
type_name "fabgl.Transform"
enabled 1
property_count 3
property "localPosition" vec3 16 24 0
property "localRotation" vec3 0 0 0
property "localScale" vec3 1 1 1
component_end
entity_end
scene_end
```

Version 2 serializes every component registered by the reflection registry, including its
stable type GUID, enabled state, and all properties marked `Serialize`. Property tags are
`bool`, `sint`, `uint`, `float`, `fixed`, `string`, `enum`, `flags`, `vec2`, `vec3`, `rect`,
`color`, `asset`, and `entity`. Asset/entity references may be `nil`; non-nil entity references
must resolve inside the scene. Unknown components or properties, type-tag mismatches, duplicate
IDs, invalid counts, and components without reflected serialization metadata are hard errors;
data is never silently discarded.

The reader still accepts legacy version 1 scenes containing entity state and inline Transform
fields. Loading and saving (or running the project CLI migrator) rewrites them canonically as
version 2. Frozen migration and malformed-input fixtures cover that path.

## `.fgltilemap` version 2 and legacy `.fglt` version 1

The canonical tilemap is a bounded little-endian `FGLT` version 2 binary. Its fixed header stores
the exact encoded byte length, map GUID, dimensions, tile dimensions, and record counts. The
ordered payload retains all authoring/runtime data:

- up to 32 named tile, collision, or object layers, each with visibility, opacity, parallax and a
  complete canonical 1-, 2-, or 4-byte tile-ID grid;
- stable non-zero object IDs, owning layer, UTF-8 type, bounded rectangle and optional asset GUID;
- up to 4,096 bounded chunks with owning layer, rectangle and exact cell data (which must agree
  with the corresponding layer region);
- up to 256 animations/4,096 frames with a tile ID and positive per-frame millisecond duration;
- up to 64 non-overlapping tileset GUID ranges mapping local tiles into global tile IDs.

Counts, all multiplication/addition, floating-point values, UTF-8 strings, tile ranges, duplicate
IDs/names, chunk bounds/data and reserved fields are validated before allocation. Records whose
order is not canonical are rejected by byte-for-byte re-encoding. The default decoded map cap is
1,048,576 cells and the encoded cap is 64 MiB.

The reader still accepts frozen `FGLT` version 1 files containing only 16-bit dimensions and one
flat grid. Saving such a file emits version 2 with a `Ground` layer. `.fglt` is therefore a read
and migration alias, not the extension for new assets.

## `.fgltileset` version 1

`FGLX` version 1 is the canonical bounded tileset serializer. It stores its own non-nil asset
GUID, UTF-8 name, a non-nil source-image asset GUID, tile width/height, margin, spacing, tile
count, column count, and a sorted unique table of colliding local tile IDs. The runtime resolves
the source image through the project manifest, validates every computed source rectangle, slices
the real pixels, maps collision IDs through the tilemap's declared range, and fails on a missing
or mismatched GUID. It never substitutes generated checker tiles for a canonical tileset.

## `.fgla` versions 1 and 2

`FGLA` is bounded mono audio. Its 24-byte little-endian header stores magic, version, flags,
sample rate, sample count, and loop range. Version 1 stores signed PCM16. Version 2 with the
compression flag stores independent 128-sample delta blocks: a PCM16 anchor followed by signed
8-bit deltas quantized in steps of 256. The streaming flag is orthogonal to encoding.

Readers require an exact payload length, known flags, a 4–192 kHz rate, a non-empty bounded
sample count, and loop points inside the clip. `inspectAudioClip` validates without expanding the
payload; `decodeAudioClipFrames` decodes an arbitrary bounded window without allocation. Project
runtime uses that window decoder with the mixer's fixed voice cache when the streaming flag is
set. The format contains no external path or SD-sector table.

## `.fglm` version 2 and legacy version 1

Compiled low-poly meshes use little-endian `FGLM`. The fixed 40-byte header contains the magic,
`u16` version, `u16` flags, `u32` vertex/index counts, then six finite `f32` values for minimum
and maximum bounds. Version 2 stores a tightly packed XYZ `f32` array, an optional parallel UV
`f32` array when flag bit 0 is set, and 16-bit triangle indices. All other flags are rejected.
The exact byte length, bounds, finite values, index ranges, non-degenerate triangles, and configured
65,535 vertex/triangle limits are checked before a mesh is exposed to runtime code.

The OBJ importer duplicates a position deterministically when faces reference it with different
UVs, so FGLM retains one bounded index stream while still supporting texture-atlas seams. Version 1
contains XYZ followed directly by indices and remains readable; writers emit version 2.

## Project and editor saves

The version 2 project document stores `kind`, `formatVersion`, project GUID, display name,
`projectRoot`, a separate `startupScene` path, optional preview demo, process/argument build model,
stable asset/import metadata, input contexts, package dependencies, and PC/ESP32 target profiles.
Its optional `performance` object has its own version (`1`), independent PC/ESP32
Safe/Balanced/Maximum/Custom selections, and complete Custom time/count/storage values. Omitting
that object deterministically selects PC Balanced and ESP32 Safe; a later save writes the current
canonical block. See [the performance budget contract](docs/PERFORMANCE_BUDGETS.md).

Early v1 files may contain an inline `scene` summary; readers ignore it and writers do not emit it.
Qt uses `QSaveFile` and the portable CLI uses a same-directory `.part` file plus atomic replacement.
Editor recovery/autosave status and operating instructions are tracked in the user and editor
guides rather than being part of the project file contract.

Image-source `import.settings` accepts the strict canonical fields documented in
[ASSET_PIPELINE.md](ASSET_PIPELINE.md#canonical-project-image-settings). Crop and grid rectangles
are integer pixel units; pivot coordinates are normalized `[0,1]`; pixels-per-unit is finite and
positive; atlas packing is bounded to 4096 pixels with at most 64 pixels padding. Atlas output and
grid slicing are a single validated combination and use asset type `sprite.atlas`. Ordinary FGLI
output uses asset type `image`. The only accepted compression identifier is `rle`, and residency
is `preload` or `stream`. Readers reject unknown nested or top-level fields.

## `.fglprefab` version 2

Prefab v2 stores a stable asset GUID, optional nested-base asset GUID, root component defaults and
a stable local entity hierarchy. Components retain a stable type GUID/name and typed property map.
Entities have stable GUIDs, active state and an optional local parent. The writer sorts components,
properties and entities canonically; the reader rejects duplicate/nil IDs, missing parents,
hierarchy/dependency cycles, non-finite values, invalid type tags, inconsistent counts and trailing
data. Limits cap input at 16 MiB, a line at 64 KiB, entities/components at 4096 and properties at
16384 per component.

```text
fglprefab 2
asset_guid 6b509725-5271-4aef-a8cb-b4cf452f728d
name "Enemy"
nested_base nil
root_component_count 0
entity_count 1
entity_begin
entity_guid f3d02c15-07d5-4387-af5c-53e145906587
name "Body"
active 1
parent nil
component_count 0
entity_end
prefab_end
```

Legacy v1 flat root-component files load through a frozen migration path and are saved as v2.
`PrefabLibrary` resolves nested assets and reports a missing dependency instead of guessing. An
unpack operation materializes the resolved hierarchy and severs the instance link.

Scene v2 persists each baked, linked instance on its root entity with the internal
`fabgl.PrefabInstanceLink` component. Its bounded string property contains canonical
`fglprefabinstance 1` data: source prefab GUID, scene root/entity GUIDs, source-to-scene GUID map,
missing-source flag, property overrides, added components, and removed component GUIDs. The
embedded property and added-component maps reuse the `.fglprefab` typed-value codec. The total
decoded link is capped by the 65,536-byte reflected-string limit; entity/mapping/removed counts are
each bounded at 4096. Duplicate, nil, overlapping, incomplete, trailing, or non-canonical state is
rejected. Revert/apply update the component atomically with the baked scene state; unpack removes
the component while leaving the baked hierarchy intact. A missing source keeps its baked entity or
explicit placeholder visible after reload.

## `.fglmaterial` version 1

Material assets carry a stable asset GUID/name and every bounded runtime material field: base
texture/palette references, optional transparent index, inline palette, tint/flat/emissive colors,
dither/sampling/lighting/blend modes, fog/billboard/double-sided flags and compatible-renderer
mask. Color channels and indexes are 0–255; palettes contain at most 256 colors; GUID references
are canonical or `nil`; unknown enum values, invalid masks, malformed fields and trailing data are
errors.

```text
fglmaterial 1
asset_guid 4ac4d047-8e68-4983-8ad1-6406d0ae8bc3
name "Hero"
base_texture nil
palette_asset nil
transparent_index nil
tint 255 255 255 255
flat_color 255 255 255 255
emissive 0 0 0 255
emissive_strength 0
color_mode texture
dither none
sampling nearest
lighting unlit
blend opaque
fog 1
billboard 0
double_sided 0
compatible_renderers 15
palette_count 0
material_end
```

## Asset metadata and GUIDs

An asset's sidecar metadata stores its GUID, importer ID/version, source fingerprint, import
settings, dependency GUIDs, and target outputs. Moving source and sidecar together preserves
references. Missing sidecars receive a new GUID and a visible warning; reusing a GUID for two
paths is a hard error.

## `.fglanim` version 1

Animation clips are line-oriented UTF-8 source assets with a stable `AssetGuid`. They store the
clip name, positive duration, loop flag, property tracks, curve keys, interpolation, and named
events. A complete minimal shape is:

```text
fglanim 1
clip_guid d111a29e-e1b8-5e8a-9ddb-c32b5b6563d4
clip_name "Walk"
duration 1
looping 1
track_count 1
track_begin
property "transform.position.x"
key_count 2
key 0 0 0 0 linear
key 1 16 0 0 linear
track_end
event_count 1
event 0.5 "footstep"
clip_end
```

Interpolation tags are `step`, `linear`, and `cubic_hermite`; every key stores time, value,
incoming tangent, outgoing tangent, and interpolation. Track paths are unique and emitted in
lexicographic order. Keys must be strictly increasing and inside `[0, duration]`. Events are
canonicalized by time then name, and exact duplicates are rejected. The default reader limits are
4 MiB source, 256 tracks, 8,192 total keys, 1,024 events, and 1,024 decoded bytes per string.

## `.fglcontroller` version 1

Animator controllers also carry a stable `AssetGuid`. States refer to `.fglanim` clips only by
their asset GUID; runtime pointers or paths never enter the file. Parameters are declared as
`boolean`, `integer`, `float`, or `trigger`, with a typed default where applicable. Conditions are
checked against those declarations.

```text
fglcontroller 1
controller_guid 11d2d9bb-4bf0-58f0-a4ca-cee32722ef37
controller_name "Player"
initial_state "idle"
parameter_count 2
parameter "moving" boolean 0
parameter "speed" float 0
state_count 2
state "idle" 913e342f-8e06-53d4-93d9-07bc822afb38
state "run" 68cb5f8c-9c05-5bf6-a4b5-63be2b5aa077
transition_count 1
transition_begin
from "idle"
to "run"
minimum_normalized_time 0
has_exit_time 0
exit_time 0
blend_duration 0.15
condition_count 2
condition "moving" boolean_equals 1
condition "speed" float_greater 0.25
transition_end
controller_end
```

Integer condition tags are `integer_equals`, `integer_not_equals`, `integer_greater`, and
`integer_less`; float tags are `float_greater` and `float_less`; trigger conditions use
`trigger_set` with no value. Parameters and states are emitted by name, and conditions within a
transition are canonicalized. Transition record order is intentionally preserved because it is
runtime priority when more than one transition is eligible. `minimum_normalized_time`, optional
exit time, and non-negative blend duration are all retained.

The reader rejects undeclared or mistyped condition parameters, missing source/target/initial
states, nil clip GUIDs, non-finite or negative timing, duplicate names, unknown tags, newer
versions, excessive counts, and trailing data. Defaults are 4 MiB source, 256 parameters, 256
states, 1,024 transitions, 4,096 total conditions, and 1,024 decoded bytes per string. Resolving a
state's clip is a runtime operation through an explicit application-provided resolver; the parser
never guesses a path or substitutes a missing clip.

## `.fglvisual` version 1

Visual scripts use a strict, line-oriented UTF-8 format. A graph stores its asset GUID, display
name, entry node, node layouts, comment boxes, typed pins, payload fields, optional asset/entity/
component references, and edges. Node kinds are serialized by the registry's stable name (for
example `flow.branch`), never by an enum ordinal or localized display text.

```text
fglvisual 1
graph_guid 4d90d31e-0a88-58c3-b15a-03503ac52d4c
graph_name "Open Door"
entry 1
comment_count 0
node_count 2
node_begin
id 1
type "event.start"
name "Start"
layout 0 0 180 80
number 0
boolean 0
variable ""
callback ""
payload ""
asset nil
entity nil
component nil
pin_count 1
pin 1 "flow" flow output
node_end
node_begin
id 2
type "flow.return"
name "Return"
layout 240 0 180 80
number 0
boolean 0
variable ""
callback ""
payload ""
asset nil
entity nil
component nil
pin_count 2
pin 1 "flow" flow input
pin 2 "value" number input
node_end
edge_count 1
edge 1 1 2 1
graph_end
```

The example only illustrates records; `flow.return.value` is required, so a complete executable
graph also connects a numeric producer to pin 2. Strings use the same quoted control-character
escapes as scenes. The writer orders comments by ID, nodes by ID, and edges by their four endpoint
IDs, normalizes negative zero, and emits round-trip-safe numeric precision. Consequently, loading
and saving a valid v1 graph yields one canonical byte sequence.

The reader accepts blank lines and `#` comments, but otherwise requires the exact v1 record order
and rejects trailing data. It rejects unknown versions and registry type names, duplicate or zero
IDs, non-finite values, invalid layouts, pin-schema changes, dangling/type-incompatible edges,
cycles, missing mandatory connections/references, and non-canonical nil references. Default
safety limits are 4 MiB source text, 512 nodes, 1,024 edges, 4,096 pins, 128 comment boxes, and
1,024 decoded bytes per string. Unknown fields are not silently retained; a future schema change
must increment the format version and provide an explicit migration.

## `.fglpack`

All integers use little-endian byte order. A pack begins with magic, format version, target
profile ID, build fingerprint, entry count, and index checksum. Each sorted index entry holds
asset GUID, type ID, flags/storage class, aligned offset, stored size, decoded size, and payload
checksum. Payloads are ordered by GUID for deterministic output. A reader verifies ranges and
integer overflow before allocating or reading.

## `.fglreplay` version 2

PC input recordings are strict UTF-8 text. The header is `fglreplay 2`; each canonical frame line
stores its zero-based index, five legacy demo bits, a bounded control count, then lexicographically
ordered quoted control-name/finite-float pairs. Names are limited to 64 safe ASCII characters,
values to `[-1, 1]`, controls to 128 per frame, and the file ends with the exact `end` marker.
Keyboard, real mouse and XInput gamepad snapshots therefore replay through the same `InputMap`
path as live input. The reader also accepts version 1 lines containing only the five legacy bits;
the writer always emits version 2. Unknown versions, duplicate controls, non-finite/out-of-range
values, non-contiguous frame indices, trailing records and empty recordings are rejected.

## `.fglsave` version 1

Game saves are separate from project and scene authoring data. A slot contains an ASCII header,
one blank line, and an opaque binary payload:

```text
FGLSAVE 1
schema 3
size 128
checksum 89abcdef

<exactly 128 payload bytes>
```

The checksum is CRC-32 over the schema version encoded little-endian followed by the payload.
The reader uses bounded, locale-independent integer parsing, requires exactly four unique header
fields, rejects trailing integer text and oversized values, and checks both size and checksum
before migration. Schema migrations are registered as explicit sequential `N -> N+1` functions;
missing steps and saves newer than the runtime are errors.

`MemorySaveStorage` is available for tests and ephemeral sessions. `FileSaveStorage` stores safe
ASCII slot names as `<slot>.fglsave` under an explicitly configured directory, commits through a
same-directory `.part` file, rejects symlinks, bounds a file to 64 MiB, and rotates up to 16
`.bakN` generations. The same directory contract can point at a PC save folder or a mounted SD
card. Callers can inspect a backup without silently replacing the live slot.

### ESP32 bounded slot codec

The target firmware uses a separate binary representation, also stored with the `.fglsave`
extension, because it cannot allocate the desktop document/container model. It is not an
`.fglscene` and is not silently interpreted by desktop `SaveSystem`. Its 32-byte little-endian
header is:

| Offset | Type | Meaning |
| ---: | --- | --- |
| 0 | 4 bytes | `FGLS` |
| 4 | `u16` | codec version, currently 1 |
| 6 | `u16` | header size, exactly 32 |
| 8 | `u32` | gameplay schema version |
| 12 | `u32` | caller sequence |
| 16 | `u32` | payload byte count |
| 20 | `u32` | CRC-32 over schema, sequence and payload |
| 24 | `u32` | bounded entity-count cross-check |
| 28 | `u32` | reserved zero |

The version-1 payload starts with `FGSD`, document version/reserved words, scene GUID/name, player
GUID, typed primitive/scene/player maps and entity records. Decode rejects unknown versions,
non-finite floats, duplicate/zero entity GUIDs, duplicate keys, invalid Boolean tags, trailing
bytes and any count or string beyond the compile-time limits. A schema newer than the runtime is
rejected; older schemas require an explicit consecutive `N -> N+1` callback.

Named slots live at `/fabglstudio/saves/<slot>.fglsave`; names contain only ASCII letters, digits,
`_` and `-` and are at most 24 characters. SD writes stage the complete checksummed file as
`.tmp`, verify its byte count, rename the current live file to `.bak`, then rename the staged file
to live. A failed second rename restores the old live path; if restoration itself fails, the old
bytes remain addressable as `.bak`. Load may recover that generation after a missing, unreadable,
malformed or checksum-invalid live file. Arduino FS does not promise power-loss-atomic FAT
metadata, so real card/power-cycle durability remains a physical HIL boundary.

## Migration policy

Readers support the current version and explicitly registered older versions. Migration is a
pure step from one version to the next, validated after every step and covered by a frozen
fixture. Files newer than the reader are opened read-only or rejected; fields are not guessed.
Saving a migrated file uses atomic replacement and retains the original backup.
