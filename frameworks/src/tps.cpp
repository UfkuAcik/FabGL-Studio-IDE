#include <fabgl/frameworks/tps.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace fabgl::frameworks {

namespace {

float moveTowards(float current, float target, float maximumDelta) noexcept {
    if (std::fabs(target - current) <= maximumDelta)
        return target;
    return current + (target > current ? maximumDelta : -maximumDelta);
}

} // namespace

void updateThirdPerson(ThirdPersonState& state, Vec2 input, float cameraYawRadians, float speed,
                       float acceleration, float deltaSeconds) noexcept {
    const auto delta = std::clamp(deltaSeconds, 0.0F, 0.1F);
    const auto magnitude = std::sqrt(input.x * input.x + input.y * input.y);
    if (magnitude > 1.0F)
        input = {input.x / magnitude, input.y / magnitude};
    const auto cosine = std::cos(cameraYawRadians);
    const auto sine = std::sin(cameraYawRadians);
    const Vec3 desired{(input.x * cosine + input.y * sine) * speed, 0.0F,
                       (-input.x * sine + input.y * cosine) * speed};
    state.velocity.x = moveTowards(state.velocity.x, desired.x, acceleration * delta);
    state.velocity.z = moveTowards(state.velocity.z, desired.z, acceleration * delta);
    state.position = state.position + state.velocity * delta;
    if (std::fabs(state.velocity.x) + std::fabs(state.velocity.z) > 0.001F) {
        state.facingRadians = std::atan2(state.velocity.x, state.velocity.z);
    }
}

std::optional<std::size_t> selectTarget(Vec3 origin, Vec3 forward, float maximumDistance,
                                        float minimumFacingDot,
                                        const std::vector<TargetCandidate>& candidates) noexcept {
    const auto forwardLength =
        std::sqrt(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
    if (forwardLength <= 0.00001F || maximumDistance <= 0.0F)
        return std::nullopt;
    forward = forward * (1.0F / forwardLength);
    auto bestScore = -std::numeric_limits<float>::infinity();
    std::optional<std::size_t> best;
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (!candidates[index].active)
            continue;
        const auto relative = candidates[index].position - origin;
        const auto distance =
            std::sqrt(relative.x * relative.x + relative.y * relative.y + relative.z * relative.z);
        if (distance <= 0.00001F || distance > maximumDistance)
            continue;
        const auto direction = relative * (1.0F / distance);
        const auto facing =
            direction.x * forward.x + direction.y * forward.y + direction.z * forward.z;
        if (facing < minimumFacingDot)
            continue;
        const auto score = facing * 2.0F - distance / maximumDistance;
        if (score > bestScore) {
            bestScore = score;
            best = index;
        }
    }
    return best;
}

} // namespace fabgl::frameworks
