#include "test_harness.h"

#include <fabgl/audio/audio_mixer.h>
#include <fabgl/input/input_map.h>
#include <fabgl/project/project_asset_library.h>
#include <fabgl/project/project_visual_host.h>
#include <fabgl/reflection/reflection.h>
#include <fabgl/runtime/scene_runtime.h>
#include <fabgl/scene/builtin_components.h>
#include <fabgl/scene/entity.h>
#include <fabgl/scene/scene.h>
#include <fabgl/visual/visual_graph.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace fabgl;

namespace {

ReflectionRegistry builtinRegistry() {
    ReflectionRegistry registry;
    FGL_CHECK(registerBuiltinComponentTypes(registry));
    return registry;
}

DataComponent* attachBuiltin(Entity& entity, const ReflectionRegistry& registry,
                             const std::string_view name) {
    auto created = createBuiltinDataComponent(registry, name);
    FGL_CHECK(created);
    auto component = std::move(created).value();
    auto* raw = component.get();
    FGL_CHECK(entity.addComponent(std::move(component)));
    return raw;
}

VisualNode typedNode(const VisualBuiltinNodeType type, const VisualNodeId id,
                     const char* name) {
    auto created = VisualNodeRegistry::builtins().create(type, id, name);
    FGL_CHECK(created);
    auto node = std::move(created).value();
    if (const auto* definition = VisualNodeRegistry::builtins().find(type))
        node.callbackName = definition->hostCallback;
    return node;
}

VisualGraph delayedComponentGraph(const AssetGuid graphAsset,
                                  const ComponentTypeGuid healthType) {
    VisualGraph graph;
    graph.setGuid(graphAsset);
    graph.setName("Production desktop visual host fixture");
    auto start = typedNode(VisualBuiltinNodeType::EventStart, 1U, "Start");
    auto delaySeconds = typedNode(VisualBuiltinNodeType::NumberConstant, 2U, "Delay seconds");
    delaySeconds.numberValue = 0.01;
    auto delay = typedNode(VisualBuiltinNodeType::FlowDelay, 3U, "Delay");
    auto healthValue = typedNode(VisualBuiltinNodeType::NumberConstant, 4U, "Health value");
    healthValue.numberValue = 77.0;
    auto component = typedNode(VisualBuiltinNodeType::ComponentAction, 5U, "Set health");
    component.callbackPayload = "set:current";
    component.componentReference = healthType;
    auto returnValue = typedNode(VisualBuiltinNodeType::NumberConstant, 6U, "Return value");
    returnValue.numberValue = 1.0;
    auto returned = typedNode(VisualBuiltinNodeType::FlowReturn, 7U, "Return");
    std::vector<VisualNode> nodes;
    nodes.push_back(std::move(start));
    nodes.push_back(std::move(delaySeconds));
    nodes.push_back(std::move(delay));
    nodes.push_back(std::move(healthValue));
    nodes.push_back(std::move(component));
    nodes.push_back(std::move(returnValue));
    nodes.push_back(std::move(returned));
    for (auto& node : nodes)
        FGL_CHECK(graph.addNode(std::move(node)));
    FGL_CHECK(graph.addEdge({1U, 1U, 3U, 1U}));
    FGL_CHECK(graph.addEdge({2U, 1U, 3U, 2U}));
    FGL_CHECK(graph.addEdge({3U, 3U, 5U, 1U}));
    FGL_CHECK(graph.addEdge({4U, 1U, 5U, 2U}));
    FGL_CHECK(graph.addEdge({5U, 3U, 7U, 1U}));
    FGL_CHECK(graph.addEdge({6U, 1U, 7U, 2U}));
    graph.setEntryNode(1U);
    return graph;
}

project::ProjectVisualHostServices servicesFor(
    Scene& scene, InputMap& input, AudioMixer& audio, const AssetGuid clipAsset,
    const std::shared_ptr<const project::ProjectAudioClip>& clip) {
    project::ProjectVisualHostServices services;
    services.scene = &scene;
    services.input = &input;
    services.audio = &audio;
    services.audioClipResolver = [clipAsset, clip](const AssetGuid requested) {
        return requested == clipAsset ? clip : std::shared_ptr<const project::ProjectAudioClip>{};
    };
    return services;
}

} // namespace

FGL_TEST(project_visual_host_dispatches_input_scene_reflection_audio_and_bounded_diagnostics) {
    auto registry = builtinRegistry();
    Scene scene{"Visual host services"};
    const auto ownerId = EntityGuid::fromStableName("tests.visual-host.owner");
    const auto targetId = EntityGuid::fromStableName("tests.visual-host.target");
    auto owner = scene.createEntity("Owner", ownerId);
    auto target = scene.createEntity("Target", targetId);
    FGL_CHECK(owner && target);
    auto* health = attachBuiltin(*owner.value(), registry, "Health");
    const auto healthType = health->typeId();

    InputMap input;
    FGL_CHECK(input.defineContext("Gameplay", 100));
    FGL_CHECK(input.bindAction("Gameplay", "Jump", {"Key.Space", 1.0F, 0.5F}));
    FGL_CHECK(input.bindAxis("Gameplay", "MoveX", {"Key.D", 1.0F, 0.0F}));
    FGL_CHECK(input.setControlValue("Key.Space", 1.0F));
    FGL_CHECK(input.setControlValue("Key.D", 0.75F));
    input.update();

    AudioMixerConfig audioConfig;
    audioConfig.maximumVoices = 2U;
    audioConfig.maximumBuses = 4U;
    AudioMixer audio{audioConfig};
    FGL_CHECK(audio.createBus({1U}));
    FGL_CHECK(audio.createBus({2U}));
    FGL_CHECK(audio.createBus({3U}));
    const auto clipAsset = AssetGuid::fromStableName("tests.visual-host.audio");
    auto mutableClip = std::make_shared<project::ProjectAudioClip>();
    mutableClip->sampleRate = 48'000U;
    mutableClip->samples.assign(64U, 0.25F);
    std::shared_ptr<const project::ProjectAudioClip> clip = mutableClip;

    project::ProjectVisualHostLimits limits;
    limits.maximumDiagnostics = 4U;
    limits.maximumRetainedAudioClips = 4U;
    auto created = project::ProjectVisualHost::create(
        servicesFor(scene, input, audio, clipAsset, clip), limits);
    FGL_CHECK(created);
    std::optional<project::ProjectVisualHost> host;
    host.emplace(std::move(created).value());

    VisualHostCallDescriptor inputCall;
    inputCall.name = "input.action";
    inputCall.payload = "pressed:Jump";
    FGL_CHECK_NEAR(host->callbacks().dispatch(inputCall, {}).value(), 1.0, 0.0001F);
    inputCall.payload = "axis:MoveX";
    FGL_CHECK_NEAR(host->callbacks().dispatch(inputCall, {}).value(), 0.75, 0.0001F);

    VisualHostCallDescriptor entityCall;
    entityCall.name = "entity.action";
    entityCall.payload = "set_active";
    entityCall.argumentCount = 1U;
    entityCall.entityReference = targetId;
    FGL_CHECK(host->callbacks().dispatch(entityCall, {0.0}));
    FGL_CHECK(!target.value()->active());

    VisualHostCallDescriptor componentCall;
    componentCall.name = "component.action";
    componentCall.payload = "set:current";
    componentCall.argumentCount = 1U;
    componentCall.componentReference = healthType;
    componentCall.execution.ownerEntity = ownerId;
    FGL_CHECK(host->callbacks().dispatch(componentCall, {41.0}));
    FGL_CHECK(std::get<std::int64_t>(health->get("current").value()) == 41);

    VisualHostCallDescriptor audioCall;
    audioCall.name = "audio.play";
    audioCall.payload = "sfx";
    audioCall.argumentCount = 1U;
    audioCall.assetReference = clipAsset;
    FGL_CHECK(host->callbacks().dispatch(audioCall, {0.5}));
    FGL_CHECK(audio.stats().activeVoices == 1U);
    FGL_CHECK(host->retainedAudioClipCount() == 1U);

    const auto sceneAsset = AssetGuid::fromStableName("tests.visual-host.scene");
    VisualHostCallDescriptor sceneCall;
    sceneCall.name = "scene.load";
    sceneCall.assetReference = sceneAsset;
    FGL_CHECK(host->callbacks().dispatch(sceneCall, {}));
    const auto requests = host->takeSceneLoadRequests();
    FGL_CHECK(requests.size() == 1U && requests.front() == sceneAsset);
    FGL_CHECK(host->diagnostics().size() == limits.maximumDiagnostics);
    FGL_CHECK(host->stats().calls == 6U);
    FGL_CHECK(host->stats().audioVoicesStarted == 1U);

    auto copiedCallbacks = host->callbacks();
    host.reset();
    FGL_CHECK(audio.stats().activeVoices == 0U);
    auto expired = copiedCallbacks.dispatch(inputCall, {});
    FGL_CHECK(!expired && expired.error().code() == ErrorCode::InvalidState);
}

FGL_TEST(project_visual_host_runs_typed_component_action_after_nonblocking_delay_in_scene_runtime) {
    auto registry = builtinRegistry();
    Scene scene{"Visual host runtime integration"};
    const auto ownerId = EntityGuid::fromStableName("tests.visual-host.runtime-owner");
    auto owner = scene.createEntity("Scripted", ownerId);
    FGL_CHECK(owner);
    auto* health = attachBuiltin(*owner.value(), registry, "Health");
    auto* visual = attachBuiltin(*owner.value(), registry, "VisualScriptComponent");
    auto* animatorData = attachBuiltin(*owner.value(), registry, "Animator");
    auto* uiData = attachBuiltin(*owner.value(), registry, "UITransform");
    const auto graphAsset = AssetGuid::fromStableName("tests.visual-host.runtime-graph");
    const auto controllerAsset =
        AssetGuid::fromStableName("tests.visual-host.runtime-controller");
    FGL_CHECK(visual->set("graph", PropertyValue(graphAsset)));
    FGL_CHECK(health->set("current", PropertyValue(std::int64_t{1})));
    FGL_CHECK(animatorData->set("controller", PropertyValue(controllerAsset)));
    FGL_CHECK(animatorData->set("initialState", PropertyValue(std::string("Idle"))));
    FGL_CHECK(uiData->set("widgetType", PropertyValue(std::int64_t{5})));
    auto graphSource = serializeVisualGraph(delayedComponentGraph(graphAsset, health->typeId()));
    FGL_CHECK(graphSource);

    InputMap input;
    AudioMixer audio;
    const auto unusedClipAsset = AssetGuid::fromStableName("tests.visual-host.unused-audio");
    auto mutableClip = std::make_shared<project::ProjectAudioClip>();
    mutableClip->sampleRate = 48'000U;
    mutableClip->samples.assign(1U, 0.0F);
    std::shared_ptr<const project::ProjectAudioClip> clip = mutableClip;
    auto hostResult = project::ProjectVisualHost::create(
        servicesFor(scene, input, audio, unusedClipAsset, clip));
    FGL_CHECK(hostResult);
    auto host = std::move(hostResult).value();

    SceneRuntimeConfig config{{0.0F, 0.0F}};
    config.visualGraphSourceResolver =
        [graphAsset, source = graphSource.value()](const AssetGuid requested) {
            return requested == graphAsset
                       ? Result<std::string>::success(source)
                       : Result<std::string>::failure(
                             Error(ErrorCode::NotFound, "visual host fixture graph was not found"));
        };
    config.animatorFactory = [controllerAsset](const AssetGuid requested) {
        if (requested != controllerAsset) {
            return Result<std::unique_ptr<AnimatorController>>::failure(
                Error(ErrorCode::NotFound, "visual host fixture controller was not found"));
        }
        auto controller = std::make_unique<AnimatorController>();
        auto idle = std::make_shared<AnimationClip>("Idle", 1.0F, true);
        auto run = std::make_shared<AnimationClip>("Run", 1.0F, true);
        auto added = controller->addState("Idle", idle);
        if (!added)
            return Result<std::unique_ptr<AnimatorController>>::failure(added.error());
        added = controller->addState("Run", run);
        if (!added)
            return Result<std::unique_ptr<AnimatorController>>::failure(added.error());
        return Result<std::unique_ptr<AnimatorController>>::success(std::move(controller));
    };
    config.visualHostCallbacks = host.callbacks();
    FGL_CHECK(scene.start());
    SceneRuntime runtime{scene, std::move(config)};
    FGL_CHECK(host.bindRuntime(runtime));
    FGL_CHECK(runtime.initialize());
    FGL_CHECK(std::get<std::int64_t>(health->get("current").value()) == 1);

    VisualHostCallDescriptor animationCall;
    animationCall.name = "animation.play";
    animationCall.payload = "Run";
    animationCall.argumentCount = 1U;
    animationCall.assetReference =
        AssetGuid::fromStableName("tests.visual-host.runtime-animation");
    animationCall.execution.ownerEntity = ownerId;
    FGL_CHECK(host.callbacks().dispatch(animationCall, {1.5}));
    FGL_CHECK(runtime.animatorFor(ownerId) != nullptr);
    FGL_CHECK(runtime.animatorFor(ownerId)->currentState() == "Run");
    FGL_CHECK_NEAR(std::get<double>(animatorData->get("speed").value()), 1.5, 0.0001F);

    VisualHostCallDescriptor uiCall;
    uiCall.name = "ui.action";
    uiCall.payload = "set_value";
    uiCall.argumentCount = 1U;
    uiCall.execution.ownerEntity = ownerId;
    FGL_CHECK(host.callbacks().dispatch(uiCall, {0.75}));
    const auto uiElement = runtime.uiElementFor(ownerId);
    FGL_CHECK(uiElement.has_value());
    FGL_CHECK_NEAR(runtime.ui().widget(*uiElement)->value, 0.75, 0.0001F);

    FGL_CHECK(runtime.update(0.005F));
    FGL_CHECK(std::get<std::int64_t>(health->get("current").value()) == 1);
    FGL_CHECK(runtime.update(0.006F));
    FGL_CHECK(std::get<std::int64_t>(health->get("current").value()) == 77);
    FGL_CHECK(host.stats().calls == 3U);
    FGL_CHECK(host.stats().failures == 0U);
}
