# Performance and memory budgets

Budgets are validation thresholds, not promises. `Safe`, `Balanced`, and `Maximum` profiles
provide starting points; a project may define `Custom`. The editor labels every value as
measured-PC, estimated-ESP32, or measured-ESP32.

| Resource | Safe | Balanced | Maximum | Enforcement point |
|---|---:|---:|---:|---|
| Active entities | 128 | 384 | 768 | Scene compile/runtime counter |
| Active components | 512 | 1,536 | 3,072 | Scene compile/runtime counter |
| 2D sprite submissions/frame | 128 | 384 | 800 | Renderer counter |
| Raycast internal width | 160 | 256 | 320 | Build profile/render target |
| Ray DDA steps/frame | 8,000 | 20,000 | 40,000 | Renderer profiler |
| Low-poly triangles/frame | 64 | 160 | 320 | Experimental renderer counter |
| Particles | 64 | 192 | 384 | Fixed pool capacity |
| Simultaneous audio voices | 4 | 8 | 12 | Mixer voice allocator |
| Fixed updates after stall | 3 | 5 | 8 | Engine-loop catch-up clamp |
| Frame target | 33.3 ms | 33.3 ms | 16.7 ms for simple 2D | Runtime profiler |

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
