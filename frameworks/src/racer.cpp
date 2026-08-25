#include <fabgl/frameworks/racer.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

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

    const auto gearCount = std::clamp(config.forwardGears, 1, 8);
    if (state.speed < -0.01F) {
        state.gear = -1;
        state.normalizedRpm =
            std::clamp(std::fabs(state.speed) / std::max(1.0F, config.reverseSpeed), 0.0F, 1.0F);
    } else {
        const auto normalizedSpeed =
            std::clamp(state.speed / std::max(1.0F, config.maximumSpeed), 0.0F, 1.0F);
        state.gear = std::clamp(
            1 + static_cast<int>(normalizedSpeed * static_cast<float>(gearCount)), 1, gearCount);
        const auto gearStart = static_cast<float>(state.gear - 1) / static_cast<float>(gearCount);
        const auto gearEnd = static_cast<float>(state.gear) / static_cast<float>(gearCount);
        const auto inGear = (normalizedSpeed - gearStart) / std::max(0.001F, gearEnd - gearStart);
        state.normalizedRpm = std::clamp(0.2F + inGear * 0.8F, 0.0F, 1.0F);
    }
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

RaceCountdown::RaceCountdown(float durationSeconds) noexcept
    : durationSeconds_(std::isfinite(durationSeconds) ? std::max(0.0F, durationSeconds) : 3.0F) {}

void RaceCountdown::reset() noexcept {
    elapsedSeconds_ = 0.0F;
}

void RaceCountdown::update(float deltaSeconds) noexcept {
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F || complete())
        return;
    elapsedSeconds_ = std::min(durationSeconds_, elapsedSeconds_ + deltaSeconds);
}

bool RaceCountdown::complete() const noexcept {
    return elapsedSeconds_ >= durationSeconds_;
}

float RaceCountdown::remainingSeconds() const noexcept {
    return std::max(0.0F, durationSeconds_ - elapsedSeconds_);
}

int RaceCountdown::displayedNumber() const noexcept {
    return complete() ? 0 : std::max(1, static_cast<int>(std::ceil(remainingSeconds())));
}

OpponentControl updateOpponentDriver(OpponentDriverState& state, const VehicleConfig& vehicleConfig,
                                     const OpponentDriverConfig& driverConfig,
                                     const OpponentPerception& perception,
                                     float deltaSeconds) noexcept {
    const auto finiteOr = [](const float value, const float fallback) {
        return std::isfinite(value) ? value : fallback;
    };
    const auto roadHalfWidth = std::max(0.1F, finiteOr(perception.roadHalfWidth, 1.0F));
    const auto recoveryMargin = std::clamp(finiteOr(driverConfig.recoveryMargin, 0.9F), 0.1F, 1.0F);
    OpponentControl control;
    control.recovering = std::fabs(state.vehicle.lateral) > roadHalfWidth * recoveryMargin;
    if (control.recovering) {
        state.desiredLateral = 0.0F;
    } else if (perception.slowerVehicleAhead) {
        const auto overtake = std::clamp(std::fabs(finiteOr(driverConfig.overtakeOffset, 0.55F)),
                                         0.0F, roadHalfWidth * 0.8F);
        state.desiredLateral =
            finiteOr(perception.slowerVehicleLateral, 0.0F) >= 0.0F ? -overtake : overtake;
    } else {
        state.desiredLateral = 0.0F;
    }

    const auto curve = finiteOr(perception.curveAhead, 0.0F);
    const auto steeringGain = std::max(0.0F, finiteOr(driverConfig.steeringGain, 1.5F));
    const auto anticipation = std::max(0.0F, finiteOr(driverConfig.curveAnticipation, 12.0F));
    control.steering = std::clamp((state.desiredLateral - state.vehicle.lateral) * steeringGain -
                                      curve * anticipation,
                                  -1.0F, 1.0F);

    const auto skill = std::clamp(finiteOr(driverConfig.skill, 0.5F), 0.0F, 1.0F);
    const auto baseTarget = std::clamp(finiteOr(driverConfig.targetSpeed, 55.0F), 0.0F,
                                       std::max(0.0F, vehicleConfig.maximumSpeed));
    const auto cornerPenalty =
        std::clamp(std::fabs(curve) * std::max(0.0F, finiteOr(driverConfig.cornerSlowdown, 10.0F)),
                   0.0F, 0.75F);
    auto targetSpeed = baseTarget * (0.75F + skill * 0.25F) * (1.0F - cornerPenalty);
    if (control.recovering)
        targetSpeed = std::min(targetSpeed, baseTarget * 0.45F);
    const auto speedError = targetSpeed - state.vehicle.speed;
    const auto response = std::max(1.0F, baseTarget * 0.15F);
    control.throttle = std::clamp(speedError / response, 0.0F, 1.0F);
    control.brake = std::clamp(-speedError / response, 0.0F, 1.0F);
    updateVehicle(state.vehicle, vehicleConfig, control.throttle, control.brake, control.steering,
                  skill > 0.75F && std::fabs(control.steering) > 0.8F, deltaSeconds);
    return control;
}

std::vector<RacePosition> rankRacePositions(const std::vector<RaceProgress>& progress) {
    auto ordered = progress;
    const auto finiteDistance = [](const float value) {
        return std::isfinite(value) ? value : -std::numeric_limits<float>::infinity();
    };
    const auto finiteFinish = [](const float value) {
        return std::isfinite(value) && value >= 0.0F ? value
                                                     : std::numeric_limits<float>::infinity();
    };
    std::stable_sort(ordered.begin(), ordered.end(), [&](const auto& lhs, const auto& rhs) {
        if (lhs.finished != rhs.finished)
            return lhs.finished;
        if (lhs.finished) {
            return std::tuple{finiteFinish(lhs.finishTimeSeconds), lhs.participantId} <
                   std::tuple{finiteFinish(rhs.finishTimeSeconds), rhs.participantId};
        }
        if (lhs.completedLaps != rhs.completedLaps)
            return lhs.completedLaps > rhs.completedLaps;
        if (lhs.checkpointsPassed != rhs.checkpointsPassed)
            return lhs.checkpointsPassed > rhs.checkpointsPassed;
        const auto lhsDistance = finiteDistance(lhs.distanceAlongLap);
        const auto rhsDistance = finiteDistance(rhs.distanceAlongLap);
        if (lhsDistance != rhsDistance)
            return lhsDistance > rhsDistance;
        return lhs.participantId < rhs.participantId;
    });
    std::vector<RacePosition> positions;
    positions.reserve(ordered.size());
    for (std::size_t index = 0U; index < ordered.size(); ++index) {
        positions.push_back({ordered[index].participantId,
                             static_cast<std::uint16_t>(std::min<std::size_t>(
                                 index + 1U, std::numeric_limits<std::uint16_t>::max()))});
    }
    return positions;
}

RaceHudData makeRaceHud(const VehicleState& vehicle, const LapTracker& laps, std::uint16_t position,
                        std::uint16_t participantCount, const RaceCountdown& countdown) noexcept {
    RaceHudData hud;
    const auto speed = std::isfinite(vehicle.speed) ? std::fabs(vehicle.speed) : 0.0F;
    hud.speedKph = static_cast<std::uint32_t>(
        std::clamp(std::round(static_cast<double>(speed) * 3.6), 0.0,
                   static_cast<double>(std::numeric_limits<std::uint32_t>::max())));
    hud.gear = vehicle.gear;
    hud.currentLap = std::min(laps.lap(), laps.targetLaps());
    hud.targetLaps = laps.targetLaps();
    hud.participantCount = std::max<std::uint16_t>(1U, participantCount);
    hud.position = std::clamp<std::uint16_t>(position, 1U, hud.participantCount);
    hud.countdownNumber = countdown.displayedNumber();
    hud.finishVisible = laps.finished();
    return hud;
}

} // namespace fabgl::frameworks
