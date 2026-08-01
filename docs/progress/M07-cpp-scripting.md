# Milestone 7 report

## Milestone

**M7 — C++ scripting. Status: partial.**

## Completed work

The versioned `ScriptComponent` lifecycle and type-checked reflection property model are usable
from C++. `fabgl_project_cli new-script` emits a buildable reflected component without overwriting
existing files. Editor source includes a tabbed C++ editor, line numbers, highlighting,
find/replace, save prompts, build process output, and clickable file/line diagnostics.

## Changed files

`engine/include/fabgl/scripting/script_component.h`, `engine/src/script_component.cpp`,
`tools/project_cli/script_generator.*`, `apps/studio/src/CodeEditor.*`, `BuildRunner.*`, tests,
and `SCRIPTING_API.md`.

## Architecture decisions

Gameplay code remains portable C++ and project processes are launched without a command shell.

## Commands run

Component lifecycle/API compatibility/property tests and shared CTest passed. A generated
`PlayerController.cpp` compiled with strict warnings. Qt editor behavior was not built or run, so
code-editor diagnostics remain source-reviewed rather than release-verified.

## Test results

- Passed: component lifecycle, API version, reflected-property and generator assertions.
- Passed: generated C++ smoke compilation.
- Failed: 0 release CTest programs.
- Skipped: Qt code editor, clangd, automatic target integration, and diagnostic-navigation acceptance.

## Remaining work

Add automatic source discovery/build integration, clangd completion/refactoring, and component
serializer registration. Hot reload is not implemented.
