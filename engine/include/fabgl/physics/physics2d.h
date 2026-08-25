#pragma once

#include "fabgl/core/result.h"
#include "fabgl/math/types.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <variant>
#include <vector>

namespace fabgl {

struct PhysicsBodyId final {
    std::uint32_t value = 0;

    [[nodiscard]] bool valid() const noexcept {
        return value != 0;
    }
    friend bool operator==(PhysicsBodyId lhs, PhysicsBodyId rhs) noexcept {
        return lhs.value == rhs.value;
    }
    friend bool operator!=(PhysicsBodyId lhs, PhysicsBodyId rhs) noexcept {
        return !(lhs == rhs);
    }
    friend bool operator<(PhysicsBodyId lhs, PhysicsBodyId rhs) noexcept {
        return lhs.value < rhs.value;
    }
};

struct AabbShape final {
    Vec2 halfExtents{0.5F, 0.5F};
};

struct CircleShape final {
    float radius = 0.5F;
};

struct PointShape final {};

using CollisionShape2D = std::variant<AabbShape, CircleShape, PointShape>;

struct PhysicsBody2D final {
    PhysicsBodyId id;
    Vec2 position{};
    Vec2 velocity{};
    CollisionShape2D shape = AabbShape{};
    std::uint32_t layer = 1U;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    bool dynamic = false;
    bool trigger = false;
    bool kinematic = false;
    float mass = 1.0F;
    float gravityScale = 1.0F;
    float restitution = 0.0F;
    float friction = 0.5F;
};

struct Contact2D final {
    PhysicsBodyId first;
    PhysicsBodyId second;
    Vec2 normal{};
    Vec2 point{};
    float penetration = 0.0F;
    bool trigger = false;
};

struct RaycastHit2D final {
    PhysicsBodyId body;
    Vec2 point{};
    Vec2 normal{};
    float distance = 0.0F;
    bool trigger = false;
};

struct TileCollisionMapId final {
    std::uint32_t value = 0U;

    [[nodiscard]] bool valid() const noexcept {
        return value != 0U;
    }
    friend bool operator==(TileCollisionMapId lhs, TileCollisionMapId rhs) noexcept {
        return lhs.value == rhs.value;
    }
};

// A compact collision-only grid. Non-zero cells are materialized as static
// AABBs in the world, so ordinary contacts, raycasts, overlap queries and
// character movement all observe exactly the same geometry.
struct TileCollisionMap2D final {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    Vec2 origin{};
    Vec2 tileSize{1.0F, 1.0F};
    std::vector<std::uint8_t> solidCells;
    std::uint32_t layer = 1U;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    bool trigger = false;
};

struct CharacterController2DSettings final {
    Vec2 halfExtents{0.4F, 0.9F};
    Vec2 gravity{0.0F, 9.81F};
    float skinWidth = 0.001F;
    std::uint32_t layer = 1U;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    std::uint32_t maximumSubsteps = 32U;
};

struct CharacterMove2D final {
    Vec2 position{};
    Vec2 velocity{};
    bool grounded = false;
    bool hitCeiling = false;
    bool hitWall = false;
    std::vector<PhysicsBodyId> touchedBodies;
};

enum class PhysicsDebugShape2D : std::uint8_t {
    Aabb,
    Circle,
    Point,
};

struct PhysicsDebugPrimitive2D final {
    PhysicsBodyId body;
    PhysicsDebugShape2D shape = PhysicsDebugShape2D::Point;
    Vec2 center{};
    Vec2 halfExtents{};
    float radius = 0.0F;
    bool trigger = false;
};

class PhysicsWorld2D final {
  public:
    static constexpr std::size_t MaximumBodies = 4096U;
    static constexpr std::size_t MaximumTileCells = 65536U;

    [[nodiscard]] Result<void> setGravity(Vec2 gravity);
    [[nodiscard]] Vec2 gravity() const noexcept {
        return gravity_;
    }
    [[nodiscard]] Result<PhysicsBodyId> addBody(PhysicsBody2D body);
    [[nodiscard]] Result<void> removeBody(PhysicsBodyId id);
    [[nodiscard]] PhysicsBody2D* findBody(PhysicsBodyId id) noexcept;
    [[nodiscard]] const PhysicsBody2D* findBody(PhysicsBodyId id) const noexcept;
    [[nodiscard]] Result<void> setPosition(PhysicsBodyId id, Vec2 position);
    [[nodiscard]] Result<void> setVelocity(PhysicsBodyId id, Vec2 velocity);

    [[nodiscard]] Result<TileCollisionMapId> addTileCollisionMap(TileCollisionMap2D map);
    [[nodiscard]] Result<void> removeTileCollisionMap(TileCollisionMapId id);
    [[nodiscard]] const TileCollisionMap2D* findTileCollisionMap(
        TileCollisionMapId id) const noexcept;

    [[nodiscard]] Result<void> step(float deltaSeconds);
    [[nodiscard]] const std::vector<Contact2D>& contacts() const noexcept {
        return contacts_;
    }
    [[nodiscard]] Result<std::optional<RaycastHit2D>>
    raycast(Vec2 origin, Vec2 direction, float maximumDistance,
            std::uint32_t layerMask = 0xFFFFFFFFU) const;
    [[nodiscard]] Result<std::vector<PhysicsBodyId>>
    overlap(Vec2 position, CollisionShape2D shape,
            std::uint32_t layerMask = 0xFFFFFFFFU, bool includeTriggers = true) const;
    [[nodiscard]] Result<CharacterMove2D>
    moveCharacter(Vec2 position, Vec2 velocity, float deltaSeconds,
                  const CharacterController2DSettings& settings = {}) const;
    [[nodiscard]] std::vector<PhysicsDebugPrimitive2D> debugPrimitives() const;
    [[nodiscard]] std::size_t bodyCount() const noexcept {
        return bodies_.size();
    }

  private:
    struct TileCollisionRecord final {
        TileCollisionMap2D map;
        std::vector<PhysicsBodyId> bodies;
    };

    [[nodiscard]] Result<PhysicsBody2D*> requireBody(PhysicsBodyId id);

    std::map<std::uint32_t, PhysicsBody2D> bodies_;
    std::map<std::uint32_t, TileCollisionRecord> tileCollisionMaps_;
    std::vector<Contact2D> contacts_;
    std::uint32_t nextBodyId_ = 1;
    std::uint32_t nextTileCollisionMapId_ = 1U;
    Vec2 gravity_{0.0F, 9.81F};
};

} // namespace fabgl
