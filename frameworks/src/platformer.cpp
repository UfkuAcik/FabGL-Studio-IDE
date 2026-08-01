#include <fabgl/frameworks/platformer.h>

#include <algorithm>
#include <cmath>

namespace fabgl::frameworks {

namespace {

float moveTowards(float current, float target, float maximumDelta) noexcept {
    if (std::fabs(target - current) <= maximumDelta)
        return target;
    return current + (target > current ? maximumDelta : -maximumDelta);
}

Rect bodyRect(const PlatformerState& state, const PlatformerConfig& config) noexcept {
    return {state.position.x, state.position.y, config.bodySize.x, config.bodySize.y};
}

bool overlaps(const Rect& first, const Rect& second) noexcept {
    return first.x < second.x + second.width && first.x + first.width > second.x &&
           first.y < second.y + second.height && first.y + first.height > second.y;
}

} // namespace

void PlatformerController::step(PlatformerState& state, const PlatformerInput& input,
                                float deltaSeconds, const std::vector<Rect>& solidPlatforms,
                                const std::vector<Rect>& oneWayPlatforms) const noexcept {
    const auto delta = std::clamp(deltaSeconds, 0.0F, 0.05F);
    state.coyoteRemaining =
        state.grounded ? config_.coyoteTime : std::max(0.0F, state.coyoteRemaining - delta);
    state.jumpBufferRemaining = input.jumpPressed
                                    ? config_.jumpBufferTime
                                    : std::max(0.0F, state.jumpBufferRemaining - delta);
    const auto targetVelocity = std::clamp(input.horizontal, -1.0F, 1.0F) * config_.moveSpeed;
    state.velocity.x = moveTowards(state.velocity.x, targetVelocity, config_.acceleration * delta);

    if (state.jumpBufferRemaining > 0.0F && state.coyoteRemaining > 0.0F) {
        state.velocity.y = -config_.jumpSpeed;
        state.jumpBufferRemaining = 0.0F;
        state.coyoteRemaining = 0.0F;
        state.grounded = false;
    }
    const auto gravityMultiplier = !input.jumpHeld && state.velocity.y < 0.0F ? 2.2F : 1.0F;
    state.velocity.y = std::min(config_.maximumFallSpeed,
                                state.velocity.y + config_.gravity * gravityMultiplier * delta);

    state.position.x += state.velocity.x * delta;
    for (const auto& platform : solidPlatforms) {
        const auto body = bodyRect(state, config_);
        if (!overlaps(body, platform))
            continue;
        if (state.velocity.x > 0.0F)
            state.position.x = platform.x - config_.bodySize.x;
        else if (state.velocity.x < 0.0F)
            state.position.x = platform.x + platform.width;
        state.velocity.x = 0.0F;
    }

    const auto previousBottom = state.position.y + config_.bodySize.y;
    state.position.y += state.velocity.y * delta;
    state.grounded = false;
    for (const auto& platform : solidPlatforms) {
        const auto body = bodyRect(state, config_);
        if (!overlaps(body, platform))
            continue;
        if (state.velocity.y > 0.0F) {
            state.position.y = platform.y - config_.bodySize.y;
            state.grounded = true;
        } else if (state.velocity.y < 0.0F) {
            state.position.y = platform.y + platform.height;
        }
        state.velocity.y = 0.0F;
    }
    if (state.velocity.y >= 0.0F) {
        for (const auto& platform : oneWayPlatforms) {
            const auto currentBottom = state.position.y + config_.bodySize.y;
            const auto horizontalOverlap = state.position.x < platform.x + platform.width &&
                                           state.position.x + config_.bodySize.x > platform.x;
            if (horizontalOverlap && previousBottom <= platform.y && currentBottom >= platform.y) {
                state.position.y = platform.y - config_.bodySize.y;
                state.velocity.y = 0.0F;
                state.grounded = true;
            }
        }
    }
    if (state.grounded)
        state.coyoteRemaining = config_.coyoteTime;
}

} // namespace fabgl::frameworks
