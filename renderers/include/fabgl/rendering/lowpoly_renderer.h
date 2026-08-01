#pragma once

#include <fabgl/math/types.h>
#include <fabgl/rendering/framebuffer.h>

#include <cstdint>
#include <vector>

namespace fabgl::rendering {

struct LowPolyVertex final {
    Vec3 position{};
};

struct LowPolyTriangle final {
    std::uint16_t a = 0;
    std::uint16_t b = 0;
    std::uint16_t c = 0;
    Color color{255, 255, 255, 255};
};

struct LowPolyMesh final {
    std::vector<LowPolyVertex> vertices;
    std::vector<LowPolyTriangle> triangles;
};

struct LowPolyCamera final {
    Vec3 position{0.0F, 0.0F, -4.0F};
    float focalLength = 1.2F;
    float nearPlane = 0.1F;
};

struct LowPolyStats final {
    std::uint32_t submitted = 0;
    std::uint32_t culled = 0;
    std::uint32_t drawn = 0;
};

class LowPolyRenderer final {
  public:
    explicit LowPolyRenderer(Framebuffer& framebuffer) : framebuffer_(framebuffer) {}

    [[nodiscard]] LowPolyStats render(const LowPolyMesh& mesh, const Mat4& model,
                                      const LowPolyCamera& camera) noexcept;

  private:
    Framebuffer& framebuffer_;
};

[[nodiscard]] LowPolyMesh makeDemoCube();

} // namespace fabgl::rendering
