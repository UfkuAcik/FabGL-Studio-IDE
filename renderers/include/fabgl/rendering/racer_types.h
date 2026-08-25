#pragma once

#include <fabgl/math/types.h>

#include <cstdint>

namespace fabgl::rendering {

struct RoadSegment final {
    float curve = 0.0F;
    float hill = 0.0F;
    float width = 1.0F;
    Color road{68, 68, 72, 255};
    Color grass{40, 126, 54, 255};
    Color rumble{235, 235, 235, 255};
};

enum class RacerWeatherKind : std::uint8_t {
    Clear,
    Rain,
    Fog,
    Storm,
};

struct RacerWeatherMetadata final {
    RacerWeatherKind kind = RacerWeatherKind::Clear;
    float intensity = 0.0F;
    float visibility = 1.0F;
    float wind = 0.0F;
    Color tint{205, 220, 232, 255};
    std::uint32_t seed = 0U;
};

} // namespace fabgl::rendering
