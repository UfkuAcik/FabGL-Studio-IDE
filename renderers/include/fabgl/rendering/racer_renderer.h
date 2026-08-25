#pragma once

#include <fabgl/rendering/framebuffer.h>
#include <fabgl/rendering/racer_track.h>
#include <fabgl/rendering/renderer_2d.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace fabgl::rendering {

struct RacerCamera final {
    float distance = 0.0F;
    float lateral = 0.0F;
    float speed = 0.0F;
};

struct RacerStats final {
    std::uint32_t scanlines = 0;
    std::uint32_t segmentsSampled = 0;
    std::uint32_t roadsideObjectsDrawn = 0;
    std::uint32_t backgroundLayersDrawn = 0;
    std::uint32_t opponentsDrawn = 0;
    std::uint32_t weatherPixelsBlended = 0;
    std::uint32_t resolvedSpriteAssets = 0;
    std::uint32_t missingSpriteAssets = 0;
};

using RacerSpriteResolver = std::function<std::shared_ptr<const Sprite>(AssetGuid)>;

struct Mode7Texture final {
    int width = 0;
    int height = 0;
    std::vector<Color> pixels;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] Color sample(float x, float y) const noexcept;
};

struct Mode7Camera final {
    Vec2 position{};
    float headingRadians = 0.0F;
    float altitude = 1.0F;
};

struct Mode7Settings final {
    int horizon = -1;
    int internalWidth = 0;
    float scale = 1.0F;
    float maximumDistance = 64.0F;
    float fogStart = 24.0F;
    float fogEnd = 64.0F;
    Color skyColor{76U, 157U, 220U, 255U};
    Color fogColor{150U, 170U, 180U, 255U};
};

struct Mode7Stats final {
    std::uint32_t scanlines = 0U;
    std::uint32_t samples = 0U;
    std::uint32_t foggedSamples = 0U;
};

class RacerRenderer final {
  public:
    explicit RacerRenderer(Framebuffer& framebuffer) : framebuffer_(framebuffer) {}

    [[nodiscard]] RacerStats render(const std::vector<RoadSegment>& track,
                                    const RacerCamera& camera) noexcept;
    // Renders the complete authoring asset, including resolved metadata-driven scenery,
    // opponents, parallax layers, segment length, and weather tinting.
    [[nodiscard]] RacerStats render(const RacerTrackAsset& track, const RacerCamera& camera,
                                    const RacerSpriteResolver& sprites = {}) noexcept;
    // General rotated/scaled ground plane used by road or terrain-based pseudo-3D games.
    [[nodiscard]] Mode7Stats renderMode7(const Mode7Texture& texture, const Mode7Camera& camera,
                                         const Mode7Settings& settings = {}) noexcept;

  private:
    Framebuffer& framebuffer_;
};

[[nodiscard]] std::vector<RoadSegment> makeDemoTrack();

} // namespace fabgl::rendering
