#include "fabgl/physics/physics2d.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

namespace fabgl {
namespace {

float lengthSquared(Vec2 value) noexcept {
    return value.x * value.x + value.y * value.y;
}

float length(Vec2 value) noexcept {
    return std::sqrt(lengthSquared(value));
}

Vec2 normalize(Vec2 value) noexcept {
    const auto magnitude = length(value);
    return magnitude > 1.0e-6F ? Vec2{value.x / magnitude, value.y / magnitude} : Vec2{};
}

bool finite(Vec2 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

bool validShape(const CollisionShape2D& shape) noexcept {
    return std::visit(
        [](const auto& concrete) {
            using Shape = std::decay_t<decltype(concrete)>;
            if constexpr (std::is_same_v<Shape, AabbShape>) {
                return finite(concrete.halfExtents) && concrete.halfExtents.x > 0.0F &&
                       concrete.halfExtents.y > 0.0F;
            } else if constexpr (std::is_same_v<Shape, CircleShape>) {
                return std::isfinite(concrete.radius) && concrete.radius > 0.0F;
            } else {
                return true;
            }
        },
        shape);
}

std::optional<Contact2D> collideAabbs(const PhysicsBody2D& first, const PhysicsBody2D& second) {
    const auto& firstShape = std::get<AabbShape>(first.shape);
    const auto& secondShape = std::get<AabbShape>(second.shape);
    const auto delta = second.position - first.position;
    const auto overlapX = firstShape.halfExtents.x + secondShape.halfExtents.x - std::fabs(delta.x);
    const auto overlapY = firstShape.halfExtents.y + secondShape.halfExtents.y - std::fabs(delta.y);
    if (overlapX < 0.0F || overlapY < 0.0F)
        return std::nullopt;

    Contact2D contact;
    if (overlapX <= overlapY) {
        contact.normal = {delta.x < 0.0F ? -1.0F : 1.0F, 0.0F};
        contact.penetration = overlapX;
    } else {
        contact.normal = {0.0F, delta.y < 0.0F ? -1.0F : 1.0F};
        contact.penetration = overlapY;
    }
    contact.point = (first.position + second.position) * 0.5F;
    return contact;
}

std::optional<Contact2D> collideCircles(const PhysicsBody2D& first, const PhysicsBody2D& second) {
    const auto firstRadius = std::get<CircleShape>(first.shape).radius;
    const auto secondRadius = std::get<CircleShape>(second.shape).radius;
    const auto delta = second.position - first.position;
    const auto distanceSquared = lengthSquared(delta);
    const auto totalRadius = firstRadius + secondRadius;
    if (distanceSquared > totalRadius * totalRadius)
        return std::nullopt;
    const auto distance = std::sqrt(distanceSquared);
    Contact2D contact;
    contact.normal = distance > 1.0e-6F ? delta * (1.0F / distance) : Vec2{1.0F, 0.0F};
    contact.penetration = totalRadius - distance;
    contact.point = first.position + contact.normal * (firstRadius - contact.penetration * 0.5F);
    return contact;
}

std::optional<Contact2D> collideCircleAabb(const PhysicsBody2D& circle, const PhysicsBody2D& box) {
    const auto radius = std::get<CircleShape>(circle.shape).radius;
    const auto half = std::get<AabbShape>(box.shape).halfExtents;
    const Vec2 minimum{box.position.x - half.x, box.position.y - half.y};
    const Vec2 maximum{box.position.x + half.x, box.position.y + half.y};
    const Vec2 closest{std::clamp(circle.position.x, minimum.x, maximum.x),
                       std::clamp(circle.position.y, minimum.y, maximum.y)};
    const auto fromBox = circle.position - closest;
    const auto distanceSquared = lengthSquared(fromBox);
    if (distanceSquared > radius * radius)
        return std::nullopt;

    Contact2D contact;
    if (distanceSquared > 1.0e-12F) {
        const auto distance = std::sqrt(distanceSquared);
        contact.normal = fromBox * (-1.0F / distance);
        contact.penetration = radius - distance;
        contact.point = closest;
        return contact;
    }

    const auto left = circle.position.x - minimum.x;
    const auto right = maximum.x - circle.position.x;
    const auto top = circle.position.y - minimum.y;
    const auto bottom = maximum.y - circle.position.y;
    const auto nearest = std::min(std::min(left, right), std::min(top, bottom));
    if (nearest == left)
        contact.normal = {-1.0F, 0.0F};
    else if (nearest == right)
        contact.normal = {1.0F, 0.0F};
    else if (nearest == top)
        contact.normal = {0.0F, -1.0F};
    else
        contact.normal = {0.0F, 1.0F};
    contact.penetration = radius + nearest;
    contact.point = circle.position + contact.normal * nearest;
    return contact;
}

std::optional<Contact2D> collidePointAabb(const PhysicsBody2D& point, const PhysicsBody2D& box) {
    const auto half = std::get<AabbShape>(box.shape).halfExtents;
    const Vec2 minimum{box.position.x - half.x, box.position.y - half.y};
    const Vec2 maximum{box.position.x + half.x, box.position.y + half.y};
    if (point.position.x < minimum.x || point.position.x > maximum.x ||
        point.position.y < minimum.y || point.position.y > maximum.y) {
        return std::nullopt;
    }

    const auto left = point.position.x - minimum.x;
    const auto right = maximum.x - point.position.x;
    const auto top = point.position.y - minimum.y;
    const auto bottom = maximum.y - point.position.y;
    const auto nearest = std::min(std::min(left, right), std::min(top, bottom));
    Contact2D contact;
    if (nearest == left)
        contact.normal = {1.0F, 0.0F};
    else if (nearest == right)
        contact.normal = {-1.0F, 0.0F};
    else if (nearest == top)
        contact.normal = {0.0F, 1.0F};
    else
        contact.normal = {0.0F, -1.0F};
    contact.penetration = nearest;
    contact.point = point.position;
    return contact;
}

std::optional<Contact2D> collidePointCircle(const PhysicsBody2D& point,
                                            const PhysicsBody2D& circle) {
    const auto radius = std::get<CircleShape>(circle.shape).radius;
    const auto delta = circle.position - point.position;
    const auto distanceSquared = lengthSquared(delta);
    if (distanceSquared > radius * radius)
        return std::nullopt;
    const auto distance = std::sqrt(distanceSquared);
    Contact2D contact;
    contact.normal = distance > 1.0e-6F ? delta * (1.0F / distance) : Vec2{1.0F, 0.0F};
    contact.penetration = radius - distance;
    contact.point = point.position;
    return contact;
}

std::optional<Contact2D> collidePoints(const PhysicsBody2D& first,
                                       const PhysicsBody2D& second) {
    if (lengthSquared(second.position - first.position) > 1.0e-12F)
        return std::nullopt;
    Contact2D contact;
    contact.normal = {1.0F, 0.0F};
    contact.point = first.position;
    return contact;
}

std::optional<Contact2D> collide(const PhysicsBody2D& first, const PhysicsBody2D& second) {
    if (std::holds_alternative<AabbShape>(first.shape) &&
        std::holds_alternative<AabbShape>(second.shape)) {
        return collideAabbs(first, second);
    }
    if (std::holds_alternative<CircleShape>(first.shape) &&
        std::holds_alternative<CircleShape>(second.shape)) {
        return collideCircles(first, second);
    }
    if (std::holds_alternative<PointShape>(first.shape) &&
        std::holds_alternative<PointShape>(second.shape)) {
        return collidePoints(first, second);
    }
    if (std::holds_alternative<PointShape>(first.shape) &&
        std::holds_alternative<AabbShape>(second.shape)) {
        return collidePointAabb(first, second);
    }
    if (std::holds_alternative<AabbShape>(first.shape) &&
        std::holds_alternative<PointShape>(second.shape)) {
        auto contact = collidePointAabb(second, first);
        if (contact)
            contact->normal = contact->normal * -1.0F;
        return contact;
    }
    if (std::holds_alternative<PointShape>(first.shape) &&
        std::holds_alternative<CircleShape>(second.shape)) {
        return collidePointCircle(first, second);
    }
    if (std::holds_alternative<CircleShape>(first.shape) &&
        std::holds_alternative<PointShape>(second.shape)) {
        auto contact = collidePointCircle(second, first);
        if (contact)
            contact->normal = contact->normal * -1.0F;
        return contact;
    }
    if (std::holds_alternative<CircleShape>(first.shape) &&
        std::holds_alternative<AabbShape>(second.shape))
        return collideCircleAabb(first, second);
    if (std::holds_alternative<AabbShape>(first.shape) &&
        std::holds_alternative<CircleShape>(second.shape)) {
        auto contact = collideCircleAabb(second, first);
        if (contact)
            contact->normal = contact->normal * -1.0F;
        return contact;
    }
    return std::nullopt;
}

void resolveContact(PhysicsBody2D& first, PhysicsBody2D& second, const Contact2D& contact) {
    if (contact.trigger)
        return;
    if (contact.penetration > 0.0F) {
        if (first.dynamic && second.dynamic) {
            const auto correction = contact.normal * (contact.penetration * 0.5F);
            first.position = first.position - correction;
            second.position = second.position + correction;
        } else if (first.dynamic) {
            first.position = first.position - contact.normal * contact.penetration;
        } else if (second.dynamic) {
            second.position = second.position + contact.normal * contact.penetration;
        }
    }

    const auto firstInverseMass = first.dynamic ? 1.0F / first.mass : 0.0F;
    const auto secondInverseMass = second.dynamic ? 1.0F / second.mass : 0.0F;
    const auto inverseMassSum = firstInverseMass + secondInverseMass;
    if (inverseMassSum <= 0.0F)
        return;

    const auto relativeVelocity = second.velocity - first.velocity;
    const auto velocityAlongNormal = relativeVelocity.x * contact.normal.x +
                                     relativeVelocity.y * contact.normal.y;
    if (velocityAlongNormal > 0.0F)
        return;

    const auto restitution = std::min(first.restitution, second.restitution);
    const auto impulseMagnitude = -(1.0F + restitution) * velocityAlongNormal / inverseMassSum;
    const auto impulse = contact.normal * impulseMagnitude;
    if (first.dynamic)
        first.velocity = first.velocity - impulse * firstInverseMass;
    if (second.dynamic)
        second.velocity = second.velocity + impulse * secondInverseMass;

    auto tangent = relativeVelocity - contact.normal * velocityAlongNormal;
    const auto tangentLengthSquared = tangent.x * tangent.x + tangent.y * tangent.y;
    if (tangentLengthSquared <= 1.0e-12F)
        return;
    tangent = tangent * (1.0F / std::sqrt(tangentLengthSquared));
    auto frictionImpulseMagnitude =
        -(relativeVelocity.x * tangent.x + relativeVelocity.y * tangent.y) / inverseMassSum;
    const auto friction = std::sqrt(first.friction * second.friction);
    frictionImpulseMagnitude =
        std::clamp(frictionImpulseMagnitude, -impulseMagnitude * friction,
                   impulseMagnitude * friction);
    const auto frictionImpulse = tangent * frictionImpulseMagnitude;
    if (first.dynamic)
        first.velocity = first.velocity - frictionImpulse * firstInverseMass;
    if (second.dynamic)
        second.velocity = second.velocity + frictionImpulse * secondInverseMass;
}

std::optional<RaycastHit2D> raycastAabb(const PhysicsBody2D& body, Vec2 origin, Vec2 direction,
                                        float maximumDistance) {
    const auto half = std::get<AabbShape>(body.shape).halfExtents;
    const Vec2 minimum{body.position.x - half.x, body.position.y - half.y};
    const Vec2 maximum{body.position.x + half.x, body.position.y + half.y};
    float minimumTime = 0.0F;
    float maximumTime = maximumDistance;
    Vec2 normal{};
    for (int axis = 0; axis < 2; ++axis) {
        const auto originValue = axis == 0 ? origin.x : origin.y;
        const auto directionValue = axis == 0 ? direction.x : direction.y;
        const auto minimumValue = axis == 0 ? minimum.x : minimum.y;
        const auto maximumValue = axis == 0 ? maximum.x : maximum.y;
        if (std::fabs(directionValue) < 1.0e-7F) {
            if (originValue < minimumValue || originValue > maximumValue)
                return std::nullopt;
            continue;
        }
        auto nearTime = (minimumValue - originValue) / directionValue;
        auto farTime = (maximumValue - originValue) / directionValue;
        float nearNormal = -1.0F;
        if (nearTime > farTime) {
            std::swap(nearTime, farTime);
            nearNormal = 1.0F;
        }
        if (nearTime > minimumTime) {
            minimumTime = nearTime;
            normal = axis == 0 ? Vec2{nearNormal, 0.0F} : Vec2{0.0F, nearNormal};
        }
        maximumTime = std::min(maximumTime, farTime);
        if (minimumTime > maximumTime)
            return std::nullopt;
    }
    if (minimumTime < 0.0F || minimumTime > maximumDistance)
        return std::nullopt;
    return RaycastHit2D{body.id, origin + direction * minimumTime, normal, minimumTime,
                        body.trigger};
}

std::optional<RaycastHit2D> raycastCircle(const PhysicsBody2D& body, Vec2 origin, Vec2 direction,
                                          float maximumDistance) {
    const auto radius = std::get<CircleShape>(body.shape).radius;
    const auto offset = origin - body.position;
    const auto b = 2.0F * (offset.x * direction.x + offset.y * direction.y);
    const auto c = lengthSquared(offset) - radius * radius;
    const auto discriminant = b * b - 4.0F * c;
    if (discriminant < 0.0F)
        return std::nullopt;
    const auto root = std::sqrt(discriminant);
    auto distance = (-b - root) * 0.5F;
    if (distance < 0.0F)
        distance = (-b + root) * 0.5F;
    if (distance < 0.0F || distance > maximumDistance)
        return std::nullopt;
    const auto point = origin + direction * distance;
    return RaycastHit2D{body.id, point, normalize(point - body.position), distance, body.trigger};
}

std::optional<RaycastHit2D> raycastPoint(const PhysicsBody2D& body, Vec2 origin, Vec2 direction,
                                         float maximumDistance) {
    const auto offset = body.position - origin;
    const auto distance = offset.x * direction.x + offset.y * direction.y;
    if (distance < 0.0F || distance > maximumDistance)
        return std::nullopt;
    const auto closest = origin + direction * distance;
    if (lengthSquared(body.position - closest) > 1.0e-8F)
        return std::nullopt;
    return RaycastHit2D{body.id, body.position, direction * -1.0F, distance, body.trigger};
}

} // namespace

Result<void> PhysicsWorld2D::setGravity(Vec2 gravity) {
    if (!std::isfinite(gravity.x) || !std::isfinite(gravity.y)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "gravity must contain finite values"));
    }
    gravity_ = gravity;
    return Result<void>::success();
}

Result<PhysicsBodyId> PhysicsWorld2D::addBody(PhysicsBody2D body) {
    if (!validShape(body.shape) || !finite(body.position) || !finite(body.velocity) ||
        body.layer == 0U ||
        !std::isfinite(body.mass) || body.mass <= 0.0F || !std::isfinite(body.gravityScale) ||
        !std::isfinite(body.restitution) || body.restitution < 0.0F || body.restitution > 1.0F ||
        !std::isfinite(body.friction) || body.friction < 0.0F ||
        (body.dynamic && body.kinematic)) {
        return Result<PhysicsBodyId>::failure(
            Error(ErrorCode::InvalidArgument, "physics body is invalid"));
    }
    if (bodies_.size() >= MaximumBodies || nextBodyId_ == 0U) {
        return Result<PhysicsBodyId>::failure(
            Error(ErrorCode::CapacityExceeded, "2D physics body capacity is exhausted"));
    }
    body.id = PhysicsBodyId{nextBodyId_++};
    const auto id = body.id;
    bodies_.emplace(id.value, std::move(body));
    return Result<PhysicsBodyId>::success(id);
}

Result<PhysicsBody2D*> PhysicsWorld2D::requireBody(PhysicsBodyId id) {
    auto* body = findBody(id);
    if (body == nullptr) {
        return Result<PhysicsBody2D*>::failure(
            Error(ErrorCode::NotFound, "physics body was not found")
                .addContext("body", std::to_string(id.value)));
    }
    return Result<PhysicsBody2D*>::success(body);
}

Result<void> PhysicsWorld2D::removeBody(PhysicsBodyId id) {
    if (bodies_.erase(id.value) == 0U) {
        return Result<void>::failure(Error(ErrorCode::NotFound, "physics body was not found"));
    }
    return Result<void>::success();
}

PhysicsBody2D* PhysicsWorld2D::findBody(PhysicsBodyId id) noexcept {
    const auto iterator = bodies_.find(id.value);
    return iterator == bodies_.end() ? nullptr : &iterator->second;
}

const PhysicsBody2D* PhysicsWorld2D::findBody(PhysicsBodyId id) const noexcept {
    const auto iterator = bodies_.find(id.value);
    return iterator == bodies_.end() ? nullptr : &iterator->second;
}

Result<void> PhysicsWorld2D::setPosition(PhysicsBodyId id, Vec2 position) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "body position must be finite"));
    }
    auto body = requireBody(id);
    if (!body)
        return Result<void>::failure(body.error());
    body.value()->position = position;
    return Result<void>::success();
}

Result<void> PhysicsWorld2D::setVelocity(PhysicsBodyId id, Vec2 velocity) {
    if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "body velocity must be finite"));
    }
    auto body = requireBody(id);
    if (!body)
        return Result<void>::failure(body.error());
    body.value()->velocity = velocity;
    return Result<void>::success();
}

Result<TileCollisionMapId> PhysicsWorld2D::addTileCollisionMap(TileCollisionMap2D map) {
    if (map.width == 0U || map.height == 0U ||
        map.width > MaximumTileCells / map.height || !finite(map.origin) ||
        !finite(map.tileSize) || map.tileSize.x <= 0.0F || map.tileSize.y <= 0.0F ||
        map.layer == 0U) {
        return Result<TileCollisionMapId>::failure(
            Error(ErrorCode::InvalidArgument, "tile collision map metadata is invalid"));
    }
    const auto cellCount = static_cast<std::size_t>(map.width) * map.height;
    if (cellCount > MaximumTileCells || map.solidCells.size() != cellCount) {
        return Result<TileCollisionMapId>::failure(
            Error(ErrorCode::CapacityExceeded,
                  "tile collision map cell count is invalid or exceeds the limit"));
    }
    const auto solidCount = static_cast<std::size_t>(
        std::count_if(map.solidCells.begin(), map.solidCells.end(),
                      [](std::uint8_t cell) { return cell != 0U; }));
    if (solidCount > MaximumBodies - bodies_.size() || nextTileCollisionMapId_ == 0U ||
        solidCount > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() -
                                              nextBodyId_) +
                         1U) {
        return Result<TileCollisionMapId>::failure(
            Error(ErrorCode::CapacityExceeded, "tile collision map exceeds physics capacity"));
    }

    TileCollisionRecord record;
    record.map = std::move(map);
    record.bodies.reserve(solidCount);
    for (std::uint32_t y = 0U; y < record.map.height; ++y) {
        for (std::uint32_t x = 0U; x < record.map.width; ++x) {
            const auto index = static_cast<std::size_t>(y) * record.map.width + x;
            if (record.map.solidCells[index] == 0U)
                continue;
            PhysicsBody2D body;
            body.position = {
                record.map.origin.x + (static_cast<float>(x) + 0.5F) * record.map.tileSize.x,
                record.map.origin.y + (static_cast<float>(y) + 0.5F) * record.map.tileSize.y,
            };
            body.shape = AabbShape{record.map.tileSize * 0.5F};
            body.layer = record.map.layer;
            body.collisionMask = record.map.collisionMask;
            body.trigger = record.map.trigger;
            auto added = addBody(body);
            if (!added) {
                for (const auto addedBody : record.bodies)
                    bodies_.erase(addedBody.value);
                return Result<TileCollisionMapId>::failure(
                    added.error().withContext("operation", "materialize tile collision map"));
            }
            record.bodies.push_back(added.value());
        }
    }

    const TileCollisionMapId id{nextTileCollisionMapId_++};
    tileCollisionMaps_.emplace(id.value, std::move(record));
    return Result<TileCollisionMapId>::success(id);
}

Result<void> PhysicsWorld2D::removeTileCollisionMap(TileCollisionMapId id) {
    const auto iterator = tileCollisionMaps_.find(id.value);
    if (iterator == tileCollisionMaps_.end()) {
        return Result<void>::failure(
            Error(ErrorCode::NotFound, "tile collision map was not found"));
    }
    for (const auto body : iterator->second.bodies)
        bodies_.erase(body.value);
    tileCollisionMaps_.erase(iterator);
    return Result<void>::success();
}

const TileCollisionMap2D*
PhysicsWorld2D::findTileCollisionMap(TileCollisionMapId id) const noexcept {
    const auto iterator = tileCollisionMaps_.find(id.value);
    return iterator == tileCollisionMaps_.end() ? nullptr : &iterator->second.map;
}

Result<void> PhysicsWorld2D::step(float deltaSeconds) {
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0F) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "physics delta must be finite and non-negative"));
    }
    for (auto& body : bodies_) {
        if (body.second.dynamic) {
            body.second.velocity =
                body.second.velocity + gravity_ * (body.second.gravityScale * deltaSeconds);
        }
        if (body.second.dynamic || body.second.kinematic) {
            body.second.position = body.second.position + body.second.velocity * deltaSeconds;
        }
    }

    contacts_.clear();
    for (auto first = bodies_.begin(); first != bodies_.end(); ++first) {
        auto second = first;
        ++second;
        for (; second != bodies_.end(); ++second) {
            auto& firstBody = first->second;
            auto& secondBody = second->second;
            if ((firstBody.collisionMask & secondBody.layer) == 0U ||
                (secondBody.collisionMask & firstBody.layer) == 0U) {
                continue;
            }
            auto contact = collide(firstBody, secondBody);
            if (!contact)
                continue;
            contact->first = firstBody.id;
            contact->second = secondBody.id;
            contact->trigger = firstBody.trigger || secondBody.trigger;
            contacts_.push_back(*contact);
            resolveContact(firstBody, secondBody, *contact);
        }
    }
    return Result<void>::success();
}

Result<std::optional<RaycastHit2D>> PhysicsWorld2D::raycast(Vec2 origin, Vec2 direction,
                                                            float maximumDistance,
                                                            std::uint32_t layerMask) const {
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(direction.x) ||
        !std::isfinite(direction.y) || !std::isfinite(maximumDistance) || maximumDistance < 0.0F) {
        return Result<std::optional<RaycastHit2D>>::failure(
            Error(ErrorCode::InvalidArgument, "raycast arguments are invalid"));
    }
    const auto directionLength = length(direction);
    if (directionLength <= 1.0e-6F) {
        return Result<std::optional<RaycastHit2D>>::failure(
            Error(ErrorCode::InvalidArgument, "raycast direction cannot be zero"));
    }
    direction = direction * (1.0F / directionLength);

    std::optional<RaycastHit2D> closest;
    for (const auto& entry : bodies_) {
        const auto& body = entry.second;
        if ((body.layer & layerMask) == 0U)
            continue;
        auto hit = std::holds_alternative<AabbShape>(body.shape)
                       ? raycastAabb(body, origin, direction, maximumDistance)
                   : std::holds_alternative<CircleShape>(body.shape)
                       ? raycastCircle(body, origin, direction, maximumDistance)
                       : raycastPoint(body, origin, direction, maximumDistance);
        if (hit && (!closest || hit->distance < closest->distance ||
                    (hit->distance == closest->distance && hit->body < closest->body))) {
            closest = *hit;
        }
    }
    return Result<std::optional<RaycastHit2D>>::success(closest);
}

Result<std::vector<PhysicsBodyId>> PhysicsWorld2D::overlap(Vec2 position,
                                                           CollisionShape2D shape,
                                                           std::uint32_t layerMask,
                                                           bool includeTriggers) const {
    PhysicsBody2D query;
    query.position = position;
    query.shape = std::move(shape);
    query.layer = 1U;
    auto validQuery = PhysicsWorld2D{}.addBody(query);
    if (!validQuery) {
        return Result<std::vector<PhysicsBodyId>>::failure(
            validQuery.error().withContext("query", "overlap"));
    }

    std::vector<PhysicsBodyId> result;
    for (const auto& entry : bodies_) {
        const auto& body = entry.second;
        if ((body.layer & layerMask) == 0U || (!includeTriggers && body.trigger))
            continue;
        if (collide(query, body))
            result.push_back(body.id);
    }
    return Result<std::vector<PhysicsBodyId>>::success(std::move(result));
}

Result<CharacterMove2D>
PhysicsWorld2D::moveCharacter(Vec2 position, Vec2 velocity, float deltaSeconds,
                              const CharacterController2DSettings& settings) const {
    if (!finite(position) || !finite(velocity) || !finite(settings.halfExtents) ||
        !finite(settings.gravity) || settings.halfExtents.x <= 0.0F ||
        settings.halfExtents.y <= 0.0F || !std::isfinite(settings.skinWidth) ||
        settings.skinWidth < 0.0F || settings.layer == 0U ||
        settings.maximumSubsteps == 0U || settings.maximumSubsteps > 64U ||
        !std::isfinite(deltaSeconds) || deltaSeconds < 0.0F) {
        return Result<CharacterMove2D>::failure(
            Error(ErrorCode::InvalidArgument, "2D character movement settings are invalid"));
    }

    CharacterMove2D result;
    result.position = position;
    result.velocity = velocity + settings.gravity * deltaSeconds;
    const auto displacement = result.velocity * deltaSeconds;
    if (!finite(result.velocity) || !finite(displacement)) {
        return Result<CharacterMove2D>::failure(
            Error(ErrorCode::InvalidArgument, "2D character movement overflowed"));
    }
    const auto safeSpan = std::max(0.001F, std::min(settings.halfExtents.x,
                                                   settings.halfExtents.y));
    const auto requiredStepEstimate =
        std::ceil(std::max(std::fabs(displacement.x), std::fabs(displacement.y)) / safeSpan);
    const auto requiredSteps = requiredStepEstimate >= static_cast<float>(settings.maximumSubsteps)
                                   ? settings.maximumSubsteps
                                   : static_cast<std::uint32_t>(
                                         std::max(1.0F, requiredStepEstimate));
    const auto steps = std::min(requiredSteps, settings.maximumSubsteps);
    const auto stepSeconds = steps == 0U ? 0.0F : deltaSeconds / static_cast<float>(steps);

    PhysicsBody2D query;
    query.shape = AabbShape{{settings.halfExtents.x + settings.skinWidth,
                             settings.halfExtents.y + settings.skinWidth}};
    query.layer = settings.layer;
    query.collisionMask = settings.collisionMask;
    for (std::uint32_t stepIndex = 0U; stepIndex < steps; ++stepIndex) {
        static_cast<void>(stepIndex);
        query.position = result.position + result.velocity * stepSeconds;
        for (std::uint32_t pass = 0U; pass < 4U; ++pass) {
            bool corrected = false;
            for (const auto& [key, body] : bodies_) {
                static_cast<void>(key);
                if (body.trigger || (settings.collisionMask & body.layer) == 0U ||
                    (body.collisionMask & settings.layer) == 0U) {
                    continue;
                }
                auto contact = collide(query, body);
                if (!contact)
                    continue;
                if (contact->penetration > 0.0F) {
                    query.position = query.position - contact->normal * contact->penetration;
                    corrected = true;
                }
                const auto velocityAlongNormal =
                    result.velocity.x * contact->normal.x +
                    result.velocity.y * contact->normal.y;
                if (velocityAlongNormal > 0.0F) {
                    result.velocity =
                        result.velocity - contact->normal * velocityAlongNormal;
                }
                result.grounded = result.grounded || contact->normal.y > 0.5F;
                result.hitCeiling = result.hitCeiling || contact->normal.y < -0.5F;
                result.hitWall = result.hitWall || std::fabs(contact->normal.x) > 0.5F;
                if (std::find(result.touchedBodies.begin(), result.touchedBodies.end(),
                              body.id) == result.touchedBodies.end()) {
                    result.touchedBodies.push_back(body.id);
                }
            }
            if (!corrected)
                break;
        }
        result.position = query.position;
    }
    return Result<CharacterMove2D>::success(std::move(result));
}

std::vector<PhysicsDebugPrimitive2D> PhysicsWorld2D::debugPrimitives() const {
    std::vector<PhysicsDebugPrimitive2D> primitives;
    primitives.reserve(bodies_.size());
    for (const auto& [key, body] : bodies_) {
        static_cast<void>(key);
        PhysicsDebugPrimitive2D primitive;
        primitive.body = body.id;
        primitive.center = body.position;
        primitive.trigger = body.trigger;
        if (const auto* box = std::get_if<AabbShape>(&body.shape)) {
            primitive.shape = PhysicsDebugShape2D::Aabb;
            primitive.halfExtents = box->halfExtents;
        } else if (const auto* circle = std::get_if<CircleShape>(&body.shape)) {
            primitive.shape = PhysicsDebugShape2D::Circle;
            primitive.radius = circle->radius;
        } else {
            primitive.shape = PhysicsDebugShape2D::Point;
        }
        primitives.push_back(primitive);
    }
    return primitives;
}

} // namespace fabgl
