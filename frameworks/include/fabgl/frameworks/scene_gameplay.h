#pragma once

#include <fabgl/core/guid.h>
#include <fabgl/core/result.h>
#include <fabgl/frameworks/platformer.h>
#include <fabgl/frameworks/racer.h>
#include <fabgl/frameworks/top_down.h>
#include <fabgl/frameworks/tps.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>

namespace fabgl {
class InputMap;
class Scene;
} // namespace fabgl

namespace fabgl::frameworks {

struct SceneGameplayLimits final {
    std::size_t maximumControlledEntities = 256U;
    float maximumDeltaSeconds = 0.1F;
};

// Stable, renderer-independent evidence that the project-backed framework layer actually advanced.
// The PC player exposes these counters in its bounded headless result line so examples can assert
// gameplay behavior instead of merely checking that a controller component was serialized.
struct SceneGameplayStats final {
    std::uint64_t updates = 0U;

    std::uint32_t platformerCollectibles = 0U;
    std::uint32_t platformerDamageEvents = 0U;
    std::uint32_t platformerCheckpoints = 0U;
    std::uint32_t platformerTransitions = 0U;
    int platformerHealth = 0;

    std::uint32_t topDownShots = 0U;
    std::uint32_t topDownHits = 0U;
    std::uint32_t topDownPickups = 0U;
    std::uint32_t topDownEnemies = 0U;
    std::uint32_t topDownRoomTransitions = 0U;

    std::uint32_t fpsShots = 0U;
    std::uint32_t fpsHits = 0U;
    std::uint32_t fpsDoorActivations = 0U;
    std::uint32_t fpsPickups = 0U;
    std::uint32_t fpsKeys = 0U;
    std::uint32_t fpsSecrets = 0U;

    std::uint32_t tpsShots = 0U;
    std::uint32_t tpsHits = 0U;
    std::uint32_t tpsPickups = 0U;
    std::uint32_t tpsTargets = 0U;

    std::uint32_t racerOpponents = 0U;
    std::uint32_t racerCheckpointCrossings = 0U;
    std::uint16_t racerPosition = 0U;
    int racerLap = 0;
    int racerGear = 0;
    std::uint32_t racerSpeedKph = 0U;
    int racerCountdown = 0;
    bool racerFinished = false;
};

// Runs the built-in controller components against the project's InputMap. It
// is platform-neutral and intentionally owns only transient controller state;
// authoritative positions remain in the serialized Scene transforms.
class SceneGameplayRuntime final {
  public:
    explicit SceneGameplayRuntime(Scene& scene, SceneGameplayLimits limits = {});
    ~SceneGameplayRuntime();

    SceneGameplayRuntime(const SceneGameplayRuntime&) = delete;
    SceneGameplayRuntime& operator=(const SceneGameplayRuntime&) = delete;
    SceneGameplayRuntime(SceneGameplayRuntime&&) = delete;
    SceneGameplayRuntime& operator=(SceneGameplayRuntime&&) = delete;

    [[nodiscard]] Result<void> initialize();
    [[nodiscard]] Result<void> update(InputMap& input, float deltaSeconds);
    void shutdown() noexcept;

    [[nodiscard]] bool initialized() const noexcept {
        return initialized_;
    }
    [[nodiscard]] std::size_t controlledEntityCount() const noexcept;
    [[nodiscard]] const SceneGameplayStats& stats() const noexcept {
        return stats_;
    }

  private:
    struct State;

    Scene* scene_ = nullptr;
    SceneGameplayLimits limits_;
    std::unique_ptr<State> state_;
    SceneGameplayStats stats_;
    bool initialized_ = false;
};

} // namespace fabgl::frameworks
