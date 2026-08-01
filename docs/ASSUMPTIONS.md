# Assumptions register

Assumptions are temporary engineering inputs, not claims of verified behavior.

| ID | Assumption | Reason | Validation / consequence |
|---|---|---|---|
| A-001 | Source files and project data use UTF-8. | Required for portable Unicode names. | Round-trip tests use Turkish and spaced paths. |
| A-002 | PC preview prioritizes semantic determinism over matching VGA electrical timing. | Hardware timing cannot be reproduced by a desktop window. | Golden/replay tests cover semantics; real timing requires hardware telemetry. |
| A-003 | A USB serial adapter identity is insufficient to authorize upload. | CH340 devices are used by many boards. | User/diagnostic confirmation selects a board before any write. |
| A-004 | Qt can be absent on contributor machines. | The initial Windows environment has no Qt installation. | Core/tools/tests configure independently; Studio target is conditional. |
| A-005 | Toolchains must live in a versioned managed profile, not be silently upgraded. | FabGL compatibility can lag ESP32 core releases. | Pins and checksums live in a manifest and change through an ADR. |
| A-006 | The initial ECS favors serialization and editability over archetype throughput. | ESP32 scenes are bounded and authoring stability is critical. | Profile real scenes; introduce packed system views only for measured hot paths. |
| A-007 | Low-poly 3D and TPS are experimental until measured on the target board. | Their usable budgets depend on resolution, PSRAM, and scene complexity. | UI/docs label them Experimental and publish only measured results. |
| A-008 | Online package repositories are outside the first stable local-package scope. | They add signing, identity, and service-operation requirements. | Local packages have manifests and trust checks; remote service is a later ADR. |

Resolved assumptions are retained with a date and link to the replacing evidence.
