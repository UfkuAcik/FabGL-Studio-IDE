# ADR 0015: Keep rendering modes modular over a bounded framebuffer

- Status: Accepted
- Date: 2026-08-13

## Context

The product supports 2D, tilemap, raycast, pseudo-3D racing, Mode 7, and low-poly presentation on
very different host and target budgets. A single stateful renderer would couple unrelated memory
and feature costs.

## Options considered

1. One monolithic renderer with mode flags.
2. A desktop GPU scene graph mirrored approximately on ESP32.
3. Independent deterministic renderer modules sharing small math, asset, framebuffer, and budget
   contracts.

## Decision

Choose option 3. Each renderer validates its own bounded inputs and emits into a common software
framebuffer. Scene presentation selects modules and asset resolvers; platform adapters only present
the result.

## Rationale

Modules allow each mode to use a target-appropriate algorithm while tests compare exact shared
semantics and firmware can include only needed paths.

## Positive consequences

- Feature and memory budgets are isolated by renderer.
- Headless reference rendering is deterministic.
- PC, Studio, and FabGL presentation share scene interpretation.

## Negative consequences

- Cross-mode effects require explicit composition.
- Software rendering limits desktop-only visual complexity.

## Reconsider when

Add a GPU backend if measured desktop needs require it, provided the software reference backend and
tolerance-based conformance tests remain authoritative.
