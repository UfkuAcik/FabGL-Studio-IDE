#pragma once

#include <fabgl/math/types.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace fabgl::frameworks {

struct ThirdPersonState final {
    Vec3 position{};
    Vec3 velocity{};
    float facingRadians = 0.0F;
    bool grounded = false;
    int health = 100;
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

struct ThirdPersonInput final {
    Vec2 movement{};
    bool jumpPressed = false;
};

struct ThirdPersonCharacterConfig final {
    float speed = 6.0F;
    float acceleration = 30.0F;
    float gravity = 20.0F;
    float jumpSpeed = 7.0F;
    float bodyRadius = 0.3F;
};

void updateThirdPersonCharacter(ThirdPersonState& state, const ThirdPersonInput& input,
                                float cameraYawRadians, float groundHeight, float deltaSeconds,
                                const ThirdPersonCharacterConfig& config,
                                const std::vector<Rect>& xzObstacles = {}) noexcept;

struct ThirdPersonCamera final {
    Vec3 pivot{};
    float yawRadians = 0.0F;
    float pitchRadians = 0.25F;
    float distance = 4.0F;
    float minimumDistance = 0.5F;
    float maximumDistance = 8.0F;
    float collisionRadius = 0.15F;

    void orbit(float yawDelta, float pitchDelta, float zoomDelta) noexcept;
    [[nodiscard]] Vec3 desiredPosition() const noexcept;
    [[nodiscard]] Vec3 resolveCollision(const std::vector<Rect>& xzObstacles) const noexcept;
};

enum class TpsAttackMode : std::uint8_t { Hitscan, Projectile };
enum class TpsCharacterPresentation : std::uint8_t { Billboard, LowPolyExperimental };

struct TpsWeapon final {
    TpsAttackMode mode = TpsAttackMode::Hitscan;
    int ammunition = 20;
    int damage = 10;
    float range = 20.0F;
    float fireInterval = 0.25F;
    float cooldown = 0.0F;
    float projectileSpeed = 12.0F;

    void update(float deltaSeconds) noexcept;
    [[nodiscard]] bool tryFire() noexcept;
};

struct TpsAttackResult final {
    bool fired = false;
    std::optional<std::size_t> target;
    Vec3 projectileVelocity{};
    int damage = 0;
};

[[nodiscard]] TpsAttackResult attackTps(TpsWeapon& weapon, Vec3 origin, Vec3 direction,
                                        const std::vector<TargetCandidate>& candidates) noexcept;

struct TpsEnemy final {
    Vec3 position{};
    int health = 30;
    bool active = true;
};

struct TpsPickup final {
    std::uint32_t id = 0U;
    Vec3 position{};
    float radius = 0.5F;
    int health = 0;
    int ammunition = 0;
    bool active = true;
};

[[nodiscard]] bool applyTpsPickup(TpsPickup& pickup, ThirdPersonState& player,
                                  TpsWeapon& weapon) noexcept;

[[nodiscard]] constexpr bool isExperimental(const TpsCharacterPresentation presentation) noexcept {
    return presentation == TpsCharacterPresentation::LowPolyExperimental;
}

} // namespace fabgl::frameworks
