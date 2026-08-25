# Milestone 4 report

## Milestone

**M4 — Editor/engine integration. Status: complete.**

## Completed work

Hierarchy, reflected multi-type Inspector, Scene and Game views, GUID-synchronized selection,
select/move/rotate/scale gizmos, pan/zoom/frame, entity/component edits and undo/redo operate on the
authoring scene. Play/Pause/Step/Stop uses a serialized scene copy plus `SceneRuntime`, so Stop
shuts down transient systems and restores the untouched edit-time scene. Game View uses the same
component-driven `ScenePresenter` as the PC player and exposes target resolution/aspect, integer
and pixel-perfect scaling, palette preview, target FPS, speed, fullscreen and ESP32-estimate mode.
Manifest-bound visual assets are supplied by the shared `ProjectAssetLibrary`; open, save and
asset-refresh rebuild the resolver, while corrupt or missing assets remain non-blocking bounded
placeholders with a Console diagnostic.
Standard PC builds also validate the schema-2 gameplay result, project GUID and bounded module
location before PC Play receives the native script module.

## Test results

Portable scene/runtime tests cover serialization, bindings and project asset resolution; Qt tests
cover play-state isolation, toolbar/menu actions, undo/redo, view settings, real Racer asset reload,
non-blocking missing-asset fallback, native-module argument modeling, and build-result/path
rejection. Hardware parity is deliberately not inferred from PC simulation.
