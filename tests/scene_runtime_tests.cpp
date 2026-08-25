#include "test_harness.h"

#include "fabgl/reflection/reflection.h"
#include "fabgl/runtime/scene_runtime.h"
#include "fabgl/scene/builtin_components.h"
#include "fabgl/scene/entity.h"
#include "fabgl/scene/scene.h"
#include "fabgl/serialization/scene_serializer.h"
#include "fabgl/visual/visual_graph.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using namespace fabgl;

namespace {

class ContactProbe final : public Component {
  public:
    static ComponentTypeGuid staticTypeId() {
        return ComponentTypeGuid::fromStableName("test.scene-runtime-contact-probe.v1");
    }
    ComponentTypeGuid typeId() const noexcept override {
        return staticTypeId();
    }
    std::string_view typeName() const noexcept override {
        return "ContactProbe";
    }

    int collisionEnter = 0;
    int collisionStay = 0;
    int collisionExit = 0;
    int triggerEnter = 0;
    int triggerExit = 0;

  protected:
    void onCollisionEnter(EntityGuid) override {
        ++collisionEnter;
    }
    void onCollisionStay(EntityGuid) override {
        ++collisionStay;
    }
    void onCollisionExit(EntityGuid) override {
        ++collisionExit;
    }
    void onTriggerEnter(EntityGuid) override {
        ++triggerEnter;
    }
    void onTriggerExit(EntityGuid) override {
        ++triggerExit;
    }
};

DataComponent* attachBuiltin(Entity& entity, const ReflectionRegistry& registry,
                             std::string_view name) {
    auto created = createBuiltinDataComponent(registry, name);
    FGL_CHECK(created);
    auto component = std::move(created).value();
    auto* raw = component.get();
    auto attached = entity.addComponent(std::move(component));
    FGL_CHECK(attached);
    return raw;
}

DataComponent* findBuiltin(Entity& entity, std::string_view name) {
    const auto id = ComponentTypeGuid::fromStableName(std::string("fabgl.component.") +
                                                      std::string(name) + ".v1");
    return dynamic_cast<DataComponent*>(entity.getComponent(id));
}

ReflectionRegistry builtinRegistry() {
    ReflectionRegistry registry;
    FGL_CHECK(registerBuiltinComponentTypes(registry));
    return registry;
}

VisualNode runtimeVisualNode(const VisualBuiltinNodeType type, const VisualNodeId id,
                             std::string name) {
    auto created = VisualNodeRegistry::builtins().create(type, id, std::move(name));
    FGL_CHECK(created);
    return std::move(created).value();
}

void addRuntimeVisualNode(VisualGraph& graph, VisualNode node) {
    FGL_CHECK(graph.addNode(std::move(node)));
}

void addRuntimeVisualEdge(VisualGraph& graph, const VisualNodeId sourceNode,
                          const VisualPinId sourcePin, const VisualNodeId targetNode,
                          const VisualPinId targetPin) {
    FGL_CHECK(graph.addEdge({sourceNode, sourcePin, targetNode, targetPin}));
}

VisualGraph lifecycleVisualGraph(const AssetGuid asset) {
    VisualGraph graph;
    graph.setGuid(asset);
    graph.setName("Serialized scene lifecycle graph");
    const std::vector<std::pair<VisualBuiltinNodeType, double>> events{
        {VisualBuiltinNodeType::EventStart, 11.0},
        {VisualBuiltinNodeType::EventUpdate, 22.0},
        {VisualBuiltinNodeType::EventFixedUpdate, 33.0},
        {VisualBuiltinNodeType::EventLateUpdate, 44.0},
        {VisualBuiltinNodeType::EventTriggerEnter, 55.0},
        {VisualBuiltinNodeType::EventTriggerExit, 66.0},
    };
    auto base = VisualNodeId{1U};
    for (const auto& event : events) {
        addRuntimeVisualNode(graph, runtimeVisualNode(event.first, base, "Event"));
        auto call = runtimeVisualNode(VisualBuiltinNodeType::FunctionCall,
                                      static_cast<VisualNodeId>(base + 1U), "Set health");
        call.callbackName = "tests.health.set";
        addRuntimeVisualNode(graph, std::move(call));
        auto value = runtimeVisualNode(VisualBuiltinNodeType::NumberConstant,
                                       static_cast<VisualNodeId>(base + 2U), "Health value");
        value.numberValue = event.second;
        addRuntimeVisualNode(graph, std::move(value));
        addRuntimeVisualNode(graph,
                             runtimeVisualNode(VisualBuiltinNodeType::FlowReturn,
                                               static_cast<VisualNodeId>(base + 3U), "Return"));
        addRuntimeVisualEdge(graph, base, 1U, static_cast<VisualNodeId>(base + 1U), 1U);
        addRuntimeVisualEdge(graph, static_cast<VisualNodeId>(base + 2U), 1U,
                             static_cast<VisualNodeId>(base + 1U), 2U);
        addRuntimeVisualEdge(graph, static_cast<VisualNodeId>(base + 1U), 3U,
                             static_cast<VisualNodeId>(base + 3U), 1U);
        addRuntimeVisualEdge(graph, static_cast<VisualNodeId>(base + 2U), 1U,
                             static_cast<VisualNodeId>(base + 3U), 2U);
        base = static_cast<VisualNodeId>(base + 4U);
    }
    graph.setEntryNode(1U);
    return graph;
}

VisualGraph boundedStartVisualGraph(const AssetGuid asset) {
    VisualGraph graph;
    graph.setGuid(asset);
    graph.setName("Bounded start graph");
    addRuntimeVisualNode(graph, runtimeVisualNode(VisualBuiltinNodeType::EventStart, 1U, "Start"));
    auto value = runtimeVisualNode(VisualBuiltinNodeType::NumberConstant, 2U, "Value");
    value.numberValue = 1.0;
    addRuntimeVisualNode(graph, std::move(value));
    addRuntimeVisualNode(graph, runtimeVisualNode(VisualBuiltinNodeType::FlowReturn, 3U, "Return"));
    addRuntimeVisualEdge(graph, 1U, 1U, 3U, 1U);
    addRuntimeVisualEdge(graph, 2U, 1U, 3U, 2U);
    graph.setEntryNode(1U);
    return graph;
}

VisualGraph delayedStartVisualGraph(const AssetGuid asset) {
    VisualGraph graph;
    graph.setGuid(asset);
    graph.setName("Delayed serialized start graph");
    addRuntimeVisualNode(graph, runtimeVisualNode(VisualBuiltinNodeType::EventStart, 1U, "Start"));
    addRuntimeVisualNode(graph, runtimeVisualNode(VisualBuiltinNodeType::FlowDelay, 2U, "Delay"));
    auto seconds = runtimeVisualNode(VisualBuiltinNodeType::NumberConstant, 3U, "Seconds");
    seconds.numberValue = 0.5;
    addRuntimeVisualNode(graph, std::move(seconds));
    auto call = runtimeVisualNode(VisualBuiltinNodeType::FunctionCall, 4U, "Set health");
    call.callbackName = "tests.health.set";
    addRuntimeVisualNode(graph, std::move(call));
    auto health = runtimeVisualNode(VisualBuiltinNodeType::NumberConstant, 5U, "Health");
    health.numberValue = 99.0;
    addRuntimeVisualNode(graph, std::move(health));
    addRuntimeVisualNode(graph,
                         runtimeVisualNode(VisualBuiltinNodeType::FlowReturn, 6U, "Return"));
    graph.setEntryNode(1U);
    addRuntimeVisualEdge(graph, 1U, 1U, 2U, 1U);
    addRuntimeVisualEdge(graph, 3U, 1U, 2U, 2U);
    addRuntimeVisualEdge(graph, 2U, 3U, 4U, 1U);
    addRuntimeVisualEdge(graph, 5U, 1U, 4U, 2U);
    addRuntimeVisualEdge(graph, 4U, 3U, 6U, 1U);
    addRuntimeVisualEdge(graph, 5U, 1U, 6U, 2U);
    return graph;
}

bool errorHasContext(const Error& error, const std::string_view key,
                     const std::optional<std::string_view> value = std::nullopt) {
    for (const auto& context : error.context()) {
        if (context.key == key && (!value || context.value == *value))
            return true;
    }
    return false;
}

} // namespace

FGL_TEST(scene_runtime_maps_reflected_rigidbody_to_physics_and_back_to_transform) {
    auto registry = builtinRegistry();
    Scene scene{"Physics scene"};
    auto playerResult = scene.createEntity("Player");
    FGL_CHECK(playerResult);
    auto* player = playerResult.value();
    auto* collider = attachBuiltin(*player, registry, "Collider2D");
    auto* rigidbody = attachBuiltin(*player, registry, "Rigidbody2D");
    FGL_CHECK(rigidbody->set("mass", PropertyValue(2.0)));
    FGL_CHECK(rigidbody->set("gravityScale", PropertyValue(0.5)));

    SceneRuntime runtime{scene, {{0.0F, 10.0F}}};
    FGL_CHECK(runtime.initialize());
    FGL_CHECK(runtime.physicsBodyCount() == 1);
    FGL_CHECK(runtime.fixedUpdate(0.5F));
    FGL_CHECK_NEAR(player->transform().localPosition().y, 1.25F, 0.0001F);
    auto velocity = rigidbody->get("velocity");
    FGL_CHECK(velocity);
    FGL_CHECK_NEAR(std::get<Vec2>(velocity.value()).y, 2.5F, 0.0001F);

    FGL_CHECK(collider->set("enabled", PropertyValue(false)));
    FGL_CHECK(runtime.fixedUpdate(0.0F));
    FGL_CHECK(runtime.physicsBodyCount() == 0);
    runtime.shutdown();
    FGL_CHECK(!runtime.initialized());
}

FGL_TEST(scene_runtime_dispatches_collision_lifecycle_and_trigger_events) {
    auto registry = builtinRegistry();
    Scene scene{"Contacts"};
    auto floorResult = scene.createEntity("Floor");
    auto actorResult = scene.createEntity("Actor");
    FGL_CHECK(floorResult && actorResult);
    auto* floor = floorResult.value();
    auto* actor = actorResult.value();
    floor->transform().setLocalPosition({0.0F, 2.0F, 0.0F});
    actor->transform().setLocalPosition({0.0F, 1.25F, 0.0F});
    auto* floorCollider = attachBuiltin(*floor, registry, "Collider2D");
    FGL_CHECK(floorCollider->set("size", PropertyValue(Vec2{4.0F, 1.0F})));
    attachBuiltin(*actor, registry, "Collider2D");
    auto* actorBody = attachBuiltin(*actor, registry, "Rigidbody2D");
    FGL_CHECK(actorBody->set("gravityScale", PropertyValue(0.0)));
    auto probeResult = actor->addComponent<ContactProbe>();
    FGL_CHECK(probeResult);
    auto* probe = probeResult.value();
    FGL_CHECK(scene.start());

    SceneRuntime runtime{scene, {{0.0F, 0.0F}}};
    FGL_CHECK(runtime.initialize());
    FGL_CHECK(runtime.fixedUpdate(0.0F));
    FGL_CHECK(probe->collisionEnter == 1);
    FGL_CHECK(runtime.activeContactCount() == 1);
    FGL_CHECK(runtime.fixedUpdate(0.0F));
    FGL_CHECK(probe->collisionStay == 1);

    const auto actorPhysicsBody = runtime.bodyFor(actor->id());
    FGL_CHECK(actorPhysicsBody);
    FGL_CHECK(runtime.physics().setPosition(*actorPhysicsBody, {0.0F, -4.0F}));
    FGL_CHECK(runtime.fixedUpdate(0.0F));
    FGL_CHECK(probe->collisionExit == 1);

    FGL_CHECK(floorCollider->set("trigger", PropertyValue(true)));
    // Recreate the body so collider runtime-editable settings are picked up deterministically.
    FGL_CHECK(floorCollider->set("enabled", PropertyValue(false)));
    FGL_CHECK(runtime.fixedUpdate(0.0F));
    FGL_CHECK(floorCollider->set("enabled", PropertyValue(true)));
    FGL_CHECK(runtime.fixedUpdate(0.0F));
    const auto actorBodyId = runtime.bodyFor(actor->id());
    FGL_CHECK(actorBodyId);
    FGL_CHECK(runtime.physics().setPosition(*actorBodyId, {0.0F, 1.25F}));
    FGL_CHECK(runtime.fixedUpdate(0.0F));
    FGL_CHECK(probe->triggerEnter == 1);
    FGL_CHECK(runtime.fixedUpdate(0.0F));
    FGL_CHECK(probe->triggerEnter == 1);
    FGL_CHECK(runtime.physics().setPosition(*actorBodyId, {0.0F, -4.0F}));
    FGL_CHECK(runtime.fixedUpdate(0.0F));
    FGL_CHECK(probe->triggerExit == 1);
}

FGL_TEST(scene_runtime_rejects_invalid_gravity_and_collider_configuration) {
    auto registry = builtinRegistry();
    Scene scene{"Invalid physics"};
    auto entity = scene.createEntity("Bad collider");
    FGL_CHECK(entity);
    auto* collider = attachBuiltin(*entity.value(), registry, "Collider2D");
    FGL_CHECK(collider->set("shape", PropertyValue(std::int64_t{1})));
    FGL_CHECK(collider->set("radius", PropertyValue(-1.0)));

    SceneRuntime invalidCollider{scene};
    FGL_CHECK(!invalidCollider.initialize());
    FGL_CHECK(!invalidCollider.initialized());

    SceneRuntime invalidGravity{scene, {{0.0F, std::numeric_limits<float>::infinity()}}};
    FGL_CHECK(!invalidGravity.initialize());
}

FGL_TEST(scene_runtime_executes_serialized_visual_graph_lifecycle_and_trigger_events) {
    auto registry = builtinRegistry();
    const auto graphAsset = AssetGuid::fromStableName("tests.runtime.visual.lifecycle");
    auto graphText = serializeVisualGraph(lifecycleVisualGraph(graphAsset));
    FGL_CHECK(graphText);

    Scene authored{"Visual runtime integration"};
    const auto scriptedId = EntityGuid::fromStableName("tests.runtime.visual.scripted");
    const auto triggerId = EntityGuid::fromStableName("tests.runtime.visual.trigger");
    auto scriptedResult = authored.createEntity("Scripted", scriptedId);
    auto triggerResult = authored.createEntity("Trigger", triggerId);
    FGL_CHECK(scriptedResult && triggerResult);
    auto* visual = attachBuiltin(*scriptedResult.value(), registry, "VisualScriptComponent");
    auto* health = attachBuiltin(*scriptedResult.value(), registry, "Health");
    FGL_CHECK(visual->set("graph", PropertyValue(graphAsset)));
    FGL_CHECK(health->set("current", PropertyValue(std::int64_t{1})));
    auto* scriptedCollider = attachBuiltin(*scriptedResult.value(), registry, "Collider2D");
    auto* triggerCollider = attachBuiltin(*triggerResult.value(), registry, "Collider2D");
    FGL_CHECK(scriptedCollider->set("size", PropertyValue(Vec2{2.0F, 2.0F})));
    FGL_CHECK(triggerCollider->set("size", PropertyValue(Vec2{2.0F, 2.0F})));
    FGL_CHECK(triggerCollider->set("trigger", PropertyValue(true)));

    auto sceneText = SceneSerializer::serialize(authored);
    FGL_CHECK(sceneText);
    auto loaded = SceneSerializer::deserialize(sceneText.value());
    FGL_CHECK(loaded);
    auto* scripted = loaded.value()->findEntity(scriptedId);
    FGL_CHECK(scripted != nullptr);
    auto* loadedHealth = findBuiltin(*scripted, "Health");
    FGL_CHECK(loadedHealth != nullptr);

    std::vector<VisualRuntimeEvent> callbackEvents;
    std::vector<std::optional<EntityGuid>> callbackOtherEntities;
    SceneRuntimeConfig config{{0.0F, 0.0F}};
    config.visualGraphSourceResolver = [graphAsset,
                                        source = graphText.value()](const AssetGuid requested) {
        if (requested != graphAsset) {
            return Result<std::string>::failure(
                Error(ErrorCode::NotFound, "test visual graph asset was not found"));
        }
        return Result<std::string>::success(source);
    };
    FGL_CHECK(config.visualHostCallbacks.add(
        "tests.health.set", std::uint8_t{1},
        [&loaded, &callbackEvents,
         &callbackOtherEntities](const VisualHostCallDescriptor& descriptor,
                                 const std::vector<double>& arguments) -> Result<double> {
            if (!descriptor.execution.ownerEntity || arguments.size() != 1U ||
                !std::isfinite(arguments[0]) || std::floor(arguments[0]) != arguments[0]) {
                return Result<double>::failure(
                    Error(ErrorCode::InvalidArgument, "test health callback context is invalid"));
            }
            auto* owner = loaded.value()->findEntity(*descriptor.execution.ownerEntity);
            auto* target = owner == nullptr ? nullptr : findBuiltin(*owner, "Health");
            if (target == nullptr) {
                return Result<double>::failure(
                    Error(ErrorCode::NotFound, "test health component was not found"));
            }
            auto set =
                target->set("current", PropertyValue(static_cast<std::int64_t>(arguments[0])));
            if (!set)
                return Result<double>::failure(set.error());
            callbackEvents.push_back(descriptor.execution.event);
            callbackOtherEntities.push_back(descriptor.execution.otherEntity);
            return Result<double>::success(arguments[0]);
        }));

    FGL_CHECK(loaded.value()->start());
    SceneRuntime runtime{*loaded.value(), std::move(config)};
    FGL_CHECK(runtime.initialize());
    FGL_CHECK(runtime.visualScriptCount() == 1U);
    FGL_CHECK(std::get<std::int64_t>(loadedHealth->get("current").value()) == 11);

    FGL_CHECK(runtime.update(0.25F));
    FGL_CHECK(std::get<std::int64_t>(loadedHealth->get("current").value()) == 22);
    FGL_CHECK_NEAR(runtime.visualVariable(scriptedId, "runtime.delta_seconds").value(), 0.25,
                   0.0001F);
    FGL_CHECK(runtime.fixedUpdate(0.5F));
    FGL_CHECK(std::get<std::int64_t>(loadedHealth->get("current").value()) == 55);
    FGL_CHECK(callbackEvents.size() == 4U);
    FGL_CHECK(callbackEvents[2U] == VisualRuntimeEvent::FixedUpdate);
    FGL_CHECK(callbackEvents[3U] == VisualRuntimeEvent::TriggerEnter);
    FGL_CHECK(callbackOtherEntities[3U] == triggerId);
    FGL_CHECK(runtime.lateUpdate(0.75F));
    FGL_CHECK(std::get<std::int64_t>(loadedHealth->get("current").value()) == 44);

    scripted->transform().setLocalPosition({20.0F, 20.0F, 0.0F});
    FGL_CHECK(runtime.fixedUpdate(0.0F));
    FGL_CHECK(std::get<std::int64_t>(loadedHealth->get("current").value()) == 66);
    FGL_CHECK(callbackEvents.back() == VisualRuntimeEvent::TriggerExit);
    FGL_CHECK(callbackOtherEntities.back() == triggerId);
}

FGL_TEST(scene_runtime_resumes_visual_delay_on_later_updates_without_synchronous_sleep) {
    auto registry = builtinRegistry();
    const auto graphAsset = AssetGuid::fromStableName("tests.runtime.visual.delay");
    auto graphText = serializeVisualGraph(delayedStartVisualGraph(graphAsset));
    FGL_CHECK(graphText);

    Scene scene{"Visual latent delay"};
    auto entity = scene.createEntity("Delayed actor");
    FGL_CHECK(entity);
    auto* visual = attachBuiltin(*entity.value(), registry, "VisualScriptComponent");
    auto* health = attachBuiltin(*entity.value(), registry, "Health");
    FGL_CHECK(visual->set("graph", PropertyValue(graphAsset)));
    FGL_CHECK(health->set("current", PropertyValue(std::int64_t{1})));

    std::size_t synchronousDelayCalls = 0U;
    std::size_t healthCalls = 0U;
    SceneRuntimeConfig config{{0.0F, 0.0F}};
    config.visualGraphSourceResolver =
        [graphAsset, source = graphText.value()](const AssetGuid requested) {
            if (requested != graphAsset) {
                return Result<std::string>::failure(
                    Error(ErrorCode::NotFound, "delayed visual graph was not found"));
            }
            return Result<std::string>::success(source);
        };
    FGL_CHECK(config.visualHostCallbacks.add(
        "time.delay", std::uint8_t{1},
        [&synchronousDelayCalls](const VisualHostCallDescriptor&,
                                 const std::vector<double>&) {
            ++synchronousDelayCalls;
            return Result<double>::success(0.0);
        }));
    FGL_CHECK(config.visualHostCallbacks.add(
        "tests.health.set", std::uint8_t{1},
        [&health, &healthCalls](const VisualHostCallDescriptor&,
                               const std::vector<double>& arguments) {
            ++healthCalls;
            auto set = health->set(
                "current", PropertyValue(static_cast<std::int64_t>(arguments.at(0U))));
            return set ? Result<double>::success(arguments.at(0U))
                       : Result<double>::failure(set.error());
        }));

    FGL_CHECK(scene.start());
    SceneRuntime runtime{scene, std::move(config)};
    FGL_CHECK(runtime.initialize());
    FGL_CHECK(std::get<std::int64_t>(health->get("current").value()) == 1);
    FGL_CHECK(synchronousDelayCalls == 0U && healthCalls == 0U);
    FGL_CHECK(runtime.update(0.25F));
    FGL_CHECK(runtime.update(0.24F));
    FGL_CHECK(std::get<std::int64_t>(health->get("current").value()) == 1);
    FGL_CHECK(runtime.update(0.02F));
    FGL_CHECK(std::get<std::int64_t>(health->get("current").value()) == 99);
    FGL_CHECK(synchronousDelayCalls == 0U && healthCalls == 1U);
    FGL_CHECK(runtime.update(1.0F));
    FGL_CHECK(healthCalls == 1U);
}

FGL_TEST(scene_runtime_visual_graph_diagnostics_are_contextual_and_bounded) {
    auto registry = builtinRegistry();
    const auto graphAsset = AssetGuid::fromStableName("tests.runtime.visual.bad");
    Scene scene{"Bad visual runtime"};
    auto entity = scene.createEntity("Scripted");
    FGL_CHECK(entity);
    auto* visual = attachBuiltin(*entity.value(), registry, "VisualScriptComponent");
    FGL_CHECK(visual->set("graph", PropertyValue(graphAsset)));

    SceneRuntimeConfig missingConfig;
    missingConfig.visualGraphSourceResolver = [](AssetGuid) {
        return Result<std::string>::failure(Error(ErrorCode::NotFound, "visual source is missing"));
    };
    SceneRuntime missing{scene, std::move(missingConfig)};
    auto missingResult = missing.initialize();
    FGL_CHECK(!missingResult);
    FGL_CHECK(missingResult.error().code() == ErrorCode::NotFound);
    FGL_CHECK(errorHasContext(missingResult.error(), "visual_stage", "resolve"));
    FGL_CHECK(errorHasContext(missingResult.error(), "visual_graph_asset", graphAsset.toString()));
    FGL_CHECK(errorHasContext(missingResult.error(), "entity", entity.value()->id().toString()));

    SceneRuntimeConfig corruptConfig;
    corruptConfig.visualGraphSourceResolver = [](AssetGuid) {
        return Result<std::string>::success(std::string("not an fglvisual file\n"));
    };
    SceneRuntime corrupt{scene, std::move(corruptConfig)};
    auto corruptResult = corrupt.initialize();
    FGL_CHECK(!corruptResult);
    FGL_CHECK(corruptResult.error().code() == ErrorCode::InvalidFormat);
    FGL_CHECK(errorHasContext(corruptResult.error(), "visual_stage", "deserialize"));

    auto boundedGraph = serializeVisualGraph(boundedStartVisualGraph(graphAsset));
    FGL_CHECK(boundedGraph);
    SceneRuntimeConfig executionLimit;
    executionLimit.maximumVisualInstructionsPerInvocation = 1U;
    executionLimit.maximumVisualInstructionsPerPhase = 1U;
    executionLimit.visualGraphSourceResolver = [source = boundedGraph.value()](AssetGuid) {
        return Result<std::string>::success(source);
    };
    SceneRuntime executionBounded{scene, std::move(executionLimit)};
    auto executionResult = executionBounded.initialize();
    FGL_CHECK(!executionResult);
    FGL_CHECK(executionResult.error().code() == ErrorCode::CapacityExceeded);
    FGL_CHECK(errorHasContext(executionResult.error(), "visual_stage", "execute"));
    FGL_CHECK(errorHasContext(executionResult.error(), "visual_event", "start"));

    SceneRuntimeConfig unsafeLimits;
    unsafeLimits.maximumVisualInstructionsPerPhase =
        SceneRuntimeMaximumVisualInstructionsPerPhase + 1U;
    SceneRuntime bounded{scene, std::move(unsafeLimits)};
    auto boundedResult = bounded.initialize();
    FGL_CHECK(!boundedResult);
    FGL_CHECK(boundedResult.error().code() == ErrorCode::InvalidArgument);
}

FGL_TEST(runtime_builtin_components_round_trip_all_scene_binding_properties_in_v2) {
    auto registry = builtinRegistry();
    Scene scene{"Runtime bindings"};
    auto actorResult = scene.createEntity("Actor");
    auto targetResult = scene.createEntity("Target");
    FGL_CHECK(actorResult && targetResult);
    auto* actor = actorResult.value();
    const auto targetId = targetResult.value()->id();
    const auto controllerAsset = AssetGuid::fromStableName("tests.runtime.controller");

    auto* animator = attachBuiltin(*actor, registry, "Animator");
    FGL_CHECK(animator->set("controller", PropertyValue(controllerAsset)));
    FGL_CHECK(animator->set("initialState", PropertyValue(std::string("walk"))));
    FGL_CHECK(animator->set("speed", PropertyValue(1.5)));
    FGL_CHECK(animator->set("playing", PropertyValue(false)));

    auto* particles = attachBuiltin(*actor, registry, "ParticleEmitter");
    FGL_CHECK(particles->set("rate", PropertyValue(6.0)));
    FGL_CHECK(particles->set("maxParticles", PropertyValue(std::uint64_t{16})));
    FGL_CHECK(particles->set("burstOnStart", PropertyValue(std::uint64_t{3})));
    FGL_CHECK(particles->set("lifetime", PropertyValue(2.5)));
    FGL_CHECK(particles->set("velocity", PropertyValue(Vec2{2.0F, -1.0F})));
    FGL_CHECK(particles->set("startColor", PropertyValue(Color{255, 0, 0, 255})));
    FGL_CHECK(particles->set("endColor", PropertyValue(Color{0, 0, 255, 0})));
    FGL_CHECK(particles->set("endSize", PropertyValue(4.0)));
    FGL_CHECK(particles->set("endRotation", PropertyValue(180.0)));
    FGL_CHECK(particles->set("cullOutsideBounds", PropertyValue(true)));
    FGL_CHECK(particles->set("cullingBounds", PropertyValue(Rect{-5.0F, -5.0F, 10.0F, 10.0F})));

    auto* ui = attachBuiltin(*actor, registry, "UITransform");
    FGL_CHECK(ui->set("widgetType", PropertyValue(std::int64_t{7})));
    FGL_CHECK(ui->set("anchorMaximum", PropertyValue(Vec2{1.0F, 1.0F})));
    FGL_CHECK(ui->set("offsetMaximum", PropertyValue(Vec2{})));
    FGL_CHECK(ui->set("items", PropertyValue(std::string("Easy;Normal;Hard"))));
    FGL_CHECK(ui->set("selectedIndex", PropertyValue(std::int64_t{1})));

    auto* navigation = attachBuiltin(*actor, registry, "NavigationAgent");
    FGL_CHECK(navigation->set("speed", PropertyValue(4.5)));
    FGL_CHECK(navigation->set("target", PropertyValue(targetId)));
    FGL_CHECK(navigation->set("arrivalRadius", PropertyValue(0.25)));
    FGL_CHECK(navigation->set("requireLineOfSight", PropertyValue(true)));
    FGL_CHECK(navigation->set("obstacleMask", PropertyValue(std::uint64_t{8})));

    auto serialized = SceneSerializer::serialize(scene);
    FGL_CHECK(serialized);
    FGL_CHECK(serialized.value().find("fglscene 2") == 0U);
    auto loaded = SceneSerializer::deserialize(serialized.value());
    FGL_CHECK(loaded);
    auto* loadedActor = loaded.value()->findEntity(actor->id());
    FGL_CHECK(loadedActor != nullptr);
    auto* loadedAnimator = findBuiltin(*loadedActor, "Animator");
    auto* loadedParticles = findBuiltin(*loadedActor, "ParticleEmitter");
    auto* loadedUi = findBuiltin(*loadedActor, "UITransform");
    auto* loadedNavigation = findBuiltin(*loadedActor, "NavigationAgent");
    FGL_CHECK(loadedAnimator && loadedParticles && loadedUi && loadedNavigation);
    FGL_CHECK(std::get<AssetGuid>(loadedAnimator->get("controller").value()) == controllerAsset);
    FGL_CHECK(std::get<std::string>(loadedAnimator->get("initialState").value()) == "walk");
    FGL_CHECK_NEAR(std::get<double>(loadedParticles->get("lifetime").value()), 2.5, 0.0001F);
    FGL_CHECK(std::get<Color>(loadedParticles->get("endColor").value()) == Color{0, 0, 255, 0});
    FGL_CHECK(std::get<std::string>(loadedUi->get("items").value()) == "Easy;Normal;Hard");
    FGL_CHECK(std::get<std::int64_t>(loadedUi->get("selectedIndex").value()) == 1);
    FGL_CHECK(std::get<EntityGuid>(loadedNavigation->get("target").value()) == targetId);
    FGL_CHECK(std::get<std::uint64_t>(loadedNavigation->get("obstacleMask").value()) == 8U);
    auto serializedAgain = SceneSerializer::serialize(*loaded.value());
    FGL_CHECK(serializedAgain && serializedAgain.value() == serialized.value());
}

FGL_TEST(scene_runtime_play_mode_binds_animation_particles_ui_and_navigation_to_entities) {
    auto registry = builtinRegistry();
    Scene scene{"Integrated play mode"};
    auto actorResult = scene.createEntity("Animated emitter");
    auto agentResult = scene.createEntity("Agent");
    auto targetResult = scene.createEntity("Target");
    auto uiResult = scene.createEntity("Toggle");
    FGL_CHECK(actorResult && agentResult && targetResult && uiResult);
    auto* actor = actorResult.value();
    auto* agent = agentResult.value();
    auto* target = targetResult.value();
    auto* uiEntity = uiResult.value();
    target->transform().setLocalPosition({4.0F, 0.0F, 0.0F});

    const auto controllerAsset = AssetGuid::fromStableName("tests.runtime.play-controller");
    auto* animator = attachBuiltin(*actor, registry, "Animator");
    FGL_CHECK(animator->set("controller", PropertyValue(controllerAsset)));
    FGL_CHECK(animator->set("initialState", PropertyValue(std::string("move"))));
    auto* particles = attachBuiltin(*actor, registry, "ParticleEmitter");
    FGL_CHECK(particles->set("rate", PropertyValue(2.0)));
    FGL_CHECK(particles->set("maxParticles", PropertyValue(std::uint64_t{4})));
    FGL_CHECK(particles->set("burstOnStart", PropertyValue(std::uint64_t{1})));
    FGL_CHECK(particles->set("lifetime", PropertyValue(2.0)));

    auto* navigation = attachBuiltin(*agent, registry, "NavigationAgent");
    FGL_CHECK(navigation->set("speed", PropertyValue(2.0)));
    FGL_CHECK(navigation->set("target", PropertyValue(target->id())));
    FGL_CHECK(navigation->set("arrivalRadius", PropertyValue(0.0)));

    auto* uiTransform = attachBuiltin(*uiEntity, registry, "UITransform");
    FGL_CHECK(uiTransform->set("widgetType", PropertyValue(std::int64_t{4})));
    FGL_CHECK(uiTransform->set("offsetMaximum", PropertyValue(Vec2{100.0F, 40.0F})));
    FGL_CHECK(uiTransform->set("text", PropertyValue(std::string("Music"))));

    SceneRuntimeConfig config;
    config.gravity = {};
    config.uiViewport = {0.0F, 0.0F, 100.0F, 40.0F};
    config.animatorFactory = [controllerAsset](AssetGuid requested) {
        if (requested != controllerAsset) {
            return Result<std::unique_ptr<AnimatorController>>::failure(
                Error(ErrorCode::NotFound, "test animator asset was not found"));
        }
        AnimationCurve curve;
        auto added = curve.addKey({0.0F, 0.0F});
        if (!added)
            return Result<std::unique_ptr<AnimatorController>>::failure(added.error());
        added = curve.addKey({1.0F, 10.0F});
        if (!added)
            return Result<std::unique_ptr<AnimatorController>>::failure(added.error());
        auto clip = std::make_shared<AnimationClip>("Move", 1.0F, true);
        added = clip->addTrack("Transform.localPosition.x", std::move(curve));
        if (!added)
            return Result<std::unique_ptr<AnimatorController>>::failure(added.error());
        auto controller = std::make_unique<AnimatorController>();
        added = controller->addState("move", std::move(clip));
        if (!added)
            return Result<std::unique_ptr<AnimatorController>>::failure(added.error());
        return Result<std::unique_ptr<AnimatorController>>::success(std::move(controller));
    };

    FGL_CHECK(scene.start());
    SceneRuntime runtime{scene, config};
    FGL_CHECK(runtime.initialize());
    FGL_CHECK(runtime.animatorCount() == 1U);
    FGL_CHECK(runtime.particleEmitterCount() == 1U);
    FGL_CHECK(runtime.navigationAgentCount() == 1U);
    FGL_CHECK(runtime.uiElementCount() == 1U);
    FGL_CHECK(runtime.particleStatsFor(actor->id()).activeParticles == 1U);

    FGL_CHECK(scene.update(0.5F));
    FGL_CHECK(runtime.update(0.5F));
    FGL_CHECK_NEAR(actor->transform().localPosition().x, 5.0F, 0.0001F);
    FGL_CHECK_NEAR(agent->transform().localPosition().x, 1.0F, 0.0001F);
    FGL_CHECK(runtime.navigationStateFor(agent->id()) == "move");
    FGL_CHECK(runtime.particleStatsFor(actor->id()).activeParticles == 2U);
    const auto* actorParticles = runtime.particlesFor(actor->id());
    FGL_CHECK(actorParticles != nullptr);
    FGL_CHECK_NEAR(actorParticles->particleAtSlot(1U)->position.x, 5.0F, 0.0001F);
    const auto* animationFrame = runtime.animationFrameFor(actor->id());
    FGL_CHECK(animationFrame != nullptr && animationFrame->state == "move");

    const auto uiElement = runtime.uiElementFor(uiEntity->id());
    FGL_CHECK(uiElement);
    FGL_CHECK(runtime.ui().pointerDown({10.0F, 10.0F}));
    FGL_CHECK(runtime.ui().pointerUp({10.0F, 10.0F}));
    FGL_CHECK(runtime.update(0.0F));
    FGL_CHECK(std::get<bool>(uiTransform->get("checked").value()));

    FGL_CHECK(animator->set("enabled", PropertyValue(false)));
    FGL_CHECK(runtime.update(0.0F));
    FGL_CHECK(runtime.animatorCount() == 0U);
    runtime.shutdown();
    FGL_CHECK(runtime.particleEmitterCount() == 0U);
    FGL_CHECK(runtime.navigationAgentCount() == 0U);
    FGL_CHECK(runtime.uiElementCount() == 0U);
}
