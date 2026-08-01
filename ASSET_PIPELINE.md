# Asset pipeline

FabGL Studio preserves authoring sources and compiles target-specific data into deterministic
artifacts. Asset identity is a GUID stored independently of the path, so moving source plus
metadata keeps every scene/prefab reference valid.

## Command-line compiler

The dependency-free tool builds as `fabgl_asset_compiler`. On Windows it uses Windows Imaging
Component to decode PNG, JPEG, and BMP without executing external programs. Image data is
converted to a bounded indexed format:

```powershell
fabgl_asset_compiler image hero.png hero.fgli --width 32 --height 32 --colors 16 --dither
```

Implemented image stages:

- nearest-neighbor resize;
- transparent-index reservation and alpha threshold;
- deterministic 15-bit histogram palette selection;
- nearest-palette mapping;
- optional Floyd–Steinberg dithering;
- RLE indexed output with versioned `FGLI` header;
- decoded, palette, packed, and pixels-per-frame cost estimates.

WAV import accepts bounded PCM mono/stereo 8/16/24/32-bit input, downmixes to mono, linearly
resamples, optionally trims silence and normalizes, validates loop points, and emits signed
16-bit `FGLA` data:

```powershell
fabgl_asset_compiler audio music.wav music.fgla --rate 22050 --stream
```

## Asset database

`AssetDatabase` rejects nil/duplicate GUIDs, duplicate normalized cross-platform paths, unsafe
relative paths, missing dependencies, and dependency cycles. Moving an asset changes only its
normalized path. A deterministic topological build order ensures dependencies compile first.

File watchers and editor scans translate operating-system events into database transactions.
They debounce bursts and fingerprint content before reimporting; a timestamp change alone is
not a cache miss. Import cache keys include source bytes, importer ID/version, normalized
settings, dependency outputs, engine pack version, and target profile.

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

## Remaining target-specific stages

Sprite slicing/atlas layout, fonts, tilemaps, thumbnails, audio compression, SD streaming
chunks, and target pixel formats build on the same importer and pack contracts. Until those
stages are implemented, the editor reports them as unavailable rather than silently emitting
desktop data for firmware.
