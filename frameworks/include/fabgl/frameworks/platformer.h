#pragma once

#include <fabgl/math/types.h>

#include <vector>

namespace fabgl::frameworks {

struct PlatformerConfig final {
    float moveSpeed = 70.0F;
    float acceleration = 700.0F;
    float gravity = 280.0F;
    float jumpSpeed = 125.0F;
    float coyoteTime = 0.10F;
    float jumpBufferTime = 0.12F;
    float maximumFallSpeed = 180.0F;
    Vec2 bodySize{8.0F, 12.0F};
};

struct PlatformerInput final {
    float horizontal = 0.0F;
    bool jumpPressed = false;
    bool jumpHeld = false;
};

struct PlatformerState final {
    Vec2 position{};
    Vec2 velocity{};
    bool grounded = false;
    float coyoteRemaining = 0.0F;
    float jumpBufferRemaining = 0.0F;
};

class PlatformerController final {
  public:
    explicit PlatformerController(PlatformerConfig config = {}) : config_(config) {}

    void step(PlatformerState& state, const PlatformerInput& input, float deltaSeconds,
              const std::vector<Rect>& solidPlatforms,
              const std::vector<Rect>& oneWayPlatforms = {}) const noexcept;

  private:
    PlatformerConfig config_;
};

} // namespace fabgl::frameworks
