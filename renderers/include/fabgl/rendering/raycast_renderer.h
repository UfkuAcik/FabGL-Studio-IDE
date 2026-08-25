#pragma once

#include <fabgl/math/types.h>
#include <fabgl/rendering/framebuffer.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace fabgl::rendering {

struct RaycastTexture final {
    int width = 0;
    int height = 0;
    std::vector<Color> pixels;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] Color sample(float u, float v) const noexcept;
};

struct RaycastDoor final {
    int x = 0;
    int y = 0;
    float openness = 0.0F;
    bool secret = false;
};

struct RaycastMap final {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> cells;
    std::vector<Color> wallPalette;
    std::vector<RaycastTexture> wallTextures;
    std::vector<std::uint8_t> sectorLighting;
    std::vector<RaycastDoor> doors;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::uint8_t cell(int x, int y) const noexcept;
    [[nodiscard]] std::uint8_t light(int x, int y) const noexcept;
    [[nodiscard]] const RaycastDoor* door(int x, int y) const noexcept;
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
    const RaycastTexture* texture = nullptr;
    enum class Kind : std::uint8_t { Scenery, Enemy, Item } kind = Kind::Scenery;
};

struct RaycastRenderSettings final {
    int internalWidth = 0;
    std::uint16_t maximumDdaSteps = 256U;
    std::uint16_t maximumBillboards = 256U;
    bool floorAndCeiling = false;
    bool distanceFog = false;
    bool minimap = false;
    bool weaponOverlay = false;
    bool fixedPointCoordinates = false;
    float ambientLight = 0.22F;
    float fogStart = 4.0F;
    float fogEnd = 18.0F;
    float maximumPitch = 48.0F;
    Color ceilingColor{35U, 42U, 58U, 255U};
    Color floorColor{40U, 34U, 28U, 255U};
    Color fogColor{18U, 20U, 26U, 255U};
    const RaycastTexture* floorTexture = nullptr;
    const RaycastTexture* ceilingTexture = nullptr;
    const RaycastTexture* weaponTexture = nullptr;
    Color weaponColor{90U, 95U, 105U, 255U};
};

struct RaycastStats final {
    std::uint32_t rays = 0;
    std::uint32_t ddaSteps = 0;
    std::uint32_t billboards = 0;
    std::uint32_t texturedWallColumns = 0;
    std::uint32_t floorCeilingPixels = 0;
    std::uint32_t doorsHit = 0;
    std::uint32_t secretWallsHit = 0;
    std::uint32_t transparentSpritePixels = 0;
    std::uint32_t enemies = 0;
    std::uint32_t items = 0;
    std::uint32_t lookupEntries = 0;
    bool fixedPointPath = false;
};

class RaycastRenderer final {
  public:
    explicit RaycastRenderer(Framebuffer& framebuffer) : framebuffer_(framebuffer) {}

    [[nodiscard]] RaycastStats render(const RaycastMap& map, const RaycastCamera& camera,
                                      const std::vector<Billboard>& billboards = {},
                                      const RaycastRenderSettings& settings = {}) noexcept;

  private:
    Framebuffer& framebuffer_;
    std::vector<float> depthBuffer_;
    std::vector<float> cameraXLookup_;
    int lookupWidth_ = 0;
};

[[nodiscard]] RaycastMap makeDemoRaycastMap();

} // namespace fabgl::rendering
