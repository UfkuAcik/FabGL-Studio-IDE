#pragma once

#include <fabgl/core/result.h>
#include <fabgl/math/types.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fabgl::frameworks {

struct HealthArmor final {
    int health = 100;
    int armor = 0;

    [[nodiscard]] int applyDamage(int amount) noexcept;
    [[nodiscard]] bool alive() const noexcept {
        return health > 0;
    }
};

enum class DoorPhase { Closed, Opening, Open, Closing };

struct DoorState final {
    DoorPhase phase = DoorPhase::Closed;
    float openness = 0.0F;
    float speed = 1.5F;
    float holdSeconds = 2.0F;
    float holdRemaining = 0.0F;

    void activate() noexcept;
    void update(float deltaSeconds) noexcept;
    [[nodiscard]] bool blocksMovement() const noexcept {
        return openness < 0.9F;
    }
};

struct HitscanTarget final {
    Vec2 position{};
    float radius = 0.3F;
    bool active = true;
};

struct HitscanResult final {
    bool hit = false;
    std::size_t targetIndex = 0;
    float distance = 0.0F;
};

[[nodiscard]] HitscanResult hitscan(Vec2 origin, Vec2 direction, float maximumDistance,
                                    const std::vector<HitscanTarget>& targets) noexcept;

struct FpsGrid final {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> cells;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool blocked(Vec2 position, float radius = 0.2F) const noexcept;
};

struct FirstPersonState final {
    Vec2 position{1.5F, 1.5F};
    float yawRadians = 0.0F;
    float pitch = 0.0F;
};

struct FirstPersonInput final {
    float forward = 0.0F;
    float strafe = 0.0F;
    float mouseYaw = 0.0F;
    float mousePitch = 0.0F;
};

void updateFirstPerson(FirstPersonState& state, const FirstPersonInput& input, float movementSpeed,
                       float lookSensitivity, float maximumPitch, float bodyRadius,
                       float deltaSeconds, const FpsGrid& grid) noexcept;

struct FpsWeapon final {
    int magazineSize = 12;
    int ammunition = 12;
    int reserveAmmunition = 24;
    float fireInterval = 0.18F;
    float cooldown = 0.0F;
    float reloadSeconds = 0.8F;
    float reloadRemaining = 0.0F;
    bool projectile = false;

    void update(float deltaSeconds) noexcept;
    [[nodiscard]] bool tryFire() noexcept;
    [[nodiscard]] bool beginReload() noexcept;
    [[nodiscard]] bool reloading() const noexcept {
        return reloadRemaining > 0.0F;
    }
};

struct FpsProjectile final {
    Vec2 position{};
    Vec2 velocity{};
    float lifetime = 0.0F;
    int damage = 1;
    bool active = false;
};

class FpsProjectilePool final {
  public:
    explicit FpsProjectilePool(std::size_t capacity);
    [[nodiscard]] bool spawn(Vec2 position, Vec2 direction, float speed, float lifetime,
                             int damage) noexcept;
    void update(float deltaSeconds, const FpsGrid& grid) noexcept;
    [[nodiscard]] const std::vector<FpsProjectile>& projectiles() const noexcept {
        return projectiles_;
    }

  private:
    std::vector<FpsProjectile> projectiles_;
};

class FpsKeyRing final {
  public:
    explicit FpsKeyRing(std::size_t capacity = 16U) noexcept;
    [[nodiscard]] bool grant(std::uint16_t keyId) noexcept;
    [[nodiscard]] bool has(std::uint16_t keyId) const noexcept;
    [[nodiscard]] bool consume(std::uint16_t keyId) noexcept;
    [[nodiscard]] const std::vector<std::uint16_t>& keys() const noexcept {
        return keys_;
    }

  private:
    std::size_t capacity_ = 16U;
    std::vector<std::uint16_t> keys_;
};

struct LockedDoorState final {
    DoorState door{};
    std::uint16_t requiredKey = 0U;
    bool consumeKey = false;

    [[nodiscard]] bool activate(FpsKeyRing& keys) noexcept;
};

struct FpsEnemy final {
    Vec2 position{};
    float speed = 1.2F;
    float stopDistance = 0.6F;
    HealthArmor vitality{};
    bool active = true;

    void chase(Vec2 target, float deltaSeconds, const FpsGrid& grid) noexcept;
};

enum class FpsPickupKind : std::uint8_t { Health, Armor, Ammunition, Key };

struct FpsPickup final {
    std::uint32_t id = 0U;
    Vec2 position{};
    float radius = 0.35F;
    FpsPickupKind kind = FpsPickupKind::Health;
    int amount = 1;
    std::uint16_t keyId = 0U;
    bool active = true;
};

[[nodiscard]] bool applyFpsPickup(FpsPickup& pickup, Vec2 playerPosition, HealthArmor& health,
                                  FpsWeapon& weapon, FpsKeyRing& keys) noexcept;

enum class FpsTriggerKind : std::uint8_t { Event, LevelExit, SecretArea };

struct FpsTrigger final {
    std::uint32_t id = 0U;
    Rect bounds{};
    FpsTriggerKind kind = FpsTriggerKind::Event;
    bool oneShot = true;
    bool active = true;
};

struct FpsTriggerResult final {
    std::uint32_t activated = 0U;
    bool levelExit = false;
    bool secretArea = false;
};

[[nodiscard]] FpsTriggerResult activateFpsTriggers(Vec2 playerPosition,
                                                   std::vector<FpsTrigger>& triggers) noexcept;

struct FpsHudData final {
    int health = 0;
    int armor = 0;
    int ammunition = 0;
    int reserveAmmunition = 0;
    std::uint32_t keys = 0U;
    bool reloading = false;
};

[[nodiscard]] FpsHudData makeFpsHud(const HealthArmor& health, const FpsWeapon& weapon,
                                    const FpsKeyRing& keys) noexcept;

struct FpsSaveState final {
    FirstPersonState player{};
    HealthArmor vitality{};
    int ammunition = 0;
    int reserveAmmunition = 0;
    std::vector<std::uint16_t> keys;
    std::uint32_t level = 0U;
    std::uint32_t secretsFound = 0U;
};

[[nodiscard]] Result<std::string> serializeFpsSave(const FpsSaveState& state);
[[nodiscard]] Result<FpsSaveState> deserializeFpsSave(std::string_view source,
                                                      std::size_t maximumBytes = 4096U);

} // namespace fabgl::frameworks
