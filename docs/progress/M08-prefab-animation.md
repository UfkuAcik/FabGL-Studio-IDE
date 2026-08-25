# Milestone 8 report

## Milestone

**M8 — Prefab and animation. Status: complete for the v1 authoring scope.**

## Completed work

Prefabs support stable asset/entity GUIDs, local entity hierarchy, nested dependencies, property
and added/removed component overrides, apply/revert, missing/cycle diagnostics and unpack. The
strict canonical `.fglprefab` reader migrates v1 to v2 and bounds entities, components, properties,
strings and references. Baked instances now persist a canonical, bounded `fglprefabinstance 1`
link in Scene v2, including source-to-scene GUID mappings and every override class. The Prefab
Editor discovers those links after reload; revert/apply remain available, missing assets retain a
visible baked/placeholder instance, and unpack removes the link without deleting the hierarchy.

Animation supports step/linear/cubic curves, clips, looping events, typed bool/trigger/int/float
controller parameters, ordered transitions, exit time and blending. `.fglanim` and
`.fglcontroller` use stable asset GUIDs, bounded canonical readers and explicit clip GUID
resolution. The Qt Animator panel exposes states, parameters, transitions, preview and timeline
controls. The Animation Showcase participates in deterministic example replay and contains a real
`AnimatedCharacter.fglprefab` plus a baked, linked animated hierarchy with a persisted particle
rate override.

## Test results

Strict MinGW/GCC 13 tests cover prefab v1 migration/v2 round-trip, nested hierarchy, dependency
cycles, overrides/unpack, malformed data, animation authoring round-trip/corruption, resolver
de-duplication/missing clips and runtime evaluation. No filesystem path is guessed by the core
serializers; editor/project code owns safe file selection and atomic writes.

Dedicated Qt tests click instantiate/override/revert/apply/unpack, save Scene v2, reconstruct the
panel and document, and repeat operations on the discovered instance. Engine tests cover canonical
instance-state ordering, bounds/corruption, full override round-trip and a real Scene v2 component
round-trip. Example integration validates and deterministically prepares the prefab GUID/type entry.
