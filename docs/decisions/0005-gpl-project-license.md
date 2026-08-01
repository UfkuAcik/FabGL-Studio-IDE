# ADR-0005: GPL-3.0-or-later project license

- **Status:** Accepted
- **Date:** 2026-08-01

## Context

Target firmware statically links the GPL-3.0-or-later Olimex FabGL fork. The project is intended
to be open source, while the desktop application dynamically uses LGPLv3 Qt modules and invokes
GPL command-line build tools as separate processes.

## Options considered

1. Permissive desktop/core license with a separately GPL firmware target.
2. GPL-3.0-or-later for first-party code, preserving all third-party licenses.
3. Proprietary distribution under separately negotiated commercial FabGL/Olimex licenses.

## Decision

License first-party FabGL Studio code as GPL-3.0-or-later. Dynamically link LGPL-eligible Qt
modules, keep managed command-line tools out of the Studio process, and ship exact corresponding
firmware source/build inputs. Closed-source firmware is not supported by the default project.

## Rationale

One compatible license minimizes ambiguity for shared engine/runtime code and matches the
open-source goal. It avoids creating a fragile claim that cross-platform gameplay code becomes
differently licensed only at the platform boundary.

## Positive consequences

- Clear compatibility with FabGL-linked firmware.
- Improvements to shipped runtime/editor code remain available to users.
- A single contribution policy is easier to audit.

## Negative consequences

- Proprietary games/firmware cannot use the default runtime distribution model.
- Offline packages carry substantial source and notice obligations.
- Qt deployment must still independently satisfy LGPLv3 requirements.

## Reconsider when

Reconsider only after qualified legal review and commercial licenses from all relevant FabGL
and Olimex rights holders, without weakening the rights of existing GPL releases.
