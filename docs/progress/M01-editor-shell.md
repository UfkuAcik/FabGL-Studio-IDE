# Milestone 1 report

## Milestone

**M1 — Editor shell. Status: partial.**

## Completed work

Qt source implements the main window, dock panels, menus/toolbars, theme and layout persistence,
project create/open/save, recent projects, console, build settings, and modified-data prompts.

## Changed files

`apps/studio/src/MainWindow.*`, `ProjectDocument.*`, `BuildRunner.*`, and `apps/studio/CMakeLists.txt`.

## Architecture decisions

ADR 0001 keeps Qt out of the engine and permits a headless build when Qt is absent.

## Commands run

The dependency-free core-only validation passed 2/2 and the full non-Qt Release suite passed 8/8.
**Qt build, editor launch, layout restore, and visual QA were skipped** because no compatible Qt 6
SDK was installed; Studio-enabled configure correctly reported the omitted target.

## Test results

- Passed: portable release build and 8 non-Qt CTest programs.
- Failed: 0 CTest programs.
- Skipped: all Qt editor launch and persistence acceptance checks.

## Remaining work

Add first-run settings and graphical automated smoke tests, then validate create/save/reopen on
Windows 10/11. The editor and CLI now share the separate `startupScene` v1 contract; the Qt path
still needs a built integration test.
