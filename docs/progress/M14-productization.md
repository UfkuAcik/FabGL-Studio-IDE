# Milestone 14 report

## Milestone

**M14 — Productization. Status: complete for the unsigned 0.1 release candidate.**

## Completed work

Ten validated example projects, current user/editor/API/package documentation, managed Qt
6.8.3/MinGW 13.1 and NSIS 3.12 bootstrap paths, CMake install rules, Qt runtime deployment,
portable ZIP and Windows installer pipelines, staged CLI/GUI smoke checks, recovery/safe-mode,
license/notice installation, and firmware/toolchain metadata staging are implemented.

Studio performs bounded atomic autosave, detects an unclean session, rotates restore backups,
lists corrupt snapshots without restoring them, reopens the last project unless suppressed, and
supports `--safe-mode`, `--disable-plugins`, and `--no-reopen-last-project`.

## Architecture decisions

Toolchain caches are not bundled. ZIP and NSIS artifacts contain the deployed host application,
SDK, scripts, examples, and documentation. Trust and recovery are user-scoped state and cannot be
self-granted by a project manifest.

## Test results

The managed Qt strict target and its 19-slot offscreen smoke suite pass. Packaging and final clean
artifact hashes are recorded by the release pipeline/final report rather than copied into this
milestone file, so a later build cannot leave stale evidence here.

## External release gates

Code signing, publisher infrastructure, a Windows 10/11 clean-machine matrix, real-display DPI
and accessibility review, and store/notarization workflows require external release resources.
They are not claimed by this unsigned development repository.
