# Milestone 15 report

## Milestone

**M15 — Final quality pass. Status: partial.**

## Completed work

Strict warnings, formatting/static-analysis configuration, Windows/Linux core CI skeleton, source
policy checks, deterministic renderer goldens, unit/integration-style subsystem tests, licensing
files, release documentation, and truthful milestone evidence are present.

## Changed files

`.clang-format`, `.clang-tidy`, `.github/workflows/ci.yml`, `tests/`, documentation files,
`packaging/`, and `docs/progress/`.

## Architecture decisions

Release claims require evidence; estimated PC/ESP32 values and experimental low-poly are labeled.

## Commands run

On 2026-08-01, `ctest --preset release` reported **8 passed, 0 failed, 0 skipped** CTest programs.
This count is programs: first-party binaries contain 63 passing assertions, example integration
validates and replays all ten projects against exact checksums, and two offline hardware-log
fixtures cover PASS and expected-FAIL parser branches.

## Test results

- Passed: 8 release CTest programs, 132-file clang-format gate, 44-document/5-ADR contract,
  strict-warning builds, stage smoke, ZIP/checksum, and workflow lint/actionlint.
- Failed: 0 executed checks.
- Skipped: Qt GUI, analyzers/sanitizers, clean machine, NSIS, HIL, soak, and leak tests.

## Remaining work

The CI defines the Release/compiler/Qt/clang-tidy matrix but it has not run on a remote runner yet.
Qt GUI tests, sanitizer/leak runs, performance regression thresholds, recorded-input replay,
firmware HIL/soak, Windows 10/11 clean-machine ZIP/installer tests, UI accessibility/polish, and
signed release notes remain. This repository is therefore not a stable 1.0 release.
