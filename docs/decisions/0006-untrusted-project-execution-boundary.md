# ADR 0006: Treat project code as untrusted until a path is explicitly trusted

- Status: Accepted
- Date: 2026-08-09

## Context

A FabGL Studio project may contain native C++ and build commands. Merely opening scene or asset
data must not silently grant authority to execute that code. Trust must also survive normal editor
restarts without becoming project-controlled data.

## Options considered

1. Execute project commands whenever a project opens.
2. Put a trust flag inside the project manifest.
3. Keep trust in user-scoped state and gate every execution boundary.

## Decision

The editor stores canonical trusted project paths in user-scoped settings separate from the
project. Build, PC play and ESP32 actions remain disabled for an external untrusted project until
the user acknowledges the warning. Processes receive a program and argument vector, never an
interpolated shell command. Safe mode disables plugin activation and external execution while
still allowing recovery/export. Telemetry is off and the editor sends no project data.

## Rationale

Option 3 keeps the authority decision outside attacker-controlled content and makes the same
decision reusable for build, play, upload, package, and extension entry points. A shell-free
argument vector also avoids adding command parsing semantics to project data.

## Positive consequences

- Opening and inspecting data does not execute native code.
- Moving or changing the trust subject forces a fresh decision.
- Safe mode has a small, testable execution surface.

## Negative consequences

Trusting a project authorizes its configured native build program; it is not a sandbox. Moving a
project requires a new trust decision. Package/plugin trust remains a distinct content-bound
decision, so project trust cannot silently approve executable package entry points.

## Reconsider when

Reconsider the path-based identity when signed project identities or a documented process sandbox
are available. Never move trust into the project file itself.
