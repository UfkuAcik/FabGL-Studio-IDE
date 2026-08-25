#pragma once

#include "fabgl/core/result.h"
#include "fabgl/math/types.h"
#include "fabgl/navigation/grid_navigation.h"
#include "fabgl/physics/physics2d.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fabgl {

struct AiStateCallbacks final {
    std::function<Result<void>()> onEnter;
    std::function<Result<void>(float)> onUpdate;
    std::function<Result<void>()> onExit;
};

struct AiTransition final {
    std::string fromState;
    std::string toState;
    std::function<bool()> condition;
};

class AiStateMachine final {
  public:
    [[nodiscard]] Result<void> addState(std::string name, AiStateCallbacks callbacks = {});
    [[nodiscard]] Result<void> addTransition(AiTransition transition);
    [[nodiscard]] Result<void> transitionTo(std::string_view state);
    [[nodiscard]] Result<void> update(float deltaSeconds);
    [[nodiscard]] const std::string& currentState() const noexcept {
        return currentState_;
    }

  private:
    std::map<std::string, AiStateCallbacks> states_;
    std::vector<AiTransition> transitions_;
    std::string currentState_;
};

struct WaypointFollowerSettings final {
    float speed = 1.0F;
    float arrivalRadius = 0.1F;
    bool loop = false;
};

struct WaypointStep final {
    Vec2 position{};
    Vec2 velocity{};
    std::size_t waypointIndex = 0;
    bool reachedWaypoint = false;
    bool finished = false;
};

class WaypointFollower final {
  public:
    [[nodiscard]] Result<void> configure(std::vector<Vec2> waypoints,
                                         WaypointFollowerSettings settings = {});
    void reset() noexcept {
        waypointIndex_ = 0;
        finished_ = false;
    }
    [[nodiscard]] Result<WaypointStep> update(Vec2 position, float deltaSeconds);
    [[nodiscard]] std::size_t waypointIndex() const noexcept {
        return waypointIndex_;
    }
    [[nodiscard]] bool finished() const noexcept {
        return finished_;
    }

  private:
    std::vector<Vec2> waypoints_;
    WaypointFollowerSettings settings_;
    std::size_t waypointIndex_ = 0;
    bool finished_ = false;
};

enum class AiBehavior2DState : std::uint8_t {
    Idle,
    Patrol,
    Chase,
    Flee,
    Attack,
    Search,
    ReturnToSpawn,
    FollowPath,
};

struct AiBehavior2DSettings final {
    float patrolSpeed = 1.5F;
    float chaseSpeed = 3.0F;
    float fleeSpeed = 3.5F;
    float searchSpeed = 1.5F;
    float returnSpeed = 2.0F;
    float detectionRadius = 8.0F;
    float loseTargetRadius = 12.0F;
    float attackRadius = 1.0F;
    float arrivalRadius = 0.1F;
    float searchSeconds = 2.0F;
};

struct AiBehavior2DInput final {
    Vec2 position{};
    std::optional<Vec2> targetPosition;
    bool targetVisible = false;
    float deltaSeconds = 0.0F;
};

struct AiBehavior2DStep final {
    AiBehavior2DState state = AiBehavior2DState::Idle;
    Vec2 velocity{};
    Vec2 desiredPosition{};
    bool attack = false;
    bool reachedDestination = false;
};

class AiBehaviorController2D final {
  public:
    [[nodiscard]] Result<void> configure(Vec2 spawnPosition,
                                         std::vector<Vec2> patrolPoints = {},
                                         AiBehavior2DSettings settings = {});
    [[nodiscard]] Result<void> setState(AiBehavior2DState state);
    [[nodiscard]] Result<AiBehavior2DStep> update(const AiBehavior2DInput& input);
    [[nodiscard]] AiBehavior2DState state() const noexcept {
        return state_;
    }

  private:
    [[nodiscard]] Vec2 velocityToward(Vec2 from, Vec2 to, float speed) const noexcept;

    Vec2 spawnPosition_{};
    Vec2 lastKnownTarget_{};
    std::vector<Vec2> patrolPoints_;
    AiBehavior2DSettings settings_{};
    AiBehavior2DState state_ = AiBehavior2DState::Idle;
    std::size_t patrolIndex_ = 0U;
    float searchElapsed_ = 0.0F;
    bool configured_ = false;
    bool hasLastKnownTarget_ = false;
};

struct NavigationDoor2D final {
    GridPosition position{};
    bool open = false;
    std::string requiredKey;
};

[[nodiscard]] Result<std::vector<GridPosition>>
findDoorAwarePath(const GridNavigation& navigation, GridPosition start, GridPosition goal,
                  const std::vector<NavigationDoor2D>& doors,
                  const std::set<std::string, std::less<>>& availableKeys = {});

[[nodiscard]] Result<bool> hasLineOfSight(const PhysicsWorld2D& world, Vec2 origin, Vec2 target,
                                          std::uint32_t obstacleLayerMask = 0xFFFFFFFFU);

struct RacingBehaviorSettings final {
    float targetSpeed = 10.0F;
    float arrivalRadius = 1.0F;
    float maximumSteeringRadians = 0.75F;
    float speedResponse = 5.0F;
    bool loop = true;
};

struct RacingControl final {
    float steering = 0.0F;
    float throttle = 0.0F;
    float brake = 0.0F;
    std::size_t targetWaypoint = 0;
    bool finished = false;
};

class RacingBehavior final {
  public:
    [[nodiscard]] Result<void> configure(std::vector<Vec2> racingLine,
                                         RacingBehaviorSettings settings = {});
    void reset() noexcept {
        targetWaypoint_ = 0;
        finished_ = false;
    }
    [[nodiscard]] Result<RacingControl> update(Vec2 position, float headingRadians,
                                               float speed);

  private:
    std::vector<Vec2> racingLine_;
    RacingBehaviorSettings settings_;
    std::size_t targetWaypoint_ = 0;
    bool finished_ = false;
};

} // namespace fabgl
