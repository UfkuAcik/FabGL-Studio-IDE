# Changelog

All notable changes are documented here. The format follows Keep a Changelog and versions use
Semantic Versioning once public releases begin.

## [Unreleased]

### Added

- Greenfield C++20/CMake project foundation with dependency-free core build path.
- Architecture, roadmap, assumptions, risks, and initial ADRs.
- Portable ECS/scene/reflection core, scene v2 migration, input, 2D/experimental 3D physics,
  animation, native/audio mixing, hierarchical prefabs, materials, visual bytecode, UI,
  particles, navigation, profiling and persistent save storage.
- Versioned reflected C++ gameplay script API, safe starter-script generator and deterministic
  external project compile/link integration.
- Automatic fail-closed native gameplay preview restart on clean source saves/reloads, preserving
  Studio Playing/Paused or external-player kind, coalescing builds, validating module results, and
  honoring extension pre/post build lifecycle hooks.
- Deterministic 2D, raycast, pseudo-3D racer, and experimental low-poly renderers.
- Native Windows/headless PC player with WinMM output and ten deterministic project replays.
- Image/atlas/thumbnail, audio, CSV/JSON tilemap, OBJ and BDF import, GUID database/cache and
  deterministic target packs.
- Qt 6 Studio with reflected Inspector, functional Scene/Game/Code/Visual/Animator/Profiler/
  Memory/Build tools, play-state cloning, undo/redo, themes, named layouts and serial workflows.
- Transactional mixed-value multi-selection editing, trusted bounded AssetImporter/
  CustomInspector/CustomWindow product hooks, severity-aware build output, and Studio actions for
  the unified ESP32 build/deploy and individual hardware-diagnostic workflows.
- Atomic autosave/recovery, safe mode, last-project restore and explicit external-project trust.
- Schema-2 local package manager with install/list/validate/remove, locks, ownership and
  content-bound executable trust.
- Locked Arduino-ESP32/FabGL profiles, project export, diagnostic firmware, guarded upload and
  serial scripts.
- A portable allocation-free lifecycle scheduler foundation and deterministic external-SD asset
  pack/binder primitives. Firmware scheduler adoption and the physical SD streaming adapter remain
  explicitly partial; ESP32 runtime capacities are gated at 48 entities, 64 assets and 128 live
  particles to preserve internal-DRAM headroom.
- Cross-platform CI, strict-warning/security/replay tests, Qt deployment, portable ZIP and pinned
  NSIS installer flow.

### Security

- Enforced separate project and package trust, bounded canonical paths, reparse/link rejection,
  program/argument process APIs, telemetry-off defaults and exact port/profile upload approval.
