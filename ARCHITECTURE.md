# FabGL Studio architecture

## Goals and constraints

FabGL Studio has a relatively unconstrained desktop authoring process and a tightly bounded
ESP32 runtime. The architecture therefore shares semantic code—IDs, scenes, component
lifecycle, input actions, physics rules, animation evaluation, visual-script bytecode, and
asset references—while isolating operating-system, UI, graphics, audio, storage, and upload
details behind adapters.

The engine has no Qt dependency. The editor may own Qt objects, but it communicates with the
engine through plain C++ values and explicit commands. No editor type can occur in a public
runtime header.

## Layering

```text
Qt Studio       CLI tools        PC player         ESP32 firmware
    |               |                |                    |
editor services  build services  SDL/platform adapter  FabGL adapter
    |               |                |                    |
    +---------------+------- portable engine ------------+
                            |
             scenes / ECS / resources / simulation
                            |
            versioned source data -> compiled target pack
```

Dependencies point downward. Frameworks and renderers depend on public engine interfaces;
the engine does not depend on any framework. Platform adapters implement timing, input,
render presentation, audio output, persistence, and diagnostics.

## Engine execution

The common loop has explicit phases: initialize, load resources, load scene, fixed update,
physics, variable update, animation, audio, render submission, render, present, and shutdown.
A bounded accumulator prevents a stalled frame from creating an unbounded fixed-update burst.
Profiler samples identify whether values are measured on PC, measured on ESP32, or estimated
by a budget model.

## Entity/component model

The first implementation is an editor-friendly object ECS rather than an archetype ECS.
Entities own stable IDs and compact component collections. Components have a type ID,
reflection metadata, enabled state, and lifecycle callbacks. Systems may maintain packed
views for hot paths. This favors understandable serialization and deterministic lifecycle
over peak desktop throughput; target-specific packed data can be emitted by the asset
compiler without changing authoring semantics.

Transform hierarchy is maintained separately from display-tree UI. Reparenting rejects self
and descendant cycles, updates both parent and child links, and propagates a dirty flag. World
transforms are evaluated parent-first and cached.

## Data and persistence

Source formats (`.fglproject`, `.fglscene`, `.fglprefab`, `.fglmaterial`, `.fglanim`,
`.fglcontroller`, `.fglvisual`, `.fgltrack`, `.fgltileset`) are UTF-8, explicitly versioned,
and use relative normalized paths. Entities and assets retain GUID identity through rename or
move. Readers validate schema/version and return structured errors instead of partially
loading corrupt data.

Writes use a same-directory temporary file followed by atomic replacement where the platform
supports it. Backup rotation and recovery metadata are editor services. Game save slots use a
separate checksummed, migratable format; scene serialization is never reused as a save-game
shortcut.

`.fglpack` is the target artifact: a versioned header and index followed by aligned payloads.
The index records GUID, kind, offset, compressed size, decoded size, storage class, and
checksum. Asset compilation is deterministic so identical inputs and settings yield identical
packs.

## Rendering

Rendering is command based. Gameplay submits bounded POD-like commands; a renderer consumes
them through a software framebuffer or a platform accelerator. Separate renderer modules
cover 2D/tilemap, grid raycast, pseudo-3D racer, and experimental low-poly triangles. They
share palettes, materials, camera values, profiling counters, and clipping rules, not one
unbounded universal pipeline.

The PC backend is a reference implementation and compatibility simulator. Golden-image tests
exercise the software framebuffer, while real ESP32 performance claims require serial
telemetry from hardware.

## Editor model

The Qt Widgets editor uses dockable model/view panels. Selection is a service shared by
Hierarchy, Scene View, and Inspector. Mutations are command objects and enter an undo stack;
continuous gestures are grouped in transactions. Play mode clones the authoring scene so
runtime mutation cannot silently modify source data.

Long operations run as cancellable jobs. Processes are started with executable and argument
arrays, never a concatenated shell string. Diagnostics preserve file, line, column, severity,
tool, and raw text for navigation.

## Trust and containment

Opening project data does not execute scripts, build steps, or plugins. The editor records a
per-project trust decision before executing code. Archive extraction canonicalizes every
destination and rejects paths outside the chosen root. Asset importers receive explicit input
and output roots. Tool and plugin processes receive no implicit shell evaluation.

## Portability and allocation

Public runtime code uses C++20 with small standard-library abstractions. Hot paths preallocate
and use value/handle ownership. Shared ownership is reserved for desktop services where it is
actually required. ESP32 platform code decides internal RAM versus PSRAM placement and may
use fixed-capacity containers selected at pack time.

Major choices and their reconsideration conditions are recorded in `docs/decisions/`.
