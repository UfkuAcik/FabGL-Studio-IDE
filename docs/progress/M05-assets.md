# Milestone 5 report

## Milestone

**M5 — Asset pipeline. Status: partial.**

## Completed work

Implemented a stable-GUID asset database with path normalization/moves/dependency ordering, Windows
image decode and indexed conversion, PCM WAV conversion, atomic file I/O, deterministic binary
pack build/inspection, checksums, storage classes, and cost estimates.

## Changed files

`tools/asset_pipeline/`, `ASSET_PIPELINE.md`, `FILE_FORMATS.md`, and
`tests/asset_pipeline_tests.cpp`.

## Architecture decisions

ADR 0003 separates readable source formats from bounded target artifacts.

## Commands run

PNG, WAV, Unicode-path, pack, and pack-inspection smoke commands were run. The asset/project CTest
program passed; the release suite passed 8/8.

## Test results

- Passed: PNG/WAV conversion smoke, Unicode path, asset/project test executable, pack inspection.
- Failed: 0 release CTest programs.
- Skipped: unimplemented importer/cache/editor workflows.

## Remaining work

Linux image decode, metadata sidecars/watchers, incremental cache, font/tilemap/model/atlas/
thumbnail import, compressed audio, and editor import settings are not implemented.
