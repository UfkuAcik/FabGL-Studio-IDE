# ADR 0010: Keep presentation adapters small instead of requiring SDL

- Status: Accepted
- Date: 2026-08-13

## Context

The portable renderer must run headless in tests, in a native Windows player, inside Qt Studio,
and on FabGL. SDL would provide a desktop window/audio layer, but would not replace Qt in Studio
or FabGL on the board and would add another shipped runtime dependency.

## Options considered

1. Require SDL for every desktop runtime path.
2. Let Qt types enter the renderer and engine.
3. Render into a platform-neutral framebuffer and keep window, input, and audio adapters thin.

## Decision

Choose option 3. The PC player owns its native presentation/audio adapter, Studio owns a Qt
adapter, tests consume the framebuffer directly, and firmware presents through FabGL. SDL is not
a current dependency.

## Rationale

The shared framebuffer and command semantics are the behavior that needs parity. Keeping adapter
code small avoids a second GUI stack in Studio and keeps deterministic headless tests independent
of a window server.

## Positive consequences

- Renderer tests need no display server.
- Engine and renderer APIs remain Qt-, SDL-, and FabGL-free.
- Platform-specific input/audio failures stay outside gameplay state.

## Negative consequences

- Native window and audio code needs separate maintenance.
- SDL ecosystem features are not inherited automatically.

## Reconsider when

Adopt SDL for the standalone player if measured portability or controller support outweighs the
distribution cost, while preserving the neutral framebuffer and headless backend.
