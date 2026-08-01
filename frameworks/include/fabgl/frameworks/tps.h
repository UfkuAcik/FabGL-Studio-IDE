#pragma once

#include <fabgl/math/types.h>

#include <cstddef>
#include <optional>
#include <vector>

namespace fabgl::frameworks {

struct ThirdPersonState final {
    Vec3 position{};
    Vec3 velocity{};
    float facingRadians = 0.0F;
};

void updateThirdPerson(ThirdPersonState& state, Vec2 input, float cameraYawRadians, float speed,
                       float acceleration, float deltaSeconds) noexcept;

struct TargetCandidate final {
    Vec3 position{};
    bool active = true;
};

[[nodiscard]] std::optional<std::size_t>
selectTarget(Vec3 origin, Vec3 forward, float maximumDistance, float minimumFacingDot,
             const std::vector<TargetCandidate>& candidates) noexcept;

} // namespace fabgl::frameworks
