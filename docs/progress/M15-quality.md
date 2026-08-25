# Milestone 15 report

## Milestone

**M15 — Final quality pass. Status: complete for locally executable checks.**

## Completed work

Strict warnings-as-errors, formatting/static-analysis configuration, Windows/Linux/Qt/ESP32 CI,
source-policy and documentation-contract checks, deterministic renderer goldens and replay,
unit/integration/security/negative-path tests, managed desktop toolchains, packaging smoke tests,
licensing files, and evidence-based completion documentation are present.

The Studio tests run offscreen with a 120-second outer guard. This replaces the former 30-second
guard that could terminate a healthy but slower GUI suite without actionable output. Project-open
tests use a non-modal error-returning path, so malformed data fails immediately instead of leaving
headless CI waiting on a message box.

## Current local evidence

- Managed Qt 6.8.3, MinGW 13.1, Release, warnings as errors: Studio target built successfully.
- `fabgl_studio_smoke_tests`: 19 passed, 0 failed, 0 skipped.
- Covered paths include project v2 migration/preservation, trust, recovery, code-editor external
  changes and bounded indexing, serial safety gates, visual/animation panels, layouts, and scene
  editing.
- The deterministic PC renderer regression gate is documented in
  [M15-performance-regression.md](M15-performance-regression.md), including its measured local
  result, CI budgets and explicit separation from physical ESP32/HIL claims.

The final aggregate CTest/package/HIL counts and artifact hashes belong in the final report after
the clean end-to-end run.

## External quality gates

Remote CI status, code signing, long hardware soak, destructive power-loss testing, physical
peripheral certification, accessibility review, and clean-machine installer UX remain external
evidence. They do not invalidate local software completion, but they must be completed before a
stable signed 1.0 release claim.
