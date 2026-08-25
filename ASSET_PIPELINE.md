# Asset pipeline

FabGL Studio preserves authoring sources and compiles target-specific data into deterministic
artifacts. Asset identity is a GUID stored independently of the path, so moving source plus
metadata keeps every scene/prefab reference valid.

## Command-line compiler

The tool builds as `fabgl_asset_compiler`. Its CSV, JSON, OBJ, BDF, WAV, binary encoders, and
inspectors have no third-party parser dependency. On Windows it uses Windows Imaging Component
to decode PNG, JPEG, and BMP without executing external programs. Image data is converted to a
bounded indexed format:

```powershell
fabgl_asset_compiler image hero.png hero.fgli --width 32 --height 32 --colors 16 --dither
fabgl_asset_compiler thumbnail hero.png hero-thumb.fgli --max-width 128 --max-height 128 --colors 16
```

Implemented image stages:

- nearest-neighbor resize;
- validated crop and uniform sprite-grid slicing;
- transparent-index reservation and alpha threshold;
- deterministic 15-bit histogram palette selection;
- nearest-palette mapping;
- optional Floyd–Steinberg dithering;
- RLE indexed output with versioned `FGLI` header;
- aspect-preserving, no-upscale thumbnail generation with the same inspectable `FGLI` format;
- strict `FGLI` decoding that rejects invalid headers, palette references, truncated runs, and
  non-canonical adjacent runs;
- decoded, palette, packed, and pixels-per-frame cost estimates.
- deterministic shelf-packed sprite atlases with padding, optional power-of-two sizing, pivots,
  region metadata, and an embedded indexed `FGLS` payload.

### Canonical project image settings

PNG/JPEG/BMP project sources use one strict settings object in the `.fglproject` manifest. Studio,
`project prepare`, and ESP32 export decode this same schema and call the same compiler, so preview
and target bytes cannot drift. Older projects may keep `{}` and receive the documented defaults.
Unknown fields, missing grid dimensions, a grid without atlas output, atlas plus whole-image
resize, out-of-range pivots/PPU, unsupported compression, and source-bound crop/slice errors are
rejected instead of ignored.

```json
{
  "targetWidth": 0,
  "targetHeight": 0,
  "paletteSize": 16,
  "alphaThreshold": 127,
  "dither": false,
  "reserveTransparentIndex": true,
  "crop": {"x": 0, "y": 0, "width": 64, "height": 32},
  "slice": {"mode": "grid", "frameWidth": 16, "frameHeight": 16, "margin": 0, "spacing": 0},
  "atlas": {"enabled": true, "maxWidth": 256, "padding": 1, "powerOfTwo": true},
  "pivot": {"x": 0.5, "y": 1.0},
  "pixelsPerUnit": 16,
  "compression": "rle",
  "residency": "stream"
}
```

`crop`, `slice`, `atlas`, and `pivot` are optional; absent values preserve the complete source,
produce a normal `image`, use the center pivot, and preload. Enabling atlas changes the canonical
asset type to `sprite.atlas`; disabling it changes the type back to `image`. Grid frames receive
stable `frame_00000` names. `rle` is the sole supported texture compression and is the actual
FGLI/FGLS representation, rather than a UI-only choice. `residency` is either `preload` or
`stream`; ESP32 streaming cost uses bounded palette/scanline working memory while PC cost remains
the real decoded resident image. Flash/PSRAM/SD placement remains the adjacent `esp32Target`.

The Studio dialog exposes typed fields, the transformed thumbnail, storage/decode estimates and
pixels-per-frame render cost. Saving performs a synchronous validated reimport before updating the
project manifest.

WAV import accepts bounded PCM mono/stereo 8/16/24/32-bit input, downmixes to mono, linearly
resamples, optionally trims silence and normalizes, validates loop points, and emits signed
16-bit or block-delta-compressed `FGLA` data. Both encodings have a strict inspector and an
allocation-free window decoder. A runtime clip marked `--stream` remains in compact encoded form
and the mixer fetches at most 128 frames into each voice's fixed cache instead of expanding the
whole clip to float PCM:

```powershell
fabgl_asset_compiler audio music.wav music.fgla --rate 22050 --stream --compress
fabgl_asset_compiler atlas sprites.txt sprites.fgls --max-width 512 --padding 1 --colors 32
```

## Structured source importers

Tilemaps accept either rectangular CSV rows or a strict JSON object containing unsigned
`width`, `height`, and a flat `tiles` array. Dimensions are capped at 4096 by 4096 and the
default decoded-cell cap is 1,048,576. Imports emit canonical `.fgltilemap` (`FGLT` version 2),
selecting the smallest canonical 1-, 2-, or 4-byte tile-ID width. Identical CSV and JSON maps
produce identical output with a migrated `Ground` layer:

```powershell
fabgl_asset_compiler tilemap level.csv level.fgltilemap
fabgl_asset_compiler tilemap level.json level.fgltilemap
fabgl_asset_compiler tileset terrain.fgltileset `
  --guid 26b2d039-7f1e-4a29-bc76-e51775654809 --name Terrain `
  --image 7a20b50d-681a-4d78-957f-753272822dbd `
  --tile-width 8 --tile-height 8 --count 64 --columns 8 --collision 1,2,9
```

The tilemap v2 serializer preserves named tile/collision/object layers, visibility, opacity,
parallax, stable objects, chunk rectangles plus exact chunk cells, per-frame tile animation
durations, map GUID, and non-overlapping tileset GUID ranges. `FGLX` tilesets preserve the source
image GUID, slicing metadata, and sorted collision IDs. At runtime those GUIDs resolve to the real
indexed-image pixels; missing assets, mismatched tile counts/dimensions, or out-of-image slices
are hard errors. Legacy `.fglt` (`FGLT` v1) remains inspectable and migrates on the next save.

The low-poly Wavefront OBJ importer accepts finite XYZ positions, optional referenced texture
coordinates/normals, and faces of 3 to 64 vertices. Positive and relative negative indices are
validated before deterministic fan triangulation. `FGLM` version 2 stores positions, normalized or
atlas UVs, 16-bit triangle indices, and checked bounds; a position referenced with multiple UVs is
duplicated deterministically to retain a compact single-index runtime stream. Version 1 meshes
remain readable. The default caps are 65,535 vertices and 65,535 triangles:

```powershell
fabgl_asset_compiler mesh scenery.obj scenery.fglm
```

The bitmap-font importer reads Adobe BDF metrics and 1-bit glyph rows, sorts unique Unicode
encodings, and shelf-packs a compact atlas. Glyph dimensions default to at most 64 by 64, with at
most 1,024 glyphs and a 512 by 2,048 atlas. `FGLF` version 1 contains the bit-packed atlas plus
per-glyph code point, rectangle, offsets, and advance:

```powershell
fabgl_asset_compiler font terminal.bdf terminal.fglf --atlas-width 128 --padding 1
```

Use the strict format inspector for compiled images/thumbnails, tilemaps, meshes, and fonts. It
checks the magic, version, reserved fields, exact payload length, limits, indices/rectangles, and
canonical encoding before returning metadata:

```powershell
fabgl_asset_compiler inspect-asset level.fgltilemap
fabgl_asset_compiler inspect-asset terrain.fgltileset
fabgl_asset_compiler inspect-asset scenery.fglm
fabgl_asset_compiler inspect-asset terminal.fglf
```

## Asset database

`AssetDatabase` rejects nil/duplicate GUIDs, duplicate normalized cross-platform paths, unsafe
relative paths, missing dependencies, and dependency cycles. Moving an asset changes only its
normalized path. A deterministic topological build order ensures dependencies compile first.
Source snapshots classify records as missing, dirty, unchanged, or untracked using content
fingerprints; changing importer versions or normalized settings invalidates prior outputs.

`IAssetImporter` and `AssetImporterRegistry` form the native importer-plugin boundary. The
registry owns importer lifetimes, rejects duplicate IDs/extensions, dispatches case-insensitively,
and computes a deterministic cache key from source bytes, GUID, importer ID/version, settings,
target profile, pipeline version, and dependency output keys. Empty or unsafe results fail before
they enter a pack.

`CsvTilemapImporter`, `JsonTilemapImporter`, `WavefrontObjImporter`, and `BdfFontImporter`
implement that boundary directly. Applications opt into the importers they need, which avoids
claiming the generic `.json` extension when JSON is used for a different asset kind.

Editor scans translate operating-system events into source snapshots and fingerprint content
before reimporting; a timestamp change alone is not a cache miss.

## Target pack

A manifest line is `GUID type-id storage-class "payload path"`, where storage is `flash`,
`ram`, `psram`, or `sd`:

```powershell
fabgl_asset_compiler pack build-assets.txt game.fglpack
fabgl_asset_compiler inspect game.fglpack
```

The builder sorts entries by GUID, aligns payloads, rejects duplicate/nil IDs and 4 GiB range
overflow, and records per-payload plus whole-build FNV-1a checksums. The inspector validates
magic/version/layout, sorted uniqueness, bounds, alignment, and checksums before exposing any
entry. Atomic file writes use a `.part` file, flush, and replace.

## Safety

Importers receive explicit files and byte buffers; asset browsing never launches a source.
Generated paths must be safe relative paths without drive roots, UNC roots, NUL, or `..`
segments. Archive import additionally canonicalizes every extracted destination. Native/custom
importer plugins require project trust and a declared package entry point.

## Explicit target limits

- TTF/OTF outline rasterization is intentionally not built in; convert an outline font to BDF or
  provide a trusted font plugin. The `font` command rejects non-BDF input.
- glTF/GLB, animation, skinning, embedded MTL materials, and OBJ line/point primitives are not
  supported by the low-poly importer. Convert geometry to the documented OBJ triangle/polygon
  subset first. Referenced texture coordinates are stored in `FGLM` version 2; normals, groups,
  and material names are validated/accepted but remain authoring metadata rather than runtime
  payload. Bind a `.fglmaterial` asset through `MeshRenderer.material`; its `baseTexture` image is
  dependency-checked and sampled with nearest-neighbor atlas UVs by the experimental renderer.
- TMX and general-purpose/nested JSON are not import inputs. JSON imports use exactly the bounded
  flat schema documented above; use `.fgltilemap` for the complete authoring model. CSV must be
  rectangular and contain no empty cells.
- Thumbnail files can be generated deterministically, but editor-specific thumbnail cache
  eviction policy remains an editor concern.
- `FGLA` streaming provides bounded random-access decoding over the compact resident payload and
  reports cache refills, decoded frames, and underruns. Background SD/file I/O with double
  buffering is not implemented; selecting `sd` controls target-pack placement, not real-time PC
  filesystem reads.

The registry or CLI reports unsupported input instead of silently copying desktop source data
into firmware. Image, thumbnail, atlas, WAV, compressed audio, CSV/JSON tilemap, low-poly OBJ,
BDF bitmap font, opaque pack payloads, and target storage placement are implemented.
