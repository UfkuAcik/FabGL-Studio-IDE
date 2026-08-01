#include "fabgl/physics/physics2d.h"

#include <algorithm>
#include <cmath>
#include <limits>
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

std::optional<Contact2D> collide(const PhysicsBody2D& first, const PhysicsBody2D& second) {
    if (std::holds_alternative<AabbShape>(first.shape) &&
        std::holds_alternative<AabbShape>(second.shape)) {
        return collideAabbs(first, second);
    }
    if (std::holds_alternative<CircleShape>(first.shape) &&
        std::holds_alternative<CircleShape>(second.shape)) {
        return collideCircles(first, second);
    }
    if (std::holds_alternative<CircleShape>(first.shape))
        return collideCircleAabb(first, second);
    auto contact = collideCircleAabb(second, first);
    if (contact)
        contact->normal = contact->normal * -1.0F;
    return contact;
}

void resolveContact(PhysicsBody2D& first, PhysicsBody2D& second, const Contact2D& contact) {
    if (contact.trigger || contact.penetration <= 0.0F)
        return;
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

} // namespace

Result<PhysicsBodyId> PhysicsWorld2D::addBody(PhysicsBody2D body) {
    const bool validShape = std::visit(
        [](const auto& shape) {
            using Shape = std::decay_t<decltype(shape)>;
            if constexpr (std::is_same_v<Shape, AabbShape>) {
                return std::isfinite(shape.halfExtents.x) && std::isfinite(shape.halfExtents.y) &&
                       shape.halfExtents.x > 0.0F && shape.halfExtents.y > 0.0F;
            } else {
                return std::isfinite(shape.radius) && shape.radius > 0.0F;
            }
        },
        body.shape);
    if (!validShape || !std::isfinite(body.position.x) || !std::isfinite(body.position.y) ||
        !std::isfinite(body.velocity.x) || !std::isfinite(body.velocity.y) || body.layer == 0U) {
        return Result<PhysicsBodyId>::failure(
            Error(ErrorCode::InvalidArgument, "physics body is invalid"));
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

Result<void> PhysicsWorld2D::step(float deltaSeconds) {
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0F) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "physics delta must be finite and non-negative"));
    }
    for (auto& body : bodies_) {
        if (body.second.dynamic)
            body.second.position = body.second.position + body.second.velocity * deltaSeconds;
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
                       : raycastCircle(body, origin, direction, maximumDistance);
        if (hit && (!closest || hit->distance < closest->distance ||
                    (hit->distance == closest->distance && hit->body < closest->body))) {
            closest = *hit;
        }
    }
    return Result<std::optional<RaycastHit2D>>::success(closest);
}

} // namespace fabgl
