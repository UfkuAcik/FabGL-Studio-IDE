#include "fabgl/physics/physics3d.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <type_traits>
#include <utility>

namespace fabgl {
namespace experimental {
namespace {

constexpr float Epsilon = 0.000001F;

struct CollisionGeometry final {
    Vec3 normal{};
    Vec3 point{};
    float penetration = 0.0F;
};

struct ShapeRayHit final {
    Vec3 point{};
    Vec3 normal{};
    float distance = 0.0F;
};

[[nodiscard]] bool finite(Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] float dot(Vec3 lhs, Vec3 rhs) noexcept {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

[[nodiscard]] float lengthSquared(Vec3 value) noexcept {
    return dot(value, value);
}

[[nodiscard]] float length(Vec3 value) noexcept {
    return std::sqrt(lengthSquared(value));
}

[[nodiscard]] Vec3 normalized(Vec3 value) noexcept {
    const auto magnitude = length(value);
    return magnitude > Epsilon ? value * (1.0F / magnitude) : Vec3{1.0F, 0.0F, 0.0F};
}

[[nodiscard]] Vec3 negated(Vec3 value) noexcept {
    return {-value.x, -value.y, -value.z};
}

[[nodiscard]] float component(Vec3 value, std::size_t axis) noexcept {
    if (axis == 0U)
        return value.x;
    return axis == 1U ? value.y : value.z;
}

[[nodiscard]] Vec3 axisVector(std::size_t axis, float value) noexcept {
    if (axis == 0U)
        return {value, 0.0F, 0.0F};
    if (axis == 1U)
        return {0.0F, value, 0.0F};
    return {0.0F, 0.0F, value};
}

[[nodiscard]] bool validShape(const CollisionShape3D& shape) noexcept {
    return std::visit(
        [](const auto& concrete) {
            using Shape = std::decay_t<decltype(concrete)>;
            if constexpr (std::is_same_v<Shape, AabbShape3D>) {
                return finite(concrete.halfExtents) && concrete.halfExtents.x >= 0.0F &&
                       concrete.halfExtents.y >= 0.0F && concrete.halfExtents.z >= 0.0F;
            } else if constexpr (std::is_same_v<Shape, SphereShape3D>) {
                return std::isfinite(concrete.radius) && concrete.radius >= 0.0F;
            } else if constexpr (std::is_same_v<Shape, CapsuleShape3D>) {
                return std::isfinite(concrete.radius) && concrete.radius >= 0.0F &&
                       std::isfinite(concrete.halfHeight) && concrete.halfHeight >= 0.0F;
            } else {
                return finite(concrete.normal) && lengthSquared(concrete.normal) > Epsilon;
            }
        },
        shape);
}

[[nodiscard]] float shapeMotionSpan(const CollisionShape3D& shape) noexcept {
    return std::visit(
        [](const auto& concrete) {
            using Shape = std::decay_t<decltype(concrete)>;
            if constexpr (std::is_same_v<Shape, AabbShape3D>) {
                return std::max(Epsilon, std::min({concrete.halfExtents.x,
                                                   concrete.halfExtents.y,
                                                   concrete.halfExtents.z}));
            } else if constexpr (std::is_same_v<Shape, SphereShape3D> ||
                                 std::is_same_v<Shape, CapsuleShape3D>) {
                return std::max(Epsilon, concrete.radius);
            } else {
                return 0.0F;
            }
        },
        shape);
}

[[nodiscard]] std::optional<CollisionGeometry> collideAabbs(Vec3 aPosition, AabbShape3D a,
                                                            Vec3 bPosition, AabbShape3D b) {
    const auto delta = bPosition - aPosition;
    const auto x = a.halfExtents.x + b.halfExtents.x - std::abs(delta.x);
    const auto y = a.halfExtents.y + b.halfExtents.y - std::abs(delta.y);
    const auto z = a.halfExtents.z + b.halfExtents.z - std::abs(delta.z);
    if (x < 0.0F || y < 0.0F || z < 0.0F)
        return std::nullopt;
    CollisionGeometry collision;
    collision.point = (aPosition + bPosition) * 0.5F;
    collision.penetration = x;
    collision.normal = {delta.x >= 0.0F ? 1.0F : -1.0F, 0.0F, 0.0F};
    if (y < collision.penetration) {
        collision.penetration = y;
        collision.normal = {0.0F, delta.y >= 0.0F ? 1.0F : -1.0F, 0.0F};
    }
    if (z < collision.penetration) {
        collision.penetration = z;
        collision.normal = {0.0F, 0.0F, delta.z >= 0.0F ? 1.0F : -1.0F};
    }
    return collision;
}

[[nodiscard]] std::optional<CollisionGeometry> collideSpheres(Vec3 aPosition, float aRadius,
                                                              Vec3 bPosition, float bRadius) {
    const auto delta = bPosition - aPosition;
    const auto distance = length(delta);
    const auto radius = aRadius + bRadius;
    if (distance > radius)
        return std::nullopt;
    const auto normal = distance > Epsilon ? delta * (1.0F / distance) : Vec3{1.0F, 0.0F, 0.0F};
    return CollisionGeometry{normal, aPosition + normal * aRadius, radius - distance};
}

[[nodiscard]] std::optional<CollisionGeometry> collideSphereAabb(Vec3 spherePosition,
                                                                 float radius,
                                                                 Vec3 boxPosition,
                                                                 AabbShape3D box) {
    const Vec3 minimum = boxPosition - box.halfExtents;
    const Vec3 maximum = boxPosition + box.halfExtents;
    const Vec3 closest{
        std::clamp(spherePosition.x, minimum.x, maximum.x),
        std::clamp(spherePosition.y, minimum.y, maximum.y),
        std::clamp(spherePosition.z, minimum.z, maximum.z),
    };
    const auto delta = closest - spherePosition;
    const auto distance = length(delta);
    if (distance > radius)
        return std::nullopt;
    if (distance > Epsilon)
        return CollisionGeometry{delta * (1.0F / distance), closest, radius - distance};

    const auto local = spherePosition - boxPosition;
    const auto x = box.halfExtents.x - std::abs(local.x);
    const auto y = box.halfExtents.y - std::abs(local.y);
    const auto z = box.halfExtents.z - std::abs(local.z);
    CollisionGeometry collision;
    collision.normal = {local.x >= 0.0F ? -1.0F : 1.0F, 0.0F, 0.0F};
    collision.penetration = radius + x;
    if (y < x && y <= z) {
        collision.normal = {0.0F, local.y >= 0.0F ? -1.0F : 1.0F, 0.0F};
        collision.penetration = radius + y;
    } else if (z < x && z < y) {
        collision.normal = {0.0F, 0.0F, local.z >= 0.0F ? -1.0F : 1.0F};
        collision.penetration = radius + z;
    }
    collision.point = spherePosition + collision.normal * radius;
    return collision;
}

[[nodiscard]] std::pair<Vec3, Vec3> closestCapsulePoints(Vec3 aPosition, CapsuleShape3D a,
                                                         Vec3 bPosition,
                                                         CapsuleShape3D b) noexcept {
    const auto aMinimum = aPosition.y - a.halfHeight;
    const auto aMaximum = aPosition.y + a.halfHeight;
    const auto bMinimum = bPosition.y - b.halfHeight;
    const auto bMaximum = bPosition.y + b.halfHeight;
    float aY = 0.0F;
    float bY = 0.0F;
    if (aMaximum < bMinimum) {
        aY = aMaximum;
        bY = bMinimum;
    } else if (bMaximum < aMinimum) {
        aY = aMinimum;
        bY = bMaximum;
    } else {
        aY = std::max(aMinimum, bMinimum);
        bY = aY;
    }
    return {{aPosition.x, aY, aPosition.z}, {bPosition.x, bY, bPosition.z}};
}

[[nodiscard]] std::optional<CollisionGeometry> collideCapsuleSphere(
    Vec3 capsulePosition, CapsuleShape3D capsule, Vec3 spherePosition, float sphereRadius) {
    const auto closestY = std::clamp(spherePosition.y, capsulePosition.y - capsule.halfHeight,
                                     capsulePosition.y + capsule.halfHeight);
    return collideSpheres({capsulePosition.x, closestY, capsulePosition.z}, capsule.radius,
                          spherePosition, sphereRadius);
}

[[nodiscard]] std::optional<CollisionGeometry> collideCapsules(Vec3 aPosition,
                                                               CapsuleShape3D a,
                                                               Vec3 bPosition,
                                                               CapsuleShape3D b) {
    const auto [aPoint, bPoint] = closestCapsulePoints(aPosition, a, bPosition, b);
    return collideSpheres(aPoint, a.radius, bPoint, b.radius);
}

[[nodiscard]] std::optional<CollisionGeometry> collideCapsuleAabb(Vec3 capsulePosition,
                                                                 CapsuleShape3D capsule,
                                                                 Vec3 boxPosition,
                                                                 AabbShape3D box) {
    const Vec3 minimum = boxPosition - box.halfExtents;
    const Vec3 maximum = boxPosition + box.halfExtents;
    const auto segmentMinimum = capsulePosition.y - capsule.halfHeight;
    const auto segmentMaximum = capsulePosition.y + capsule.halfHeight;
    float segmentY = 0.0F;
    float boxY = 0.0F;
    if (segmentMaximum < minimum.y) {
        segmentY = segmentMaximum;
        boxY = minimum.y;
    } else if (segmentMinimum > maximum.y) {
        segmentY = segmentMinimum;
        boxY = maximum.y;
    } else {
        segmentY = std::clamp(capsulePosition.y, minimum.y, maximum.y);
        boxY = segmentY;
    }
    const Vec3 segmentPoint{capsulePosition.x, segmentY, capsulePosition.z};
    const Vec3 boxPoint{std::clamp(capsulePosition.x, minimum.x, maximum.x), boxY,
                        std::clamp(capsulePosition.z, minimum.z, maximum.z)};
    const auto delta = boxPoint - segmentPoint;
    const auto distance = length(delta);
    if (distance > capsule.radius)
        return std::nullopt;
    if (distance > Epsilon) {
        return CollisionGeometry{delta * (1.0F / distance), boxPoint,
                                 capsule.radius - distance};
    }

    const Vec3 expanded{box.halfExtents.x + capsule.radius,
                        box.halfExtents.y + capsule.radius + capsule.halfHeight,
                        box.halfExtents.z + capsule.radius};
    const auto local = capsulePosition - boxPosition;
    const auto x = expanded.x - std::abs(local.x);
    const auto y = expanded.y - std::abs(local.y);
    const auto z = expanded.z - std::abs(local.z);
    CollisionGeometry collision;
    collision.normal = {local.x >= 0.0F ? -1.0F : 1.0F, 0.0F, 0.0F};
    collision.penetration = x;
    if (y < x && y <= z) {
        collision.normal = {0.0F, local.y >= 0.0F ? -1.0F : 1.0F, 0.0F};
        collision.penetration = y;
    } else if (z < x && z < y) {
        collision.normal = {0.0F, 0.0F, local.z >= 0.0F ? -1.0F : 1.0F};
        collision.penetration = z;
    }
    collision.point = capsulePosition + collision.normal * capsule.radius;
    return collision;
}

[[nodiscard]] float shapeSupport(const CollisionShape3D& shape, Vec3 normal) noexcept {
    return std::visit(
        [normal](const auto& concrete) {
            using Shape = std::decay_t<decltype(concrete)>;
            if constexpr (std::is_same_v<Shape, AabbShape3D>) {
                return std::abs(normal.x) * concrete.halfExtents.x +
                       std::abs(normal.y) * concrete.halfExtents.y +
                       std::abs(normal.z) * concrete.halfExtents.z;
            } else if constexpr (std::is_same_v<Shape, SphereShape3D>) {
                return concrete.radius;
            } else if constexpr (std::is_same_v<Shape, CapsuleShape3D>) {
                return concrete.radius + std::abs(normal.y) * concrete.halfHeight;
            } else {
                return 0.0F;
            }
        },
        shape);
}

[[nodiscard]] std::optional<CollisionGeometry> collidePlaneShape(
    Vec3 planePosition, PlaneShape3D plane, Vec3 shapePosition,
    const CollisionShape3D& shape) {
    if (std::holds_alternative<PlaneShape3D>(shape))
        return std::nullopt;
    const auto normal = normalized(plane.normal);
    const auto support = shapeSupport(shape, normal);
    const auto distance = dot(shapePosition - planePosition, normal);
    if (distance > support)
        return std::nullopt;
    return CollisionGeometry{normal, shapePosition - normal * support, support - distance};
}

[[nodiscard]] std::optional<CollisionGeometry> collide(Vec3 aPosition,
                                                       const CollisionShape3D& a,
                                                       Vec3 bPosition,
                                                       const CollisionShape3D& b) {
    if (const auto* aPlane = std::get_if<PlaneShape3D>(&a))
        return collidePlaneShape(aPosition, *aPlane, bPosition, b);
    if (const auto* bPlane = std::get_if<PlaneShape3D>(&b)) {
        auto collision = collidePlaneShape(bPosition, *bPlane, aPosition, a);
        if (collision)
            collision->normal = negated(collision->normal);
        return collision;
    }
    if (const auto* aBox = std::get_if<AabbShape3D>(&a)) {
        if (const auto* bBox = std::get_if<AabbShape3D>(&b))
            return collideAabbs(aPosition, *aBox, bPosition, *bBox);
        if (const auto* bSphere = std::get_if<SphereShape3D>(&b)) {
            auto collision = collideSphereAabb(bPosition, bSphere->radius, aPosition, *aBox);
            if (collision)
                collision->normal = negated(collision->normal);
            return collision;
        }
        auto collision = collideCapsuleAabb(bPosition, std::get<CapsuleShape3D>(b),
                                            aPosition, *aBox);
        if (collision)
            collision->normal = negated(collision->normal);
        return collision;
    }
    if (const auto* aSphere = std::get_if<SphereShape3D>(&a)) {
        if (const auto* bBox = std::get_if<AabbShape3D>(&b))
            return collideSphereAabb(aPosition, aSphere->radius, bPosition, *bBox);
        if (const auto* bSphere = std::get_if<SphereShape3D>(&b))
            return collideSpheres(aPosition, aSphere->radius, bPosition, bSphere->radius);
        auto collision = collideCapsuleSphere(bPosition, std::get<CapsuleShape3D>(b),
                                              aPosition, aSphere->radius);
        if (collision)
            collision->normal = negated(collision->normal);
        return collision;
    }
    const auto aCapsule = std::get<CapsuleShape3D>(a);
    if (const auto* bBox = std::get_if<AabbShape3D>(&b))
        return collideCapsuleAabb(aPosition, aCapsule, bPosition, *bBox);
    if (const auto* bSphere = std::get_if<SphereShape3D>(&b))
        return collideCapsuleSphere(aPosition, aCapsule, bPosition, bSphere->radius);
    return collideCapsules(aPosition, aCapsule, bPosition, std::get<CapsuleShape3D>(b));
}

[[nodiscard]] std::optional<ShapeRayHit> raycastAabb(Vec3 origin, Vec3 direction,
                                                     float maximumDistance, Vec3 position,
                                                     AabbShape3D box) {
    const auto minimum = position - box.halfExtents;
    const auto maximum = position + box.halfExtents;
    float nearTime = 0.0F;
    float farTime = maximumDistance;
    Vec3 nearNormal = negated(direction);
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        const auto originAxis = component(origin, axis);
        const auto directionAxis = component(direction, axis);
        const auto minimumAxis = component(minimum, axis);
        const auto maximumAxis = component(maximum, axis);
        if (std::abs(directionAxis) <= Epsilon) {
            if (originAxis < minimumAxis || originAxis > maximumAxis)
                return std::nullopt;
            continue;
        }
        const auto enteringBoundary = directionAxis > 0.0F ? minimumAxis : maximumAxis;
        const auto leavingBoundary = directionAxis > 0.0F ? maximumAxis : minimumAxis;
        const auto entering = (enteringBoundary - originAxis) / directionAxis;
        const auto leaving = (leavingBoundary - originAxis) / directionAxis;
        if (entering > nearTime) {
            nearTime = entering;
            nearNormal = axisVector(axis, directionAxis > 0.0F ? -1.0F : 1.0F);
        }
        farTime = std::min(farTime, leaving);
        if (nearTime > farTime)
            return std::nullopt;
    }
    if (nearTime < 0.0F || nearTime > maximumDistance)
        return std::nullopt;
    return ShapeRayHit{origin + direction * nearTime, nearNormal, nearTime};
}

[[nodiscard]] std::optional<ShapeRayHit> raycastSphere(Vec3 origin, Vec3 direction,
                                                       float maximumDistance, Vec3 position,
                                                       float radius) {
    const auto relative = origin - position;
    const auto b = dot(relative, direction);
    const auto c = dot(relative, relative) - radius * radius;
    const auto discriminant = b * b - c;
    if (discriminant < 0.0F)
        return std::nullopt;
    const auto root = std::sqrt(discriminant);
    auto distance = -b - root;
    if (distance < 0.0F)
        distance = c <= 0.0F ? 0.0F : -b + root;
    if (distance < 0.0F || distance > maximumDistance)
        return std::nullopt;
    const auto point = origin + direction * distance;
    const auto normal = distance <= Epsilon ? negated(direction) : normalized(point - position);
    return ShapeRayHit{point, normal, distance};
}

[[nodiscard]] std::optional<ShapeRayHit> raycastCapsule(Vec3 origin, Vec3 direction,
                                                        float maximumDistance, Vec3 position,
                                                        CapsuleShape3D capsule) {
    std::optional<ShapeRayHit> closest;
    const auto relative = origin - position;
    const auto a = direction.x * direction.x + direction.z * direction.z;
    if (a > Epsilon) {
        const auto b = 2.0F * (relative.x * direction.x + relative.z * direction.z);
        const auto c = relative.x * relative.x + relative.z * relative.z -
                       capsule.radius * capsule.radius;
        const auto discriminant = b * b - 4.0F * a * c;
        if (discriminant >= 0.0F) {
            const auto root = std::sqrt(discriminant);
            const float candidates[2] = {(-b - root) / (2.0F * a),
                                         (-b + root) / (2.0F * a)};
            for (const auto distance : candidates) {
                if (distance < 0.0F || distance > maximumDistance)
                    continue;
                const auto y = origin.y + direction.y * distance;
                if (y < position.y - capsule.halfHeight ||
                    y > position.y + capsule.halfHeight) {
                    continue;
                }
                const auto point = origin + direction * distance;
                const auto normal = normalized(
                    Vec3{point.x - position.x, 0.0F, point.z - position.z});
                closest = ShapeRayHit{point, normal, distance};
                break;
            }
        }
    }
    for (const auto endY : {position.y - capsule.halfHeight,
                            position.y + capsule.halfHeight}) {
        auto hit = raycastSphere(origin, direction, maximumDistance,
                                 {position.x, endY, position.z}, capsule.radius);
        if (hit && (!closest || hit->distance < closest->distance))
            closest = hit;
    }
    return closest;
}

[[nodiscard]] std::optional<ShapeRayHit> raycastPlane(Vec3 origin, Vec3 direction,
                                                      float maximumDistance, Vec3 position,
                                                      PlaneShape3D plane) {
    const auto normal = normalized(plane.normal);
    const auto denominator = dot(direction, normal);
    if (std::abs(denominator) <= Epsilon)
        return std::nullopt;
    const auto distance = dot(position - origin, normal) / denominator;
    if (distance < 0.0F || distance > maximumDistance)
        return std::nullopt;
    return ShapeRayHit{origin + direction * distance,
                       denominator < 0.0F ? normal : negated(normal), distance};
}

[[nodiscard]] std::optional<ShapeRayHit> raycastShape(Vec3 origin, Vec3 direction,
                                                      float maximumDistance, Vec3 position,
                                                      const CollisionShape3D& shape) {
    if (const auto* box = std::get_if<AabbShape3D>(&shape))
        return raycastAabb(origin, direction, maximumDistance, position, *box);
    if (const auto* sphere = std::get_if<SphereShape3D>(&shape))
        return raycastSphere(origin, direction, maximumDistance, position, sphere->radius);
    if (const auto* capsule = std::get_if<CapsuleShape3D>(&shape))
        return raycastCapsule(origin, direction, maximumDistance, position, *capsule);
    return raycastPlane(origin, direction, maximumDistance, position,
                        std::get<PlaneShape3D>(shape));
}

} // namespace

Result<PhysicsBody3DId> PhysicsWorld3D::addBody(PhysicsBody3D body) {
    if (!finite(body.position) || !finite(body.velocity) || !validShape(body.shape) ||
        body.layer == 0U) {
        return Result<PhysicsBody3DId>::failure(
            Error(ErrorCode::InvalidArgument, "3D physics body is invalid"));
    }
    if (bodies_.size() >= MaximumBodies || nextBodyId_ == 0U) {
        return Result<PhysicsBody3DId>::failure(
            Error(ErrorCode::CapacityExceeded, "3D physics body capacity is exhausted"));
    }
    if (auto* plane = std::get_if<PlaneShape3D>(&body.shape))
        plane->normal = normalized(plane->normal);
    body.id = {nextBodyId_++};
    const auto id = body.id;
    bodies_.emplace(id.value, std::move(body));
    return Result<PhysicsBody3DId>::success(id);
}

Result<void> PhysicsWorld3D::removeBody(PhysicsBody3DId id) {
    if (bodies_.erase(id.value) == 0U)
        return Result<void>::failure(Error(ErrorCode::NotFound, "3D physics body was not found"));
    return Result<void>::success();
}

PhysicsBody3D* PhysicsWorld3D::findBody(PhysicsBody3DId id) noexcept {
    const auto iterator = bodies_.find(id.value);
    return iterator == bodies_.end() ? nullptr : &iterator->second;
}

const PhysicsBody3D* PhysicsWorld3D::findBody(PhysicsBody3DId id) const noexcept {
    const auto iterator = bodies_.find(id.value);
    return iterator == bodies_.end() ? nullptr : &iterator->second;
}

Result<void> PhysicsWorld3D::setPosition(PhysicsBody3DId id, Vec3 position) {
    auto* body = findBody(id);
    if (body == nullptr)
        return Result<void>::failure(Error(ErrorCode::NotFound, "3D physics body was not found"));
    if (!finite(position))
        return Result<void>::failure(Error(ErrorCode::InvalidArgument, "3D position is invalid"));
    body->position = position;
    return Result<void>::success();
}

Result<void> PhysicsWorld3D::setVelocity(PhysicsBody3DId id, Vec3 velocity) {
    auto* body = findBody(id);
    if (body == nullptr)
        return Result<void>::failure(Error(ErrorCode::NotFound, "3D physics body was not found"));
    if (!finite(velocity))
        return Result<void>::failure(Error(ErrorCode::InvalidArgument, "3D velocity is invalid"));
    body->velocity = velocity;
    return Result<void>::success();
}

Result<std::vector<Contact3D>> PhysicsWorld3D::detectContacts(bool includeTriggers) const {
    std::vector<Contact3D> contacts;
    for (auto first = bodies_.begin(); first != bodies_.end(); ++first) {
        for (auto second = std::next(first); second != bodies_.end(); ++second) {
            const auto& a = first->second;
            const auto& b = second->second;
            if ((a.collisionMask & b.layer) == 0U || (b.collisionMask & a.layer) == 0U ||
                (!includeTriggers && (a.trigger || b.trigger))) {
                continue;
            }
            auto geometry = collide(a.position, a.shape, b.position, b.shape);
            if (geometry) {
                contacts.push_back({a.id, b.id, geometry->normal, geometry->point,
                                    geometry->penetration, a.trigger || b.trigger});
            }
        }
    }
    return Result<std::vector<Contact3D>>::success(std::move(contacts));
}

Result<std::optional<RaycastHit3D>> PhysicsWorld3D::raycast(Vec3 origin, Vec3 direction,
                                                            float maximumDistance,
                                                            std::uint32_t layerMask,
                                                            bool includeTriggers) const {
    if (!finite(origin) || !finite(direction) || !std::isfinite(maximumDistance) ||
        maximumDistance < 0.0F || length(direction) <= Epsilon) {
        return Result<std::optional<RaycastHit3D>>::failure(
            Error(ErrorCode::InvalidArgument, "3D ray is invalid"));
    }
    direction = normalized(direction);
    std::optional<RaycastHit3D> closest;
    for (const auto& [id, body] : bodies_) {
        static_cast<void>(id);
        if ((body.layer & layerMask) == 0U || (!includeTriggers && body.trigger))
            continue;
        auto hit = raycastShape(origin, direction, maximumDistance, body.position, body.shape);
        if (hit && (!closest || hit->distance < closest->distance ||
                    (hit->distance == closest->distance && body.id < closest->body))) {
            closest = RaycastHit3D{body.id, hit->point, hit->normal, hit->distance, body.trigger};
        }
    }
    return Result<std::optional<RaycastHit3D>>::success(closest);
}

Result<std::vector<PhysicsBody3DId>> PhysicsWorld3D::overlap(Vec3 position,
                                                             CollisionShape3D shape,
                                                             std::uint32_t layerMask,
                                                             bool includeTriggers) const {
    if (!finite(position) || !validShape(shape)) {
        return Result<std::vector<PhysicsBody3DId>>::failure(
            Error(ErrorCode::InvalidArgument, "3D overlap shape is invalid"));
    }
    std::vector<PhysicsBody3DId> overlaps;
    for (const auto& [id, body] : bodies_) {
        static_cast<void>(id);
        if ((body.layer & layerMask) == 0U || (!includeTriggers && body.trigger))
            continue;
        if (collide(position, shape, body.position, body.shape))
            overlaps.push_back(body.id);
    }
    return Result<std::vector<PhysicsBody3DId>>::success(std::move(overlaps));
}

Result<KinematicMove3D>
PhysicsWorld3D::moveShape(Vec3 position, Vec3 velocity, CollisionShape3D shape,
                          Vec3 displacement, std::uint32_t layer,
                          std::uint32_t collisionMask, std::uint32_t maximumSubsteps,
                          std::optional<PhysicsBody3DId> ignoredBody) const {
    if (!finite(position) || !finite(velocity) || !finite(displacement) || !validShape(shape) ||
        std::holds_alternative<PlaneShape3D>(shape) || layer == 0U || maximumSubsteps == 0U ||
        maximumSubsteps > 64U) {
        return Result<KinematicMove3D>::failure(
            Error(ErrorCode::InvalidArgument, "3D kinematic movement is invalid"));
    }
    const auto span = shapeMotionSpan(shape);
    const auto movementLength = length(displacement);
    const auto estimate = std::ceil(movementLength / span);
    const auto steps = estimate >= static_cast<float>(maximumSubsteps)
                           ? maximumSubsteps
                           : static_cast<std::uint32_t>(std::max(1.0F, estimate));
    auto stepDisplacement = displacement * (1.0F / static_cast<float>(steps));

    KinematicMove3D result;
    result.position = position;
    result.velocity = velocity;
    for (std::uint32_t step = 0U; step < steps; ++step) {
        static_cast<void>(step);
        auto candidate = result.position + stepDisplacement;
        for (std::uint32_t pass = 0U; pass < 4U; ++pass) {
            bool corrected = false;
            for (const auto& [key, body] : bodies_) {
                static_cast<void>(key);
                if ((ignoredBody && body.id == *ignoredBody) || body.trigger ||
                    (collisionMask & body.layer) == 0U ||
                    (body.collisionMask & layer) == 0U) {
                    continue;
                }
                auto contact = collide(candidate, shape, body.position, body.shape);
                if (!contact)
                    continue;
                if (contact->penetration > 0.0F) {
                    candidate = candidate - contact->normal * contact->penetration;
                    corrected = true;
                }
                const auto stepIntoSurface = dot(stepDisplacement, contact->normal);
                if (stepIntoSurface > 0.0F)
                    stepDisplacement = stepDisplacement - contact->normal * stepIntoSurface;
                const auto velocityIntoSurface = dot(result.velocity, contact->normal);
                if (velocityIntoSurface > 0.0F)
                    result.velocity = result.velocity - contact->normal * velocityIntoSurface;
                result.grounded = result.grounded || contact->normal.y < -0.5F;
                result.hitCeiling = result.hitCeiling || contact->normal.y > 0.5F;
                result.hitWall = result.hitWall || std::abs(contact->normal.y) <= 0.5F;
                if (std::find(result.touchedBodies.begin(), result.touchedBodies.end(),
                              body.id) == result.touchedBodies.end()) {
                    result.touchedBodies.push_back(body.id);
                }
            }
            if (!corrected)
                break;
        }
        result.position = candidate;
    }
    return Result<KinematicMove3D>::success(std::move(result));
}

Result<KinematicMove3D> PhysicsWorld3D::moveKinematic(PhysicsBody3DId id, Vec3 displacement,
                                                       std::uint32_t maximumSubsteps) {
    auto* body = findBody(id);
    if (body == nullptr)
        return Result<KinematicMove3D>::failure(
            Error(ErrorCode::NotFound, "3D physics body was not found"));
    if (!body->kinematic) {
        return Result<KinematicMove3D>::failure(
            Error(ErrorCode::InvalidArgument, "3D body is not kinematic"));
    }
    auto movement = moveShape(body->position, body->velocity, body->shape, displacement,
                              body->layer, body->collisionMask, maximumSubsteps, id);
    if (!movement)
        return movement;
    body->position = movement.value().position;
    body->velocity = movement.value().velocity;
    return movement;
}

Result<KinematicMove3D>
PhysicsWorld3D::moveCharacter(Vec3 position, Vec3 velocity, float deltaSeconds,
                              const CharacterController3DSettings& settings) const {
    if (!finite(settings.gravity) || !std::isfinite(settings.skinWidth) ||
        settings.skinWidth < 0.0F || settings.layer == 0U ||
        settings.maximumSubsteps == 0U || settings.maximumSubsteps > 64U ||
        !std::isfinite(deltaSeconds) || deltaSeconds < 0.0F ||
        !std::isfinite(settings.capsule.radius) || settings.capsule.radius <= 0.0F ||
        !std::isfinite(settings.capsule.halfHeight) || settings.capsule.halfHeight < 0.0F) {
        return Result<KinematicMove3D>::failure(
            Error(ErrorCode::InvalidArgument, "3D character settings are invalid"));
    }
    velocity = velocity + settings.gravity * deltaSeconds;
    const auto displacement = velocity * deltaSeconds;
    if (!finite(velocity) || !finite(displacement)) {
        return Result<KinematicMove3D>::failure(
            Error(ErrorCode::InvalidArgument, "3D character movement overflowed"));
    }
    auto capsule = settings.capsule;
    capsule.radius += settings.skinWidth;
    return moveShape(position, velocity, capsule, displacement, settings.layer,
                     settings.collisionMask, settings.maximumSubsteps, std::nullopt);
}

Result<ArcadeVehicle3DState>
PhysicsWorld3D::stepArcadeVehicle(ArcadeVehicle3DState state, float throttle,
                                  float steering, float brake, float deltaSeconds,
                                  const ArcadeVehicle3DSettings& settings) const {
    if (!finite(state.position) || !finite(state.velocity) ||
        !std::isfinite(state.yawRadians) || !std::isfinite(state.speed) ||
        !std::isfinite(throttle) || !std::isfinite(steering) || !std::isfinite(brake) ||
        !std::isfinite(deltaSeconds) || deltaSeconds < 0.0F ||
        !validShape(settings.collisionBox) || !std::isfinite(settings.acceleration) ||
        settings.acceleration < 0.0F || !std::isfinite(settings.braking) ||
        settings.braking < 0.0F || !std::isfinite(settings.maximumSpeed) ||
        settings.maximumSpeed <= 0.0F || !std::isfinite(settings.drag) ||
        settings.drag < 0.0F || !std::isfinite(settings.steeringRate) ||
        settings.steeringRate < 0.0F || settings.layer == 0U ||
        settings.maximumSubsteps == 0U || settings.maximumSubsteps > 64U) {
        return Result<ArcadeVehicle3DState>::failure(
            Error(ErrorCode::InvalidArgument, "arcade vehicle settings are invalid"));
    }
    throttle = std::clamp(throttle, -1.0F, 1.0F);
    steering = std::clamp(steering, -1.0F, 1.0F);
    brake = std::clamp(brake, 0.0F, 1.0F);
    state.speed += throttle * settings.acceleration * deltaSeconds;
    const auto slowing = (settings.braking * brake + settings.drag) * deltaSeconds;
    if (state.speed > 0.0F)
        state.speed = std::max(0.0F, state.speed - slowing);
    else if (state.speed < 0.0F)
        state.speed = std::min(0.0F, state.speed + slowing);
    state.speed = std::clamp(state.speed, -settings.maximumSpeed, settings.maximumSpeed);
    const auto steeringScale = std::min(1.0F, std::abs(state.speed) / 2.0F);
    state.yawRadians += steering * settings.steeringRate * steeringScale * deltaSeconds;
    const Vec3 forward{std::sin(state.yawRadians), 0.0F, std::cos(state.yawRadians)};
    state.velocity = forward * state.speed;
    auto movement = moveShape(state.position, state.velocity, settings.collisionBox,
                              state.velocity * deltaSeconds, settings.layer,
                              settings.collisionMask, settings.maximumSubsteps, std::nullopt);
    if (!movement)
        return Result<ArcadeVehicle3DState>::failure(movement.error());
    state.position = movement.value().position;
    state.velocity = movement.value().velocity;
    state.touchedBodies = std::move(movement.value().touchedBodies);
    state.collided = !state.touchedBodies.empty();
    if (state.collided)
        state.speed = dot(state.velocity, forward);
    return Result<ArcadeVehicle3DState>::success(std::move(state));
}

std::vector<PhysicsDebugPrimitive3D> PhysicsWorld3D::debugPrimitives() const {
    std::vector<PhysicsDebugPrimitive3D> primitives;
    primitives.reserve(bodies_.size());
    for (const auto& [key, body] : bodies_) {
        static_cast<void>(key);
        PhysicsDebugPrimitive3D primitive;
        primitive.body = body.id;
        primitive.center = body.position;
        primitive.trigger = body.trigger;
        if (const auto* box = std::get_if<AabbShape3D>(&body.shape)) {
            primitive.shape = PhysicsDebugShape3D::Aabb;
            primitive.halfExtents = box->halfExtents;
        } else if (const auto* sphere = std::get_if<SphereShape3D>(&body.shape)) {
            primitive.shape = PhysicsDebugShape3D::Sphere;
            primitive.radius = sphere->radius;
        } else if (const auto* capsule = std::get_if<CapsuleShape3D>(&body.shape)) {
            primitive.shape = PhysicsDebugShape3D::Capsule;
            primitive.radius = capsule->radius;
            primitive.halfHeight = capsule->halfHeight;
        } else {
            primitive.shape = PhysicsDebugShape3D::Plane;
            primitive.normal = std::get<PlaneShape3D>(body.shape).normal;
        }
        primitives.push_back(primitive);
    }
    return primitives;
}

} // namespace experimental
} // namespace fabgl
