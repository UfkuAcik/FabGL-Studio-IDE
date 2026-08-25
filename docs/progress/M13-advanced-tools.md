# Milestone 13 report

## Milestone

**M13 — Advanced editor tools. Status: complete for the documented v1 scope.**

## 2026-08-22 performance-budget closure

- Added independently versioned PC/ESP32 Safe, Balanced, Maximum, and Custom project budgets with
  deterministic v2 migration and canonical serialization.
- Project Settings edits every Custom time/count/storage limit; Memory Analyzer can switch the
  persisted profiles and distinguishes measured, estimated, and unavailable values.
- Profiler reports the complete phase/count/storage surface, real presenter triangle/ray and
  runtime particle counts, largest imported asset, most expensive measured PC system, and
  serial-measured ESP32 frame/heap-fragmentation/SD-read data when present. Missing telemetry is
  explicitly unavailable rather than estimated.
- Asset Browser capacity and Flash/Internal RAM/PSRAM/SD thresholds follow the selected ESP32
  performance profile. Budget excess produces warning/error styling and optimization guidance.

## Completed work

Portable audio buses/voice limits, fixed-pool emitters, runtime widgets, AI/navigation,
profiling/budgets, save slots/checksums, material authoring, and content-bound local package
install/validate/remove are implemented. The Qt editor exposes functional visual-script,
animator, profiler, memory-budget, target/device, serial-monitor, and code/navigation panels.

ESP32 gameplay saves use a separate fixed-capacity binary codec and explicit SD transaction
adapter. Runtime capture/restore is stable-GUID based, checksummed and schema-versioned; named
slots retain one rollback generation. Firmware boot/diagnostics perform no save write. Host fakes
cover corruption, migration, bounds and failed-write rollback, while physical SD/power-loss
durability remains an explicit HIL requirement.

Project manifests persist v2 input contexts/actions/axes, package requirements, and explicit
PC/ESP32 target profiles. Studio displays the selected manifest profile and blocks build/upload
when it is unsupported. Project trust remains separate from project and package data.

## Architecture decisions

Target-sensitive systems are bounded. Native project/package execution is explicit-trust only;
the editor passes program and argument arrays without a command shell. Estimates are labeled and
never reported as physical ESP32 measurements.

## Test results

Managed Qt 6.8.3/MinGW 13.1 strict compilation passed. The offscreen Studio suite passed all 19
Qt test slots, including advanced panels, v1-to-v2 project migration, v2 field preservation,
unsupported-path rejection, trust/safe-mode gating, autosave/recovery, code indexing, and serial
workflow controls.

## Remaining external certification boundary

The dedicated material, particle, tile, track and package editors, profiler timeline, and clangd
integration are implemented. Physical VGA/audio/memory/peripheral certification and long-duration
board soak/power-cycle evidence still require the approved board and cannot be inferred from PC
tests.
