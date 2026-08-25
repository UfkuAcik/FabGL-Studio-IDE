#include <fabgl/frameworks/top_down.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace fabgl::frameworks {

namespace {

Vec2 normalized(const Vec2 value) noexcept {
    const auto length = std::sqrt(value.x * value.x + value.y * value.y);
    return std::isfinite(length) && length > 0.00001F ? Vec2{value.x / length, value.y / length}
                                                      : Vec2{};
}

bool finiteRect(const Rect value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.width) &&
           std::isfinite(value.height) && value.width >= 0.0F && value.height >= 0.0F;
}

} // namespace

Vec2 quantizeTopDownDirection(const Vec2 movement, const TopDownDirectionMode mode) noexcept {
    const auto direction = normalized(movement);
    if (mode == TopDownDirectionMode::Free || (direction.x == 0.0F && direction.y == 0.0F)) {
        return direction;
    }
    if (mode == TopDownDirectionMode::FourWay) {
        return std::fabs(direction.x) >= std::fabs(direction.y)
                   ? Vec2{direction.x < 0.0F ? -1.0F : 1.0F, 0.0F}
                   : Vec2{0.0F, direction.y < 0.0F ? -1.0F : 1.0F};
    }
    constexpr float QuarterTurn = 0.7853981633974483F;
    const auto octant = std::round(std::atan2(direction.y, direction.x) / QuarterTurn);
    return {std::cos(octant * QuarterTurn), std::sin(octant * QuarterTurn)};
}

void updateTopDown(TopDownState& state, const Vec2 movement, const Vec2 aim, const float speed,
                   const float deltaSeconds, const TopDownDirectionMode directionMode) noexcept {
    if (!std::isfinite(deltaSeconds) || !std::isfinite(state.position.x) ||
        !std::isfinite(state.position.y)) {
        return;
    }
    const auto direction = quantizeTopDownDirection(movement, directionMode);
    state.velocity = direction * std::max(0.0F, std::isfinite(speed) ? speed : 0.0F);
    state.position = state.position + state.velocity * std::clamp(deltaSeconds, 0.0F, 0.1F);
    const auto aimDirection = normalized(aim);
    if (aimDirection.x != 0.0F || aimDirection.y != 0.0F) {
        state.aim = aimDirection;
    }
}

ProjectilePool::ProjectilePool(const std::size_t capacity)
    : projectiles_(std::min<std::size_t>(capacity, 4096U)) {}

bool ProjectilePool::spawn(const Vec2 position, const Vec2 direction, const float speed,
                           const float lifetime, const int damage) noexcept {
    const auto normalizedDirection = normalized(direction);
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(speed) ||
        !std::isfinite(lifetime) || lifetime <= 0.0F || speed < 0.0F || damage < 0 ||
        (normalizedDirection.x == 0.0F && normalizedDirection.y == 0.0F)) {
        return false;
    }
    const auto available =
        std::find_if(projectiles_.begin(), projectiles_.end(),
                     [](const Projectile& projectile) { return !projectile.active; });
    if (available == projectiles_.end()) {
        return false;
    }
    *available = {position, normalizedDirection * speed, lifetime, damage, true};
    return true;
}

void ProjectilePool::update(const float deltaSeconds) noexcept {
    if (!std::isfinite(deltaSeconds)) {
        return;
    }
    const auto delta = std::clamp(deltaSeconds, 0.0F, 0.1F);
    for (auto& projectile : projectiles_) {
        if (!projectile.active) {
            continue;
        }
        projectile.position = projectile.position + projectile.velocity * delta;
        projectile.lifetime -= delta;
        if (projectile.lifetime <= 0.0F) {
            projectile.active = false;
        }
    }
}

std::size_t ProjectilePool::activeCount() const noexcept {
    return static_cast<std::size_t>(
        std::count_if(projectiles_.begin(), projectiles_.end(),
                      [](const Projectile& projectile) { return projectile.active; }));
}

bool ProjectilePool::deactivate(const std::size_t index) noexcept {
    if (index >= projectiles_.size() || !projectiles_[index].active) {
        return false;
    }
    projectiles_[index].active = false;
    return true;
}

void Weapon::update(const float deltaSeconds) noexcept {
    if (std::isfinite(deltaSeconds) && std::isfinite(cooldown)) {
        cooldown = std::max(0.0F, cooldown - std::max(0.0F, deltaSeconds));
    }
}

bool Weapon::tryFire(const bool pressed, const bool held) noexcept {
    const auto requested = automatic ? held : pressed;
    if (!requested || cooldown > 0.0F || ammunition <= 0 || !std::isfinite(fireInterval) ||
        fireInterval < 0.0F) {
        return false;
    }
    --ammunition;
    cooldown = fireInterval;
    return true;
}

int Weapon::reload() noexcept {
    magazineSize = std::max(0, magazineSize);
    ammunition = std::clamp(ammunition, 0, magazineSize);
    reserveAmmunition = std::max(0, reserveAmmunition);
    const auto transferred = std::min(magazineSize - ammunition, reserveAmmunition);
    ammunition += transferred;
    reserveAmmunition -= transferred;
    return transferred;
}

int TopDownHealth::damage(const int amount) noexcept {
    maximum = std::max(1, maximum);
    current = std::clamp(current, 0, maximum);
    const auto applied = std::min(current, std::max(0, amount));
    current -= applied;
    return applied;
}

int TopDownHealth::heal(const int amount) noexcept {
    maximum = std::max(1, maximum);
    current = std::clamp(current, 0, maximum);
    const auto applied = std::min(maximum - current, std::max(0, amount));
    current += applied;
    return applied;
}

TopDownHitscanResult topDownHitscan(const Vec2 origin, Vec2 direction, const float maximumDistance,
                                    const std::vector<TopDownHitscanTarget>& targets) noexcept {
    direction = normalized(direction);
    if ((direction.x == 0.0F && direction.y == 0.0F) || !std::isfinite(maximumDistance) ||
        maximumDistance <= 0.0F || !std::isfinite(origin.x) || !std::isfinite(origin.y)) {
        return {};
    }
    TopDownHitscanResult result;
    auto nearest = std::numeric_limits<float>::infinity();
    for (std::size_t index = 0U; index < targets.size(); ++index) {
        const auto& target = targets[index];
        if (!target.active || !std::isfinite(target.position.x) ||
            !std::isfinite(target.position.y) || !std::isfinite(target.radius) ||
            target.radius <= 0.0F) {
            continue;
        }
        const auto relative = target.position - origin;
        const auto projection = relative.x * direction.x + relative.y * direction.y;
        if (projection < 0.0F || projection > maximumDistance || projection >= nearest) {
            continue;
        }
        const auto closest = origin + direction * projection;
        const auto offset = target.position - closest;
        if (offset.x * offset.x + offset.y * offset.y <= target.radius * target.radius) {
            nearest = projection;
            result = {true, index, projection};
        }
    }
    return result;
}

void TopDownEnemy::chase(const Vec2 target, const float deltaSeconds) noexcept {
    if (!active || !health.alive() || !std::isfinite(target.x) || !std::isfinite(target.y) ||
        !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(speed) ||
        !std::isfinite(stopDistance) || !std::isfinite(deltaSeconds)) {
        velocity = {};
        return;
    }
    const auto offset = target - position;
    const auto distance = std::sqrt(offset.x * offset.x + offset.y * offset.y);
    if (distance <= std::max(0.0F, stopDistance) || distance <= 0.00001F) {
        velocity = {};
        return;
    }
    velocity = offset * (std::max(0.0F, speed) / distance);
    position = position + velocity * std::clamp(deltaSeconds, 0.0F, 0.1F);
}

TopDownInventory::TopDownInventory(const std::size_t maximumSlots) noexcept
    : maximumSlots_(std::clamp<std::size_t>(maximumSlots, 1U, 256U)) {
    slots_.reserve(maximumSlots_);
}

bool TopDownInventory::add(const std::uint16_t itemId, const std::uint16_t countToAdd) noexcept {
    if (itemId == 0U || countToAdd == 0U) {
        return false;
    }
    const auto found = std::find_if(slots_.begin(), slots_.end(),
                                    [itemId](const auto& slot) { return slot.itemId == itemId; });
    if (found != slots_.end()) {
        if (countToAdd > std::numeric_limits<std::uint16_t>::max() - found->count) {
            return false;
        }
        found->count = static_cast<std::uint16_t>(found->count + countToAdd);
        return true;
    }
    if (slots_.size() >= maximumSlots_) {
        return false;
    }
    slots_.push_back({itemId, countToAdd});
    return true;
}

bool TopDownInventory::consume(const std::uint16_t itemId,
                               const std::uint16_t countToConsume) noexcept {
    if (itemId == 0U || countToConsume == 0U) {
        return false;
    }
    const auto found = std::find_if(slots_.begin(), slots_.end(),
                                    [itemId](const auto& slot) { return slot.itemId == itemId; });
    if (found == slots_.end() || found->count < countToConsume) {
        return false;
    }
    found->count = static_cast<std::uint16_t>(found->count - countToConsume);
    if (found->count == 0U) {
        slots_.erase(found);
    }
    return true;
}

std::uint16_t TopDownInventory::count(const std::uint16_t itemId) const noexcept {
    const auto found = std::find_if(slots_.begin(), slots_.end(),
                                    [itemId](const auto& slot) { return slot.itemId == itemId; });
    return found == slots_.end() ? 0U : found->count;
}

bool applyTopDownPickup(TopDownPickup& pickup, const Vec2 playerPosition, TopDownHealth& health,
                        Weapon& weapon, TopDownInventory& inventory) noexcept {
    if (!pickup.active || !finiteRect(pickup.bounds) || !pickup.bounds.contains(playerPosition) ||
        pickup.amount <= 0) {
        return false;
    }
    bool applied = false;
    switch (pickup.kind) {
    case TopDownPickupKind::Health:
        applied = health.heal(pickup.amount) > 0;
        break;
    case TopDownPickupKind::Ammunition:
        if (pickup.amount <= std::numeric_limits<int>::max() - weapon.reserveAmmunition) {
            weapon.reserveAmmunition += pickup.amount;
            applied = true;
        }
        break;
    case TopDownPickupKind::Inventory:
        if (pickup.amount <= std::numeric_limits<std::uint16_t>::max()) {
            applied = inventory.add(pickup.itemId, static_cast<std::uint16_t>(pickup.amount));
        }
        break;
    }
    if (applied) {
        pickup.active = false;
    }
    return applied;
}

bool TopDownArena::valid() const noexcept {
    return finiteRect(bounds) && bounds.width > 0.0F && bounds.height > 0.0F;
}

Vec2 TopDownArena::constrain(Vec2 position, const Vec2 bodySize) const noexcept {
    if (!valid() || !std::isfinite(position.x) || !std::isfinite(position.y) ||
        !std::isfinite(bodySize.x) || !std::isfinite(bodySize.y)) {
        return position;
    }
    const auto width = std::clamp(bodySize.x, 0.0F, bounds.width);
    const auto height = std::clamp(bodySize.y, 0.0F, bounds.height);
    position.x = std::clamp(position.x, bounds.x, bounds.x + bounds.width - width);
    position.y = std::clamp(position.y, bounds.y, bounds.y + bounds.height - height);
    return position;
}

std::optional<std::uint32_t>
topDownRoomAt(const Vec2 playerPosition,
              const std::vector<TopDownRoomTransition>& transitions) noexcept {
    if (!std::isfinite(playerPosition.x) || !std::isfinite(playerPosition.y)) {
        return std::nullopt;
    }
    for (const auto& transition : transitions) {
        if (transition.enabled && finiteRect(transition.bounds) &&
            transition.bounds.contains(playerPosition)) {
            return transition.roomId;
        }
    }
    return std::nullopt;
}

} // namespace fabgl::frameworks
