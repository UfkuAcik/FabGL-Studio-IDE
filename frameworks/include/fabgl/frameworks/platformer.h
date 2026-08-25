#pragma once

#include <fabgl/math/types.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace fabgl::frameworks {

struct PlatformerConfig final {
    float moveSpeed = 70.0F;
    float acceleration = 700.0F;
    float gravity = 280.0F;
    float jumpSpeed = 125.0F;
    float coyoteTime = 0.10F;
    float jumpBufferTime = 0.12F;
    float maximumFallSpeed = 180.0F;
    Vec2 bodySize{8.0F, 12.0F};
};

struct PlatformerInput final {
    float horizontal = 0.0F;
    bool jumpPressed = false;
    bool jumpHeld = false;
};

struct PlatformerState final {
    Vec2 position{};
    Vec2 velocity{};
    bool grounded = false;
    float coyoteRemaining = 0.0F;
    float jumpBufferRemaining = 0.0F;
};

class PlatformerController final {
  public:
    explicit PlatformerController(PlatformerConfig config = {}) : config_(config) {}

    void step(PlatformerState& state, const PlatformerInput& input, float deltaSeconds,
              const std::vector<Rect>& solidPlatforms,
              const std::vector<Rect>& oneWayPlatforms = {}) const noexcept;
    [[nodiscard]] Vec2 bodySize() const noexcept {
        return config_.bodySize;
    }

  private:
    PlatformerConfig config_;
};

struct MovingPlatform final {
    Rect bounds{};
    Vec2 velocity{};
    Vec2 minimum{};
    Vec2 maximum{};
    bool pingPong = true;

    [[nodiscard]] Vec2 update(float deltaSeconds) noexcept;
};

struct PlatformerCollectible final {
    std::uint32_t id = 0U;
    Rect bounds{};
    int value = 1;
    bool active = true;
};

struct PlatformerEnemy final {
    std::uint32_t id = 0U;
    Rect bounds{};
    float patrolMinimumX = 0.0F;
    float patrolMaximumX = 0.0F;
    float speed = 20.0F;
    int contactDamage = 1;
    bool active = true;
    bool movingRight = true;

    void update(float deltaSeconds) noexcept;
};

struct PlatformerCheckpoint final {
    std::uint32_t id = 0U;
    Rect bounds{};
    Vec2 respawnPosition{};
};

struct PlatformerLevelTransition final {
    std::uint32_t levelId = 0U;
    Rect bounds{};
    bool enabled = true;
};

struct PlatformerWorldState final {
    int maximumHealth = 3;
    int health = 3;
    int score = 0;
    std::uint32_t collected = 0U;
    std::optional<std::uint32_t> checkpointId;
    Vec2 respawnPosition{};
    std::optional<std::uint32_t> requestedLevel;
    float damageCooldown = 0.0F;
    bool dead = false;
};

struct PlatformerCamera final {
    Vec2 position{};
    Vec2 viewport{320.0F, 180.0F};
    Rect worldBounds{0.0F, 0.0F, 320.0F, 180.0F};
    float followRate = 8.0F;

    void follow(Vec2 target, float deltaSeconds) noexcept;
};

struct PlatformerHud final {
    int health = 0;
    int maximumHealth = 0;
    int score = 0;
    std::uint32_t collected = 0U;
    bool gameOverVisible = false;
};

class PlatformerWorld final {
  public:
    explicit PlatformerWorld(std::size_t maximumRecords = 256U);

    [[nodiscard]] bool addMovingPlatform(MovingPlatform platform);
    [[nodiscard]] bool addCollectible(PlatformerCollectible collectible);
    [[nodiscard]] bool addEnemy(PlatformerEnemy enemy);
    [[nodiscard]] bool addCheckpoint(PlatformerCheckpoint checkpoint);
    [[nodiscard]] bool addTransition(PlatformerLevelTransition transition);
    void update(PlatformerController& controller, PlatformerState& player,
                PlatformerWorldState& state, PlatformerCamera& camera, const PlatformerInput& input,
                float deltaSeconds, const std::vector<Rect>& staticPlatforms,
                const std::vector<Rect>& oneWayPlatforms = {}) noexcept;
    [[nodiscard]] PlatformerHud hud(const PlatformerWorldState& state) const noexcept;

    [[nodiscard]] const std::vector<MovingPlatform>& movingPlatforms() const noexcept {
        return movingPlatforms_;
    }
    [[nodiscard]] const std::vector<PlatformerCollectible>& collectibles() const noexcept {
        return collectibles_;
    }
    [[nodiscard]] const std::vector<PlatformerEnemy>& enemies() const noexcept {
        return enemies_;
    }

  private:
    std::size_t maximumRecords_ = 256U;
    std::vector<MovingPlatform> movingPlatforms_;
    std::vector<PlatformerCollectible> collectibles_;
    std::vector<PlatformerEnemy> enemies_;
    std::vector<PlatformerCheckpoint> checkpoints_;
    std::vector<PlatformerLevelTransition> transitions_;
};

} // namespace fabgl::frameworks
