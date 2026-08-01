# Changelog

All notable changes are documented here. The format follows Keep a Changelog and versions use
Semantic Versioning once public releases begin.

## [Unreleased]

### Added

- Greenfield C++20/CMake project foundation with dependency-free core build path.
- Architecture, roadmap, assumptions, risks, and initial ADRs.
- Portable ECS/scene/reflection core plus input, physics, animation, audio mixing, prefab,
  visual-bytecode, UI, particles, navigation, profiling, save, and package foundations.
- Versioned reflected C++ gameplay script API and safe starter-script generator.
- Deterministic 2D, raycast, pseudo-3D racer, and experimental low-poly renderers.
- Native Windows/headless PC player with ten distinct deterministic example previews.
- PNG/JPEG/BMP and WAV import on Windows, GUID asset database, and deterministic asset packs.
- Qt 6 Studio source with projects/scenes, docks, Scene/Game views, Inspector, code editor,
  build diagnostics, play-state cloning, undo/redo, themes, and layouts.
- Locked Arduino-ESP32/FabGL toolchain manager, diagnostic firmware, and guarded host scripts.
- Cross-platform CI, strict-warning tests, release staging, portable ZIP, and optional NSIS flow.

### Security

- Established explicit project/plugin trust, bounded path, process argument, telemetry, and
  upload-selection policies before importer or plugin execution is introduced.
