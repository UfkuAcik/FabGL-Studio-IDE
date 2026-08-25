# ADR 0009: Use a trusted source extension host before a stable binary ABI

- Status: Accepted
- Date: 2026-08-13

## Context

FabGL Studio packages declare editor, runtime, importer, inspector, window, build, renderer, and
framework entry points. The first release must be extensible without pretending that arbitrary
downloaded native libraries can be loaded safely or that a pre-1.0 C++ class layout is a stable
ABI.

## Options considered

1. Dynamically load every installed native entry point.
2. Treat entry points only as descriptive package metadata.
3. Compile reviewed source packages into a host and register typed extensions through a small,
   Qt-free activation contract.

## Decision

Use option 3. `ExtensionRegistry` owns `IExtension` objects, validates stable identities and
dependencies, computes a deterministic activation order, and rolls back partial activation.
`ExtensionHost` exposes bounded named hooks and requires a host-supplied, content-bound trust
decision. Safe mode and the disable-extensions switch fail closed. Dynamic discovery remains
outside the first-release boundary.

### 2026-08-22 implementation amendment

The product now accepts an explicitly declared `.dll`, `.so`, or `.dylib` entry from the validated
project-local package lock. This is not filesystem scanning and not a stable third-party ABI: the
module must use extension-module ABI v1 and be built with the exact compiler, C++ runtime, FabGL
headers, configuration, and product release that loads it. The same content SHA-256 used for local
approval must still match before the library is opened.

After activation, each entry kind registers bounded service descriptors through a temporary,
Qt-free host table. Studio and Player dispatch only the documented operations for that kind.
Failures are caught at the boundary, reported, and disable that service for the rest of the
project session. Safe Mode, disabled plugins, an untrusted Studio project path, or any lock/content
mismatch still prevents the native library from being opened.

## Rationale

This provides real extensibility for source packages and every declared entry-point category while
keeping trust, lifetime, dependency, and error propagation explicit.

## Positive consequences

- Engine/runtime extension interfaces remain Qt-free and testable.
- A failed activation cannot leave earlier extensions or hooks active.
- Built-in extensions work in safe mode, while external code requires exact trust.
- A later C or IPC ABI can adapt to the same registry semantics.

## Negative consequences

- Installing a package does not compile or load it automatically.
- Native source extensions must be rebuilt as distribution-matched modules (or linked into an
  embedding host).
- Module ABI v1 detects obvious contract mismatches but is still not a stable cross-compiler or
  cross-release binary ABI.

## Reconsider when

Introduce a versioned C ABI or out-of-process protocol only after compatibility fixtures exist for
multiple compilers and versions, signature verification is available, and process isolation has a
documented threat model.
