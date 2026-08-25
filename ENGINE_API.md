# Engine API guide

Public headers live under `engine/include/fabgl/` and contain no desktop/platform toolkit
types. Namespace `fabgl` is the stable API root; renderer APIs use `fabgl::rendering`.

## Errors and values

Operations that can fail return `Result<T>` or `Result<void>` with an `ErrorCode`, message, and
key/value context. Callers must propagate or explicitly handle errors; corrupt authoring data
does not become a partially valid scene.

GUID types (`EntityGuid`, `AssetGuid`, `SceneGuid`, `ComponentTypeGuid`) prevent mixing domains.
Use random UUIDs for new durable objects and deterministic stable-name IDs for registered
engine types. Random UUIDs use the operating system RNG on supported hosts (BCrypt on Windows,
`getrandom` on Linux) with a process/time/counter fallback; canonical text is lowercase UUID form.

## Save games

`SaveSystem` wraps an `ISaveStorage` and keeps game state independent from scene authoring files.
It validates safe slot names, CRC-32 and payload length, supports multiple slots, and applies only
explicit sequential schema migrations. `MemorySaveStorage` is useful for tests.
`FileSaveStorage(directory, backupCount)` provides persistent PC or mounted-SD storage through a
same-directory atomic commit and bounded `.bakN` rotation. Reads reject symlinks and files above
64 MiB. Gameplay code owns the payload schema and should encode primitive, vector, player, entity,
and scene state deterministically before calling `save()`.

Math values include `Vec2`, `Vec3`, `Rect`, `Color`, Q16.16 `Fixed`, and `Mat4`. `Fixed` is
available to target-sensitive code but is not imposed on every subsystem.

## Engine loop and measured phases

`EngineLoop` owns the bounded fixed-step accumulator and the ordered fixed, physics, update,
AI/gameplay, animation, audio, asset-streaming, render-submission, rendering and presentation
callbacks. `FrameMetrics` reports the measured CPU duration of every phase separately as well as
the enclosing frame duration, fixed-step/drop counts and interpolation state. A missing callback
reports zero rather than an estimate. Hosts should send these fields to the measured-PC profiler
channel; ESP32 serial measurements and budget-model estimates remain distinct sources.

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

## Audio clips and streaming

`AudioMixer` accepts the same `AudioClipView` for memory and streaming clips. A memory view points
at interleaved mono/stereo floats. A streaming view supplies an `AudioFrameReader` callback and
stable context; the callback is `noexcept`, receives a bounded frame request, and must not retain
the output pointer. Each voice owns a fixed 128-frame cache, so mixing performs no allocation.
`AudioMixerStats` exposes cache refills, streamed frames, and short-read underruns separately from
voice and mixed-frame counters.

`ProjectAudioClip` keeps non-streaming `.fgla` assets as normalized float PCM. Streaming assets
retain the validated compact payload and use `decodeAudioClipFrames` to fill mixer windows. The
shared clip must outlive its voices. This path is compressed-memory streaming; asynchronous
filesystem/SD prefetch is deliberately not implied by the API.

## Rendering

The reference framebuffer and modular renderers are described in `RENDERERS.md`. Gameplay
submits renderer-specific bounded data instead of accessing a window or FabGL canvas directly.

## Prefab and material authoring

`PrefabLibrary` validates nested dependencies and resolves inherited root components and entity
hierarchies. `PrefabInstance` layers property, added-component and removed-component overrides;
`applyTo`, revert operations and `unpack` are explicit. `PrefabSerializer` reads legacy flat v1
and writes canonical `.fglprefab` v2 through `serialize` / `deserialize` without filesystem side
effects. `PrefabInstance::snapshot` / `fromSnapshot` expose validated deterministic override state,
while `PrefabInstanceSerializer` encodes the bounded `PrefabSceneInstance` root/entity/source map
used by Scene v2. The internal `fabgl.PrefabInstanceLink` component lets editor sessions discover
linked or missing instances after reload; unpack deliberately removes that linkage.

`MaterialAsset` pairs a stable asset GUID/name with the bounded runtime `Material` structure.
`MaterialSerializer` provides canonical `.fglmaterial` v1 text. Call `validateMaterial` for
renderer-specific errors/warnings and `estimateMaterialCost` for a deterministic upper-bound cost;
an estimate is not a target timing measurement.

## Animation authoring and runtime binding

`<fabgl/animation/animation_authoring.h>` keeps durable authoring assets separate from the
existing sampling runtime. `AnimationClipAsset` owns a stable asset GUID, curves, and events;
`AnimatorControllerAsset` owns its GUID, typed parameter declarations, named states, and ordered
transitions. Use `serializeAnimationClipAsset` / `deserializeAnimationClipAsset` for `.fglanim`
v1 and the corresponding `serializeAnimatorControllerAsset` /
`deserializeAnimatorControllerAsset` functions for `.fglcontroller` v1. Both APIs are bounded,
strict, locale-independent, and produce canonical text.

`buildAnimationClip` converts validated clip authoring data to the existing immutable shared
runtime clip. Building a controller requires an explicit `AnimationClipResolver`; each state clip
GUID must resolve to a valid `shared_ptr<const AnimationClip>`. The builder caches repeated GUIDs,
applies typed parameter defaults, adds transitions in their declared priority order, and starts
the declared initial state. Missing or null clips are errors—there is no implicit placeholder.

```cpp
fabgl::AnimationClipResolver clips = [&cache](fabgl::AssetGuid id) {
    return cache.loadAnimationClip(id); // Result<shared_ptr<const AnimationClip>>
};
auto runtime = fabgl::buildAnimatorController(controllerAsset, clips);
if (!runtime) {
    return fabgl::Result<void>::failure(runtime.error());
}
```

This builder can be wrapped by `SceneRuntimeConfig::animatorFactory` after the application has
loaded/deserialized a controller asset. Asset lookup, storage, hot reload, and missing-asset UI
remain application/editor responsibilities; the engine format layer does not perform file I/O.

API compatibility follows semantic versioning after 1.0. Before 1.0, every breaking change is
recorded in `CHANGELOG.md` and file-format changes require migrations regardless of API status.
