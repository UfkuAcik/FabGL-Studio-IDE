#pragma once

#include <fabgl/math/types.h>

#include <cstddef>
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

} // namespace fabgl::frameworks
