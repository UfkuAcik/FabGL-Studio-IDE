#pragma once

#include <fabgl/math/types.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace fabgl::frameworks {

struct TopDownState final {
    Vec2 position{};
    Vec2 velocity{};
    Vec2 aim{1.0F, 0.0F};
};

enum class TopDownDirectionMode : std::uint8_t { Free, FourWay, EightWay };

[[nodiscard]] Vec2 quantizeTopDownDirection(Vec2 movement, TopDownDirectionMode mode) noexcept;

void updateTopDown(TopDownState& state, Vec2 movement, Vec2 aim, float speed, float deltaSeconds,
                   TopDownDirectionMode directionMode = TopDownDirectionMode::Free) noexcept;

struct Projectile final {
    Vec2 position{};
    Vec2 velocity{};
    float lifetime = 0.0F;
    int damage = 1;
    bool active = false;
};

class ProjectilePool final {
  public:
    explicit ProjectilePool(std::size_t capacity);
    [[nodiscard]] bool spawn(Vec2 position, Vec2 direction, float speed, float lifetime,
                             int damage) noexcept;
    void update(float deltaSeconds) noexcept;
    [[nodiscard]] std::size_t activeCount() const noexcept;
    [[nodiscard]] bool deactivate(std::size_t index) noexcept;
    [[nodiscard]] const std::vector<Projectile>& projectiles() const noexcept {
        return projectiles_;
    }

  private:
    std::vector<Projectile> projectiles_;
};

struct Weapon final {
    int magazineSize = 8;
    int ammunition = 8;
    int reserveAmmunition = 0;
    float fireInterval = 0.15F;
    float cooldown = 0.0F;
    bool automatic = false;

    void update(float deltaSeconds) noexcept;
    [[nodiscard]] bool tryFire(bool pressed, bool held) noexcept;
    [[nodiscard]] int reload() noexcept;
};

struct TopDownHealth final {
    int maximum = 100;
    int current = 100;

    [[nodiscard]] int damage(int amount) noexcept;
    [[nodiscard]] int heal(int amount) noexcept;
    [[nodiscard]] bool alive() const noexcept {
        return current > 0;
    }
};

struct TopDownHitscanTarget final {
    Vec2 position{};
    float radius = 0.3F;
    bool active = true;
};

struct TopDownHitscanResult final {
    bool hit = false;
    std::size_t targetIndex = 0U;
    float distance = 0.0F;
};

[[nodiscard]] TopDownHitscanResult
topDownHitscan(Vec2 origin, Vec2 direction, float maximumDistance,
               const std::vector<TopDownHitscanTarget>& targets) noexcept;

struct TopDownEnemy final {
    Vec2 position{};
    Vec2 velocity{};
    float speed = 25.0F;
    float stopDistance = 0.5F;
    TopDownHealth health{};
    bool active = true;

    void chase(Vec2 target, float deltaSeconds) noexcept;
};

struct TopDownInventorySlot final {
    std::uint16_t itemId = 0U;
    std::uint16_t count = 0U;
};

class TopDownInventory final {
  public:
    explicit TopDownInventory(std::size_t maximumSlots = 16U) noexcept;

    [[nodiscard]] bool add(std::uint16_t itemId, std::uint16_t count = 1U) noexcept;
    [[nodiscard]] bool consume(std::uint16_t itemId, std::uint16_t count = 1U) noexcept;
    [[nodiscard]] std::uint16_t count(std::uint16_t itemId) const noexcept;
    [[nodiscard]] const std::vector<TopDownInventorySlot>& slots() const noexcept {
        return slots_;
    }

  private:
    std::size_t maximumSlots_ = 16U;
    std::vector<TopDownInventorySlot> slots_;
};

enum class TopDownPickupKind : std::uint8_t { Health, Ammunition, Inventory };

struct TopDownPickup final {
    std::uint32_t id = 0U;
    Rect bounds{};
    TopDownPickupKind kind = TopDownPickupKind::Health;
    std::uint16_t itemId = 0U;
    int amount = 1;
    bool active = true;
};

[[nodiscard]] bool applyTopDownPickup(TopDownPickup& pickup, Vec2 playerPosition,
                                      TopDownHealth& health, Weapon& weapon,
                                      TopDownInventory& inventory) noexcept;

struct TopDownArena final {
    Rect bounds{};

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] Vec2 constrain(Vec2 position, Vec2 bodySize = {}) const noexcept;
};

struct TopDownRoomTransition final {
    std::uint32_t roomId = 0U;
    Rect bounds{};
    bool enabled = true;
};

[[nodiscard]] std::optional<std::uint32_t>
topDownRoomAt(Vec2 playerPosition, const std::vector<TopDownRoomTransition>& transitions) noexcept;

} // namespace fabgl::frameworks
