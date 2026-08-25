#include "fabgl/navigation/ai_behaviors.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace fabgl {
namespace {

[[nodiscard]] bool finite(Vec2 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] float length(Vec2 value) noexcept {
    return std::sqrt(value.x * value.x + value.y * value.y);
}

[[nodiscard]] Vec2 normalized(Vec2 value) noexcept {
    const auto magnitude = length(value);
    return magnitude > 0.0F ? value * (1.0F / magnitude) : Vec2{};
}

[[nodiscard]] float wrapAngle(float angle) noexcept {
    constexpr float Pi = 3.14159265358979323846F;
    constexpr auto TwoPi = Pi * 2.0F;
    while (angle > Pi)
        angle -= TwoPi;
    while (angle < -Pi)
        angle += TwoPi;
    return angle;
}

} // namespace

Result<void> AiStateMachine::addState(std::string name, AiStateCallbacks callbacks) {
    if (name.empty())
        return Result<void>::failure(Error(ErrorCode::InvalidArgument, "AI state name is empty"));
    if (states_.find(name) != states_.end()) {
        return Result<void>::failure(
            Error(ErrorCode::AlreadyExists, "AI state already exists").addContext("state", name));
    }
    states_.emplace(std::move(name), std::move(callbacks));
    return Result<void>::success();
}

Result<void> AiStateMachine::addTransition(AiTransition transition) {
    if (states_.find(transition.fromState) == states_.end() ||
        states_.find(transition.toState) == states_.end() || !transition.condition) {
        return Result<void>::failure(Error(ErrorCode::InvalidArgument, "AI transition is invalid"));
    }
    transitions_.push_back(std::move(transition));
    return Result<void>::success();
}

Result<void> AiStateMachine::transitionTo(std::string_view state) {
    const auto next = states_.find(std::string(state));
    if (next == states_.end()) {
        return Result<void>::failure(
            Error(ErrorCode::NotFound, "AI state was not found")
                .addContext("state", std::string(state)));
    }
    if (next->first == currentState_)
        return Result<void>::success();

    const auto previous = states_.find(currentState_);
    if (previous != states_.end() && previous->second.onExit) {
        auto exited = previous->second.onExit();
        if (!exited)
            return exited;
    }
    if (next->second.onEnter) {
        auto entered = next->second.onEnter();
        if (!entered) {
            if (previous != states_.end() && previous->second.onEnter)
                static_cast<void>(previous->second.onEnter());
            return entered;
        }
    }
    currentState_ = next->first;
    return Result<void>::success();
}

Result<void> AiStateMachine::update(float deltaSeconds) {
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0F)
        return Result<void>::failure(Error(ErrorCode::InvalidArgument, "AI delta is invalid"));
    auto current = states_.find(currentState_);
    if (current == states_.end())
        return Result<void>::failure(Error(ErrorCode::InvalidState, "AI has no current state"));

    for (const auto& transition : transitions_) {
        if (transition.fromState == currentState_ && transition.condition()) {
            auto changed = transitionTo(transition.toState);
            if (!changed)
                return changed;
            current = states_.find(currentState_);
            break;
        }
    }
    if (current->second.onUpdate)
        return current->second.onUpdate(deltaSeconds);
    return Result<void>::success();
}

Result<void> WaypointFollower::configure(std::vector<Vec2> waypoints,
                                         WaypointFollowerSettings settings) {
    if (waypoints.empty() || !std::all_of(waypoints.begin(), waypoints.end(), finite) ||
        !std::isfinite(settings.speed) || settings.speed <= 0.0F ||
        !std::isfinite(settings.arrivalRadius) || settings.arrivalRadius < 0.0F) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "waypoint follower configuration is invalid"));
    }
    waypoints_ = std::move(waypoints);
    settings_ = settings;
    reset();
    return Result<void>::success();
}

Result<WaypointStep> WaypointFollower::update(Vec2 position, float deltaSeconds) {
    if (waypoints_.empty()) {
        return Result<WaypointStep>::failure(
            Error(ErrorCode::InvalidState, "waypoint follower is not configured"));
    }
    if (!finite(position) || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0F) {
        return Result<WaypointStep>::failure(
            Error(ErrorCode::InvalidArgument, "waypoint update input is invalid"));
    }
    WaypointStep step;
    step.position = position;
    step.waypointIndex = waypointIndex_;
    step.finished = finished_;
    if (finished_ || deltaSeconds == 0.0F)
        return Result<WaypointStep>::success(step);

    auto remaining = settings_.speed * deltaSeconds;
    std::size_t guard = 0;
    while (!finished_ && guard++ < 1024U) {
        const auto delta = waypoints_[waypointIndex_] - step.position;
        const auto distance = length(delta);
        if (distance <= settings_.arrivalRadius || remaining >= distance) {
            step.position = waypoints_[waypointIndex_];
            remaining = remaining > distance ? remaining - distance : 0.0F;
            step.reachedWaypoint = true;
            if (waypointIndex_ + 1U < waypoints_.size()) {
                ++waypointIndex_;
            } else if (settings_.loop) {
                waypointIndex_ = 0;
            } else {
                finished_ = true;
            }
            if (remaining <= 0.0F)
                break;
            continue;
        }
        const auto direction = normalized(delta);
        step.position = step.position + direction * remaining;
        step.velocity = direction * settings_.speed;
        remaining = 0.0F;
    }
    if (!finished_ && step.velocity == Vec2{}) {
        step.velocity = normalized(waypoints_[waypointIndex_] - step.position) * settings_.speed;
    }
    step.waypointIndex = waypointIndex_;
    step.finished = finished_;
    return Result<WaypointStep>::success(step);
}

Result<void> AiBehaviorController2D::configure(Vec2 spawnPosition,
                                               std::vector<Vec2> patrolPoints,
                                               AiBehavior2DSettings settings) {
    const auto validPositive = [](float value) {
        return std::isfinite(value) && value > 0.0F;
    };
    if (!finite(spawnPosition) || patrolPoints.size() > 256U ||
        !std::all_of(patrolPoints.begin(), patrolPoints.end(), finite) ||
        !validPositive(settings.patrolSpeed) || !validPositive(settings.chaseSpeed) ||
        !validPositive(settings.fleeSpeed) || !validPositive(settings.searchSpeed) ||
        !validPositive(settings.returnSpeed) || !validPositive(settings.detectionRadius) ||
        !validPositive(settings.loseTargetRadius) ||
        settings.loseTargetRadius < settings.detectionRadius ||
        !std::isfinite(settings.attackRadius) || settings.attackRadius < 0.0F ||
        settings.attackRadius > settings.detectionRadius ||
        !std::isfinite(settings.arrivalRadius) || settings.arrivalRadius < 0.0F ||
        !std::isfinite(settings.searchSeconds) || settings.searchSeconds < 0.0F) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "2D AI behavior configuration is invalid"));
    }
    spawnPosition_ = spawnPosition;
    lastKnownTarget_ = spawnPosition;
    patrolPoints_ = std::move(patrolPoints);
    settings_ = settings;
    patrolIndex_ = 0U;
    searchElapsed_ = 0.0F;
    hasLastKnownTarget_ = false;
    state_ = patrolPoints_.empty() ? AiBehavior2DState::Idle : AiBehavior2DState::Patrol;
    configured_ = true;
    return Result<void>::success();
}

Result<void> AiBehaviorController2D::setState(const AiBehavior2DState state) {
    if (!configured_)
        return Result<void>::failure(Error(ErrorCode::InvalidState, "2D AI is not configured"));
    if ((state == AiBehavior2DState::Patrol || state == AiBehavior2DState::FollowPath) &&
        patrolPoints_.empty()) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "2D AI state requires a configured path"));
    }
    state_ = state;
    if (state == AiBehavior2DState::Search)
        searchElapsed_ = 0.0F;
    return Result<void>::success();
}

Vec2 AiBehaviorController2D::velocityToward(Vec2 from, Vec2 to, float speed) const noexcept {
    return normalized(to - from) * speed;
}

Result<AiBehavior2DStep> AiBehaviorController2D::update(const AiBehavior2DInput& input) {
    if (!configured_) {
        return Result<AiBehavior2DStep>::failure(
            Error(ErrorCode::InvalidState, "2D AI is not configured"));
    }
    if (!finite(input.position) || !std::isfinite(input.deltaSeconds) ||
        input.deltaSeconds < 0.0F ||
        (input.targetPosition && !finite(*input.targetPosition)) ||
        (input.targetVisible && !input.targetPosition)) {
        return Result<AiBehavior2DStep>::failure(
            Error(ErrorCode::InvalidArgument, "2D AI update input is invalid"));
    }

    const auto targetDistance = input.targetPosition
                                    ? length(*input.targetPosition - input.position)
                                    : std::numeric_limits<float>::infinity();
    if (input.targetVisible && input.targetPosition) {
        lastKnownTarget_ = *input.targetPosition;
        hasLastKnownTarget_ = true;
        searchElapsed_ = 0.0F;
        if (state_ != AiBehavior2DState::Flee) {
            if (targetDistance <= settings_.attackRadius)
                state_ = AiBehavior2DState::Attack;
            else if (targetDistance <= settings_.detectionRadius ||
                     state_ == AiBehavior2DState::Chase ||
                     state_ == AiBehavior2DState::Attack ||
                     state_ == AiBehavior2DState::Search) {
                state_ = AiBehavior2DState::Chase;
            }
        }
    } else if ((state_ == AiBehavior2DState::Chase ||
                state_ == AiBehavior2DState::Attack) &&
               hasLastKnownTarget_) {
        state_ = AiBehavior2DState::Search;
        searchElapsed_ = 0.0F;
    }

    AiBehavior2DStep output;
    output.state = state_;
    output.desiredPosition = input.position;
    switch (state_) {
    case AiBehavior2DState::Idle:
        break;
    case AiBehavior2DState::Patrol:
    case AiBehavior2DState::FollowPath: {
        const auto target = patrolPoints_[patrolIndex_];
        output.desiredPosition = target;
        if (length(target - input.position) <= settings_.arrivalRadius) {
            output.reachedDestination = true;
            if (patrolIndex_ + 1U < patrolPoints_.size())
                ++patrolIndex_;
            else if (state_ == AiBehavior2DState::Patrol)
                patrolIndex_ = 0U;
            else
                state_ = AiBehavior2DState::Idle;
            output.state = state_;
        } else {
            output.velocity = velocityToward(input.position, target, settings_.patrolSpeed);
        }
        break;
    }
    case AiBehavior2DState::Chase:
        if (!input.targetPosition || targetDistance > settings_.loseTargetRadius) {
            state_ = hasLastKnownTarget_ ? AiBehavior2DState::Search
                                         : AiBehavior2DState::ReturnToSpawn;
            searchElapsed_ = 0.0F;
            output.state = state_;
        } else {
            output.desiredPosition = *input.targetPosition;
            output.velocity =
                velocityToward(input.position, *input.targetPosition, settings_.chaseSpeed);
        }
        break;
    case AiBehavior2DState::Flee:
        if (!input.targetPosition || targetDistance >= settings_.loseTargetRadius) {
            state_ = AiBehavior2DState::ReturnToSpawn;
            output.state = state_;
        } else {
            output.desiredPosition = input.position + (input.position - *input.targetPosition);
            output.velocity =
                velocityToward(*input.targetPosition, input.position, settings_.fleeSpeed);
        }
        break;
    case AiBehavior2DState::Attack:
        output.desiredPosition = input.targetPosition.value_or(lastKnownTarget_);
        output.attack = input.targetVisible && input.targetPosition &&
                        targetDistance <= settings_.attackRadius;
        if (!output.attack) {
            state_ = input.targetVisible ? AiBehavior2DState::Chase
                                         : AiBehavior2DState::Search;
            output.state = state_;
        }
        break;
    case AiBehavior2DState::Search:
        searchElapsed_ += input.deltaSeconds;
        output.desiredPosition = lastKnownTarget_;
        output.reachedDestination =
            !hasLastKnownTarget_ ||
            length(lastKnownTarget_ - input.position) <= settings_.arrivalRadius;
        if (output.reachedDestination || searchElapsed_ >= settings_.searchSeconds) {
            state_ = AiBehavior2DState::ReturnToSpawn;
            output.state = state_;
        } else {
            output.velocity =
                velocityToward(input.position, lastKnownTarget_, settings_.searchSpeed);
        }
        break;
    case AiBehavior2DState::ReturnToSpawn:
        output.desiredPosition = spawnPosition_;
        if (length(spawnPosition_ - input.position) <= settings_.arrivalRadius) {
            output.reachedDestination = true;
            state_ = patrolPoints_.empty() ? AiBehavior2DState::Idle
                                           : AiBehavior2DState::Patrol;
            output.state = state_;
        } else {
            output.velocity =
                velocityToward(input.position, spawnPosition_, settings_.returnSpeed);
        }
        break;
    }
    return Result<AiBehavior2DStep>::success(output);
}

Result<std::vector<GridPosition>>
findDoorAwarePath(const GridNavigation& navigation, GridPosition start, GridPosition goal,
                  const std::vector<NavigationDoor2D>& doors,
                  const std::set<std::string, std::less<>>& availableKeys) {
    if (doors.size() > 256U) {
        return Result<std::vector<GridPosition>>::failure(
            Error(ErrorCode::CapacityExceeded, "door-aware navigation exceeds the door limit"));
    }
    auto adjusted = navigation;
    std::set<std::pair<int, int>> positions;
    for (const auto& door : doors) {
        if (!navigation.contains(door.position) || door.requiredKey.size() > 64U ||
            !positions.emplace(door.position.x, door.position.y).second) {
            return Result<std::vector<GridPosition>>::failure(
                Error(ErrorCode::InvalidArgument, "navigation door is invalid"));
        }
        const bool canOpen =
            door.open || (!door.requiredKey.empty() && availableKeys.contains(door.requiredKey));
        if (!canOpen) {
            auto blocked = adjusted.setBlocked(door.position, true);
            if (!blocked)
                return Result<std::vector<GridPosition>>::failure(blocked.error());
        }
    }
    return adjusted.findPath(start, goal);
}

Result<bool> hasLineOfSight(const PhysicsWorld2D& world, Vec2 origin, Vec2 target,
                            std::uint32_t obstacleLayerMask) {
    if (!finite(origin) || !finite(target))
        return Result<bool>::failure(Error(ErrorCode::InvalidArgument, "line of sight is invalid"));
    const auto delta = target - origin;
    const auto distance = length(delta);
    if (distance <= 0.00001F)
        return Result<bool>::success(true);
    auto hit = world.raycast(origin, delta, distance, obstacleLayerMask);
    if (!hit)
        return Result<bool>::failure(hit.error());
    return Result<bool>::success(!hit.value().has_value());
}

Result<void> RacingBehavior::configure(std::vector<Vec2> racingLine,
                                       RacingBehaviorSettings settings) {
    if (racingLine.empty() || !std::all_of(racingLine.begin(), racingLine.end(), finite) ||
        !std::isfinite(settings.targetSpeed) || settings.targetSpeed <= 0.0F ||
        !std::isfinite(settings.arrivalRadius) || settings.arrivalRadius < 0.0F ||
        !std::isfinite(settings.maximumSteeringRadians) ||
        settings.maximumSteeringRadians <= 0.0F || !std::isfinite(settings.speedResponse) ||
        settings.speedResponse <= 0.0F) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "racing behavior configuration is invalid"));
    }
    racingLine_ = std::move(racingLine);
    settings_ = settings;
    reset();
    return Result<void>::success();
}

Result<RacingControl> RacingBehavior::update(Vec2 position, float headingRadians, float speed) {
    if (racingLine_.empty()) {
        return Result<RacingControl>::failure(
            Error(ErrorCode::InvalidState, "racing behavior is not configured"));
    }
    if (!finite(position) || !std::isfinite(headingRadians) || !std::isfinite(speed) ||
        speed < 0.0F) {
        return Result<RacingControl>::failure(
            Error(ErrorCode::InvalidArgument, "racing behavior input is invalid"));
    }
    if (!finished_ && length(racingLine_[targetWaypoint_] - position) <= settings_.arrivalRadius) {
        if (targetWaypoint_ + 1U < racingLine_.size())
            ++targetWaypoint_;
        else if (settings_.loop)
            targetWaypoint_ = 0;
        else
            finished_ = true;
    }

    RacingControl output;
    output.targetWaypoint = targetWaypoint_;
    output.finished = finished_;
    if (finished_)
        return Result<RacingControl>::success(output);

    const auto delta = racingLine_[targetWaypoint_] - position;
    const auto desiredHeading = std::atan2(delta.y, delta.x);
    const auto headingError = wrapAngle(desiredHeading - headingRadians);
    output.steering =
        std::clamp(headingError / settings_.maximumSteeringRadians, -1.0F, 1.0F);
    const auto cornerFactor = 1.0F - 0.65F * std::abs(output.steering);
    const auto desiredSpeed = settings_.targetSpeed * cornerFactor;
    const auto speedError = desiredSpeed - speed;
    output.throttle = std::clamp(speedError / settings_.speedResponse, 0.0F, 1.0F);
    output.brake = std::clamp(-speedError / settings_.speedResponse, 0.0F, 1.0F);
    return Result<RacingControl>::success(output);
}

} // namespace fabgl
