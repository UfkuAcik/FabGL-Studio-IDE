#pragma once

#include "fabgl/animation/animation.h"
#include "fabgl/core/guid.h"
#include "fabgl/core/result.h"
#include "fabgl/math/types.h"
#include "fabgl/navigation/ai_behaviors.h"
#include "fabgl/particles/particle_system.h"
#include "fabgl/physics/physics2d.h"
#include "fabgl/ui/runtime_widgets.h"
#include "fabgl/visual/visual_graph.h"

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace fabgl {

class DataComponent;
class Entity;
class Scene;

struct SceneRuntimeConfig final {
    using AnimatorFactory =
        std::function<Result<std::unique_ptr<AnimatorController>>(AssetGuid controller)>;
    using VisualGraphSourceResolver = std::function<Result<std::string>(AssetGuid graph)>;

    SceneRuntimeConfig() = default;
    SceneRuntimeConfig(Vec2 gravityValue) : gravity(gravityValue) {}

    Vec2 gravity{0.0F, 9.81F};
    AnimatorFactory animatorFactory;
    VisualGraphSourceResolver visualGraphSourceResolver;
    VisualHostCallbackTable visualHostCallbacks;
    VisualReferenceResolver visualReferences;
    VisualCompileLimits visualCompileLimits;
    std::size_t maximumVisualScripts = 128U;
    std::size_t maximumVisualEventProgramsPerScript = 16U;
    std::size_t maximumVisualInstructionsPerInvocation = VisualMaximumCompiledInstructions;
    std::size_t maximumVisualInstructionsPerPhase = 32U * 1024U;
    Rect uiViewport{0.0F, 0.0F, 320.0F, 200.0F};
    float uiScale = 1.0F;
};

inline constexpr std::size_t SceneRuntimeMaximumVisualScripts = 256U;
inline constexpr std::size_t SceneRuntimeMaximumVisualEventProgramsPerScript = 32U;
inline constexpr std::size_t SceneRuntimeMaximumVisualInstructionsPerPhase = 256U * 1024U;

// Connects reflected scene components to deterministic transient runtime systems. Scene data
// remains serializable and platform-neutral; runtime objects are created on initialize and
// destroyed on shutdown.
class SceneRuntime final {
  public:
    explicit SceneRuntime(Scene& scene, SceneRuntimeConfig config = {});
    ~SceneRuntime();

    SceneRuntime(const SceneRuntime&) = delete;
    SceneRuntime& operator=(const SceneRuntime&) = delete;

    [[nodiscard]] Result<void> initialize();
    [[nodiscard]] Result<void> fixedUpdate(float deltaSeconds);
    [[nodiscard]] Result<void> update(float deltaSeconds);
    [[nodiscard]] Result<void> lateUpdate(float deltaSeconds);
    void shutdown();

    [[nodiscard]] bool initialized() const noexcept {
        return initialized_;
    }
    [[nodiscard]] std::size_t physicsBodyCount() const noexcept {
        return bodyByEntity_.size();
    }
    [[nodiscard]] std::size_t activeContactCount() const noexcept {
        return activeContacts_.size();
    }
    [[nodiscard]] std::size_t animatorCount() const noexcept {
        return animators_.size();
    }
    [[nodiscard]] std::size_t particleEmitterCount() const noexcept {
        return particleEmitters_.size();
    }
    [[nodiscard]] std::size_t navigationAgentCount() const noexcept {
        return navigationAgents_.size();
    }
    [[nodiscard]] std::size_t uiElementCount() const noexcept {
        return uiByEntity_.size();
    }
    [[nodiscard]] std::size_t visualScriptCount() const noexcept {
        return visualScripts_.size();
    }
    [[nodiscard]] std::optional<double> visualVariable(EntityGuid entity,
                                                       std::string_view name) const;
    [[nodiscard]] std::optional<PhysicsBodyId> bodyFor(EntityGuid entity) const noexcept;
    [[nodiscard]] AnimatorController* animatorFor(EntityGuid entity) noexcept;
    [[nodiscard]] const AnimatorController* animatorFor(EntityGuid entity) const noexcept;
    [[nodiscard]] const AnimationFrame* animationFrameFor(EntityGuid entity) const noexcept;
    [[nodiscard]] ParticleSystem* particlesFor(EntityGuid entity) noexcept;
    [[nodiscard]] const ParticleSystem* particlesFor(EntityGuid entity) const noexcept;
    [[nodiscard]] ParticleEmitterStats particleStatsFor(EntityGuid entity) const noexcept;
    [[nodiscard]] std::string navigationStateFor(EntityGuid entity) const;
    [[nodiscard]] std::optional<UIElementId> uiElementFor(EntityGuid entity) const noexcept;
    [[nodiscard]] RuntimeUI& ui() noexcept {
        return ui_;
    }
    [[nodiscard]] const RuntimeUI& ui() const noexcept {
        return ui_;
    }
    [[nodiscard]] PhysicsWorld2D& physics() noexcept {
        return physics_;
    }
    [[nodiscard]] const PhysicsWorld2D& physics() const noexcept {
        return physics_;
    }

  private:
    struct ContactPair final {
        EntityGuid first;
        EntityGuid second;
        bool trigger = false;

        friend bool operator<(const ContactPair& lhs, const ContactPair& rhs) noexcept {
            if (lhs.first != rhs.first)
                return lhs.first < rhs.first;
            if (lhs.second != rhs.second)
                return lhs.second < rhs.second;
            return lhs.trigger < rhs.trigger;
        }
    };

    struct AnimatorBinding;
    struct ParticleBinding;
    struct NavigationBinding;
    struct VisualScriptBinding;

    [[nodiscard]] Result<void> reconcileBodies();
    [[nodiscard]] Result<void> addBody(Entity& entity, DataComponent& collider,
                                       DataComponent* rigidbody);
    [[nodiscard]] Result<void> updateBodyFromComponents(Entity& entity, PhysicsBody2D& body,
                                                        DataComponent& collider,
                                                        DataComponent* rigidbody,
                                                        bool beforeSimulation);
    [[nodiscard]] Result<void> dispatchContact(const ContactPair& pair, bool entering,
                                               bool staying);
    [[nodiscard]] Result<void> reconcileRuntimeSystems();
    [[nodiscard]] Result<void> reconcileAnimators();
    [[nodiscard]] Result<void> reconcileParticleEmitters();
    [[nodiscard]] Result<void> reconcileNavigationAgents();
    [[nodiscard]] Result<void> reconcileUI();
    [[nodiscard]] Result<void> reconcileVisualScripts();
    [[nodiscard]] Result<void> runPendingVisualStarts(std::size_t& remainingInstructions);
    [[nodiscard]] Result<void> executeVisualPhase(VisualRuntimeEvent event, float deltaSeconds,
                                                  std::size_t& remainingInstructions);
    [[nodiscard]] Result<void> executeVisualEvent(EntityGuid entity, VisualRuntimeEvent event,
                                                  float deltaSeconds,
                                                  std::optional<EntityGuid> other,
                                                  std::size_t& remainingInstructions);
    [[nodiscard]] Result<void> advanceVisualDelays(float deltaSeconds,
                                                   std::size_t& remainingInstructions);
    [[nodiscard]] Result<void> updateAnimators(float deltaSeconds);
    [[nodiscard]] Result<void> updateParticleEmitters(float deltaSeconds);
    [[nodiscard]] Result<void> updateNavigationAgents(float deltaSeconds);
    [[nodiscard]] Result<void> updateUI();
    [[nodiscard]] DataComponent* component(Entity& entity, const char* shortName) const noexcept;

    Scene* scene_ = nullptr;
    SceneRuntimeConfig config_{};
    PhysicsWorld2D physics_{};
    std::map<EntityGuid, PhysicsBodyId> bodyByEntity_;
    std::map<std::uint32_t, EntityGuid> entityByBody_;
    std::set<ContactPair> activeContacts_;
    std::map<EntityGuid, std::unique_ptr<AnimatorBinding>> animators_;
    std::map<EntityGuid, std::unique_ptr<ParticleBinding>> particleEmitters_;
    std::map<EntityGuid, std::unique_ptr<NavigationBinding>> navigationAgents_;
    std::map<EntityGuid, std::unique_ptr<VisualScriptBinding>> visualScripts_;
    RuntimeUI ui_;
    std::map<EntityGuid, UIElementId> uiByEntity_;
    std::map<EntityGuid, UIWidgetType> uiTypes_;
    std::map<EntityGuid, std::optional<EntityGuid>> uiParents_;
    std::size_t processedUIEvents_ = 0;
    std::size_t visualFixedInstructionsRemaining_ = 0U;
    float visualFixedDeltaSeconds_ = 0.0F;
    bool initialized_ = false;
};

} // namespace fabgl
