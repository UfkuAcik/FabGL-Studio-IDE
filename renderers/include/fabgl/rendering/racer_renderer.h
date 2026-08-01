#pragma once

#include <fabgl/math/types.h>
#include <fabgl/rendering/framebuffer.h>

#include <cstdint>
#include <vector>

namespace fabgl::rendering {

struct RoadSegment final {
    float curve = 0.0F;
    float hill = 0.0F;
    float width = 1.0F;
    Color road{68, 68, 72, 255};
    Color grass{40, 126, 54, 255};
    Color rumble{235, 235, 235, 255};
};

struct RacerCamera final {
    float distance = 0.0F;
    float lateral = 0.0F;
    float speed = 0.0F;
};

struct RacerStats final {
    std::uint32_t scanlines = 0;
    std::uint32_t segmentsSampled = 0;
};

class RacerRenderer final {
  public:
    explicit RacerRenderer(Framebuffer& framebuffer) : framebuffer_(framebuffer) {}

    [[nodiscard]] RacerStats render(const std::vector<RoadSegment>& track,
                                    const RacerCamera& camera) noexcept;

  private:
    Framebuffer& framebuffer_;
};

[[nodiscard]] std::vector<RoadSegment> makeDemoTrack();

} // namespace fabgl::rendering
