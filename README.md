# FabGL Studio

FabGL Studio is an open-source desktop authoring environment and compact game runtime for
the Olimex ESP32-SBC-FabGL. It brings scene/entity authoring, deterministic PC preview,
asset compilation, profiling, and managed ESP32 builds into one project while keeping the
runtime realistic for ESP32 memory and rendering limits.

The repository is organized around one rule: gameplay data and portable engine behavior are
shared between PC and ESP32; Qt is an editor dependency, never an engine dependency.

## Current development build

This repository is under active milestone development. Every completed milestone has a
truthful report under `docs/progress/`; hardware behavior is marked unverified until it has
actually run on a board.

The consolidated evidence, artifact hashes, and known limits are in
[`docs/FINAL_REPORT.md`](docs/FINAL_REPORT.md).

The standard host workflow is:

```powershell
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

If Qt 6 is absent, configuration skips the graphical Studio target while still building the
portable engine, command-line tools, PC player, and tests. See [BUILDING.md](BUILDING.md) for
supported toolchains and exact commands.

## Repository map

- `engine/`: platform-neutral runtime, data model, serialization, simulation, and diagnostics.
- `apps/studio/`: Qt Widgets desktop editor.
- `apps/player_pc/`: deterministic PC preview/player.
- `platforms/fabgl/`: ESP32/FabGL platform adapter and firmware entry point.
- `renderers/`: bounded 2D, raycast, racer, and experimental low-poly software renderers.
- `frameworks/`: reusable gameplay modules rather than engine hard-coding.
- `tools/`: project, asset, toolchain, package, and upload utilities.
- `templates/` and `examples/`: projects that can be opened and built.
- `tests/`: unit, integration, rendering, replay, and hardware harnesses.
- `docs/decisions/`: architecture decision records (ADRs).

## Project principles

- Correct, measurable behavior takes priority over feature count.
- Human-authored files are versioned and readable; target packs are compact binary artifacts.
- Stable GUIDs, not paths, identify assets and entities.
- PC preview can expose an ESP32 compatibility budget and never presents estimates as hardware
  measurements.
- External projects and plugins are untrusted until the user explicitly trusts them.
- Telemetry is off and no project data is transmitted.

## Hardware status

A serial device being present is not proof that it is the target board. Upload and
hardware-in-the-loop tests require explicit board identification. The automated diagnostic
firmware and manual checklist live in `tests/hardware/` and `HARDWARE_TESTING.md`.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md), [ARCHITECTURE.md](ARCHITECTURE.md), and
[ROADMAP.md](ROADMAP.md). First-party C++ is formatted with clang-format, built as C++20, and
covered by CTest.
