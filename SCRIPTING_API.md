# C++ scripting API

C++ is the intended gameplay extension language. The portable API exposes a versioned
`ScriptComponent`, component lifecycle, reflected properties, scene/entity access, input mapping,
and an engine loop. Generate a safe starter pair with:

```powershell
fabgl_project_cli new-script path/to/project PlayerController
```

The command refuses invalid C++ identifiers and existing files. Add the generated `.cpp` to the
game target explicitly. Automatic source discovery, hot reload, clangd project generation, and
dynamic component serializer registration remain future integration work; the API is pre-1.0.

## Component contract

Derive from `fabgl::scripting::ScriptComponent`, create stable metadata, and override only needed
lifecycle callbacks:

```cpp
#include <fabgl/scripting/script_component.h>

class EnemyController final : public fabgl::scripting::ScriptComponent {
public:
    EnemyController() : ScriptComponent(metadataDefinition()) {}
    float speed = 3.5F;

private:
    static fabgl::TypeMetadata metadataDefinition() {
        auto metadata = fabgl::scripting::makeScriptMetadata(
            "game.EnemyController", "Enemy Controller");
        metadata.properties.push_back(fabgl::scripting::scriptProperty(
            "speed", &EnemyController::speed, 3.5F, "Movement"));
        return metadata;
    }
    void onUpdate(float deltaSeconds) override { static_cast<void>(deltaSeconds); }
};
```

Attach it through `Entity::addComponent<T>()` and handle the returned `Result<T*>`. Components
are non-copyable/non-movable and owned by their entity. `owner()` is valid after attachment.
`requiredApiVersion()` and `apiCompatible()` expose the requested compatibility boundary;
the current API version is `fabgl::scripting::CurrentApiVersion` (`0.1.0`).

Lifecycle order is create, enable, start, fixed/update/late update and collision/trigger events,
then disable and destroy. A disabled component does not receive active callbacks. Never retain a
raw entity/component pointer past scene mutation unless the owning code proves its lifetime.

## Scene and transform usage

```cpp
fabgl::Scene scene("Level 1");
auto created = scene.createEntity("Enemy");
if (!created) {
    return fabgl::Result<void>::failure(created.error());
}
fabgl::Entity* enemy = created.value();
auto script = enemy->addComponent<EnemyController>();
if (!script) {
    return fabgl::Result<void>::failure(script.error());
}
```

Use `Scene::setParent`, `clearParent`, and `worldTransform`; do not mutate hierarchy storage.
Those methods reject missing entities and cycles and propagate transform dirtiness. Durable
references should store `EntityGuid`, not pointers or display names.

## Reflection

`PropertyMetadata` describes a name/display name, type, flags, default, numeric constraints,
tooltip/category, asset filter, enum options, and type-safe reader/writer callbacks. Register a
`TypeMetadata` through `ReflectionRegistry::registerType`. A component may return that metadata
from `metadata()`.

Supported property kinds are Boolean, signed/unsigned integer, Float, Fixed, String,
Enumeration, BitFlags, Vec2, Vec3, Rect, Color, AssetReference, and EntityReference. Flags are
ReadOnly, Hidden, Serialize, RuntimeEditable, and EditorOnly. Lists, curves, component references,
quaternions, and actions/events are not present in the current property variant.

`DataComponent` is a reflected value container for built-in component schemas. It is useful for
data-driven scenes but does not replace a gameplay class with lifecycle logic.

`scriptProperty()` provides checked readers/writers for `bool`, signed/unsigned 32/64-bit
integers, `float`, `double`, `Fixed`, string, vectors, rectangles, colors, and asset/entity GUIDs.
An Inspector write using the wrong variant returns `ErrorCode::TypeMismatch`.

## Input and timing

Define named contexts, bind controls to actions or axes, set control values from the platform,
then call `InputMap::update()` once per frame. Query `action(name)` for held/pressed/released and
`axis(name)` for the resolved float value. Higher-priority enabled contexts win.

`EngineLoop` separates fixed update from variable update, clamps unusually large frame deltas,
limits catch-up steps, and exposes `FrameMetrics`. Gameplay physics should use the supplied fixed
delta; rendering may use `interpolationAlpha`. Callback failures are `Result<void>` values and
must be propagated.

## Errors and allocation rules

Fallible public APIs return `Result<T>` or `Result<void>`. Check the result before calling
`value()`; read `Error::code`, message, and context when it fails. Exceptions are reserved for
programmer misuse inside `Result`, not normal data errors.

On ESP32-sensitive paths, allocate resources during loading, use bounded pools for particles and
audio, use GUID/handle values across subsystem boundaries, and avoid per-frame strings, file I/O,
and unbounded containers. See `PERFORMANCE_BUDGETS.md`.

## Serialization warning

Scene format version 1 serializes entity identity, hierarchy, active state, and Transform. It
fails when an entity has a component without a registered serializer; it never silently drops
script state. A complete user-script component serialization/build registry is future work.
