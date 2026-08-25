# Assumptions register

Assumptions are temporary engineering inputs, not claims of verified behavior.

| ID | Assumption | Reason | Validation / consequence |
|---|---|---|---|
| A-001 | Source files and project data use UTF-8. | Required for portable Unicode names. | Round-trip tests use Turkish and spaced paths. |
| A-002 | PC preview prioritizes semantic determinism over matching VGA electrical timing. | Hardware timing cannot be reproduced by a desktop window. | Golden/replay tests cover semantics; real timing requires hardware telemetry. |
| A-003 | A USB serial adapter identity is insufficient to authorize upload. | CH340 devices are used by many boards. | User/diagnostic confirmation selects a board before any write. |
| A-004 | A system Qt installation cannot be assumed on contributor machines. | Desktop SDK availability and ABI vary. | The managed bootstrap supplies pinned Qt 6.8.3/MinGW 13.1; core/tools can still configure independently. |
| A-005 | Toolchains must live in a versioned managed profile, not be silently upgraded. | FabGL compatibility can lag ESP32 core releases. | Pins and checksums live in a manifest and change through an ADR. |
| A-006 | The initial ECS favors serialization and editability over archetype throughput. | ESP32 scenes are bounded and authoring stability is critical. | Profile real scenes; introduce packed system views only for measured hot paths. |
| A-007 | Low-poly 3D and TPS are experimental until measured on the target board. | Their usable budgets depend on resolution, PSRAM, and scene complexity. | UI/docs label them Experimental and publish only measured results. |
| A-008 | Online package repositories are outside the first stable local-package scope. | They add signing, identity, and service-operation requirements. | Local packages have manifests and trust checks; remote service is a later ADR. |
| A-009 | Native gameplay hot reload is not reliable enough for the stable workflow. | ABI and live-object replacement can corrupt editor/player state. | The supported fallback rebuilds and restarts the PC player, as allowed by the specification. |
| A-010 | `COM5`/CH340 detection does not identify an Olimex board. | The same adapter appears on many devices. | Upload remains disabled until the user explicitly confirms both port and board profile. |

Resolved assumptions are retained with a date and link to the replacing evidence.
