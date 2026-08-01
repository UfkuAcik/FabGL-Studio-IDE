# ADR-0001: Portable C++ core and Qt Widgets editor

- **Status:** Accepted
- **Date:** 2026-08-01

## Context

The product needs a professional Windows desktop editor and a runtime that also compiles for
ESP32. Qt is suitable for dockable desktop tooling but is unavailable and inappropriate on
the target runtime.

## Options considered

1. Qt types throughout the engine and editor.
2. A Qt Widgets editor above a standard C++ engine.
3. A browser/Electron editor above a native runtime.

## Decision

Use C++20 and Qt 6 Widgets for Studio, with a strict Qt-free public engine and platform
adapters. Qt discovery is optional at configure time so core tests are never blocked by an
editor SDK.

## Rationale

Qt supplies mature model/view, docking, settings, processes, and cross-platform UI. The
boundary preserves embedded portability, headless testing, and alternative future frontends.

## Positive consequences

- One semantic engine can run in PC preview and ESP32 firmware.
- CI can test core logic cheaply without installing Qt.
- Editor code can use Qt idioms without leaking them into gameplay APIs.

## Negative consequences

- Explicit value conversion is required at the editor boundary.
- Qt and desktop compiler ABIs must be paired in packaging.
- Two presentation adapters require conformance tests.

## Reconsider when

Reconsider the desktop toolkit only if Qt cannot meet licensing/distribution requirements or
measured editor responsiveness, while retaining the portable core boundary.
