# FabGL Studio editor guide

This guide describes the implemented Qt 6 desktop editor. The repository-managed desktop
toolchain pins Qt 6.8.3 and MinGW 13.1; `scripts/bootstrap_desktop.ps1` installs those versions
and `scripts/build_desktop.ps1` performs the release build, offscreen Qt smoke test, deployment,
and packaging checks. Functional offscreen coverage does not replace a final visual and
accessibility review on a real desktop.

A real safe-mode Windows capture with the bundled Platformer project is maintained at
[`docs/images/studio-overview.png`](docs/images/studio-overview.png). `FabGLStudio --screenshot
<output.png> [project.fglproject]` regenerates a noninteractive capture from the actual executable;
the option rejects non-PNG output and exits after the grab.

## Projects and persistence

Use **File > New Project**, **Open Project**, **Save Project**, and **Save Project As**. Recent
projects are stored in `QSettings`. Project JSON and scene data are separate files; both are
saved for a consistent authoring state. `QSaveFile` gives the project manifest atomic replacement
semantics. Modified documents are marked in the window title and trigger a close prompt.

Studio reads the current `.fglproject` v2 schema and the legacy v1 schema. Saving always emits
v2. The v2 asset GUID/path/type table, input contexts, actions, axes, package requirements, and
PC/ESP32 target-profile IDs are decoded, validated, retained, and written back instead of being
discarded by the editor.
Manifests are bounded, reject unknown fields and unsafe paths, and require a canonical project
GUID. Use `fabgl_project_cli validate` for the same project-level workflow before automation.

For PNG/JPEG/BMP assets, **Edit Import Settings** opens a typed image panel instead of requiring
raw JSON. It controls resize, crop, palette/alpha/dither, grid slicing, FGLS atlas packing, pivot,
pixels-per-unit, indexed RLE compression, preload/stream residency, and Flash/PSRAM/SD placement.
The panel shows the current transformed thumbnail plus payload, memory, decode and pixels-per-frame
render estimates. Saving validates the schema and real source bounds, reimports both PC and ESP32
variants, and updates the project asset type between `image` and `sprite.atlas` as required.

The **Targets / Device** panel shows the profile selected by the manifest. This Studio build
supports `pc.default` and `olimex-esp32-sbc-fabgl-revb`; build and upload actions remain disabled
for an unsupported profile rather than silently substituting another board.

## Panels

- **Hierarchy** lists scene entities and controls selection.
- **Assets / Project** exposes project-relative content for browsing and drag/drop.
- **Scene** renders the authoring view and provides selection, camera, snap, and gizmo tools.
- **Game** uses the same component-driven `ScenePresenter` as the PC player, with resolution,
  aspect, palette, frame-rate, scaling, and ESP32-simulation controls. Studio loads manifest-bound
  visual assets through the shared `ProjectAssetLibrary` on open, save, and asset refresh. A
  missing or malformed visual asset is reported in Console and uses a bounded placeholder without
  preventing the project or scene from opening.
- **Inspector** edits entity identity and reflected built-in component properties.
- **Code Editor** provides tabs, line numbers, C++ highlighting, bracket matching, automatic
  indentation, find/replace, project-wide search, a bounded project tree and symbol index,
  go-to-line, external-file change detection, save prompts, and the native preview restart
  fallback. Saving or cleanly reloading a C/C++ file below `Scripts` while a PC preview is active
  stops it, performs a verified unified PC Debug/Release build, and restores Playing, Paused, or
  external-player state with a fresh process/session. Repeated saves during compilation produce at
  most one additional queued build.
- **Visual Script** edits, validates, compiles, loads, and saves `.fglvisual` graphs.
- **Animator** edits and validates controller state, parameter, and transition data.
- **Memory Analyzer** reports bounded project, scene, component, and asset estimates.
- **Profiler** shows editor-side scene/render/build counters. These are not ESP32 measurements.
- **Console**, **Build Output**, and **Serial Monitor** show editor events, process diagnostics,
  and explicitly opened serial traffic.

Panels are dockable. Use **View > Panels** to restore an individual panel and **View > Reset
Layout** to restore the default arrangement. Layout, geometry, recent projects, and dark/light
theme are persisted with `QSettings`. Named layouts are versioned and reject incompatible state.

## Scene editing and undo

Use **Edit > Add Entity** to add an entity, select it in Hierarchy, then use Inspector or the
Scene view to edit it. Delete, entity edits, reflected component edits, and committed transform
drags are backed by `QUndoStack`. The Scene toolbar exposes select, move, rotate, scale, snapping,
pan, zoom, and frame-selected operations. The transform hierarchy engine prevents parent cycles.

The editor exposes multi-selection, hierarchy reparenting, dedicated prefab authoring with
persisted Scene v2 instance overrides, and specialist tile/track/material/particle canvases. A
linked prefab is rediscovered after project reopen; missing sources stay visible, and **Unpack**
removes only the linkage while retaining the baked scene hierarchy.

## Play controls

**Play** clones the authoring scene and initializes its transient `SceneRuntime`. **Pause** freezes
scene and runtime simulation, **Step** advances both once, and **Stop** shuts down the runtime and
discards the play snapshot so authoring data is preserved.
Runtime edits are not applied back to the authoring scene. **PC Play** starts the separate native
player with the open project after the project path has been trusted. A successful standard PC
build must produce a schema-2 gameplay build result whose project GUID and module file remain
inside the expected per-project build directory. Studio validates that result and passes the
verified native module to the player as a separate argument; stale, missing, or escaping paths are
not accepted.

## Code and diagnostics

Open source files in Code Editor and save modified tabs before a build. Build diagnostics in the
output can be activated to open a file and line when the parser recognizes the compiler format.
External file changes are detected and require an explicit reload/keep choice. Project-wide
search and symbol scans are bounded to avoid an untrusted project forcing unbounded traversal.

The editor supplies syntax highlighting and navigation, but it does not bundle clangd semantic
completion, refactoring, or a debugger backend.

## Build, ESP32, and serial workflows

The target toolbar and **Targets / Device** panel expose PC Debug/Release/custom builds and the
reference or experimental-PSRAM ESP32 configurations. Commands are modeled as an executable plus
an argument vector and started with `QProcess`; no command shell is used for project-provided
arguments. Output and exit status are streamed to Build Output.

ESP32 Build first exports through `fabgl_project_cli`, then invokes the locked build script.
Upload is enabled only after a successful verified build result, read-only port detection, an
explicit board-candidate selection, project trust, and the Olimex confirmation checkbox. A final
confirmation dialog names the exact port, profile, and build result before the port is opened.
The serial monitor is a separate read/write operation and never performs an upload.

## Trust, recovery, and safe mode

Projects opened from disk are untrusted by default. Build, Play, PC-player, script, package-hook,
and upload execution remain disabled until the exact normalized project path is trusted. Trust is
stored in user settings, not in the manifest, and is not inherited by a copied project.

While a document is modified, Studio writes bounded atomic recovery snapshots every 30 seconds
and retains the newest five per project. An unclean-session marker enables **File > Recovery
Sessions**, where valid snapshots can be restored with rotating backups or discarded. Corrupt
snapshots are identified and never restored. Recovery state and trust state are separate.

Use `--safe-mode` to disable plugins and last-project reopening, `--disable-plugins` to disable
plugins for one launch, and `--no-reopen-last-project` to suppress automatic reopen. Telemetry is
off in every mode.

Native preview restart uses these same gates. It never makes an untrusted project executable,
never substitutes an unsupported target profile, and never runs extension hooks in Safe Mode.
Registered build-step services all receive `pre-build`; if any fails, all peers are still
dispatched but the process is not started. `post-build` receives the actual process result and its
failures are reported and disabled without falsifying the compiler result.

## Current limitations

The editor is a functional authoring shell, not a complete replacement for a mature commercial
IDE. Dedicated prefab, material, particle, tile/track, package-manager, timeline-profiler, and
debugger panels remain future UI work. Hardware-facing behavior still requires the checks in
`HARDWARE_TESTING.md`; editor resource estimates are not physical ESP32 measurements.
