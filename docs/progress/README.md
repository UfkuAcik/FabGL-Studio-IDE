# Milestone evidence index

These reports describe repository evidence on 2026-08-01. Status is conservative:

- **complete**: the milestone's narrow acceptance criterion has direct build/test evidence;
- **partial**: useful implementation exists, but one or more acceptance items are absent or
  unverified;
- **blocked**: progress requires unavailable hardware/tooling or external state.

One blocked subcheck does not make an otherwise progressing milestone complete. The shared host
release validation command was `ctest --preset release`: 8/8 CTest programs passed (toolchain,
two offline hardware-log branches, engine, rendering, asset/project, frameworks, and ten-example
integration replay). Qt editor build/visual QA, physical-board upload/peripherals/soak, installer,
full static analysis, and clean-machine tests were not run in this environment.

| Milestone | Status | Primary evidence |
|---:|---|---|
| 0 | complete | Architecture, risk, licenses, pinned manifest, CMake/CI |
| 1 | partial | Qt editor source; Qt build unavailable |
| 2 | complete | Portable engine tests pass |
| 3 | partial | Deterministic native/headless PC demos; no SDL/audio output |
| 4 | partial | Scene/Hierarchy/Inspector/play/undo integration source; no Qt run |
| 5 | partial | Image/audio/asset DB/pack pass; several importers absent |
| 6 | partial / hardware blocked | Pinned toolchain and firmware compile path; no confirmed board test |
| 7 | partial | Versioned script API/generator/code editor; no automatic build glue/clangd |
| 8 | partial | Prefab and animation runtime tests; editor tools incomplete |
| 9 | partial | Validator/compiler/bytecode VM tests; no node editor/runtime scene binding |
| 10 | partial | PC raycast renderer/FPS framework pass; no map editor/ESP32 playtest |
| 11 | partial | PC racer renderer/framework pass; no track editor/opponent demo |
| 12 | partial | Experimental PC low-poly/TPS logic; no hardware budget evidence |
| 13 | partial | Audio/particles/UI/navigation/profiler/package foundations; editors absent |
| 14 | partial | Examples/docs/staged CLI and ZIP pass; no clean-machine artifact proof |
| 15 | partial | Current tests green; full release-quality matrix not run |
