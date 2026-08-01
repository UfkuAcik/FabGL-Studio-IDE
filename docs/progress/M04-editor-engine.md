# Milestone 4 report

## Milestone

**M4 — Editor/engine integration. Status: partial.**

## Completed work

Editor source connects a real scene document to Hierarchy, basic Inspector, Scene and Game views,
entity add/delete/rename/move commands, undo/redo, drag selection, snapping, and play/pause/step/stop
using an authoring-scene clone.

## Changed files

`apps/studio/src/SceneDocument.*`, `EntityModel.*`, `EntityCommands.*`, `EditorViews.*`, and
`MainWindow.*`.

## Architecture decisions

The play snapshot preserves authoring state; undoable commands own authoring mutations.

## Commands run

Engine and rendering tests passed, but the editor target was not compiled or launched because Qt
was unavailable. Therefore the milestone's “author in editor and play” acceptance test is skipped.

## Test results

- Passed: portable scene and rendering tests.
- Failed: 0 release CTest programs.
- Skipped: Qt editor author/play/undo integration.

## Remaining work

Complete reflected component Inspector controls, hierarchy editing, rotation/scale gizmos,
multi-selection, and Qt integration tests.
