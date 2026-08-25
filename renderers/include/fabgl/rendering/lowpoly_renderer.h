#pragma once

#include <fabgl/material/material.h>
#include <fabgl/math/types.h>
#include <fabgl/rendering/framebuffer.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fabgl::rendering {

struct LowPolyVertex final {
    Vec3 position{};
    Color color{255U, 255U, 255U, 255U};
    // Normalized coordinates address either a complete texture or a small atlas region.
    Vec2 uv{};
};

struct LowPolyTriangle final {
    std::uint16_t a = 0;
    std::uint16_t b = 0;
    std::uint16_t c = 0;
    Color color{255, 255, 255, 255};
    const Material* material = nullptr;
};

struct LowPolyMesh final {
    std::vector<LowPolyVertex> vertices;
    std::vector<LowPolyTriangle> triangles;
};

struct LowPolyCamera final {
    Vec3 position{0.0F, 0.0F, -4.0F};
    float yawRadians = 0.0F;
    float pitchRadians = 0.0F;
    float focalLength = 1.2F;
    float nearPlane = 0.1F;
    float farPlane = 1000.0F;
    Color fogColor{0U, 0U, 0U, 255U};
    float fogStart = 10.0F;
    float fogEnd = 100.0F;
};

enum class LowPolyProjection : std::uint8_t { Perspective, Orthographic };
enum class LowPolyQuality : std::uint8_t { Low, Medium, High };

struct LowPolyRenderSettings final {
    LowPolyProjection projection = LowPolyProjection::Perspective;
    LowPolyQuality quality = LowPolyQuality::High;
    float orthographicHeight = 4.0F;
    Vec3 directionalLight{0.0F, 0.0F, -1.0F};
    float directionalStrength = 1.0F;
    float ambientLight = 0.2F;
    std::size_t maximumTriangles = 8192U;
    std::size_t maximumBillboards = 256U;
    int maximumTextureDimension = 512;
    bool backfaceCulling = true;
    bool textures = true;
};

struct LowPolyTextureView final {
    int width = 0;
    int height = 0;
    const Color* pixels = nullptr;
    std::size_t pixelCount = 0U;

    [[nodiscard]] bool valid(int maximumDimension = 512) const noexcept;
};

struct LowPolyMaterialBinding final {
    // Used for mesh triangles that do not carry a direct material pointer. The texture is the
    // already-resolved Material::baseTexture payload and remains owned by the caller.
    const Material* material = nullptr;
    LowPolyTextureView texture;
};

struct LowPolyBillboard final {
    Vec3 position{};
    Vec2 size{1.0F, 1.0F};
    Color color{255U, 255U, 255U, 255U};
};

struct LowPolyStats final {
    std::uint32_t submitted = 0;
    std::uint32_t culled = 0;
    std::uint32_t drawn = 0;
    std::uint32_t frustumCulled = 0;
    std::uint32_t billboards = 0;
    std::uint32_t texturedTriangles = 0;
    std::uint32_t texturedPixels = 0;
    std::uint32_t capacityRejected = 0;
};

class LowPolyRenderer final {
  public:
    explicit LowPolyRenderer(Framebuffer& framebuffer) : framebuffer_(framebuffer) {}

    [[nodiscard]] LowPolyStats
    render(const LowPolyMesh& mesh, const Mat4& model, const LowPolyCamera& camera,
           const LowPolyRenderSettings& settings = {},
           const std::vector<LowPolyBillboard>& billboards = {},
           const LowPolyMaterialBinding& material = {}) noexcept;

  private:
    Framebuffer& framebuffer_;
};

[[nodiscard]] LowPolyMesh makeDemoCube();

} // namespace fabgl::rendering
