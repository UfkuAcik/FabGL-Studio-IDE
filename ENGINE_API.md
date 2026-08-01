# Engine API guide

Public headers live under `engine/include/fabgl/` and contain no desktop/platform toolkit
types. Namespace `fabgl` is the stable API root; renderer APIs use `fabgl::rendering`.

## Errors and values

Operations that can fail return `Result<T>` or `Result<void>` with an `ErrorCode`, message, and
key/value context. Callers must propagate or explicitly handle errors; corrupt authoring data
does not become a partially valid scene.

GUID types (`EntityGuid`, `AssetGuid`, `SceneGuid`, `ComponentTypeGuid`) prevent mixing domains.
Use random UUIDs for new durable objects and deterministic stable-name IDs for registered
engine types. Canonical text is lowercase UUID form.

Math values include `Vec2`, `Vec3`, `Rect`, `Color`, Q16.16 `Fixed`, and `Mat4`. `Fixed` is
available to target-sensitive code but is not imposed on every subsystem.

## Scene and components

`Scene::createEntity` creates a durable entity with an implicit Transform. Component lifecycle
is `onCreate`, enable, start, fixed update, update, late update, collision/trigger callbacks,
disable, and destroy. Scene lifecycle dispatch is deterministic in entity/component order.

Use `Scene::setParent` for hierarchy changes. It updates both sides, rejects self/descendant
cycles, and dirties affected world transforms. Do not mutate hierarchy containers directly.

Reflection registers a stable type ID plus property metadata, constraints, flags, and safe
read/write callbacks. Unknown custom component serializers cause a save failure until a
factory/serializer is registered; data is never discarded silently.

## Resources and diagnostics

`ResourceCache` uses typed handles and a byte budget with least-recently-used eviction. Loaded
resources with active handles remain owned; callers should release handles at scene/package
boundaries. Cache statistics distinguish hits, misses, loads, evictions, and resident bytes.

Structured logs include timestamp, level, system, message, optional source, and optional
entity ID. Memory and JSON-lines sinks are available; target builds can replace them with a
bounded serial sink.

## Rendering

The reference framebuffer and modular renderers are described in `RENDERERS.md`. Gameplay
submits renderer-specific bounded data instead of accessing a window or FabGL canvas directly.

API compatibility follows semantic versioning after 1.0. Before 1.0, every breaking change is
recorded in `CHANGELOG.md` and file-format changes require migrations regardless of API status.
