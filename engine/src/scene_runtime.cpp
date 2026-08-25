#include "fabgl/runtime/scene_runtime.h"

#include "fabgl/scene/builtin_components.h"
#include "fabgl/scene/entity.h"
#include "fabgl/scene/scene.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace fabgl {
namespace {

ComponentTypeGuid builtinTypeId(std::string_view shortName) {
    return ComponentTypeGuid::fromStableName(std::string("fabgl.component.") +
                                             std::string(shortName) + ".v1");
}

template <typename T>
Result<T> property(const DataComponent& component, std::string_view propertyName) {
    auto value = component.get(propertyName);
    if (!value)
        return Result<T>::failure(value.error());
    const auto* typed = std::get_if<T>(&value.value());
    if (typed == nullptr) {
        return Result<T>::failure(
            Error(ErrorCode::TypeMismatch, "runtime component property has an unexpected type")
                .addContext("component", std::string(component.typeName()))
                .addContext("property", std::string(propertyName)));
    }
    return Result<T>::success(*typed);
}

Result<bool> runtimeEnabled(const DataComponent& component) {
    if (!component.activeAndEnabled())
        return Result<bool>::success(false);
    return property<bool>(component, "enabled");
}

[[nodiscard]] float distance(Vec2 lhs, Vec2 rhs) noexcept {
    const auto delta = rhs - lhs;
    return std::sqrt(delta.x * delta.x + delta.y * delta.y);
}

[[nodiscard]] std::vector<std::string> splitItems(std::string_view text) {
    std::vector<std::string> items;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto separator = text.find(';', start);
        const auto end = separator == std::string_view::npos ? text.size() : separator;
        if (end > start)
            items.emplace_back(text.substr(start, end - start));
        if (separator == std::string_view::npos)
            break;
        start = separator + 1U;
    }
    return items;
}

[[nodiscard]] Result<void> applyAnimationValues(Entity& entity, const AnimationFrame& frame) {
    auto position = entity.transform().localPosition();
    auto rotation = entity.transform().localRotation();
    auto scale = entity.transform().localScale();
    bool positionChanged = false;
    bool rotationChanged = false;
    bool scaleChanged = false;
    for (const auto& [path, value] : frame.values) {
        if (!std::isfinite(value)) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "animation produced a non-finite value")
                    .addContext("entity", entity.id().toString())
                    .addContext("property", path));
        }
        if (path == "position.x" || path == "Transform.localPosition.x") {
            position.x = value;
            positionChanged = true;
        } else if (path == "position.y" || path == "Transform.localPosition.y") {
            position.y = value;
            positionChanged = true;
        } else if (path == "position.z" || path == "Transform.localPosition.z") {
            position.z = value;
            positionChanged = true;
        } else if (path == "rotation.x" || path == "Transform.localRotation.x") {
            rotation.x = value;
            rotationChanged = true;
        } else if (path == "rotation.y" || path == "Transform.localRotation.y") {
            rotation.y = value;
            rotationChanged = true;
        } else if (path == "rotation.z" || path == "Transform.localRotation.z") {
            rotation.z = value;
            rotationChanged = true;
        } else if (path == "scale.x" || path == "Transform.localScale.x") {
            scale.x = value;
            scaleChanged = true;
        } else if (path == "scale.y" || path == "Transform.localScale.y") {
            scale.y = value;
            scaleChanged = true;
        } else if (path == "scale.z" || path == "Transform.localScale.z") {
            scale.z = value;
            scaleChanged = true;
        }
    }
    if (positionChanged)
        entity.transform().setLocalPosition(position);
    if (rotationChanged)
        entity.transform().setLocalRotation(rotation);
    if (scaleChanged)
        entity.transform().setLocalScale(scale);
    return Result<void>::success();
}

template <typename T>
Result<T> requiredProperty(const DataComponent& component, std::string_view name) {
    auto value = property<T>(component, name);
    if (!value) {
        return Result<T>::failure(value.error().withContext("runtime", "scene_physics"));
    }
    return value;
}

std::optional<VisualRuntimeEvent> visualEventForNode(const VisualNode& node) noexcept {
    switch (node.builtinType) {
    case VisualBuiltinNodeType::Legacy:
    case VisualBuiltinNodeType::EventStart:
        return VisualRuntimeEvent::Start;
    case VisualBuiltinNodeType::EventUpdate:
        return VisualRuntimeEvent::Update;
    case VisualBuiltinNodeType::EventFixedUpdate:
        return VisualRuntimeEvent::FixedUpdate;
    case VisualBuiltinNodeType::EventLateUpdate:
        return VisualRuntimeEvent::LateUpdate;
    case VisualBuiltinNodeType::CollisionEvent:
        return VisualRuntimeEvent::CollisionEnter;
    case VisualBuiltinNodeType::EventCollisionStay:
        return VisualRuntimeEvent::CollisionStay;
    case VisualBuiltinNodeType::EventCollisionExit:
        return VisualRuntimeEvent::CollisionExit;
    case VisualBuiltinNodeType::EventTriggerEnter:
        return VisualRuntimeEvent::TriggerEnter;
    case VisualBuiltinNodeType::EventTriggerExit:
        return VisualRuntimeEvent::TriggerExit;
    default:
        return std::nullopt;
    }
}

const char* visualEventName(const VisualRuntimeEvent event) noexcept {
    switch (event) {
    case VisualRuntimeEvent::Start:
        return "start";
    case VisualRuntimeEvent::Update:
        return "update";
    case VisualRuntimeEvent::FixedUpdate:
        return "fixed_update";
    case VisualRuntimeEvent::LateUpdate:
        return "late_update";
    case VisualRuntimeEvent::CollisionEnter:
        return "collision_enter";
    case VisualRuntimeEvent::CollisionStay:
        return "collision_stay";
    case VisualRuntimeEvent::CollisionExit:
        return "collision_exit";
    case VisualRuntimeEvent::TriggerEnter:
        return "trigger_enter";
    case VisualRuntimeEvent::TriggerExit:
        return "trigger_exit";
    case VisualRuntimeEvent::None:
        break;
    }
    return "none";
}

Error visualRuntimeError(Error error, const EntityGuid entity, const AssetGuid graph,
                         const char* stage) {
    return error.withContext("visual_stage", stage)
        .withContext("visual_graph_asset", graph.toString())
        .withContext("entity", entity.toString());
}

} // namespace

struct SceneRuntime::AnimatorBinding final {
    DataComponent* component = nullptr;
    AssetGuid controllerAsset;
    std::string initialState;
    std::unique_ptr<AnimatorController> controller;
    std::optional<AnimationFrame> lastFrame;
};

struct SceneRuntime::ParticleBinding final {
    DataComponent* component = nullptr;
    std::unique_ptr<ParticleSystem> system;
    std::unique_ptr<ParticleEmitter> emitter;
};

struct SceneRuntime::NavigationBinding final {
    DataComponent* component = nullptr;
    AiStateMachine stateMachine;
    WaypointFollower follower;
    EntityGuid configuredTarget;
    Vec2 configuredPosition{};
    float configuredSpeed = 0.0F;
    float configuredArrivalRadius = 0.0F;
    bool followerConfigured = false;
    bool shouldMove = false;
};

struct SceneRuntime::VisualScriptBinding final {
    struct PendingExecution final {
        VisualVmContinuation continuation;
        VisualHostExecutionContext context;
        double remainingSeconds = 0.0;
    };

    struct Program final {
        VisualRuntimeEvent event = VisualRuntimeEvent::None;
        VisualNodeId entryNode = 0U;
        VisualBytecode bytecode;
        std::optional<PendingExecution> pending;
    };

    DataComponent* component = nullptr;
    AssetGuid graphAsset;
    std::vector<Program> programs;
    std::map<std::string, double> variables;
    double lastReturnValue = 0.0;
    bool startPending = true;
};

namespace {

struct ReflectedParticleSettings final {
    ParticleEmitterSettings settings;
    std::uint64_t burstOnStart = 0;
};

Result<ReflectedParticleSettings> reflectedParticleSettings(const DataComponent& component) {
    auto rate = property<double>(component, "rate");
    auto maximum = property<std::uint64_t>(component, "maxParticles");
    auto burst = property<std::uint64_t>(component, "burstOnStart");
    auto lifetime = property<double>(component, "lifetime");
    auto velocity = property<Vec2>(component, "velocity");
    auto acceleration = property<Vec2>(component, "acceleration");
    auto startColor = property<Color>(component, "startColor");
    auto endColor = property<Color>(component, "endColor");
    auto startSize = property<double>(component, "startSize");
    auto endSize = property<double>(component, "endSize");
    auto startRotation = property<double>(component, "startRotation");
    auto endRotation = property<double>(component, "endRotation");
    auto cull = property<bool>(component, "cullOutsideBounds");
    auto bounds = property<Rect>(component, "cullingBounds");
    if (!rate)
        return Result<ReflectedParticleSettings>::failure(rate.error());
    if (!maximum)
        return Result<ReflectedParticleSettings>::failure(maximum.error());
    if (!burst)
        return Result<ReflectedParticleSettings>::failure(burst.error());
    if (!lifetime)
        return Result<ReflectedParticleSettings>::failure(lifetime.error());
    if (!velocity)
        return Result<ReflectedParticleSettings>::failure(velocity.error());
    if (!acceleration)
        return Result<ReflectedParticleSettings>::failure(acceleration.error());
    if (!startColor)
        return Result<ReflectedParticleSettings>::failure(startColor.error());
    if (!endColor)
        return Result<ReflectedParticleSettings>::failure(endColor.error());
    if (!startSize)
        return Result<ReflectedParticleSettings>::failure(startSize.error());
    if (!endSize)
        return Result<ReflectedParticleSettings>::failure(endSize.error());
    if (!startRotation)
        return Result<ReflectedParticleSettings>::failure(startRotation.error());
    if (!endRotation)
        return Result<ReflectedParticleSettings>::failure(endRotation.error());
    if (!cull)
        return Result<ReflectedParticleSettings>::failure(cull.error());
    if (!bounds)
        return Result<ReflectedParticleSettings>::failure(bounds.error());
    if (maximum.value() == 0U || maximum.value() > 1'000'000U ||
        maximum.value() > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return Result<ReflectedParticleSettings>::failure(
            Error(ErrorCode::InvalidArgument, "scene particle capacity is outside runtime limits"));
    }
    ReflectedParticleSettings reflected;
    reflected.settings.spawnRate = static_cast<float>(rate.value());
    reflected.settings.maximumAlive = static_cast<std::size_t>(maximum.value());
    reflected.settings.particle.velocity = velocity.value();
    reflected.settings.particle.acceleration = acceleration.value();
    reflected.settings.particle.color = startColor.value();
    reflected.settings.particle.size = static_cast<float>(startSize.value());
    reflected.settings.particle.lifetimeSeconds = static_cast<float>(lifetime.value());
    reflected.settings.particle.rotationDegrees = static_cast<float>(startRotation.value());
    reflected.settings.overLifetime.colorEnabled = true;
    reflected.settings.overLifetime.endColor = endColor.value();
    reflected.settings.overLifetime.sizeEnabled = true;
    reflected.settings.overLifetime.endSize = static_cast<float>(endSize.value());
    reflected.settings.overLifetime.rotationEnabled = true;
    reflected.settings.overLifetime.endRotationDegrees = static_cast<float>(endRotation.value());
    reflected.settings.cullOutsideBounds = cull.value();
    reflected.settings.cullingBounds = bounds.value();
    reflected.burstOnStart = burst.value();
    return Result<ReflectedParticleSettings>::success(std::move(reflected));
}

Result<UILayoutProperties> reflectedUIProperties(const DataComponent& component) {
    auto anchorMinimum = property<Vec2>(component, "anchorMinimum");
    auto anchorMaximum = property<Vec2>(component, "anchorMaximum");
    auto offsetMinimum = property<Vec2>(component, "offsetMinimum");
    auto offsetMaximum = property<Vec2>(component, "offsetMaximum");
    auto visible = property<bool>(component, "visible");
    if (!anchorMinimum)
        return Result<UILayoutProperties>::failure(anchorMinimum.error());
    if (!anchorMaximum)
        return Result<UILayoutProperties>::failure(anchorMaximum.error());
    if (!offsetMinimum)
        return Result<UILayoutProperties>::failure(offsetMinimum.error());
    if (!offsetMaximum)
        return Result<UILayoutProperties>::failure(offsetMaximum.error());
    if (!visible)
        return Result<UILayoutProperties>::failure(visible.error());
    UILayoutProperties properties;
    properties.anchors = {anchorMinimum.value(), anchorMaximum.value()};
    properties.minimumOffset = offsetMinimum.value();
    properties.maximumOffset = offsetMaximum.value();
    properties.visible = visible.value();
    properties.enabled = true;
    return Result<UILayoutProperties>::success(properties);
}

Result<UIWidgetType> reflectedWidgetType(const DataComponent& component) {
    auto type = property<std::int64_t>(component, "widgetType");
    if (!type)
        return Result<UIWidgetType>::failure(type.error());
    if (type.value() < 0 || type.value() > 8) {
        return Result<UIWidgetType>::failure(
            Error(ErrorCode::InvalidArgument, "UI widget type is outside the supported range"));
    }
    return Result<UIWidgetType>::success(static_cast<UIWidgetType>(type.value()));
}

} // namespace

SceneRuntime::SceneRuntime(Scene& scene, SceneRuntimeConfig config)
    : scene_(&scene), config_(std::move(config)) {}

SceneRuntime::~SceneRuntime() {
    shutdown();
}

Result<void> SceneRuntime::initialize() {
    if (initialized_)
        return Result<void>::success();
    if (config_.maximumVisualScripts == 0U ||
        config_.maximumVisualScripts > SceneRuntimeMaximumVisualScripts ||
        config_.maximumVisualEventProgramsPerScript == 0U ||
        config_.maximumVisualEventProgramsPerScript >
            SceneRuntimeMaximumVisualEventProgramsPerScript ||
        config_.maximumVisualInstructionsPerInvocation == 0U ||
        config_.maximumVisualInstructionsPerInvocation > VisualMaximumCompiledInstructions ||
        config_.maximumVisualInstructionsPerPhase == 0U ||
        config_.maximumVisualInstructionsPerPhase > SceneRuntimeMaximumVisualInstructionsPerPhase) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "scene visual-script limits are invalid"));
    }
    auto gravity = physics_.setGravity(config_.gravity);
    if (!gravity)
        return gravity;
    auto scale = ui_.setScale(config_.uiScale);
    if (!scale)
        return scale;
    initialized_ = true;
    auto reconciled = reconcileBodies();
    if (!reconciled) {
        shutdown();
        return reconciled;
    }
    reconciled = reconcileRuntimeSystems();
    if (!reconciled) {
        shutdown();
        return reconciled;
    }
    reconciled = reconcileVisualScripts();
    if (!reconciled) {
        shutdown();
        return reconciled;
    }
    auto remainingInstructions = config_.maximumVisualInstructionsPerPhase;
    reconciled = runPendingVisualStarts(remainingInstructions);
    if (!reconciled) {
        shutdown();
        return reconciled;
    }
    return Result<void>::success();
}

std::optional<PhysicsBodyId> SceneRuntime::bodyFor(EntityGuid entity) const noexcept {
    const auto found = bodyByEntity_.find(entity);
    return found == bodyByEntity_.end() ? std::nullopt
                                        : std::optional<PhysicsBodyId>(found->second);
}

std::optional<double> SceneRuntime::visualVariable(const EntityGuid entity,
                                                   const std::string_view name) const {
    const auto binding = visualScripts_.find(entity);
    if (binding == visualScripts_.end())
        return std::nullopt;
    const auto variable = binding->second->variables.find(std::string(name));
    return variable == binding->second->variables.end() ? std::nullopt
                                                        : std::optional<double>(variable->second);
}

AnimatorController* SceneRuntime::animatorFor(EntityGuid entity) noexcept {
    const auto found = animators_.find(entity);
    return found == animators_.end() ? nullptr : found->second->controller.get();
}

const AnimatorController* SceneRuntime::animatorFor(EntityGuid entity) const noexcept {
    const auto found = animators_.find(entity);
    return found == animators_.end() ? nullptr : found->second->controller.get();
}

const AnimationFrame* SceneRuntime::animationFrameFor(EntityGuid entity) const noexcept {
    const auto found = animators_.find(entity);
    return found == animators_.end() || !found->second->lastFrame ? nullptr
                                                                  : &*found->second->lastFrame;
}

ParticleSystem* SceneRuntime::particlesFor(EntityGuid entity) noexcept {
    const auto found = particleEmitters_.find(entity);
    return found == particleEmitters_.end() ? nullptr : found->second->system.get();
}

const ParticleSystem* SceneRuntime::particlesFor(EntityGuid entity) const noexcept {
    const auto found = particleEmitters_.find(entity);
    return found == particleEmitters_.end() ? nullptr : found->second->system.get();
}

ParticleEmitterStats SceneRuntime::particleStatsFor(EntityGuid entity) const noexcept {
    const auto found = particleEmitters_.find(entity);
    return found == particleEmitters_.end() ? ParticleEmitterStats{}
                                            : found->second->emitter->stats();
}

std::string SceneRuntime::navigationStateFor(EntityGuid entity) const {
    const auto found = navigationAgents_.find(entity);
    return found == navigationAgents_.end() ? std::string{}
                                            : found->second->stateMachine.currentState();
}

std::optional<UIElementId> SceneRuntime::uiElementFor(EntityGuid entity) const noexcept {
    const auto found = uiByEntity_.find(entity);
    return found == uiByEntity_.end() ? std::nullopt : std::optional<UIElementId>(found->second);
}

DataComponent* SceneRuntime::component(Entity& entity, const char* shortName) const noexcept {
    return dynamic_cast<DataComponent*>(entity.getComponent(builtinTypeId(shortName)));
}

Result<void> SceneRuntime::addBody(Entity& entity, DataComponent& collider,
                                   DataComponent* rigidbody) {
    PhysicsBody2D body;
    const auto position = entity.transform().localPosition();
    body.position = {position.x, position.y};

    auto shape = requiredProperty<std::int64_t>(collider, "shape");
    auto size = requiredProperty<Vec2>(collider, "size");
    auto radius = requiredProperty<double>(collider, "radius");
    auto trigger = requiredProperty<bool>(collider, "trigger");
    auto layer = requiredProperty<std::uint64_t>(collider, "layer");
    auto collisionMask = requiredProperty<std::uint64_t>(collider, "collisionMask");
    if (!shape)
        return Result<void>::failure(shape.error());
    if (!size)
        return Result<void>::failure(size.error());
    if (!radius)
        return Result<void>::failure(radius.error());
    if (!trigger)
        return Result<void>::failure(trigger.error());
    if (!layer)
        return Result<void>::failure(layer.error());
    if (!collisionMask)
        return Result<void>::failure(collisionMask.error());
    if (layer.value() == 0U || layer.value() > std::numeric_limits<std::uint32_t>::max() ||
        collisionMask.value() > std::numeric_limits<std::uint32_t>::max()) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "physics layer values must fit in 32 bits")
                .addContext("entity", entity.id().toString()));
    }

    const auto scale = entity.transform().localScale();
    if (shape.value() == 0) {
        body.shape = AabbShape{{std::fabs(size.value().x * scale.x) * 0.5F,
                                std::fabs(size.value().y * scale.y) * 0.5F}};
    } else if (shape.value() == 1) {
        body.shape = CircleShape{static_cast<float>(radius.value()) *
                                 std::max(std::fabs(scale.x), std::fabs(scale.y))};
    } else if (shape.value() == 2) {
        body.shape = PointShape{};
    } else {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "unsupported Collider2D shape")
                .addContext("entity", entity.id().toString())
                .addContext("shape", std::to_string(shape.value())));
    }
    body.trigger = trigger.value();
    body.layer = static_cast<std::uint32_t>(layer.value());
    body.collisionMask = static_cast<std::uint32_t>(collisionMask.value());

    if (rigidbody != nullptr) {
        auto enabled = runtimeEnabled(*rigidbody);
        if (!enabled)
            return Result<void>::failure(enabled.error());
        if (enabled.value()) {
            auto bodyType = requiredProperty<std::int64_t>(*rigidbody, "bodyType");
            if (!bodyType)
                return Result<void>::failure(bodyType.error());
            if (bodyType.value() < 0 || bodyType.value() > 2) {
                return Result<void>::failure(
                    Error(ErrorCode::InvalidArgument, "unsupported Rigidbody2D body type")
                        .addContext("entity", entity.id().toString()));
            }
            body.kinematic = bodyType.value() == 1;
            body.dynamic = bodyType.value() == 2;
        }
        if (body.dynamic || body.kinematic) {
            auto velocity = requiredProperty<Vec2>(*rigidbody, "velocity");
            auto mass = requiredProperty<double>(*rigidbody, "mass");
            auto gravityScale = requiredProperty<double>(*rigidbody, "gravityScale");
            auto restitution = requiredProperty<double>(*rigidbody, "restitution");
            auto friction = requiredProperty<double>(*rigidbody, "friction");
            if (!velocity)
                return Result<void>::failure(velocity.error());
            if (!mass)
                return Result<void>::failure(mass.error());
            if (!gravityScale)
                return Result<void>::failure(gravityScale.error());
            if (!restitution)
                return Result<void>::failure(restitution.error());
            if (!friction)
                return Result<void>::failure(friction.error());
            body.velocity = velocity.value();
            body.mass = static_cast<float>(mass.value());
            body.gravityScale = static_cast<float>(gravityScale.value());
            body.restitution = static_cast<float>(restitution.value());
            body.friction = static_cast<float>(friction.value());
        }
    }

    auto added = physics_.addBody(body);
    if (!added) {
        return Result<void>::failure(added.error().withContext("entity", entity.id().toString()));
    }
    bodyByEntity_.emplace(entity.id(), added.value());
    entityByBody_.emplace(added.value().value, entity.id());
    return Result<void>::success();
}

Result<void> SceneRuntime::reconcileBodies() {
    std::set<EntityGuid> desired;
    for (auto* entity : scene_->entities()) {
        auto* collider = component(*entity, "Collider2D");
        if (collider == nullptr || !entity->active())
            continue;
        auto enabled = runtimeEnabled(*collider);
        if (!enabled)
            return Result<void>::failure(enabled.error());
        if (!enabled.value())
            continue;
        desired.insert(entity->id());
        if (bodyByEntity_.find(entity->id()) == bodyByEntity_.end()) {
            auto added = addBody(*entity, *collider, component(*entity, "Rigidbody2D"));
            if (!added)
                return added;
        }
    }

    for (auto iterator = bodyByEntity_.begin(); iterator != bodyByEntity_.end();) {
        if (desired.find(iterator->first) != desired.end()) {
            ++iterator;
            continue;
        }
        entityByBody_.erase(iterator->second.value);
        auto removed = physics_.removeBody(iterator->second);
        if (!removed)
            return removed;
        iterator = bodyByEntity_.erase(iterator);
    }
    return Result<void>::success();
}

Result<void> SceneRuntime::updateBodyFromComponents(Entity& entity, PhysicsBody2D& body,
                                                    DataComponent& collider,
                                                    DataComponent* rigidbody,
                                                    bool beforeSimulation) {
    if (beforeSimulation) {
        auto shape = requiredProperty<std::int64_t>(collider, "shape");
        auto size = requiredProperty<Vec2>(collider, "size");
        auto radius = requiredProperty<double>(collider, "radius");
        auto trigger = requiredProperty<bool>(collider, "trigger");
        auto layer = requiredProperty<std::uint64_t>(collider, "layer");
        auto collisionMask = requiredProperty<std::uint64_t>(collider, "collisionMask");
        if (!shape)
            return Result<void>::failure(shape.error());
        if (!size)
            return Result<void>::failure(size.error());
        if (!radius)
            return Result<void>::failure(radius.error());
        if (!trigger)
            return Result<void>::failure(trigger.error());
        if (!layer)
            return Result<void>::failure(layer.error());
        if (!collisionMask)
            return Result<void>::failure(collisionMask.error());
        if (layer.value() == 0U || layer.value() > std::numeric_limits<std::uint32_t>::max() ||
            collisionMask.value() > std::numeric_limits<std::uint32_t>::max()) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "physics layer values must fit in 32 bits")
                    .addContext("entity", entity.id().toString()));
        }
        const auto scale = entity.transform().localScale();
        if (shape.value() == 0) {
            const Vec2 halfExtents{std::fabs(size.value().x * scale.x) * 0.5F,
                                   std::fabs(size.value().y * scale.y) * 0.5F};
            if (halfExtents.x <= 0.0F || halfExtents.y <= 0.0F) {
                return Result<void>::failure(
                    Error(ErrorCode::InvalidArgument, "Collider2D size must be positive")
                        .addContext("entity", entity.id().toString()));
            }
            body.shape = AabbShape{halfExtents};
        } else if (shape.value() == 1) {
            const auto scaledRadius = static_cast<float>(radius.value()) *
                                      std::max(std::fabs(scale.x), std::fabs(scale.y));
            if (!std::isfinite(scaledRadius) || scaledRadius <= 0.0F) {
                return Result<void>::failure(
                    Error(ErrorCode::InvalidArgument, "Collider2D radius must be positive")
                        .addContext("entity", entity.id().toString()));
            }
            body.shape = CircleShape{scaledRadius};
        } else if (shape.value() == 2) {
            body.shape = PointShape{};
        } else {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "unsupported Collider2D shape")
                    .addContext("entity", entity.id().toString()));
        }
        body.trigger = trigger.value();
        body.layer = static_cast<std::uint32_t>(layer.value());
        body.collisionMask = static_cast<std::uint32_t>(collisionMask.value());

        const bool wasSimulated = body.dynamic || body.kinematic;
        body.dynamic = false;
        body.kinematic = false;
        if (rigidbody != nullptr) {
            auto enabled = runtimeEnabled(*rigidbody);
            if (!enabled)
                return Result<void>::failure(enabled.error());
            if (enabled.value()) {
                auto bodyType = requiredProperty<std::int64_t>(*rigidbody, "bodyType");
                if (!bodyType)
                    return Result<void>::failure(bodyType.error());
                if (bodyType.value() < 0 || bodyType.value() > 2) {
                    return Result<void>::failure(
                        Error(ErrorCode::InvalidArgument, "unsupported Rigidbody2D body type")
                            .addContext("entity", entity.id().toString()));
                }
                body.kinematic = bodyType.value() == 1;
                body.dynamic = bodyType.value() == 2;
            }
            if (body.dynamic || body.kinematic) {
                auto velocity = requiredProperty<Vec2>(*rigidbody, "velocity");
                auto mass = requiredProperty<double>(*rigidbody, "mass");
                auto gravityScale = requiredProperty<double>(*rigidbody, "gravityScale");
                auto restitution = requiredProperty<double>(*rigidbody, "restitution");
                auto friction = requiredProperty<double>(*rigidbody, "friction");
                if (!velocity)
                    return Result<void>::failure(velocity.error());
                if (!mass)
                    return Result<void>::failure(mass.error());
                if (!gravityScale)
                    return Result<void>::failure(gravityScale.error());
                if (!restitution)
                    return Result<void>::failure(restitution.error());
                if (!friction)
                    return Result<void>::failure(friction.error());
                if (!std::isfinite(mass.value()) || mass.value() <= 0.0 ||
                    !std::isfinite(gravityScale.value()) || !std::isfinite(restitution.value()) ||
                    restitution.value() < 0.0 || restitution.value() > 1.0 ||
                    !std::isfinite(friction.value()) || friction.value() < 0.0) {
                    return Result<void>::failure(
                        Error(ErrorCode::InvalidArgument,
                              "Rigidbody2D mass, gravity, restitution or friction is invalid")
                            .addContext("entity", entity.id().toString()));
                }
                body.velocity = velocity.value();
                body.mass = static_cast<float>(mass.value());
                body.gravityScale = static_cast<float>(gravityScale.value());
                body.restitution = static_cast<float>(restitution.value());
                body.friction = static_cast<float>(friction.value());
            }
        }

        const auto position = entity.transform().localPosition();
        if ((!body.dynamic && !body.kinematic) || !wasSimulated)
            body.position = {position.x, position.y};
    }
    if ((body.dynamic || body.kinematic) && !beforeSimulation) {
        auto position = entity.transform().localPosition();
        position.x = body.position.x;
        position.y = body.position.y;
        entity.transform().setLocalPosition(position);
        if (rigidbody != nullptr) {
            auto velocity = rigidbody->set("velocity", PropertyValue(body.velocity));
            if (!velocity)
                return velocity;
        }
    }
    return Result<void>::success();
}

Result<void> SceneRuntime::dispatchContact(const ContactPair& pair, bool entering, bool staying) {
    if (scene_->findEntity(pair.first) == nullptr || scene_->findEntity(pair.second) == nullptr)
        return Result<void>::success();

    const auto dispatchOne = [&](EntityGuid receiver, EntityGuid other) -> Result<void> {
        Result<void> dispatched = Result<void>::success();
        auto visualEvent = VisualRuntimeEvent::None;
        if (pair.trigger) {
            if (staying)
                return Result<void>::success();
            dispatched = entering ? scene_->dispatchTriggerEnter(receiver, other)
                                  : scene_->dispatchTriggerExit(receiver, other);
            visualEvent =
                entering ? VisualRuntimeEvent::TriggerEnter : VisualRuntimeEvent::TriggerExit;
        } else if (staying) {
            dispatched = scene_->dispatchCollisionStay(receiver, other);
            visualEvent = VisualRuntimeEvent::CollisionStay;
        } else if (entering) {
            dispatched = scene_->dispatchCollisionEnter(receiver, other);
            visualEvent = VisualRuntimeEvent::CollisionEnter;
        } else {
            dispatched = scene_->dispatchCollisionExit(receiver, other);
            visualEvent = VisualRuntimeEvent::CollisionExit;
        }
        if (!dispatched)
            return dispatched;
        return executeVisualEvent(receiver, visualEvent, visualFixedDeltaSeconds_, other,
                                  visualFixedInstructionsRemaining_);
    };
    auto first = dispatchOne(pair.first, pair.second);
    if (!first)
        return first;
    return dispatchOne(pair.second, pair.first);
}

Result<void> SceneRuntime::reconcileRuntimeSystems() {
    auto result = reconcileAnimators();
    if (!result)
        return result;
    result = reconcileParticleEmitters();
    if (!result)
        return result;
    result = reconcileNavigationAgents();
    if (!result)
        return result;
    return reconcileUI();
}

Result<void> SceneRuntime::reconcileAnimators() {
    std::set<EntityGuid> desired;
    for (auto* entity : scene_->entities()) {
        auto* animator = component(*entity, "Animator");
        if (animator == nullptr || !entity->active())
            continue;
        auto enabled = runtimeEnabled(*animator);
        if (!enabled)
            return Result<void>::failure(enabled.error());
        if (!enabled.value())
            continue;
        desired.insert(entity->id());

        auto controllerAsset = property<AssetGuid>(*animator, "controller");
        auto initialState = property<std::string>(*animator, "initialState");
        if (!controllerAsset)
            return Result<void>::failure(controllerAsset.error());
        if (!initialState)
            return Result<void>::failure(initialState.error());
        auto existing = animators_.find(entity->id());
        if (existing != animators_.end() &&
            (existing->second->component != animator ||
             existing->second->controllerAsset != controllerAsset.value() ||
             existing->second->initialState != initialState.value())) {
            animators_.erase(existing);
            existing = animators_.end();
        }
        if (existing != animators_.end())
            continue;
        if (controllerAsset.value().isNil()) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "Animator controller asset is not assigned")
                    .addContext("entity", entity->id().toString()));
        }
        if (!config_.animatorFactory) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidState, "scene runtime has no animator factory")
                    .addContext("entity", entity->id().toString()));
        }
        auto created = config_.animatorFactory(controllerAsset.value());
        if (!created)
            return Result<void>::failure(
                created.error().withContext("entity", entity->id().toString()));
        auto controller = std::move(created).value();
        if (!controller) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidState, "animator factory returned no controller")
                    .addContext("entity", entity->id().toString()));
        }
        if (!initialState.value().empty()) {
            auto played = controller->play(initialState.value());
            if (!played)
                return Result<void>::failure(
                    played.error().withContext("entity", entity->id().toString()));
        } else if (controller->currentState().empty()) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidState, "Animator has no initial state")
                    .addContext("entity", entity->id().toString()));
        }
        auto binding = std::make_unique<AnimatorBinding>();
        binding->component = animator;
        binding->controllerAsset = controllerAsset.value();
        binding->initialState = initialState.value();
        binding->controller = std::move(controller);
        animators_.emplace(entity->id(), std::move(binding));
    }
    for (auto iterator = animators_.begin(); iterator != animators_.end();) {
        iterator = desired.find(iterator->first) == desired.end() ? animators_.erase(iterator)
                                                                  : std::next(iterator);
    }
    return Result<void>::success();
}

Result<void> SceneRuntime::reconcileParticleEmitters() {
    std::set<EntityGuid> desired;
    for (auto* entity : scene_->entities()) {
        auto* emitterComponent = component(*entity, "ParticleEmitter");
        if (emitterComponent == nullptr || !entity->active())
            continue;
        auto enabled = runtimeEnabled(*emitterComponent);
        if (!enabled)
            return Result<void>::failure(enabled.error());
        if (!enabled.value())
            continue;
        desired.insert(entity->id());
        auto reflected = reflectedParticleSettings(*emitterComponent);
        if (!reflected)
            return Result<void>::failure(
                reflected.error().withContext("entity", entity->id().toString()));

        auto existing = particleEmitters_.find(entity->id());
        if (existing != particleEmitters_.end() &&
            (existing->second->component != emitterComponent ||
             existing->second->system->capacity() != reflected.value().settings.maximumAlive)) {
            particleEmitters_.erase(existing);
            existing = particleEmitters_.end();
        }
        if (existing != particleEmitters_.end())
            continue;

        auto binding = std::make_unique<ParticleBinding>();
        binding->component = emitterComponent;
        binding->system = std::make_unique<ParticleSystem>(reflected.value().settings.maximumAlive);
        binding->emitter =
            std::make_unique<ParticleEmitter>(*binding->system, reflected.value().settings);
        auto configured = binding->emitter->setSettings(reflected.value().settings);
        if (!configured)
            return Result<void>::failure(
                configured.error().withContext("entity", entity->id().toString()));
        const auto position = entity->transform().localPosition();
        binding->emitter->setPosition({position.x, position.y});
        if (reflected.value().burstOnStart > 0U) {
            const auto burstLimit =
                static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
            const auto requested =
                static_cast<std::size_t>(std::min(reflected.value().burstOnStart, burstLimit));
            auto burst = binding->emitter->burst(requested);
            if (!burst)
                return Result<void>::failure(
                    burst.error().withContext("entity", entity->id().toString()));
        }
        particleEmitters_.emplace(entity->id(), std::move(binding));
    }
    for (auto iterator = particleEmitters_.begin(); iterator != particleEmitters_.end();) {
        iterator = desired.find(iterator->first) == desired.end()
                       ? particleEmitters_.erase(iterator)
                       : std::next(iterator);
    }
    return Result<void>::success();
}

Result<void> SceneRuntime::reconcileNavigationAgents() {
    std::set<EntityGuid> desired;
    for (auto* entity : scene_->entities()) {
        auto* agent = component(*entity, "NavigationAgent");
        if (agent == nullptr || !entity->active())
            continue;
        auto enabled = runtimeEnabled(*agent);
        if (!enabled)
            return Result<void>::failure(enabled.error());
        if (!enabled.value())
            continue;
        desired.insert(entity->id());
        auto existing = navigationAgents_.find(entity->id());
        if (existing != navigationAgents_.end() && existing->second->component != agent) {
            navigationAgents_.erase(existing);
            existing = navigationAgents_.end();
        }
        if (existing != navigationAgents_.end())
            continue;

        auto binding = std::make_unique<NavigationBinding>();
        binding->component = agent;
        auto* raw = binding.get();
        auto state = binding->stateMachine.addState("idle");
        if (!state)
            return state;
        state = binding->stateMachine.addState("move");
        if (!state)
            return state;
        state = binding->stateMachine.addTransition(
            {"idle", "move", [raw]() { return raw->shouldMove; }});
        if (!state)
            return state;
        state = binding->stateMachine.addTransition(
            {"move", "idle", [raw]() { return !raw->shouldMove; }});
        if (!state)
            return state;
        state = binding->stateMachine.transitionTo("idle");
        if (!state)
            return state;
        navigationAgents_.emplace(entity->id(), std::move(binding));
    }
    for (auto iterator = navigationAgents_.begin(); iterator != navigationAgents_.end();) {
        iterator = desired.find(iterator->first) == desired.end()
                       ? navigationAgents_.erase(iterator)
                       : std::next(iterator);
    }
    return Result<void>::success();
}

Result<void> SceneRuntime::reconcileUI() {
    std::map<EntityGuid, UIWidgetType> desiredTypes;
    std::map<EntityGuid, std::optional<EntityGuid>> desiredParents;
    std::map<EntityGuid, Entity*> entities;
    for (auto* entity : scene_->entities()) {
        auto* transform = component(*entity, "UITransform");
        if (transform == nullptr || !entity->active())
            continue;
        auto enabled = runtimeEnabled(*transform);
        if (!enabled)
            return Result<void>::failure(enabled.error());
        if (!enabled.value())
            continue;
        auto type = reflectedWidgetType(*transform);
        if (!type)
            return Result<void>::failure(type.error());
        auto resolvedType = type.value();
        if (resolvedType == UIWidgetType::Panel) {
            if (component(*entity, "UIButton") != nullptr)
                resolvedType = UIWidgetType::Button;
            else if (component(*entity, "UIText") != nullptr)
                resolvedType = UIWidgetType::Text;
            else if (component(*entity, "UIImage") != nullptr)
                resolvedType = UIWidgetType::Image;
        }
        desiredTypes.emplace(entity->id(), resolvedType);
        desiredParents.emplace(entity->id(), entity->transform().parent());
        entities.emplace(entity->id(), entity);
    }
    for (auto& [entity, parent] : desiredParents) {
        static_cast<void>(entity);
        if (parent && desiredTypes.find(*parent) == desiredTypes.end())
            parent.reset();
    }
    if (desiredTypes == uiTypes_ && desiredParents == uiParents_)
        return Result<void>::success();

    RuntimeUI rebuilt;
    auto scaled = rebuilt.setScale(config_.uiScale);
    if (!scaled)
        return scaled;
    std::map<EntityGuid, UIElementId> mapped;
    std::set<EntityGuid> remaining;
    for (const auto& [id, type] : desiredTypes) {
        static_cast<void>(type);
        remaining.insert(id);
    }
    while (!remaining.empty()) {
        bool progressed = false;
        for (auto iterator = remaining.begin(); iterator != remaining.end();) {
            const auto id = *iterator;
            const auto parent = desiredParents[id];
            if (parent && mapped.find(*parent) == mapped.end()) {
                ++iterator;
                continue;
            }
            auto* uiTransform = component(*entities[id], "UITransform");
            auto properties = reflectedUIProperties(*uiTransform);
            if (!properties)
                return Result<void>::failure(properties.error());
            auto added = rebuilt.addWidget(desiredTypes[id],
                                           parent ? std::optional<UIElementId>(mapped[*parent])
                                                  : std::nullopt,
                                           properties.value());
            if (!added)
                return Result<void>::failure(added.error());
            mapped.emplace(id, added.value());
            iterator = remaining.erase(iterator);
            progressed = true;
        }
        if (!progressed) {
            return Result<void>::failure(
                Error(ErrorCode::CycleDetected, "runtime UI hierarchy could not be resolved"));
        }
    }
    ui_ = std::move(rebuilt);
    uiByEntity_ = std::move(mapped);
    uiTypes_ = std::move(desiredTypes);
    uiParents_ = std::move(desiredParents);
    processedUIEvents_ = 0U;
    return updateUI();
}

Result<void> SceneRuntime::updateAnimators(float deltaSeconds) {
    for (auto& [entityId, binding] : animators_) {
        auto* entity = scene_->findEntity(entityId);
        if (entity == nullptr)
            continue;
        auto speed = property<double>(*binding->component, "speed");
        auto playing = property<bool>(*binding->component, "playing");
        if (!speed)
            return Result<void>::failure(speed.error());
        if (!playing)
            return Result<void>::failure(playing.error());
        if (!std::isfinite(speed.value()) || speed.value() < 0.0) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "Animator speed is invalid")
                    .addContext("entity", entityId.toString()));
        }
        if (!playing.value())
            continue;
        auto frame = binding->controller->update(deltaSeconds * static_cast<float>(speed.value()));
        if (!frame)
            return Result<void>::failure(frame.error().withContext("entity", entityId.toString()));
        binding->lastFrame = std::move(frame).value();
        auto applied = applyAnimationValues(*entity, *binding->lastFrame);
        if (!applied)
            return applied;
    }
    return Result<void>::success();
}

Result<void> SceneRuntime::updateParticleEmitters(float deltaSeconds) {
    for (auto& [entityId, binding] : particleEmitters_) {
        auto* entity = scene_->findEntity(entityId);
        if (entity == nullptr)
            continue;
        auto reflected = reflectedParticleSettings(*binding->component);
        if (!reflected)
            return Result<void>::failure(
                reflected.error().withContext("entity", entityId.toString()));
        auto configured = binding->emitter->setSettings(reflected.value().settings);
        if (!configured)
            return Result<void>::failure(
                configured.error().withContext("entity", entityId.toString()));
        const auto position = entity->transform().localPosition();
        binding->emitter->setPosition({position.x, position.y});
        auto emitted = binding->emitter->update(deltaSeconds);
        if (!emitted)
            return Result<void>::failure(
                emitted.error().withContext("entity", entityId.toString()));
    }
    return Result<void>::success();
}

Result<void> SceneRuntime::updateNavigationAgents(float deltaSeconds) {
    for (auto& [entityId, binding] : navigationAgents_) {
        auto* entity = scene_->findEntity(entityId);
        if (entity == nullptr)
            continue;
        auto speed = property<double>(*binding->component, "speed");
        auto targetId = property<EntityGuid>(*binding->component, "target");
        auto arrival = property<double>(*binding->component, "arrivalRadius");
        auto requireSight = property<bool>(*binding->component, "requireLineOfSight");
        auto obstacleMask = property<std::uint64_t>(*binding->component, "obstacleMask");
        if (!speed)
            return Result<void>::failure(speed.error());
        if (!targetId)
            return Result<void>::failure(targetId.error());
        if (!arrival)
            return Result<void>::failure(arrival.error());
        if (!requireSight)
            return Result<void>::failure(requireSight.error());
        if (!obstacleMask)
            return Result<void>::failure(obstacleMask.error());
        if (!std::isfinite(speed.value()) || speed.value() <= 0.0 ||
            !std::isfinite(arrival.value()) || arrival.value() < 0.0 ||
            obstacleMask.value() > std::numeric_limits<std::uint32_t>::max()) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "NavigationAgent settings are invalid")
                    .addContext("entity", entityId.toString()));
        }

        auto* target = targetId.value().isNil() ? nullptr : scene_->findEntity(targetId.value());
        const auto current3 = entity->transform().localPosition();
        const Vec2 current{current3.x, current3.y};
        Vec2 targetPosition{};
        bool visible = false;
        if (target != nullptr && target->active()) {
            const auto target3 = target->transform().localPosition();
            targetPosition = {target3.x, target3.y};
            visible = true;
            if (requireSight.value() && distance(current, targetPosition) > 0.00001F) {
                auto sight = hasLineOfSight(physics_, current, targetPosition,
                                            static_cast<std::uint32_t>(obstacleMask.value()));
                if (!sight)
                    return Result<void>::failure(sight.error());
                visible = sight.value();
            }
        }
        binding->shouldMove =
            visible && distance(current, targetPosition) > static_cast<float>(arrival.value());
        auto state = binding->stateMachine.update(deltaSeconds);
        if (!state)
            return Result<void>::failure(state.error().withContext("entity", entityId.toString()));
        if (binding->stateMachine.currentState() != "move")
            continue;

        if (!binding->followerConfigured || binding->follower.finished() ||
            binding->configuredTarget != targetId.value() ||
            binding->configuredPosition != targetPosition ||
            binding->configuredSpeed != static_cast<float>(speed.value()) ||
            binding->configuredArrivalRadius != static_cast<float>(arrival.value())) {
            WaypointFollowerSettings settings;
            settings.speed = static_cast<float>(speed.value());
            settings.arrivalRadius = static_cast<float>(arrival.value());
            auto configured = binding->follower.configure({targetPosition}, settings);
            if (!configured)
                return Result<void>::failure(configured.error());
            binding->configuredTarget = targetId.value();
            binding->configuredPosition = targetPosition;
            binding->configuredSpeed = settings.speed;
            binding->configuredArrivalRadius = settings.arrivalRadius;
            binding->followerConfigured = true;
        }
        auto moved = binding->follower.update(current, deltaSeconds);
        if (!moved)
            return Result<void>::failure(moved.error());
        auto position = entity->transform().localPosition();
        position.x = moved.value().position.x;
        position.y = moved.value().position.y;
        entity->transform().setLocalPosition(position);
    }
    return Result<void>::success();
}

Result<void> SceneRuntime::updateUI() {
    const auto& events = ui_.events();
    if (processedUIEvents_ > events.size())
        processedUIEvents_ = 0U;
    for (std::size_t index = processedUIEvents_; index < events.size(); ++index) {
        const auto& event = events[index];
        const auto mapped =
            std::find_if(uiByEntity_.begin(), uiByEntity_.end(),
                         [&event](const auto& pair) { return pair.second == event.target; });
        if (mapped == uiByEntity_.end())
            continue;
        auto* entity = scene_->findEntity(mapped->first);
        auto* transform = entity == nullptr ? nullptr : component(*entity, "UITransform");
        if (transform == nullptr)
            continue;
        Result<void> synchronized = Result<void>::success();
        if (event.type == UIEventType::Toggled) {
            synchronized = transform->set("checked", PropertyValue(event.value > 0.5F));
        } else if (event.type == UIEventType::ValueChanged) {
            synchronized = transform->set("value", PropertyValue(static_cast<double>(event.value)));
        } else if (event.type == UIEventType::SelectionChanged && event.item) {
            synchronized = transform->set("selectedIndex",
                                          PropertyValue(static_cast<std::int64_t>(*event.item)));
        }
        if (!synchronized)
            return synchronized;
    }
    processedUIEvents_ = events.size();

    for (const auto& [entityId, elementId] : uiByEntity_) {
        auto* entity = scene_->findEntity(entityId);
        if (entity == nullptr)
            continue;
        auto* transform = component(*entity, "UITransform");
        auto* widget = ui_.widget(elementId);
        if (transform == nullptr || widget == nullptr)
            continue;
        auto properties = reflectedUIProperties(*transform);
        if (!properties)
            return Result<void>::failure(properties.error());
        const auto type = uiTypes_[entityId];
        properties.value().focusable = type == UIWidgetType::Button ||
                                       type == UIWidgetType::Toggle ||
                                       type == UIWidgetType::Slider || type == UIWidgetType::List;

        if (auto* button = component(*entity, "UIButton")) {
            auto enabled = runtimeEnabled(*button);
            auto interactable = property<bool>(*button, "interactable");
            if (!enabled)
                return Result<void>::failure(enabled.error());
            if (!interactable)
                return Result<void>::failure(interactable.error());
            properties.value().enabled = enabled.value() && interactable.value();
        }
        auto appliedProperties = ui_.model().setProperties(elementId, properties.value());
        if (!appliedProperties)
            return appliedProperties;

        auto text = property<std::string>(*transform, "text");
        auto minimum = property<double>(*transform, "minimum");
        auto maximum = property<double>(*transform, "maximum");
        auto value = property<double>(*transform, "value");
        auto step = property<double>(*transform, "step");
        auto checked = property<bool>(*transform, "checked");
        auto items = property<std::string>(*transform, "items");
        auto selected = property<std::int64_t>(*transform, "selectedIndex");
        auto direction = property<std::int64_t>(*transform, "layoutDirection");
        auto spacing = property<double>(*transform, "layoutSpacing");
        auto padding = property<double>(*transform, "layoutPadding");
        if (!text)
            return Result<void>::failure(text.error());
        if (!minimum)
            return Result<void>::failure(minimum.error());
        if (!maximum)
            return Result<void>::failure(maximum.error());
        if (!value)
            return Result<void>::failure(value.error());
        if (!step)
            return Result<void>::failure(step.error());
        if (!checked)
            return Result<void>::failure(checked.error());
        if (!items)
            return Result<void>::failure(items.error());
        if (!selected)
            return Result<void>::failure(selected.error());
        if (!direction)
            return Result<void>::failure(direction.error());
        if (!spacing)
            return Result<void>::failure(spacing.error());
        if (!padding)
            return Result<void>::failure(padding.error());
        if (direction.value() < 0 || direction.value() > 1)
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "UI layout direction is invalid"));

        widget->text = text.value();
        widget->checked = checked.value();
        widget->items = splitItems(items.value());
        widget->selectedItem =
            selected.value() >= 0 && static_cast<std::uint64_t>(selected.value()) <
                                         static_cast<std::uint64_t>(widget->items.size())
                ? std::optional<std::size_t>(static_cast<std::size_t>(selected.value()))
                : std::nullopt;
        widget->layoutDirection =
            direction.value() == 0 ? UILayoutDirection::Horizontal : UILayoutDirection::Vertical;
        widget->layoutSpacing = static_cast<float>(spacing.value());
        widget->layoutPadding = static_cast<float>(padding.value());
        if (type == UIWidgetType::Slider || type == UIWidgetType::Progress) {
            auto ranged =
                ui_.setRange(elementId, static_cast<float>(minimum.value()),
                             static_cast<float>(maximum.value()), static_cast<float>(step.value()));
            if (!ranged)
                return ranged;
            ranged = ui_.setValue(elementId, static_cast<float>(value.value()));
            if (!ranged)
                return ranged;
        }
        if (auto* textComponent = component(*entity, "UIText")) {
            auto enabled = runtimeEnabled(*textComponent);
            if (!enabled)
                return Result<void>::failure(enabled.error());
            if (enabled.value()) {
                auto componentText = property<std::string>(*textComponent, "text");
                if (!componentText)
                    return Result<void>::failure(componentText.error());
                widget->text = componentText.value();
            }
        }
        if (auto* image = component(*entity, "UIImage")) {
            auto enabled = runtimeEnabled(*image);
            if (!enabled)
                return Result<void>::failure(enabled.error());
            if (enabled.value()) {
                auto asset = property<AssetGuid>(*image, "image");
                if (!asset)
                    return Result<void>::failure(asset.error());
                widget->imageAsset = asset.value();
            }
        }
    }
    return ui_.layout(config_.uiViewport);
}

Result<void> SceneRuntime::reconcileVisualScripts() {
    std::set<EntityGuid> desired;
    const auto& registry = VisualNodeRegistry::builtins();
    const auto configuredAssetResolver = config_.visualReferences.assetExists;
    const auto configuredEntityResolver = config_.visualReferences.entityExists;
    const auto configuredComponentResolver = config_.visualReferences.componentExists;
    VisualReferenceResolver references;
    references.assetExists = configuredAssetResolver;
    references.entityExists = [this, configuredEntityResolver](const EntityGuid entity) {
        return scene_->findEntity(entity) != nullptr &&
               (!configuredEntityResolver || configuredEntityResolver(entity));
    };
    references.componentExists = [this, configuredComponentResolver](const ComponentTypeGuid type) {
        auto exists = false;
        for (const auto* entity : scene_->entities()) {
            if (entity->getComponent(type) != nullptr) {
                exists = true;
                break;
            }
        }
        return exists && (!configuredComponentResolver || configuredComponentResolver(type));
    };

    for (auto* entity : scene_->entities()) {
        auto* visual = component(*entity, "VisualScriptComponent");
        if (visual == nullptr || !entity->active())
            continue;
        auto enabled = runtimeEnabled(*visual);
        if (!enabled)
            return Result<void>::failure(enabled.error());
        if (!enabled.value())
            continue;
        if (desired.size() >= config_.maximumVisualScripts) {
            return Result<void>::failure(
                Error(ErrorCode::CapacityExceeded,
                      "scene exceeds the configured visual-script component limit")
                    .addContext("maximum", std::to_string(config_.maximumVisualScripts)));
        }
        desired.insert(entity->id());

        auto graphAsset = property<AssetGuid>(*visual, "graph");
        if (!graphAsset)
            return Result<void>::failure(graphAsset.error());
        if (graphAsset.value().isNil()) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "visual graph asset is not assigned")
                    .addContext("entity", entity->id().toString()));
        }
        const auto existing = visualScripts_.find(entity->id());
        if (existing != visualScripts_.end() && existing->second->component == visual &&
            existing->second->graphAsset == graphAsset.value()) {
            continue;
        }
        if (!config_.visualGraphSourceResolver) {
            return Result<void>::failure(visualRuntimeError(
                Error(ErrorCode::InvalidState, "scene runtime has no visual graph source resolver"),
                entity->id(), graphAsset.value(), "resolve"));
        }
        auto source = config_.visualGraphSourceResolver(graphAsset.value());
        if (!source) {
            return Result<void>::failure(
                visualRuntimeError(source.error(), entity->id(), graphAsset.value(), "resolve"));
        }
        auto graph = deserializeVisualGraph(source.value());
        if (!graph) {
            return Result<void>::failure(
                visualRuntimeError(graph.error(), entity->id(), graphAsset.value(), "deserialize"));
        }
        if (graph.value().guid() != graphAsset.value()) {
            return Result<void>::failure(visualRuntimeError(
                Error(ErrorCode::InvalidFormat,
                      "resolved visual graph GUID does not match the component asset GUID")
                    .addContext("resolved_graph_guid", graph.value().guid().toString()),
                entity->id(), graphAsset.value(), "identity"));
        }

        const auto report = VisualGraphValidator::validate(graph.value(), references, registry,
                                                           &config_.visualHostCallbacks);
        if (report.hasErrors()) {
            Error error(ErrorCode::InvalidFormat, "visual graph validation failed");
            for (const auto& issue : report.issues) {
                if (issue.severity != VisualIssueSeverity::Error)
                    continue;
                error.addContext("visual_issue", issue.message)
                    .addContext("visual_issue_node", std::to_string(issue.node))
                    .addContext("visual_issue_code", std::to_string(static_cast<int>(issue.code)));
                break;
            }
            return Result<void>::failure(
                visualRuntimeError(std::move(error), entity->id(), graphAsset.value(), "validate"));
        }

        auto binding = std::make_unique<VisualScriptBinding>();
        binding->component = visual;
        binding->graphAsset = graphAsset.value();
        for (const auto& pair : graph.value().nodes()) {
            const auto* definition = pair.second.builtinType == VisualBuiltinNodeType::Legacy
                                         ? registry.findLegacy(pair.second.kind)
                                         : registry.find(pair.second.builtinType);
            if (definition == nullptr || definition->execution != VisualExecutionKind::Entry)
                continue;
            const auto event = visualEventForNode(pair.second);
            if (!event)
                continue;
            if (binding->programs.size() >= config_.maximumVisualEventProgramsPerScript) {
                return Result<void>::failure(visualRuntimeError(
                    Error(ErrorCode::CapacityExceeded,
                          "visual graph exceeds the configured event-program limit")
                        .addContext("maximum",
                                    std::to_string(config_.maximumVisualEventProgramsPerScript)),
                    entity->id(), graphAsset.value(), "compile"));
            }
            auto eventGraph = graph.value();
            eventGraph.setEntryNode(pair.first);
            auto bytecode =
                VisualGraphCompiler::compile(eventGraph, references, config_.visualCompileLimits,
                                             registry, &config_.visualHostCallbacks);
            if (!bytecode) {
                return Result<void>::failure(visualRuntimeError(
                    bytecode.error()
                        .withContext("visual_entry_node", std::to_string(pair.first))
                        .withContext("visual_event", visualEventName(*event)),
                    entity->id(), graphAsset.value(), "compile"));
            }
            VisualScriptBinding::Program program;
            program.event = *event;
            program.entryNode = pair.first;
            program.bytecode = std::move(bytecode).value();
            binding->programs.push_back(std::move(program));
        }
        if (binding->programs.empty()) {
            return Result<void>::failure(
                visualRuntimeError(Error(ErrorCode::InvalidFormat,
                                         "visual graph contains no supported runtime event entry"),
                                   entity->id(), graphAsset.value(), "compile"));
        }
        visualScripts_[entity->id()] = std::move(binding);
    }

    for (auto iterator = visualScripts_.begin(); iterator != visualScripts_.end();) {
        if (desired.find(iterator->first) == desired.end())
            iterator = visualScripts_.erase(iterator);
        else
            ++iterator;
    }
    return Result<void>::success();
}

Result<void> SceneRuntime::executeVisualEvent(const EntityGuid entity,
                                              const VisualRuntimeEvent event,
                                              const float deltaSeconds,
                                              const std::optional<EntityGuid> other,
                                              std::size_t& remainingInstructions) {
    const auto found = visualScripts_.find(entity);
    if (found == visualScripts_.end())
        return Result<void>::success();
    auto& binding = *found->second;
    VisualBytecodeVm vm;
    for (auto& program : binding.programs) {
        if (program.event != event || program.pending)
            continue;
        if (remainingInstructions == 0U) {
            return Result<void>::failure(visualRuntimeError(
                Error(ErrorCode::CapacityExceeded,
                      "visual-script phase instruction budget is exhausted")
                    .addContext("visual_event", visualEventName(event))
                    .addContext("visual_entry_node", std::to_string(program.entryNode)),
                entity, binding.graphAsset, "execute"));
        }

        binding.variables["runtime.delta_seconds"] = static_cast<double>(deltaSeconds);
        binding.variables["runtime.event"] = static_cast<double>(event);
        binding.variables["runtime.has_other_entity"] = other ? 1.0 : 0.0;
        for (auto index = std::size_t{0}; index < 16U; ++index) {
            binding.variables["runtime.other_entity_" + std::to_string(index)] =
                other ? static_cast<double>(other->bytes()[index]) : 0.0;
        }
        const auto invocationBudget =
            std::min(config_.maximumVisualInstructionsPerInvocation, remainingInstructions);
        VisualHostExecutionContext context;
        context.event = event;
        context.ownerEntity = entity;
        context.otherEntity = other;
        context.deltaSeconds = static_cast<double>(deltaSeconds);
        auto executed = vm.execute(program.bytecode, binding.variables, invocationBudget,
                                   &config_.visualHostCallbacks, context);
        if (!executed) {
            return Result<void>::failure(visualRuntimeError(
                executed.error()
                    .withContext("visual_event", visualEventName(event))
                    .withContext("visual_entry_node", std::to_string(program.entryNode))
                    .withContext("visual_instruction_budget", std::to_string(invocationBudget)),
                entity, binding.graphAsset, "execute"));
        }
        auto execution = std::move(executed).value();
        remainingInstructions -= execution.executedInstructions;
        binding.variables = std::move(execution.variables);
        if (execution.completed) {
            binding.lastReturnValue = execution.returnValue;
        } else {
            if (!execution.continuation) {
                return Result<void>::failure(visualRuntimeError(
                    Error(ErrorCode::InternalError,
                          "visual VM suspended without a continuation"),
                    entity, binding.graphAsset, "execute"));
            }
            VisualScriptBinding::PendingExecution pending;
            pending.continuation = std::move(*execution.continuation);
            pending.context = context;
            pending.remainingSeconds = execution.delaySeconds;
            program.pending = std::move(pending);
        }
    }
    return Result<void>::success();
}

Result<void> SceneRuntime::advanceVisualDelays(const float deltaSeconds,
                                               std::size_t& remainingInstructions) {
    VisualBytecodeVm vm;
    for (auto& [entityId, bindingPointer] : visualScripts_) {
        const auto* entity = scene_->findEntity(entityId);
        if (entity == nullptr || !entity->active())
            continue;
        auto& binding = *bindingPointer;
        for (auto& program : binding.programs) {
            if (!program.pending)
                continue;
            program.pending->remainingSeconds -= static_cast<double>(deltaSeconds);
            if (program.pending->remainingSeconds > 0.0)
                continue;
            if (remainingInstructions == 0U) {
                return Result<void>::failure(visualRuntimeError(
                    Error(ErrorCode::CapacityExceeded,
                          "visual-script phase instruction budget is exhausted while resuming")
                        .addContext("visual_event", visualEventName(program.event))
                        .addContext("visual_entry_node", std::to_string(program.entryNode)),
                    entityId, binding.graphAsset, "resume"));
            }
            const auto invocationBudget =
                std::min(config_.maximumVisualInstructionsPerInvocation, remainingInstructions);
            auto context = program.pending->context;
            context.deltaSeconds = static_cast<double>(deltaSeconds);
            auto continuation = std::move(program.pending->continuation);
            auto resumed = vm.resume(program.bytecode, std::move(continuation), binding.variables,
                                     invocationBudget, &config_.visualHostCallbacks, context);
            if (!resumed) {
                return Result<void>::failure(visualRuntimeError(
                    resumed.error()
                        .withContext("visual_event", visualEventName(program.event))
                        .withContext("visual_entry_node", std::to_string(program.entryNode))
                        .withContext("visual_instruction_budget", std::to_string(invocationBudget)),
                    entityId, binding.graphAsset, "resume"));
            }
            auto execution = std::move(resumed).value();
            remainingInstructions -= execution.executedInstructions;
            binding.variables = std::move(execution.variables);
            if (execution.completed) {
                binding.lastReturnValue = execution.returnValue;
                program.pending.reset();
            } else {
                if (!execution.continuation) {
                    return Result<void>::failure(visualRuntimeError(
                        Error(ErrorCode::InternalError,
                              "visual VM re-suspended without a continuation"),
                        entityId, binding.graphAsset, "resume"));
                }
                program.pending->continuation = std::move(*execution.continuation);
                program.pending->context = context;
                program.pending->remainingSeconds = execution.delaySeconds;
            }
        }
    }
    return Result<void>::success();
}

Result<void> SceneRuntime::runPendingVisualStarts(std::size_t& remainingInstructions) {
    for (auto& pair : visualScripts_) {
        if (!pair.second->startPending)
            continue;
        auto executed = executeVisualEvent(pair.first, VisualRuntimeEvent::Start, 0.0F,
                                           std::nullopt, remainingInstructions);
        if (!executed)
            return executed;
        pair.second->startPending = false;
    }
    return Result<void>::success();
}

Result<void> SceneRuntime::executeVisualPhase(const VisualRuntimeEvent event,
                                              const float deltaSeconds,
                                              std::size_t& remainingInstructions) {
    for (const auto& pair : visualScripts_) {
        const auto* entity = scene_->findEntity(pair.first);
        if (entity == nullptr || !entity->active())
            continue;
        auto executed = executeVisualEvent(pair.first, event, deltaSeconds, std::nullopt,
                                           remainingInstructions);
        if (!executed)
            return executed;
    }
    return Result<void>::success();
}

Result<void> SceneRuntime::update(float deltaSeconds) {
    if (!initialized_) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "scene runtime is not initialized"));
    }
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0F) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "scene runtime update delta is invalid"));
    }
    auto result = reconcileRuntimeSystems();
    if (!result)
        return result;
    result = reconcileVisualScripts();
    if (!result)
        return result;
    auto remainingInstructions = config_.maximumVisualInstructionsPerPhase;
    result = advanceVisualDelays(deltaSeconds, remainingInstructions);
    if (!result)
        return result;
    result = runPendingVisualStarts(remainingInstructions);
    if (!result)
        return result;
    result = executeVisualPhase(VisualRuntimeEvent::Update, deltaSeconds, remainingInstructions);
    if (!result)
        return result;
    result = updateAnimators(deltaSeconds);
    if (!result)
        return result;
    result = updateNavigationAgents(deltaSeconds);
    if (!result)
        return result;
    result = updateParticleEmitters(deltaSeconds);
    if (!result)
        return result;
    return updateUI();
}

Result<void> SceneRuntime::lateUpdate(const float deltaSeconds) {
    if (!initialized_) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "scene runtime is not initialized"));
    }
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0F) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "scene runtime late-update delta is invalid"));
    }
    auto reconciled = reconcileVisualScripts();
    if (!reconciled)
        return reconciled;
    auto remainingInstructions = config_.maximumVisualInstructionsPerPhase;
    reconciled = runPendingVisualStarts(remainingInstructions);
    if (!reconciled)
        return reconciled;
    return executeVisualPhase(VisualRuntimeEvent::LateUpdate, deltaSeconds, remainingInstructions);
}

Result<void> SceneRuntime::fixedUpdate(float deltaSeconds) {
    if (!initialized_) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "scene runtime is not initialized"));
    }
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0F) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "scene runtime fixed-update delta is invalid"));
    }
    visualFixedInstructionsRemaining_ = config_.maximumVisualInstructionsPerPhase;
    visualFixedDeltaSeconds_ = deltaSeconds;
    auto reconciled = reconcileVisualScripts();
    if (!reconciled)
        return reconciled;
    reconciled = runPendingVisualStarts(visualFixedInstructionsRemaining_);
    if (!reconciled)
        return reconciled;
    reconciled = executeVisualPhase(VisualRuntimeEvent::FixedUpdate, deltaSeconds,
                                    visualFixedInstructionsRemaining_);
    if (!reconciled)
        return reconciled;
    reconciled = reconcileBodies();
    if (!reconciled)
        return reconciled;

    for (const auto& binding : bodyByEntity_) {
        auto* entity = scene_->findEntity(binding.first);
        auto* body = physics_.findBody(binding.second);
        if (entity == nullptr || body == nullptr)
            continue;
        auto* collider = component(*entity, "Collider2D");
        if (collider == nullptr)
            continue;
        auto updated = updateBodyFromComponents(*entity, *body, *collider,
                                                component(*entity, "Rigidbody2D"), true);
        if (!updated)
            return updated;
    }

    auto stepped = physics_.step(deltaSeconds);
    if (!stepped)
        return stepped;

    std::set<ContactPair> currentContacts;
    for (const auto& contact : physics_.contacts()) {
        const auto first = entityByBody_.find(contact.first.value);
        const auto second = entityByBody_.find(contact.second.value);
        if (first == entityByBody_.end() || second == entityByBody_.end())
            continue;
        ContactPair pair{first->second, second->second, contact.trigger};
        if (pair.second < pair.first)
            std::swap(pair.first, pair.second);
        currentContacts.insert(pair);
        const bool staying = activeContacts_.find(pair) != activeContacts_.end();
        auto dispatched = dispatchContact(pair, !staying, staying);
        if (!dispatched)
            return dispatched;
    }
    for (const auto& previous : activeContacts_) {
        if (currentContacts.find(previous) == currentContacts.end()) {
            auto dispatched = dispatchContact(previous, false, false);
            if (!dispatched)
                return dispatched;
        }
    }
    activeContacts_ = std::move(currentContacts);

    for (const auto& binding : bodyByEntity_) {
        auto* entity = scene_->findEntity(binding.first);
        auto* body = physics_.findBody(binding.second);
        if (entity == nullptr || body == nullptr)
            continue;
        auto* collider = component(*entity, "Collider2D");
        if (collider == nullptr)
            continue;
        auto updated = updateBodyFromComponents(*entity, *body, *collider,
                                                component(*entity, "Rigidbody2D"), false);
        if (!updated)
            return updated;
    }
    return Result<void>::success();
}

void SceneRuntime::shutdown() {
    animators_.clear();
    particleEmitters_.clear();
    navigationAgents_.clear();
    visualScripts_.clear();
    ui_ = RuntimeUI{};
    uiByEntity_.clear();
    uiTypes_.clear();
    uiParents_.clear();
    processedUIEvents_ = 0U;
    visualFixedInstructionsRemaining_ = 0U;
    visualFixedDeltaSeconds_ = 0.0F;
    activeContacts_.clear();
    bodyByEntity_.clear();
    entityByBody_.clear();
    physics_ = PhysicsWorld2D{};
    initialized_ = false;
}

} // namespace fabgl
