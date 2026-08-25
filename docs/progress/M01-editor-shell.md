# Milestone 1 report

## Milestone

**M1 — Editor shell. Status: complete.**

## Completed work

The Qt 6 editor builds as a real Windows application. It provides project create/open/save,
separate Project and Asset browsers, all required docks and menus, functional toolbar actions,
named/custom layouts, light/dark themes, recent-project state, first-run-safe settings and
unsaved-change protection. Settings are user-scoped and do not modify project data silently.

## Architecture decisions

ADR 0001 keeps Qt outside the portable engine. The managed desktop toolchain pins Qt 6.8.3 and
MinGW 13.1, while core/headless builds remain possible without Qt.

## Test results

The Studio library, executable and QtTest smoke target compile with warnings as errors. Offscreen
tests exercise construction, menus/docks, themes, layouts, project lifecycle and settings. Final
visual polish remains a human QA activity, not a substitute for functional tests.
