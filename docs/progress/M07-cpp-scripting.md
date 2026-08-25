# Milestone 7 report

## Milestone

**M7 — C++ scripting. Status: complete with the documented restart fallback.**

## Completed work

The versioned `ScriptComponent` lifecycle and type-checked reflection property model are usable
from portable C++. `fabgl_project_cli new-script` emits a reflected desktop component plus a
fixed-capacity ESP32 `Start`/`Update` companion without overwriting existing files. Both `new` and
`new-script` deterministically maintain guarded CMake/module glue that
discovers sorted gameplay sources, compiles them as C++20 with strict warnings-as-errors, and links
them to the exported `FabGLStudio::Engine` SDK target. User-owned root CMake files are preserved.

`scripts/build_project_scripts.ps1` provides the real external configure/build path. It validates
the project manifest, rejects empty source sets, traversal and symlink/reparse-point inputs, invokes
CMake/Ninja without a command shell, emits `compile_commands.json`, and preserves the compiler's
native `file:line:column` errors. Its generated link-check target ensures source objects are not
silently omitted by a static-library-only smoke test.

The editor includes multiple C++ tabs, line numbers, highlighting, bracket matching, automatic
indentation, find/replace, go-to-line, project file tree, find-in-files, bounded symbol index,
external-change detection, save prompts, build output, and clickable file/line diagnostics. Its
bounded clangd JSON-RPC client provides completion, navigation, references, hover, diagnostics,
rename, and formatting with a visible fallback when clangd is unavailable.

Studio implements the stable native restart fallback in product flow: a clean native source save
or external reload stops an active playing/paused Studio session or external PC player, invokes the
unified verified PC build, and recreates the same preview kind. Saves during compilation coalesce
into one extra build. Trust, PC target profile, Safe Mode/plugin, extension BuildStep, cancellation,
and result-integrity boundaries remain active, and the edit scene is not replaced by runtime state.

## Architecture decisions

Gameplay code remains portable C++. Project build artifacts live outside the project source tree by
default. The external driver consumes only the managed Scripts glue and an installed SDK; it never
executes or replaces a custom root `CMakeLists.txt`.

Desktop C++20 gameplay and the Arduino/Xtensa companion are separate implementations behind
versioned host contracts. The exporter stages only guarded files below `Scripts/ESP32`, so it does
not pretend arbitrary desktop code is portable to the embedded compiler.

## Verification

- Project-model tests cover identifier/keyword rejection, reflected component generation,
  deterministic managed glue, strict flags, idempotent refresh, and custom-CMake preservation.
- A generated `PlayerController.cpp` is configured and linked against the repository-installed SDK
  with strict warnings, and its `compile_commands.json` is checked for the source and `-Werror`/`/WX`.
- A deliberate generated-source syntax error is checked separately to confirm a natural source
  `file:line` diagnostic and a non-zero build result.
- External acceptance checks confirm explicit no-source failure and rejection of both project and
  Scripts junction/reparse-point paths without writes through the link target.
- Host tests cover deterministic multi-class ESP32 module generation, rejection of missing module
  entries, bounded descriptors, and script runtime dispatch. A managed Olimex Release compile of a
  generated companion records `scriptRuntime=true` and produces a linked firmware binary without
  opening a serial port.

## Explicit boundary

Studio manages a user-configured clangd executable but does not bundle one inside the application
package. Native ABI-safe in-process hot reload is intentionally not promised: the supported stable
workflow is the automatic verified full-session/player rebuild and restart described above. The
ESP32 companion supports only its documented bounded runtime view, not the full desktop engine API.
