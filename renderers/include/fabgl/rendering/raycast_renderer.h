#pragma once

#include <fabgl/math/types.h>
#include <fabgl/rendering/framebuffer.h>

#include <cstdint>
#include <vector>

namespace fabgl::rendering {

struct RaycastMap final {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> cells;
    std::vector<Color> wallPalette;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::uint8_t cell(int x, int y) const noexcept;
};

struct RaycastCamera final {
    Vec2 position{1.5F, 1.5F};
    Vec2 direction{1.0F, 0.0F};
    float fieldOfViewDegrees = 66.0F;
    float pitch = 0.0F;
};

struct Billboard final {
    Vec2 position{};
    Color color{255, 255, 255, 255};
    float radius = 0.25F;
};

struct RaycastStats final {
    std::uint32_t rays = 0;
    std::uint32_t ddaSteps = 0;
    std::uint32_t billboards = 0;
};

class RaycastRenderer final {
  public:
    explicit RaycastRenderer(Framebuffer& framebuffer) : framebuffer_(framebuffer) {}

    [[nodiscard]] RaycastStats render(const RaycastMap& map, const RaycastCamera& camera,
                                      const std::vector<Billboard>& billboards = {}) noexcept;

  private:
    Framebuffer& framebuffer_;
    std::vector<float> depthBuffer_;
};

[[nodiscard]] RaycastMap makeDemoRaycastMap();

} // namespace fabgl::rendering
