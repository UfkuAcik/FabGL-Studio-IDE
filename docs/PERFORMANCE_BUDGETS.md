# Versioned project performance budgets

FabGL Studio project format v2 accepts an optional `performance` block. Its inner schema is
versioned independently (`version: 1`), so older v2 projects migrate deterministically to PC
`Balanced` and ESP32 `Safe` without changing their target/toolchain profile IDs. Studio writes the
block on the next save.

```json
"performance": {
  "version": 1,
  "pcProfile": "balanced",
  "esp32Profile": "safe",
  "customPc": { "frameTotalMs": 16.67, "entities": 2048 },
  "customEsp32": { "frameTotalMs": 16.67, "entities": 96 }
}
```

The serializer writes every Custom field, not only the abbreviated fields above. Profiles are
`safe`, `balanced`, `maximum`, and `custom`. PC and ESP32 selections are independent and coexist
with `targetProfiles.pc` and `targetProfiles.esp32`; a performance preset never changes the board,
toolchain, or compiler profile.

## Covered limits

Every preset contains maximum time for total frame, fixed update, variable update, physics,
animation, AI, render/present, audio, and asset streaming. It also contains maximum entities,
components, draw calls, sprites, triangles, rays, particles, audio voices, resident asset bytes,
Internal RAM, PSRAM, flash, and SD bytes. A zero PSRAM or SD limit means that storage class is
unavailable, not unlimited.

`Safe` keeps the largest reserve and smallest workload. `Balanced` is the ordinary authoring
default. `Maximum` permits a larger working set and may accept a lower frame rate. `Custom` uses
the complete values stored in the manifest. Exact canonical values live in
`tools/project_cli/performance_budget.h` and are covered by round-trip tests; Studio exposes all
Custom values in Project Settings.

## Evidence labels

The UI never silently converts an estimate into a measurement:

- **Measured PC** comes from engine phase timers, renderer counters, the Studio Play asset manager,
  or the audio mixer.
- **Estimated ESP32** is either the explicitly labelled frame model or importer/storage placement
  estimate.
- **Measured ESP32** appears only after accepted serial telemetry is received.
- **Unavailable** is shown when the active runtime has not supplied a counter. Current ESP32
  telemetry reports `heapFree`, `largestBlock`, and cumulative `sdReadBytes`; Studio derives a
  labelled fragmentation indicator from the first two and displays the SD adapter's measured
  read count. Older firmware/PC preview remains `Unavailable` rather than receiving a placeholder.

The Profiler shows all phase/count/storage rows, the most expensive measured PC phase, and the
largest imported asset. The Memory Analyzer separates measured file/runtime bytes from authoring
and ESP32 importer estimates. Asset Browser limits and its Flash/Internal RAM/PSRAM/SD warnings are
derived from the selected ESP32 project profile.

## Warning, error, and suggestions

An observation above 100% of a budget is a warning. Above 125% is an error. A storage class whose
profile limit is zero is an immediate error when any payload is assigned to it. Rows include a
metric-specific optimization suggestion, such as batching/culling render work, staggering AI,
reducing active voices, or downscaling/compressing/moving assets to streaming storage.

These are validation thresholds, not hardware performance guarantees. PC measurements do not
prove ESP32 performance. ESP32 frame-rate, memory, and soak claims require telemetry from the
approved physical board, project/firmware hashes, resolution, duration, and scene/asset-pack hash.
