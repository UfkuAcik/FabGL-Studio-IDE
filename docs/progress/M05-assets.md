# Milestone 5 report

## Milestone

**M5 — Asset pipeline. Status: complete for documented source formats.**

## Completed work

The pipeline has a stable-GUID database, source fingerprint/import-settings cache keys, dependency
ordering, safe relative paths and source/sidecar moves. It imports Windows WIC raster images,
palette/dither/crop/grid/atlas data, PCM WAV with target conversion/compression, CSV/JSON tilemaps,
OBJ meshes and BDF fonts; it generates thumbnails and deterministic bounded `.fglpack` payloads.
Streaming `.fgla` clips remain compressed and are decoded through allocation-free 128-frame mixer
windows; PCM and delta window reads, cache refill metrics, and underrun behavior are tested.

## Test results

Round-trip, corruption, cache invalidation, Unicode/space path, pack inspection and CLI smoke tests
pass. Unsupported TTF, glTF and non-Windows raster decoding fail explicitly; they are not reported
as successful imports.
