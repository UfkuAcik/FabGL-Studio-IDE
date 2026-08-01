# Milestone 0 report

## Milestone

**M0 — Discovery and validation. Status: complete for the documented discovery criterion.**

## Completed work

Recorded architecture, roadmap, assumptions, risks, licenses, environment discovery, CI skeleton,
and release-locked ESP32/FabGL versions. The portable host build configures without Qt.

## Changed files

`ARCHITECTURE.md`, `BUILDING.md`, `ROADMAP.md`, `docs/ASSUMPTIONS.md`, `docs/RISKS.md`,
`docs/decisions/`, `toolchains/manifest.json`, `CMakePresets.json`, `.github/workflows/ci.yml`.

## Architecture decisions

See ADRs 0001–0005: portable core/Qt boundary, editor-friendly ECS, versioned source/binary packs,
toolchain choice, and GPL licensing.

## Commands run

Environment detection, CMake configure/build, and `ctest --preset release` were run. Current
release result: **6 passed, 0 failed, 0 skipped**. The legacy
GCC 8 host can build the current subset in C++2a mode but is not a supported release compiler.

## Test results

- Passed: release configure/build and the final 8 CTest programs.
- Failed: 0 CTest programs.
- Skipped: Qt editor and physical-board checks.

## Remaining risks

Qt 6 was unavailable, free disk space was low, and no serial device was positively identified as
the target board. Those are validation constraints, not hidden success claims.
