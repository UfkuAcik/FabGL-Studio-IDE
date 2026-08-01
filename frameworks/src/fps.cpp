#include <fabgl/frameworks/fps.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace fabgl::frameworks {

int HealthArmor::applyDamage(int amount) noexcept {
    amount = std::max(0, amount);
    const auto absorbed = std::min(armor, amount / 2);
    armor -= absorbed;
    const auto healthDamage = amount - absorbed;
    health = std::max(0, health - healthDamage);
    return healthDamage;
}

void DoorState::activate() noexcept {
    if (phase == DoorPhase::Closed || phase == DoorPhase::Closing)
        phase = DoorPhase::Opening;
    else if (phase == DoorPhase::Open)
        holdRemaining = holdSeconds;
}

void DoorState::update(float deltaSeconds) noexcept {
    const auto delta = std::clamp(deltaSeconds, 0.0F, 0.1F);
    switch (phase) {
    case DoorPhase::Closed:
        openness = 0.0F;
        break;
    case DoorPhase::Opening:
        openness = std::min(1.0F, openness + speed * delta);
        if (openness >= 1.0F) {
            phase = DoorPhase::Open;
            holdRemaining = holdSeconds;
        }
        break;
    case DoorPhase::Open:
        holdRemaining -= delta;
        if (holdRemaining <= 0.0F)
            phase = DoorPhase::Closing;
        break;
    case DoorPhase::Closing:
        openness = std::max(0.0F, openness - speed * delta);
        if (openness <= 0.0F)
            phase = DoorPhase::Closed;
        break;
    }
}

HitscanResult hitscan(Vec2 origin, Vec2 direction, float maximumDistance,
                      const std::vector<HitscanTarget>& targets) noexcept {
    const auto length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    if (length <= 0.00001F || maximumDistance <= 0.0F)
        return {};
    direction = {direction.x / length, direction.y / length};
    HitscanResult result;
    result.distance = std::numeric_limits<float>::infinity();
    for (std::size_t index = 0; index < targets.size(); ++index) {
        if (!targets[index].active || targets[index].radius <= 0.0F)
            continue;
        const auto relative = targets[index].position - origin;
        const auto projection = relative.x * direction.x + relative.y * direction.y;
        if (projection < 0.0F || projection > maximumDistance)
            continue;
        const auto closest = origin + direction * projection;
        const auto difference = targets[index].position - closest;
        if (difference.x * difference.x + difference.y * difference.y <=
                targets[index].radius * targets[index].radius &&
            projection < result.distance) {
            result = {true, index, projection};
        }
    }
    if (!result.hit)
        result.distance = 0.0F;
    return result;
}

} // namespace fabgl::frameworks
