#include <fabgl/rendering/racer_renderer.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace fabgl::rendering {

RacerStats RacerRenderer::render(const std::vector<RoadSegment>& track,
                                 const RacerCamera& camera) noexcept {
    RacerStats stats{};
    const auto width = framebuffer_.width();
    const auto height = framebuffer_.height();
    const auto horizon = height / 3;
    framebuffer_.fillRect(0, 0, width, horizon, {76, 157, 220, 255});
    framebuffer_.fillRect(0, horizon, width, height - horizon, {44, 124, 52, 255});
    if (track.empty()) {
        return stats;
    }

    auto accumulatedCurve = 0.0F;
    auto accumulatedHill = 0.0F;
    const auto baseSegment =
        static_cast<std::size_t>(std::max(0.0F, camera.distance)) % track.size();
    for (auto screenY = horizon; screenY < height; ++screenY) {
        ++stats.scanlines;
        const auto normalized =
            static_cast<float>(screenY - horizon + 1) / static_cast<float>(height - horizon);
        const auto depth = 1.0F / std::max(0.025F, normalized * normalized);
        const auto segmentOffset = static_cast<std::size_t>(depth * 1.65F);
        const auto& segment = track[(baseSegment + segmentOffset) % track.size()];
        ++stats.segmentsSampled;
        accumulatedCurve += segment.curve * normalized * normalized;
        accumulatedHill += segment.hill * 0.00012F;

        const auto perspectiveWidth =
            static_cast<float>(width) * 0.08F + static_cast<float>(width) * 0.74F * normalized;
        const auto roadHalf = perspectiveWidth * std::clamp(segment.width, 0.45F, 1.25F) * 0.5F;
        const auto center = static_cast<float>(width) * 0.5F +
                            accumulatedCurve * static_cast<float>(width) * 0.02F -
                            camera.lateral * perspectiveWidth * 0.45F;
        const auto stripePeriod = (segmentOffset / 3U) % 2U;
        auto grass = segment.grass;
        if (stripePeriod == 0U) {
            grass.r = static_cast<std::uint8_t>(std::min(255, static_cast<int>(grass.r) + 12));
            grass.g = static_cast<std::uint8_t>(std::min(255, static_cast<int>(grass.g) + 12));
        }
        framebuffer_.fillRect(0, screenY, width, 1, grass);
        const auto left = static_cast<int>(center - roadHalf);
        const auto right = static_cast<int>(center + roadHalf);
        const auto rumbleWidth = std::max(1, static_cast<int>(roadHalf * 0.08F));
        auto rumble = stripePeriod == 0U ? segment.rumble : Color{205, 45, 45, 255};
        framebuffer_.fillRect(left - rumbleWidth, screenY, rumbleWidth, 1, rumble);
        framebuffer_.fillRect(left, screenY, right - left, 1, segment.road);
        framebuffer_.fillRect(right, screenY, rumbleWidth, 1, rumble);

        if ((segmentOffset / 5U) % 2U == 0U) {
            const auto markerWidth = std::max(1, static_cast<int>(roadHalf * 0.025F));
            framebuffer_.fillRect(static_cast<int>(center) - markerWidth / 2, screenY, markerWidth,
                                  1, {235, 225, 160, 255});
        }
    }

    const auto hillShift = static_cast<int>(accumulatedHill);
    if (hillShift != 0) {
        framebuffer_.drawLine(0, horizon + hillShift, width - 1, horizon + hillShift,
                              {212, 232, 240, 255});
    }
    return stats;
}

std::vector<RoadSegment> makeDemoTrack() {
    std::vector<RoadSegment> track(240U);
    for (std::size_t index = 0; index < track.size(); ++index) {
        auto& segment = track[index];
        const auto phase = static_cast<float>(index) * 0.065F;
        segment.curve = std::sin(phase) * 0.018F + std::sin(phase * 0.37F) * 0.012F;
        segment.hill = std::sin(phase * 0.55F) * 0.8F;
        segment.width = 0.88F + std::sin(phase * 0.21F) * 0.08F;
        if ((index / 30U) % 2U != 0U) {
            segment.grass = {80, 118, 45, 255};
        }
    }
    return track;
}

} // namespace fabgl::rendering
