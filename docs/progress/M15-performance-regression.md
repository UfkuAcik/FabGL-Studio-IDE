# M15 PC renderer performance regression gate

## Scope

`fabgl_performance_regression_tests` is a deterministic host-PC regression gate. After three
warm-up frames it times 48 frames of representative 160 x 90 workloads with
`std::chrono::steady_clock`:

- layered/rotated 2D sprites plus a two-layer chunked tilemap;
- fixed-point raycasting with textures, floor/ceiling, fog, billboards, minimap and weapon;
- the pseudo-3D racer road renderer; and
- low-poly mesh, culling, fog and billboard rendering.

The test is registered in the normal CTest suite and also carries the `performance` label, so
`ctest` and `ctest -L performance` both execute it. It verifies the exact workload checksum
`9215901677396929035`, 500,000-2,000,000 counted operations, 10,000-50,000 draw units and a
4 MiB declared resource-memory envelope. The current deterministic workload produces 790,075
operations, 33,413 draw units and 81,380 declared bytes.

The declared-byte value covers the workload's vector capacities plus explicit bounded renderer
scratch envelopes. It is a repeatable PC resource-budget signal, not an operating-system RSS
measurement and not an ESP32 RAM estimate.

## Timing budgets and override

The default elapsed-time ceiling is intentionally broad: 12,000 ms for optimized builds and
30,000 ms for Debug builds. These thresholds are designed to detect a major regression or hang,
not to fail on ordinary CI runner variance. CTest adds an independent 90-second timeout.

`FGL_PERFORMANCE_BUDGET_MS` can override the elapsed-time ceiling with a finite value from 1 to
80,000 ms. For example, a dedicated fast runner can use:

```powershell
$env:FGL_PERFORMANCE_BUDGET_MS = '3000'
ctest --test-dir out/build/release -L performance --output-on-failure
```

The checksum and operation/draw/memory budgets remain enforced when the time ceiling is
overridden.

## Local PC evidence

Measured on 2026-08-13 with Windows 11 build 22621, an Intel Core i7-7700HQ (4 cores / 8 logical
processors), MinGW GCC 13.1.0, strict warnings-as-errors, and the separate Debug build
`out/build/renderer-frameworks-finish`. Five consecutive runs measured 303.300-355.802 ms with a
312.632 ms median. Every run produced the same checksum, operation count, draw count and declared
memory value.

This evidence qualifies only the host-PC software path. It makes no claim about physical ESP32,
Olimex, VGA, PSRAM, audio, input-device, thermal or long-duration performance; those require
separate HIL measurements on the confirmed board/profile.

The regression gate is separate from the versioned per-project budget profiles added on
2026-08-22. Those profiles drive Studio Profiler, Memory Analyzer, and Asset Browser validation;
their persistence and evidence-source rules are documented in
[`docs/PERFORMANCE_BUDGETS.md`](../PERFORMANCE_BUDGETS.md).
