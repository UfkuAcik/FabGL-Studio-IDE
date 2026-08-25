# C++ scripting API

C++ is the intended gameplay extension language. The portable API exposes a versioned
`ScriptComponent`, component lifecycle, reflected properties, scene/entity access, input mapping,
and an engine loop. Generate a safe starter pair with:

```powershell
fabgl_project_cli new-script path/to/project PlayerController
```

The command refuses invalid C++ identifiers and existing files. It also creates or refreshes the
managed `Scripts/FabGLStudioScripts.cmake` source-discovery glue. A managed top-level
`CMakeLists.txt` is created only when one does not exist; a user-owned top-level CMake file is
never replaced. The generated target links `FabGLStudio::Engine`, requires C++20, sorts discovered
`.cc`/`.cpp`/`.cxx` files, and treats the portable warning set as errors.

## Configure and build project scripts

Install the FabGL Studio SDK, then run the repository or installed copy of the build driver:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_project_scripts.ps1 `
  -ProjectPath path/to/project/Game.fglproject `
  -SdkRoot out/install/desktop-release
```

The driver validates manifest schema 2, the complete project/Scripts path chain, the managed glue's
schema/hash, and every recursively discovered source before invoking CMake. Empty source sets and
symbolic links, junctions, reparse points, or sources escaping `Scripts/` are rejected. Modified
glue is never executed; refresh it with `new-script`. The driver configures a shared gameplay
module against the installed `FabGLStudio::Engine`; it does not modify or execute a custom project
`CMakeLists.txt`. Generated `ModuleEntry.cpp` exports the versioned module descriptor and registers
each generated class through `FABGL_REGISTER_SCRIPT`.

In a developer checkout the default output is `out/project-scripts/<projectGuid>`; an installed
copy uses the current user's local application-data directory so a read-only installation remains
usable. The successful result contains `build-result.json`, and CMake's compilation database is at
`build/<debug-or-release>/compile_commands.json`. Syntax and build failures are emitted directly by
the selected compiler, preserving normal `file:line:column` diagnostics. Pass `-CxxCompiler` when
the compiler matching the installed SDK is not the default CMake compiler. `-DryRun` validates and
prints the plan without writing, configuring, or compiling.

The schema-2 build result records the module path and SHA-256. The PC player accepts only explicit
`--script-module` paths, validates ABI/API/type/factory contracts, and replaces serialized
`fabgl.ScriptComponent` placeholders by their registered native class before lifecycle dispatch.
Studio validates result kind/schema/project GUID, the expected per-project output boundary, the
module file, and its SHA-256 before passing that argument to Play PC. Module ownership outlives the
scene, so native destructors run before the library is unloaded. Automatic in-process hot reload
and live instance replacement are intentionally replaced by an automatic full restart fallback;
the API is pre-1.0. A clean save or external reload below `Scripts` stops an active Studio Play
(including Paused) or external PC player, runs the unified PC Debug/Release orchestrator, accepts
only its verified result/module, and then recreates the same preview kind. Source saves observed
during compilation are coalesced into one additional build, so a preview is never restarted with a
module older than the newest observed save. Build cancellation/failure or result verification
failure is fail-closed and leaves the editable scene intact. Registered BuildStep services execute
through pre/post phases; a failed pre-build service prevents process launch after all peer services
have been dispatched. Runtime/renderer/framework services receive matching Studio Play
startup/update/shutdown calls.
Studio can start a configured `clangd` process against the generated compilation database. Its
bounded JSON-RPC client owns initialization/shutdown, document synchronization, completion,
definition, references, hover, diagnostics, rename, and formatting. Missing `clangd` is a visible,
non-crashing fallback; Studio never invokes it through a command shell and confines editor reads
and atomic writes to the canonical project root.

## Portable ESP32 companion module

`new-script` also creates `Scripts/ESP32/<Class>Esp32.h/.cpp` and maintains the guarded
`Scripts/ESP32/FabGLStudioEsp32Module.cpp`. The desktop class and ESP32 companion deliberately use
different ABIs: the desktop module receives the full C++20 engine API, while the companion is
compiled directly into the pinned Arduino/Xtensa firmware and receives the allocation-free,
fixed-capacity `fabgl_project_runtime::RuntimeProject` view.

The ESP32 ABI is currently version 1. It supports at most 16 uniquely named descriptors, names of
at most 47 bytes, and `Start`/`Update` callbacks. The managed module exports
`fabglProjectGetEsp32ScriptsV1` through `FGL_ESP32_SCRIPT_MODULE`; the exporter rejects missing or
duplicate descriptors, sketches, traversal/reparse inputs, custom unmarked module glue, and a
desktop gameplay source set that has no portable companion. Only files below `Scripts/ESP32` are
staged into `src/ProjectScripts`; desktop C++ is never silently fed to the older Xtensa compiler.

This is real firmware gameplay execution, but it is not binary or source-level hot reload and it
does not claim that an arbitrary desktop component can run unchanged on ESP32. Keep callbacks
bounded, avoid allocation and blocking I/O in `Update`, and use the runtime's manifest input and
entity/component accessors. `build-result.json` records `portableScriptFileCount`,
`scriptRuntime`, firmware/RAM sizes, and hashes; compiling never implies upload.

### Explicit ESP32 gameplay saves

When the firmware has mounted its SD filesystem it binds a versioned save adapter to
`RuntimeProject`. Portable gameplay code can explicitly call
`project.saveSlot("campaign", &playerGuid)` or
`project.loadSlot("campaign", &playerGuid)`. `persistenceAvailable()` reports whether the API is
bound and `lastPersistenceError()` returns the numeric `fabgl_project_save::Error` after a failed
operation. Boot, project parsing, diagnostics, autosave and the soak workload never call these
methods; there is no implicit SD write.

The convenience API captures the scene GUID/name and at most 16 entities by stable GUID, including
name, active/component state, movement mode, transform, vertical velocity and vehicle speed. The
optional player GUID is validated before save and again before restore. Restore validates the
scene, player, every entity and every finite numeric value before mutating runtime state. Transient
input controls and authoring scene bytes are deliberately excluded.

`ProjectSaveRuntime.h` exposes the lower-level allocation-free `Document`/`SaveService` API for
game-owned state: 8 primitive, 4 scene and 4 player fields; Boolean, signed/unsigned 32-bit,
float, Vec2, Vec3, strings up to 63 bytes and entity GUID values; a 4 KiB encoded-file ceiling;
24-character safe slot names; CRC-32; sequential schema migrations; and one retained backup.
These are hard target limits. The default firmware callback captures runtime state only; a game
that needs the extra fields owns a `Document`, populates it, and passes it as the optional third
argument. Load fills the same document before applying its validated runtime snapshot:

```cpp
fabgl_project_save::Document state;
state.putPrimitive("score", fabgl_project_save::Value::unsigned32(score));
state.putPlayer("health", fabgl_project_save::Value::signed32(health));
project.saveSlot("campaign", &playerGuid, &state); // the only operation here that writes SD

if (project.loadSlot("campaign", &playerGuid, &state)) {
    const auto* savedScore = state.primitive("score");
    const auto* savedHealth = state.player("health");
    if (savedScore != nullptr &&
        savedScore->type == fabgl_project_save::ValueType::Unsigned32)
        score = savedScore->payload.unsignedValue;
    if (savedHealth != nullptr &&
        savedHealth->type == fabgl_project_save::ValueType::Signed32)
        health = savedHealth->payload.signedValue;
}
```

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
Enumeration, BitFlags, Vec2, Vec3, EulerAngles, Quaternion, Rect, Color, AssetReference,
EntityReference, ComponentReference, bounded homogeneous List, Curve, AnimationCurve,
ActionReference, and EventReference. Flags are ReadOnly, Hidden, Serialize, RuntimeEditable, and
EditorOnly. Strings, lists, and curves have explicit capacity limits; nested lists/curves are not
accepted. Numeric slider and multiline text editor hints are validated with the metadata.

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

## Visual scripting v1

`VisualNodeRegistry::builtins()` is the authoritative typed-node catalog. It includes metadata
for Event, Branch, Sequence, Delay, Variable, Function, Entity, Component, Vector, Input,
Collision, Audio, Animation, Scene, UI, Math, and Reference categories. Create nodes through
`VisualNodeRegistry::create`; this copies the registered pin IDs, names, value types, directions,
and mandatory-connection contract into a copyable `VisualNode`. `VisualGraph` and its node/edge/
comment records are value types, so editor undo snapshots can copy them without retaining runtime
pointers. Persistent files use the deterministic `.fglvisual` v1 contract in `FILE_FORMATS.md`.

The v1 compiler and VM execute a deliberately small deterministic core: start/collision entry,
branch, two-output sequence, numeric and Boolean constants, numeric variables, set/get, add,
multiply, less-than, and return. Compilation rejects cycles, incompatible or multiply-connected
inputs, missing mandatory pins, unknown schemas, and unresolved required references. Bytecode is
limited to 4,096 bytes and 1,024 instructions; the VM additionally limits its stack to 256 values,
clamps the runtime instruction budget to 1,024, and validates constant, variable, jump, and host
call operands. These are hard safety ceilings intended for the ESP32 runtime, not optimization
targets.

Delay and all calls into entity/component, vector, input, audio, animation, scene, UI, or arbitrary
game functions cross an explicit `VisualHostCallbackTable`. A host node cannot compile unless the
table contains the exact callback name and argument count, and bytecode containing a host call
cannot execute without a table. Each callback receives a validated descriptor (name, opaque
payload, and optional typed GUID references) plus at most eight numeric arguments, returns
`Result<double>`, and may not return a non-finite value. Register only application-owned names;
files cannot install callbacks or contain native function pointers.

```cpp
fabgl::VisualHostCallbackTable callbacks;
auto registered = callbacks.add(
    "game.open-door", std::uint8_t{1},
    [](const fabgl::VisualHostCallDescriptor& call,
       const std::vector<double>& arguments) -> fabgl::Result<double> {
        // Resolve call.entityReference in application-owned state before acting.
        return fabgl::Result<double>::success(arguments.front());
    });
```

This external-call layer is experimental. `flow.delay` is a bounded latent VM operation: it
returns a validated continuation instead of sleeping or invoking `time.delay`, and
`SceneRuntime` resumes that continuation during later updates after the requested interval.
Continuation instruction pointers, stack values, duration (0 to 86,400 seconds), per-invocation
instruction count, and per-phase budget are validated. Other host calls remain synchronous.
Hosts must validate referenced objects at the moment of use and keep callback lifetime/threading
under application control. The registry categories describe stable authoring schemas even where
execution still depends on that host boundary.

### Default desktop visual host

Studio Play and `fabgl_player_pc --project` install the same `ProjectVisualHost` callback table.
Input is resolved before `SceneRuntime::update`, audio clips are resolved through the active
project streaming cache and retained until their mixer voice ends, and Animator/RuntimeUI handles
are looked up from the bound `SceneRuntime` at the moment of each call. Entity and component
targets remain GUID-based. Reflected component writes accept only `RuntimeEditable`, non-read-only
Boolean or numeric properties. The adapter keeps bounded diagnostics, audio ownership, payload
bytes, and scene-load requests; a callback table copied past the host lifetime returns
`InvalidState` instead of dereferencing expired services.

The authorable desktop callback/payload contract is:

- `input.action`: `ActionName`, `held:ActionName`, `pressed:ActionName`,
  `released:ActionName`, or `axis:AxisName`.
- `entity.action`: `set_active`, `toggle_active`, `translate_x/y/z`, or
  `set_position_x/y/z`; an entity GUID is required.
- `component.action`: `set_enabled`, `toggle_enabled`, `set:property`, or `add:property`;
  a component type GUID is required and an optional entity GUID overrides the event owner.
- `audio.play`: `sfx`, `music`, or `ui`, optionally followed by `.loop`; an audio asset GUID and
  a 0..1 volume argument are required.
- `animation.play`: the payload is an Animator state name, the argument updates playback speed,
  and the asset reference remains part of the validated graph dependency contract.
- `ui.action`: `set_value`, `set_checked`, `select_index`, or `set_scale` on the owner UI element.
- `scene.load`: submits a typed asset GUID to either the application handler or a bounded request
  queue. The host never destroys/replaces the active `Scene` from inside a VM callback.
- `function.call`: the safe built-ins are `function.identity`, `math.abs`, `math.negate`,
  `math.clamp01`, and `math.sign`.

`ProjectVisualHost::validationCallbacks()` exposes the same signatures for Qt authoring/debug
preview. Project preparation also validates exactly this callback-name set, so a graph cannot pass
the build and then fail merely because a required desktop callback was never registered.

This desktop execution support does **not** imply ESP32 visual-bytecode execution. The current
ESP32 capability gate rejects projects that declare `.fglvisual`/`visual.script` assets because
the firmware has no visual bytecode VM or `ProjectVisualHost` adapter. ESP32 gameplay execution
remains the explicit bounded C++ companion module described above. PC visual-graph execution and
an ESP32 native gameplay-module build are separate evidence; neither may be reported as visual
bytecode execution on hardware.

## Errors and allocation rules

Fallible public APIs return `Result<T>` or `Result<void>`. Check the result before calling
`value()`; read `Error::code`, message, and context when it fails. Exceptions are reserved for
programmer misuse inside `Result`, not normal data errors.

On ESP32-sensitive paths, allocate resources during loading, use bounded pools for particles and
audio, use GUID/handle values across subsystem boundaries, and avoid per-frame strings, file I/O,
and unbounded containers. See `PERFORMANCE_BUDGETS.md`.

## Serialization and native-module boundary

Scene format version 2 serializes identity, hierarchy, active state, Transform, and every property
described by registered metadata. Version 1 still migrates through the strict reader. A generated
native script is represented durably by `fabgl.ScriptComponent` with its script asset GUID and
class name; the trusted module registry resolves that class at PC load time. Missing, duplicate,
ABI-incompatible, or factory-invalid registrations fail before scene lifecycle starts. Arbitrary
native C++ remains trusted code with the user's permissions and is never inferred from a project
path or loaded by an asset file.
