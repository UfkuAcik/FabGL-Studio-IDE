#include <fabgl/frameworks/racer.h>

#include <algorithm>
#include <cmath>

namespace fabgl::frameworks {

void updateVehicle(VehicleState& state, const VehicleConfig& config, float throttle, float brake,
                   float steering, bool drift, float deltaSeconds) noexcept {
    const auto delta = std::clamp(deltaSeconds, 0.0F, 0.1F);
    throttle = std::clamp(throttle, -1.0F, 1.0F);
    brake = std::clamp(brake, 0.0F, 1.0F);
    steering = std::clamp(steering, -1.0F, 1.0F);
    const auto engine = throttle * config.acceleration;
    const auto brakingForce = brake * config.braking * (state.speed >= 0.0F ? 1.0F : -1.0F);
    state.speed += (engine - brakingForce - state.speed * config.drag) * delta;
    state.speed = std::clamp(state.speed, -config.reverseSpeed, config.maximumSpeed);
    const auto speedFactor =
        std::clamp(std::fabs(state.speed) / std::max(1.0F, config.maximumSpeed), 0.15F, 1.0F);
    state.heading += steering * config.steeringRate * speedFactor * delta;
    state.lateralVelocity += steering * std::fabs(state.speed) * 0.025F * delta;
    const auto effectiveGrip = drift ? config.grip * 0.28F : config.grip;
    state.lateralVelocity *= std::max(0.0F, 1.0F - effectiveGrip * delta);
    state.lateral += state.lateralVelocity * delta;
    state.distance += state.speed * delta;
}

LapTracker::LapTracker(std::size_t checkpointCount, int targetLaps)
    : checkpointCount_(checkpointCount), targetLaps_(std::max(1, targetLaps)) {}

bool LapTracker::crossCheckpoint(std::size_t checkpoint) noexcept {
    if (finished() || checkpointCount_ == 0U || checkpoint != nextCheckpoint_)
        return false;
    nextCheckpoint_ = (nextCheckpoint_ + 1U) % checkpointCount_;
    if (nextCheckpoint_ == 0U)
        ++lap_;
    return true;
}

} // namespace fabgl::frameworks
