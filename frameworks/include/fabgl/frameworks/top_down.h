#pragma once

#include <fabgl/math/types.h>

#include <cstddef>
#include <vector>

namespace fabgl::frameworks {

struct TopDownState final {
    Vec2 position{};
    Vec2 velocity{};
    Vec2 aim{1.0F, 0.0F};
};

void updateTopDown(TopDownState& state, Vec2 movement, Vec2 aim, float speed,
                   float deltaSeconds) noexcept;

struct Projectile final {
    Vec2 position{};
    Vec2 velocity{};
    float lifetime = 0.0F;
    int damage = 1;
    bool active = false;
};

class ProjectilePool final {
  public:
    explicit ProjectilePool(std::size_t capacity);
    [[nodiscard]] bool spawn(Vec2 position, Vec2 direction, float speed, float lifetime,
                             int damage) noexcept;
    void update(float deltaSeconds) noexcept;
    [[nodiscard]] const std::vector<Projectile>& projectiles() const noexcept {
        return projectiles_;
    }

  private:
    std::vector<Projectile> projectiles_;
};

struct Weapon final {
    int magazineSize = 8;
    int ammunition = 8;
    float fireInterval = 0.15F;
    float cooldown = 0.0F;
    bool automatic = false;

    void update(float deltaSeconds) noexcept;
    [[nodiscard]] bool tryFire(bool pressed, bool held) noexcept;
    void reload() noexcept {
        ammunition = magazineSize;
    }
};

} // namespace fabgl::frameworks
