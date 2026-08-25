#include <fabgl/frameworks/tps.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace fabgl::frameworks {

namespace {

float moveTowards(const float current, const float target, const float maximumDelta) noexcept {
    if (std::fabs(target - current) <= maximumDelta) {
        return target;
    }
    return current + (target > current ? maximumDelta : -maximumDelta);
}

float length(const Vec3 value) noexcept {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

Vec3 normalized(const Vec3 value) noexcept {
    const auto magnitude = length(value);
    return std::isfinite(magnitude) && magnitude > 0.00001F ? value * (1.0F / magnitude) : Vec3{};
}

bool finite(const Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool collidesXz(const Vec3 position, const float radius,
                const std::vector<Rect>& obstacles) noexcept {
    for (const auto obstacle : obstacles) {
        if (!std::isfinite(obstacle.x) || !std::isfinite(obstacle.y) ||
            !std::isfinite(obstacle.width) || !std::isfinite(obstacle.height) ||
            obstacle.width < 0.0F || obstacle.height < 0.0F) {
            continue;
        }
        const auto closestX = std::clamp(position.x, obstacle.x, obstacle.x + obstacle.width);
        const auto closestZ = std::clamp(position.z, obstacle.y, obstacle.y + obstacle.height);
        const auto dx = position.x - closestX;
        const auto dz = position.z - closestZ;
        if (dx * dx + dz * dz < radius * radius) {
            return true;
        }
    }
    return false;
}

} // namespace

void updateThirdPerson(ThirdPersonState& state, Vec2 input, const float cameraYawRadians,
                       const float speed, const float acceleration,
                       const float deltaSeconds) noexcept {
    if (!std::isfinite(cameraYawRadians) || !std::isfinite(speed) || !std::isfinite(acceleration) ||
        !std::isfinite(deltaSeconds)) {
        return;
    }
    const auto delta = std::clamp(deltaSeconds, 0.0F, 0.1F);
    const auto magnitude = std::sqrt(input.x * input.x + input.y * input.y);
    if (!std::isfinite(magnitude)) {
        return;
    }
    if (magnitude > 1.0F) {
        input = {input.x / magnitude, input.y / magnitude};
    }
    const auto cosine = std::cos(cameraYawRadians);
    const auto sine = std::sin(cameraYawRadians);
    const Vec3 desired{(input.x * cosine + input.y * sine) * std::max(0.0F, speed), 0.0F,
                       (-input.x * sine + input.y * cosine) * std::max(0.0F, speed)};
    state.velocity.x =
        moveTowards(state.velocity.x, desired.x, std::max(0.0F, acceleration) * delta);
    state.velocity.z =
        moveTowards(state.velocity.z, desired.z, std::max(0.0F, acceleration) * delta);
    state.position = state.position + state.velocity * delta;
    if (std::fabs(state.velocity.x) + std::fabs(state.velocity.z) > 0.001F) {
        state.facingRadians = std::atan2(state.velocity.x, state.velocity.z);
    }
}

std::optional<std::size_t> selectTarget(const Vec3 origin, Vec3 forward,
                                        const float maximumDistance, const float minimumFacingDot,
                                        const std::vector<TargetCandidate>& candidates) noexcept {
    forward = normalized(forward);
    if ((forward.x == 0.0F && forward.y == 0.0F && forward.z == 0.0F) ||
        !std::isfinite(maximumDistance) || maximumDistance <= 0.0F) {
        return std::nullopt;
    }
    auto bestScore = -std::numeric_limits<float>::infinity();
    std::optional<std::size_t> best;
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (!candidates[index].active || !finite(candidates[index].position)) {
            continue;
        }
        const auto relative = candidates[index].position - origin;
        const auto distance = length(relative);
        if (distance <= 0.00001F || distance > maximumDistance) {
            continue;
        }
        const auto direction = relative * (1.0F / distance);
        const auto facing =
            direction.x * forward.x + direction.y * forward.y + direction.z * forward.z;
        if (facing < minimumFacingDot) {
            continue;
        }
        const auto score = facing * 2.0F - distance / maximumDistance;
        if (score > bestScore) {
            bestScore = score;
            best = index;
        }
    }
    return best;
}

void updateThirdPersonCharacter(ThirdPersonState& state, const ThirdPersonInput& input,
                                const float cameraYawRadians, const float groundHeight,
                                const float deltaSeconds, const ThirdPersonCharacterConfig& config,
                                const std::vector<Rect>& xzObstacles) noexcept {
    if (!std::isfinite(deltaSeconds) || !std::isfinite(input.movement.x) ||
        !std::isfinite(input.movement.y) || !std::isfinite(groundHeight) ||
        !std::isfinite(config.speed) || !std::isfinite(config.acceleration) ||
        !std::isfinite(config.gravity) || !std::isfinite(config.jumpSpeed) ||
        !std::isfinite(config.bodyRadius)) {
        return;
    }
    const auto previous = state.position;
    const auto verticalVelocity = state.velocity.y;
    updateThirdPerson(state, input.movement, cameraYawRadians, config.speed, config.acceleration,
                      deltaSeconds);
    state.velocity.y = verticalVelocity;
    const auto radius = std::max(0.0F, config.bodyRadius);
    if (collidesXz({state.position.x, state.position.y, previous.z}, radius, xzObstacles)) {
        state.position.x = previous.x;
        state.velocity.x = 0.0F;
    }
    if (collidesXz({state.position.x, state.position.y, state.position.z}, radius, xzObstacles)) {
        state.position.z = previous.z;
        state.velocity.z = 0.0F;
    }

    const auto delta = std::clamp(deltaSeconds, 0.0F, 0.1F);
    if (state.position.y <= groundHeight + 0.0001F) {
        state.position.y = groundHeight;
        state.velocity.y = 0.0F;
        state.grounded = true;
    }
    if (input.jumpPressed && state.grounded) {
        state.velocity.y = std::max(0.0F, config.jumpSpeed);
        state.grounded = false;
    }
    state.velocity.y -= std::max(0.0F, config.gravity) * delta;
    state.position.y += state.velocity.y * delta;
    if (state.position.y <= groundHeight) {
        state.position.y = groundHeight;
        state.velocity.y = 0.0F;
        state.grounded = true;
    }
}

void ThirdPersonCamera::orbit(const float yawDelta, const float pitchDelta,
                              const float zoomDelta) noexcept {
    if (!std::isfinite(yawDelta) || !std::isfinite(pitchDelta) || !std::isfinite(zoomDelta)) {
        return;
    }
    yawRadians = std::remainder(yawRadians + yawDelta, 6.283185307179586F);
    pitchRadians = std::clamp(pitchRadians + pitchDelta, -1.2F, 1.2F);
    minimumDistance = std::max(0.1F, minimumDistance);
    maximumDistance = std::max(minimumDistance, maximumDistance);
    distance = std::clamp(distance + zoomDelta, minimumDistance, maximumDistance);
}

Vec3 ThirdPersonCamera::desiredPosition() const noexcept {
    if (!finite(pivot) || !std::isfinite(yawRadians) || !std::isfinite(pitchRadians) ||
        !std::isfinite(distance) || !std::isfinite(minimumDistance) ||
        !std::isfinite(maximumDistance)) {
        return pivot;
    }
    const auto boundedDistance = std::clamp(distance, std::max(0.1F, minimumDistance),
                                            std::max(minimumDistance, maximumDistance));
    const auto horizontal = std::cos(pitchRadians) * boundedDistance;
    return {pivot.x - std::sin(yawRadians) * horizontal,
            pivot.y + std::sin(pitchRadians) * boundedDistance,
            pivot.z - std::cos(yawRadians) * horizontal};
}

Vec3 ThirdPersonCamera::resolveCollision(const std::vector<Rect>& xzObstacles) const noexcept {
    const auto desired = desiredPosition();
    const auto ray = desired - pivot;
    auto maximumT = 1.0F;
    const auto radius = std::max(0.0F, collisionRadius);
    for (const auto obstacle : xzObstacles) {
        if (!std::isfinite(obstacle.x) || !std::isfinite(obstacle.y) ||
            !std::isfinite(obstacle.width) || !std::isfinite(obstacle.height) ||
            obstacle.width < 0.0F || obstacle.height < 0.0F) {
            continue;
        }
        const auto minimumX = obstacle.x - radius;
        const auto maximumX = obstacle.x + obstacle.width + radius;
        const auto minimumZ = obstacle.y - radius;
        const auto maximumZ = obstacle.y + obstacle.height + radius;
        auto entry = 0.0F;
        auto exit = maximumT;
        const auto clipAxis = [&](const float origin, const float delta, const float minimum,
                                  const float maximum, float& first, float& last) {
            if (std::fabs(delta) <= 0.000001F) {
                return origin >= minimum && origin <= maximum;
            }
            auto a = (minimum - origin) / delta;
            auto b = (maximum - origin) / delta;
            if (a > b) {
                std::swap(a, b);
            }
            first = std::max(first, a);
            last = std::min(last, b);
            return first <= last;
        };
        if (clipAxis(pivot.x, ray.x, minimumX, maximumX, entry, exit) &&
            clipAxis(pivot.z, ray.z, minimumZ, maximumZ, entry, exit) && entry >= 0.0F &&
            entry <= maximumT) {
            maximumT = std::max(0.0F, entry - 0.01F);
        }
    }
    return pivot + ray * maximumT;
}

void TpsWeapon::update(const float deltaSeconds) noexcept {
    if (std::isfinite(deltaSeconds)) {
        cooldown = std::max(0.0F, cooldown - std::max(0.0F, deltaSeconds));
    }
}

bool TpsWeapon::tryFire() noexcept {
    if (ammunition <= 0 || cooldown > 0.0F || damage < 0 || !std::isfinite(range) ||
        range <= 0.0F || !std::isfinite(fireInterval) || fireInterval < 0.0F) {
        return false;
    }
    --ammunition;
    cooldown = fireInterval;
    return true;
}

TpsAttackResult attackTps(TpsWeapon& weapon, const Vec3 origin, Vec3 direction,
                          const std::vector<TargetCandidate>& candidates) noexcept {
    TpsAttackResult result;
    direction = normalized(direction);
    if (!finite(origin) || (direction.x == 0.0F && direction.y == 0.0F && direction.z == 0.0F) ||
        !weapon.tryFire()) {
        return result;
    }
    result.fired = true;
    result.damage = weapon.damage;
    if (weapon.mode == TpsAttackMode::Hitscan) {
        result.target = selectTarget(origin, direction, weapon.range, 0.98F, candidates);
    } else {
        result.projectileVelocity = direction * std::max(0.0F, weapon.projectileSpeed);
    }
    return result;
}

bool applyTpsPickup(TpsPickup& pickup, ThirdPersonState& player, TpsWeapon& weapon) noexcept {
    const auto offset = pickup.position - player.position;
    const auto distanceSquared = offset.x * offset.x + offset.y * offset.y + offset.z * offset.z;
    if (!pickup.active || !std::isfinite(pickup.radius) || pickup.radius < 0.0F ||
        distanceSquared > pickup.radius * pickup.radius) {
        return false;
    }
    player.health = std::clamp(player.health, 0, 100);
    auto applied = false;
    if (pickup.health > 0 && player.health < 100) {
        player.health += std::min(100 - player.health, pickup.health);
        applied = true;
    }
    if (pickup.ammunition > 0 &&
        pickup.ammunition <= std::numeric_limits<int>::max() - weapon.ammunition) {
        weapon.ammunition += pickup.ammunition;
        applied = true;
    }
    if (applied) {
        pickup.active = false;
    }
    return applied;
}

} // namespace fabgl::frameworks
