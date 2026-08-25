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
namespace experimental {

struct PhysicsBody3DId final {
    std::uint32_t value = 0U;
    [[nodiscard]] bool valid() const noexcept {
        return value != 0U;
    }
    friend bool operator==(PhysicsBody3DId lhs, PhysicsBody3DId rhs) noexcept {
        return lhs.value == rhs.value;
    }
    friend bool operator!=(PhysicsBody3DId lhs, PhysicsBody3DId rhs) noexcept {
        return !(lhs == rhs);
    }
    friend bool operator<(PhysicsBody3DId lhs, PhysicsBody3DId rhs) noexcept {
        return lhs.value < rhs.value;
    }
};

struct AabbShape3D final {
    Vec3 halfExtents{0.5F, 0.5F, 0.5F};
};

struct SphereShape3D final {
    float radius = 0.5F;
};

// Upright capsule. halfHeight is the half length of the central Y-axis
// segment and radius forms the hemispherical ends.
struct CapsuleShape3D final {
    float radius = 0.5F;
    float halfHeight = 0.5F;
};

// Infinite one-sided plane. PhysicsBody3D::position is a point on the plane;
// normal points toward the allowed side.
struct PlaneShape3D final {
    Vec3 normal{0.0F, 1.0F, 0.0F};
};

using CollisionShape3D =
    std::variant<AabbShape3D, SphereShape3D, CapsuleShape3D, PlaneShape3D>;

struct PhysicsBody3D final {
    PhysicsBody3DId id;
    Vec3 position{};
    Vec3 velocity{};
    CollisionShape3D shape = AabbShape3D{};
    std::uint32_t layer = 1U;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    bool trigger = false;
    bool kinematic = false;
};

struct Contact3D final {
    PhysicsBody3DId first;
    PhysicsBody3DId second;
    Vec3 normal{};
    Vec3 point{};
    float penetration = 0.0F;
    bool trigger = false;
};

struct RaycastHit3D final {
    PhysicsBody3DId body;
    Vec3 point{};
    Vec3 normal{};
    float distance = 0.0F;
    bool trigger = false;
};

struct KinematicMove3D final {
    Vec3 position{};
    Vec3 velocity{};
    bool grounded = false;
    bool hitCeiling = false;
    bool hitWall = false;
    std::vector<PhysicsBody3DId> touchedBodies;
};

struct CharacterController3DSettings final {
    CapsuleShape3D capsule{0.4F, 0.6F};
    Vec3 gravity{0.0F, -9.81F, 0.0F};
    float skinWidth = 0.001F;
    std::uint32_t layer = 1U;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    std::uint32_t maximumSubsteps = 32U;
};

struct ArcadeVehicle3DSettings final {
    AabbShape3D collisionBox{{0.8F, 0.4F, 1.4F}};
    float acceleration = 12.0F;
    float braking = 20.0F;
    float maximumSpeed = 35.0F;
    float drag = 1.5F;
    float steeringRate = 1.8F;
    std::uint32_t layer = 1U;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    std::uint32_t maximumSubsteps = 32U;
};

struct ArcadeVehicle3DState final {
    Vec3 position{};
    Vec3 velocity{};
    float yawRadians = 0.0F;
    float speed = 0.0F;
    bool collided = false;
    std::vector<PhysicsBody3DId> touchedBodies;
};

enum class PhysicsDebugShape3D : std::uint8_t {
    Aabb,
    Sphere,
    Capsule,
    Plane,
};

struct PhysicsDebugPrimitive3D final {
    PhysicsBody3DId body;
    PhysicsDebugShape3D shape = PhysicsDebugShape3D::Aabb;
    Vec3 center{};
    Vec3 halfExtents{};
    Vec3 normal{};
    float radius = 0.0F;
    float halfHeight = 0.0F;
    bool trigger = false;
};

// Lightweight deterministic query/kinematic world. It deliberately omits a
// general rigid-body solver and keeps the bounded feature set suitable for an
// ESP32 runtime.
class PhysicsWorld3D final {
  public:
    static constexpr std::size_t MaximumBodies = 1024U;

    [[nodiscard]] Result<PhysicsBody3DId> addBody(PhysicsBody3D body);
    [[nodiscard]] Result<void> removeBody(PhysicsBody3DId id);
    [[nodiscard]] PhysicsBody3D* findBody(PhysicsBody3DId id) noexcept;
    [[nodiscard]] const PhysicsBody3D* findBody(PhysicsBody3DId id) const noexcept;
    [[nodiscard]] Result<void> setPosition(PhysicsBody3DId id, Vec3 position);
    [[nodiscard]] Result<void> setVelocity(PhysicsBody3DId id, Vec3 velocity);

    [[nodiscard]] Result<std::vector<Contact3D>> detectContacts(
        bool includeTriggers = true) const;
    [[nodiscard]] Result<std::optional<RaycastHit3D>> raycast(
        Vec3 origin, Vec3 direction, float maximumDistance,
        std::uint32_t layerMask = 0xFFFFFFFFU, bool includeTriggers = true) const;
    [[nodiscard]] Result<std::vector<PhysicsBody3DId>> overlap(
        Vec3 position, CollisionShape3D shape,
        std::uint32_t layerMask = 0xFFFFFFFFU, bool includeTriggers = true) const;
    [[nodiscard]] Result<KinematicMove3D>
    moveKinematic(PhysicsBody3DId id, Vec3 displacement,
                  std::uint32_t maximumSubsteps = 32U);
    [[nodiscard]] Result<KinematicMove3D>
    moveCharacter(Vec3 position, Vec3 velocity, float deltaSeconds,
                  const CharacterController3DSettings& settings = {}) const;
    [[nodiscard]] Result<ArcadeVehicle3DState>
    stepArcadeVehicle(ArcadeVehicle3DState state, float throttle, float steering,
                      float brake, float deltaSeconds,
                      const ArcadeVehicle3DSettings& settings = {}) const;
    [[nodiscard]] std::vector<PhysicsDebugPrimitive3D> debugPrimitives() const;
    [[nodiscard]] std::size_t bodyCount() const noexcept {
        return bodies_.size();
    }

  private:
    [[nodiscard]] Result<KinematicMove3D>
    moveShape(Vec3 position, Vec3 velocity, CollisionShape3D shape, Vec3 displacement,
              std::uint32_t layer, std::uint32_t collisionMask,
              std::uint32_t maximumSubsteps,
              std::optional<PhysicsBody3DId> ignoredBody) const;

    std::map<std::uint32_t, PhysicsBody3D> bodies_;
    std::uint32_t nextBodyId_ = 1U;
};

} // namespace experimental
} // namespace fabgl
