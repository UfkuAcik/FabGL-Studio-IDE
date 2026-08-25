# ADR 0018: Generate a separate bounded ESP32 gameplay companion

- Status: Accepted
- Date: 2026-08-22

## Context

Desktop gameplay modules use C++20, the public engine SDK, dynamic libraries, and host-owned scene
objects. The pinned Arduino ESP32 core uses an older Xtensa compiler and a fixed-capacity firmware
runtime. Treating arbitrary desktop source as target-portable either fails late in the firmware
build or encourages unbounded APIs that are unsafe beside VGA and audio workloads.

## Options considered

1. Reject every project containing desktop C++ when exporting to ESP32.
2. Feed the desktop source tree to Xtensa and accept target-specific failures.
3. Generate a distinct versioned companion below `Scripts/ESP32`, expose only a bounded runtime
   view, and compile that companion directly into the firmware.

## Decision

Choose option 3. `new-script` emits desktop source plus an ESP32 companion and guarded module
aggregator. The exporter stages only canonical, non-reparse C/C++ files below `Scripts/ESP32`,
requires the version-1 module entry point, and rejects desktop gameplay with no companion. The ABI
allows at most 16 uniquely named descriptors with bounded names and allocation-free `Start` and
`Update` callbacks.

## Rationale

The split makes portability work explicit while preserving ordinary desktop C++ and producing a
real linked firmware gameplay path. It also keeps target capacity and compiler failures at the
export/build boundary instead of during an uploaded session.

## Positive consequences

- Desktop source cannot accidentally import unsupported host facilities into firmware.
- Target callbacks execute from the same embedded project update loop as scene/input/runtime data.
- Build evidence states whether scripts were included and records source count, hashes, flash, and
  static RAM without implying upload.

## Negative consequences

- Cross-target gameplay behavior may need two small implementations.
- The first ABI exposes only `Start` and `Update`, not the full desktop component lifecycle.
- Source-level or state-preserving hot reload is unavailable on the target.

## Reconsider when

Expand the versioned target view only after measured memory/timing evidence and backward-compatible
fixtures exist. A future portable gameplay DSL could reduce duplication, but must retain explicit
capacities and ahead-of-time validation.
