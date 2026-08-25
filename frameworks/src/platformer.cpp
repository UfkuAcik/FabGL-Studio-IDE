#include <fabgl/frameworks/platformer.h>

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

Rect bodyRect(const PlatformerState& state, const PlatformerConfig& config) noexcept {
    return {state.position.x, state.position.y, config.bodySize.x, config.bodySize.y};
}

bool overlaps(const Rect& first, const Rect& second) noexcept {
    return first.x < second.x + second.width && first.x + first.width > second.x &&
           first.y < second.y + second.height && first.y + first.height > second.y;
}

bool finite(const Vec2 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

bool validRect(const Rect value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.width) &&
           std::isfinite(value.height) && value.width >= 0.0F && value.height >= 0.0F;
}

Rect playerRect(const PlatformerState& player, const Vec2 bodySize) noexcept {
    return {player.position.x, player.position.y, std::max(0.0F, bodySize.x),
            std::max(0.0F, bodySize.y)};
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

Vec2 MovingPlatform::update(const float deltaSeconds) noexcept {
    const auto previous = Vec2{bounds.x, bounds.y};
    if (!validRect(bounds) || !finite(velocity) || !finite(minimum) || !finite(maximum) ||
        !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F) {
        return {};
    }
    const auto delta = std::clamp(deltaSeconds, 0.0F, 0.1F);
    bounds.x += velocity.x * delta;
    bounds.y += velocity.y * delta;
    const auto resolveAxis = [this](float& position, float& speed, const float lower,
                                    const float upper) {
        if (std::fabs(speed) <= 0.000001F && lower == upper) {
            return;
        }
        const auto minimumValue = std::min(lower, upper);
        const auto maximumValue = std::max(lower, upper);
        if (position < minimumValue) {
            position = minimumValue;
            speed = pingPong ? std::fabs(speed) : 0.0F;
        } else if (position > maximumValue) {
            position = maximumValue;
            speed = pingPong ? -std::fabs(speed) : 0.0F;
        }
    };
    resolveAxis(bounds.x, velocity.x, minimum.x, maximum.x);
    resolveAxis(bounds.y, velocity.y, minimum.y, maximum.y);
    return {bounds.x - previous.x, bounds.y - previous.y};
}

void PlatformerEnemy::update(const float deltaSeconds) noexcept {
    if (!active || !validRect(bounds) || !std::isfinite(speed) || !std::isfinite(patrolMinimumX) ||
        !std::isfinite(patrolMaximumX) || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F) {
        return;
    }
    const auto minimumX = std::min(patrolMinimumX, patrolMaximumX);
    const auto maximumX = std::max(patrolMinimumX, patrolMaximumX);
    const auto delta = std::clamp(deltaSeconds, 0.0F, 0.1F);
    bounds.x += (movingRight ? 1.0F : -1.0F) * std::fabs(speed) * delta;
    if (bounds.x <= minimumX) {
        bounds.x = minimumX;
        movingRight = true;
    } else if (bounds.x >= maximumX) {
        bounds.x = maximumX;
        movingRight = false;
    }
}

void PlatformerCamera::follow(const Vec2 target, const float deltaSeconds) noexcept {
    if (!finite(target) || !finite(position) || !finite(viewport) || !validRect(worldBounds) ||
        !std::isfinite(followRate) || !std::isfinite(deltaSeconds)) {
        return;
    }
    const auto desired = Vec2{target.x - viewport.x * 0.5F, target.y - viewport.y * 0.5F};
    const auto amount =
        1.0F - std::exp(-std::max(0.0F, followRate) * std::clamp(deltaSeconds, 0.0F, 0.1F));
    position = position + (desired - position) * amount;
    const auto maximumX = std::max(worldBounds.x, worldBounds.x + worldBounds.width - viewport.x);
    const auto maximumY = std::max(worldBounds.y, worldBounds.y + worldBounds.height - viewport.y);
    position.x = std::clamp(position.x, worldBounds.x, maximumX);
    position.y = std::clamp(position.y, worldBounds.y, maximumY);
}

PlatformerWorld::PlatformerWorld(const std::size_t maximumRecords)
    : maximumRecords_(std::clamp<std::size_t>(maximumRecords, 1U, 4096U)) {}

bool PlatformerWorld::addMovingPlatform(MovingPlatform platform) {
    if (movingPlatforms_.size() >= maximumRecords_ || !validRect(platform.bounds) ||
        !finite(platform.velocity) || !finite(platform.minimum) || !finite(platform.maximum)) {
        return false;
    }
    movingPlatforms_.push_back(platform);
    return true;
}

bool PlatformerWorld::addCollectible(PlatformerCollectible collectible) {
    if (collectibles_.size() >= maximumRecords_ || !validRect(collectible.bounds) ||
        collectible.value < 0 ||
        std::any_of(collectibles_.begin(), collectibles_.end(),
                    [&](const auto& value) { return value.id == collectible.id; })) {
        return false;
    }
    collectibles_.push_back(collectible);
    return true;
}

bool PlatformerWorld::addEnemy(PlatformerEnemy enemy) {
    if (enemies_.size() >= maximumRecords_ || !validRect(enemy.bounds) ||
        !std::isfinite(enemy.speed) || enemy.speed < 0.0F || enemy.contactDamage < 0 ||
        std::any_of(enemies_.begin(), enemies_.end(),
                    [&](const auto& value) { return value.id == enemy.id; })) {
        return false;
    }
    enemies_.push_back(enemy);
    return true;
}

bool PlatformerWorld::addCheckpoint(PlatformerCheckpoint checkpoint) {
    if (checkpoints_.size() >= maximumRecords_ || !validRect(checkpoint.bounds) ||
        !finite(checkpoint.respawnPosition) ||
        std::any_of(checkpoints_.begin(), checkpoints_.end(),
                    [&](const auto& value) { return value.id == checkpoint.id; })) {
        return false;
    }
    checkpoints_.push_back(checkpoint);
    return true;
}

bool PlatformerWorld::addTransition(PlatformerLevelTransition transition) {
    if (transitions_.size() >= maximumRecords_ || !validRect(transition.bounds)) {
        return false;
    }
    transitions_.push_back(transition);
    return true;
}

void PlatformerWorld::update(PlatformerController& controller, PlatformerState& player,
                             PlatformerWorldState& state, PlatformerCamera& camera,
                             const PlatformerInput& input, const float deltaSeconds,
                             const std::vector<Rect>& staticPlatforms,
                             const std::vector<Rect>& oneWayPlatforms) noexcept {
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F || state.dead) {
        camera.follow(player.position, deltaSeconds);
        return;
    }
    const auto delta = std::clamp(deltaSeconds, 0.0F, 0.1F);
    state.maximumHealth = std::max(1, state.maximumHealth);
    state.health = std::clamp(state.health, 0, state.maximumHealth);
    state.damageCooldown = std::max(0.0F, state.damageCooldown - delta);

    auto solidPlatforms = staticPlatforms;
    solidPlatforms.reserve(staticPlatforms.size() + movingPlatforms_.size());
    for (auto& platform : movingPlatforms_) {
        const auto previous = platform.bounds;
        const auto displacement = platform.update(delta);
        const auto bodySize = controller.bodySize();
        const auto feet = player.position.y + bodySize.y;
        const auto standing = player.position.x + bodySize.x > previous.x &&
                              player.position.x < previous.x + previous.width &&
                              std::fabs(feet - previous.y) <= 0.5F && player.velocity.y >= 0.0F;
        if (standing) {
            player.position = player.position + displacement;
        }
        solidPlatforms.push_back(platform.bounds);
    }
    controller.step(player, input, delta, solidPlatforms, oneWayPlatforms);

    const auto body = playerRect(player, controller.bodySize());
    for (auto& collectible : collectibles_) {
        if (collectible.active && overlaps(body, collectible.bounds)) {
            collectible.active = false;
            state.score =
                std::min(std::numeric_limits<int>::max() - collectible.value, state.score) +
                collectible.value;
            ++state.collected;
        }
    }
    for (auto& enemy : enemies_) {
        enemy.update(delta);
        if (enemy.active && state.damageCooldown <= 0.0F && overlaps(body, enemy.bounds)) {
            state.health = std::max(0, state.health - std::max(0, enemy.contactDamage));
            state.damageCooldown = 0.75F;
        }
    }
    for (const auto& checkpoint : checkpoints_) {
        if (overlaps(body, checkpoint.bounds)) {
            state.checkpointId = checkpoint.id;
            state.respawnPosition = checkpoint.respawnPosition;
        }
    }
    state.requestedLevel.reset();
    for (const auto& transition : transitions_) {
        if (transition.enabled && overlaps(body, transition.bounds)) {
            state.requestedLevel = transition.levelId;
            break;
        }
    }
    if (state.health <= 0) {
        state.dead = true;
        player.position = state.respawnPosition;
        player.velocity = {};
    }
    camera.follow({player.position.x + 4.0F, player.position.y + 6.0F}, delta);
}

PlatformerHud PlatformerWorld::hud(const PlatformerWorldState& state) const noexcept {
    return {state.health, state.maximumHealth, state.score, state.collected, state.dead};
}

} // namespace fabgl::frameworks
