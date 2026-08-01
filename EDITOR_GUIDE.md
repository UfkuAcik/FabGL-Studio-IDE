# FabGL Studio editor guide

This guide describes controls present in the Qt editor source. The current environment lacked
a compatible Qt 6 SDK, so these controls have not received a local graphical build or visual QA.
They must not be treated as release-validated UI until a Qt CI or clean-machine run passes.

## Projects and persistence

Use **File > New Project**, **Open Project**, **Save Project**, and **Save Project As**. Recent
projects are stored in `QSettings`. Project JSON and scene data are separate files; both must be
saved for a consistent authoring state. `QSaveFile` gives the project manifest atomic commit
semantics. Modified documents are marked in the window title and trigger a close prompt.

The editor's current project reader is strict. It accepts its current schema only and rejects
unsafe paths. The command-line examples currently use an earlier project schema, so validate or
migrate data rather than editing a version number manually.

## Panels

- **Hierarchy** lists scene entities and controls selection.
- **Assets / Project** exposes project-relative content for browsing and drag/drop.
- **Scene** renders the authoring view, selects entities, and supports a snapped position drag.
- **Game** renders a cloned play snapshot into the reference framebuffer.
- **Inspector** edits the selected entity's basic values.
- **Code Editor** provides tabs, line numbers, C++ highlighting, find/replace, go-to-line, and
  save prompts.
- **Profiler** shows editor-side scene/render/build counters. These are not ESP32 measurements.
- **Console** contains editor events and errors.
- **Build Output** streams the configured process output.

Panels are dockable. Use **View > Panels** to restore an individual panel and **View > Reset
Layout** to restore the default arrangement. Layout, geometry, recent projects, and dark/light
theme are persisted with `QSettings`.

## Scene editing and undo

Use **Edit > Add Entity** to add an entity, select it in Hierarchy, then use Inspector or the
Scene view to edit it. Delete and entity edits are backed by `QUndoStack`; toolbar/menu Undo and
Redo invoke those commands. Transform dragging shows a preview and commits one undoable edit on
release. The transform hierarchy engine prevents parent cycles, but the current editor does not
yet expose the complete component/reflection inspector, reparenting UI, prefab controls, multi-
selection, rotation/scale gizmos, or tile/track/material editors.

## Play controls

**Play** clones the authoring scene. **Pause** freezes the editor simulation, **Step** advances a
paused snapshot once, and **Stop** discards the play snapshot so authoring data is preserved.
This is an editor preview state machine, not hot reload and not an ESP32 emulator. Runtime edits
are not applied back to the authoring scene.

## Code and diagnostics

Open source files in Code Editor and save modified tabs before a build. Build diagnostics in the
output can be activated to open a file and line when the parser recognizes the compiler format.
The editor supplies syntax highlighting and navigation, but clangd completion, semantic
diagnostics, refactoring, generated project files, and the “new component script” wizard are not
implemented.

## Build command safety

Use **Build > Build Command** to set an executable and argument list. The editor starts it with
`QProcess` in the project root without invoking a shell. This preserves argument boundaries and
reduces command-injection risk, but a trusted project can still name an executable that changes
the machine. Review imported project build settings before running them.

**Build Project** starts the process; **Cancel Build** requests termination. The output panel
shows the command and exit code. Build, upload, and serial monitor are not yet a unified workflow,
and the current toolchain manager deliberately never uploads.

## Current limitations

The visual graph, animator/timeline, material, particle, UI, profiler timeline, package manager,
memory analyzer, breakpoint debugger, device selector, serial monitor, and hardware budget UI are
engine/data foundations or planned work, not complete editor panels. Crash recovery and autosave
rotation also remain incomplete. Consult `docs/progress/` before relying on a milestone claim.

