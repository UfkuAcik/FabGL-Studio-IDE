#include <fabgl/frameworks/top_down.h>

#include <algorithm>
#include <cmath>

namespace fabgl::frameworks {

namespace {

Vec2 normalized(Vec2 value) noexcept {
    const auto length = std::sqrt(value.x * value.x + value.y * value.y);
    return length > 0.00001F ? Vec2{value.x / length, value.y / length} : Vec2{};
}

} // namespace

void updateTopDown(TopDownState& state, Vec2 movement, Vec2 aim, float speed,
                   float deltaSeconds) noexcept {
    const auto direction = normalized(movement);
    state.velocity = direction * std::max(0.0F, speed);
    state.position = state.position + state.velocity * std::clamp(deltaSeconds, 0.0F, 0.1F);
    const auto aimDirection = normalized(aim);
    if (aimDirection.x != 0.0F || aimDirection.y != 0.0F)
        state.aim = aimDirection;
}

ProjectilePool::ProjectilePool(std::size_t capacity) : projectiles_(capacity) {}

bool ProjectilePool::spawn(Vec2 position, Vec2 direction, float speed, float lifetime,
                           int damage) noexcept {
    const auto normalizedDirection = normalized(direction);
    if (lifetime <= 0.0F || speed < 0.0F || damage < 0 ||
        (normalizedDirection.x == 0.0F && normalizedDirection.y == 0.0F)) {
        return false;
    }
    const auto available =
        std::find_if(projectiles_.begin(), projectiles_.end(),
                     [](const Projectile& projectile) { return !projectile.active; });
    if (available == projectiles_.end())
        return false;
    *available = {position, normalizedDirection * speed, lifetime, damage, true};
    return true;
}

void ProjectilePool::update(float deltaSeconds) noexcept {
    const auto delta = std::clamp(deltaSeconds, 0.0F, 0.1F);
    for (auto& projectile : projectiles_) {
        if (!projectile.active)
            continue;
        projectile.position = projectile.position + projectile.velocity * delta;
        projectile.lifetime -= delta;
        if (projectile.lifetime <= 0.0F)
            projectile.active = false;
    }
}

void Weapon::update(float deltaSeconds) noexcept {
    cooldown = std::max(0.0F, cooldown - std::max(0.0F, deltaSeconds));
}

bool Weapon::tryFire(bool pressed, bool held) noexcept {
    const auto requested = automatic ? held : pressed;
    if (!requested || cooldown > 0.0F || ammunition <= 0 || fireInterval < 0.0F)
        return false;
    --ammunition;
    cooldown = fireInterval;
    return true;
}

} // namespace fabgl::frameworks
