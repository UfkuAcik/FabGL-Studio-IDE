#pragma once

#include <fabgl/math/types.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fabgl::frameworks {

struct VehicleConfig final {
    float acceleration = 38.0F;
    float braking = 58.0F;
    float drag = 0.18F;
    float maximumSpeed = 95.0F;
    float reverseSpeed = 18.0F;
    float steeringRate = 1.25F;
    float grip = 4.5F;
    int forwardGears = 5;
};

struct VehicleState final {
    float distance = 0.0F;
    float speed = 0.0F;
    float lateral = 0.0F;
    float lateralVelocity = 0.0F;
    float heading = 0.0F;
    // -1 is reverse; forward gears are numbered from one.
    int gear = 1;
    float normalizedRpm = 0.0F;
};

void updateVehicle(VehicleState& state, const VehicleConfig& config, float throttle, float brake,
                   float steering, bool drift, float deltaSeconds) noexcept;

class LapTracker final {
  public:
    explicit LapTracker(std::size_t checkpointCount, int targetLaps = 3);
    [[nodiscard]] bool crossCheckpoint(std::size_t checkpoint) noexcept;
    [[nodiscard]] int lap() const noexcept {
        return lap_;
    }
    [[nodiscard]] int targetLaps() const noexcept {
        return targetLaps_;
    }
    [[nodiscard]] std::size_t nextCheckpoint() const noexcept {
        return nextCheckpoint_;
    }
    [[nodiscard]] int completedLaps() const noexcept {
        return std::min(lap_ - 1, targetLaps_);
    }
    [[nodiscard]] bool finished() const noexcept {
        return lap_ > targetLaps_;
    }

  private:
    std::size_t checkpointCount_ = 0;
    std::size_t nextCheckpoint_ = 0;
    int targetLaps_ = 3;
    int lap_ = 1;
};

class RaceCountdown final {
  public:
    explicit RaceCountdown(float durationSeconds = 3.0F) noexcept;

    void reset() noexcept;
    void update(float deltaSeconds) noexcept;
    [[nodiscard]] bool complete() const noexcept;
    [[nodiscard]] float remainingSeconds() const noexcept;
    // Returns the number currently shown by the start lights, or zero for GO.
    [[nodiscard]] int displayedNumber() const noexcept;

  private:
    float durationSeconds_ = 3.0F;
    float elapsedSeconds_ = 0.0F;
};

struct OpponentDriverConfig final {
    float targetSpeed = 55.0F;
    float skill = 0.5F;
    float cornerSlowdown = 10.0F;
    float steeringGain = 1.5F;
    float curveAnticipation = 12.0F;
    float overtakeOffset = 0.55F;
    float recoveryMargin = 0.9F;
};

struct OpponentPerception final {
    float curveAhead = 0.0F;
    float roadHalfWidth = 1.0F;
    bool slowerVehicleAhead = false;
    float slowerVehicleLateral = 0.0F;
};

struct OpponentControl final {
    float throttle = 0.0F;
    float brake = 0.0F;
    float steering = 0.0F;
    bool recovering = false;
};

struct OpponentDriverState final {
    VehicleState vehicle;
    float desiredLateral = 0.0F;
};

[[nodiscard]] OpponentControl updateOpponentDriver(OpponentDriverState& state,
                                                   const VehicleConfig& vehicleConfig,
                                                   const OpponentDriverConfig& driverConfig,
                                                   const OpponentPerception& perception,
                                                   float deltaSeconds) noexcept;

struct RaceProgress final {
    std::uint16_t participantId = 0U;
    std::uint32_t completedLaps = 0U;
    std::uint32_t checkpointsPassed = 0U;
    float distanceAlongLap = 0.0F;
    bool finished = false;
    float finishTimeSeconds = 0.0F;
};

struct RacePosition final {
    std::uint16_t participantId = 0U;
    std::uint16_t position = 0U;
};

[[nodiscard]] std::vector<RacePosition>
rankRacePositions(const std::vector<RaceProgress>& progress);

struct RaceHudData final {
    std::uint32_t speedKph = 0U;
    int gear = 1;
    int currentLap = 1;
    int targetLaps = 1;
    std::uint16_t position = 1U;
    std::uint16_t participantCount = 1U;
    int countdownNumber = 0;
    bool finishVisible = false;
};

[[nodiscard]] RaceHudData makeRaceHud(const VehicleState& vehicle, const LapTracker& laps,
                                      std::uint16_t position, std::uint16_t participantCount,
                                      const RaceCountdown& countdown) noexcept;

} // namespace fabgl::frameworks
