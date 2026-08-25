# Performance and memory budgets

Budgets are validation thresholds, not promises. `Safe`, `Balanced`, and `Maximum` profiles
provide starting points; a project may define `Custom`. The editor labels every value as
measured-PC, estimated-ESP32, or measured-ESP32.

The persisted schema, exact source labels, warning/error thresholds, and Custom-field contract are
documented in [docs/PERFORMANCE_BUDGETS.md](docs/PERFORMANCE_BUDGETS.md).

The compact table below lists the ESP32 preset workload limits; PC presets are intentionally
separate and substantially larger.

| Resource | Safe | Balanced | Maximum | Enforcement point |
|---|---:|---:|---:|---|
| Active entities | 96 | 256 | 768 | Scene compile/runtime counter |
| Active components | 384 | 1,024 | 3,072 | Scene compile/runtime counter |
| 2D sprite submissions/frame | 256 | 768 | 1,536 | Renderer counter |
| Draw calls/frame | 32 | 96 | 192 | Renderer counter |
| Rays/frame | 64 | 192 | 384 | Raycast profiler when available |
| Low-poly triangles/frame | 256 | 1,024 | 2,048 | Experimental renderer counter |
| Particles | 256 | 1,024 | 2,048 | Fixed pool capacity |
| Simultaneous audio voices | 4 | 8 | 12 | Mixer voice allocator |
| Frame budget | 16.67 ms | 33.33 ms | 50 ms | Runtime profiler |

Memory thresholds are derived after linking because core/FabGL/static allocations affect the
available heap. Until physical telemetry is recorded, default warnings reserve headroom:

- keep at least 96 KiB internal heap uncommitted after scene load in Safe;
- never allocate an asset into internal RAM solely because PSRAM appears present;
- keep a configurable 20% safety margin on flash/pack partitions;
- reject one allocation larger than its declared storage-class limit;
- stream SD assets in fixed buffers and do no filesystem access from the per-frame hot path.

## Hot-path rules

- Preallocate scene/system views at load time.
- Use handles/value ownership; avoid `shared_ptr` in frame loops.
- Use fixed particle/audio pools and explicit voice/object eviction.
- Cull tile chunks, sprites, billboards, and meshes before expensive work.
- Record frame phases without allocating log strings at verbose levels in Release.
- Treat PSRAM and internal RAM as different storage classes with explicit placement.
- Run the soak firmware through scene changes, load/unload, audio, and entity churn before a
  profile can be called hardware-validated.

Targets such as 60 FPS simple 2D and 30 FPS raycast/racer are goals. They become claims only
when a report includes board revision, resolution, firmware hash, scene/pack hash, duration,
average/percentile frame times, and memory telemetry.
