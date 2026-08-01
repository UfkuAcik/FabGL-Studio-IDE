#pragma once

#include <fabgl/math/types.h>

#include <cstddef>
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
};

struct VehicleState final {
    float distance = 0.0F;
    float speed = 0.0F;
    float lateral = 0.0F;
    float lateralVelocity = 0.0F;
    float heading = 0.0F;
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
    [[nodiscard]] bool finished() const noexcept {
        return lap_ > targetLaps_;
    }

  private:
    std::size_t checkpointCount_ = 0;
    std::size_t nextCheckpoint_ = 0;
    int targetLaps_ = 3;
    int lap_ = 1;
};

} // namespace fabgl::frameworks
