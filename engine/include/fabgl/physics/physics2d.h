#pragma once

#include "fabgl/core/result.h"
#include "fabgl/math/types.h"

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

using CollisionShape2D = std::variant<AabbShape, CircleShape>;

struct PhysicsBody2D final {
    PhysicsBodyId id;
    Vec2 position{};
    Vec2 velocity{};
    CollisionShape2D shape = AabbShape{};
    std::uint32_t layer = 1U;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    bool dynamic = false;
    bool trigger = false;
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

class PhysicsWorld2D final {
  public:
    [[nodiscard]] Result<PhysicsBodyId> addBody(PhysicsBody2D body);
    [[nodiscard]] Result<void> removeBody(PhysicsBodyId id);
    [[nodiscard]] PhysicsBody2D* findBody(PhysicsBodyId id) noexcept;
    [[nodiscard]] const PhysicsBody2D* findBody(PhysicsBodyId id) const noexcept;
    [[nodiscard]] Result<void> setPosition(PhysicsBodyId id, Vec2 position);
    [[nodiscard]] Result<void> setVelocity(PhysicsBodyId id, Vec2 velocity);

    [[nodiscard]] Result<void> step(float deltaSeconds);
    [[nodiscard]] const std::vector<Contact2D>& contacts() const noexcept {
        return contacts_;
    }
    [[nodiscard]] Result<std::optional<RaycastHit2D>>
    raycast(Vec2 origin, Vec2 direction, float maximumDistance,
            std::uint32_t layerMask = 0xFFFFFFFFU) const;

  private:
    [[nodiscard]] Result<PhysicsBody2D*> requireBody(PhysicsBodyId id);

    std::map<std::uint32_t, PhysicsBody2D> bodies_;
    std::vector<Contact2D> contacts_;
    std::uint32_t nextBodyId_ = 1;
};

} // namespace fabgl
