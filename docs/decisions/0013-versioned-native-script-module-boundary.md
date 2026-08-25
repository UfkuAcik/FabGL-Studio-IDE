# ADR 0013: Load native gameplay through a versioned module boundary

- Status: Accepted
- Date: 2026-08-13

## Context

Gameplay is C++-first, but project code must not be linked into engine core or executed merely by
opening a project. PC preview needs build diagnostics and reload/restart behavior; ESP32 needs
ahead-of-time firmware compilation.

## Options considered

1. Compile project sources directly into Studio.
2. Interpret a C++ subset at runtime.
3. Generate a public-API desktop module, validate its descriptor/version, and load it only at a
   trusted runtime boundary; generate a separate bounded companion ABI for target firmware.

## Decision

Choose option 3. Desktop build output is a versioned descriptor plus a native PC module. The player
checks kind, API version, project identity, file containment, and exported entry point before
component registration. Studio remains out of process and restarts preview when safe hot
replacement is not available. A clean native source save/reload stops the active PC preview, runs
the unified verified build, coalesces concurrent saves into one additional build, and recreates the
same Playing/Paused/external preview only after result verification. ESP32 receives a separately
generated, fixed-capacity companion;
desktop C++ is never silently compiled by the older target compiler.

## Rationale

The module contract separates build authority and failure from the editor process while preserving
ordinary C++ and a controlled engine API.

## Positive consequences

- Compiler diagnostics remain native and navigable.
- Untrusted code is not loaded into Studio.
- ABI/version mismatches fail explicitly.

## Negative consequences

- Native modules are compiler/platform specific.
- Target gameplay that must run on both hosts needs an explicit ESP32 companion implementation.
- State-preserving hot reload is limited.

## Reconsider when

Introduce a stable C ABI or isolated script worker after cross-version compatibility and sandbox
tests exist. Keep full player restart as the reliable fallback.
