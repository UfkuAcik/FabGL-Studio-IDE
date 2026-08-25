#pragma once

#include <fabgl/core/guid.h>
#include <fabgl/core/result.h>
#include <fabgl/rendering/raycast_renderer.h>

#include <cstddef>
#include <string>
#include <string_view>

namespace fabgl::rendering {

struct RaycastMapAsset final {
    AssetGuid guid;
    RaycastMap map;
};

struct RaycastMapFormatLimits final {
    std::size_t maximumSourceBytes = 4U * 1024U * 1024U;
    int maximumWidth = 512;
    int maximumHeight = 512;
    std::size_t maximumCells = 262'144U;
    std::size_t maximumPaletteColors = 256U;
};

[[nodiscard]] Result<void> validateRaycastMapAsset(const RaycastMapAsset& asset,
                                                   const RaycastMapFormatLimits& limits = {});
[[nodiscard]] Result<std::string>
serializeRaycastMapAsset(const RaycastMapAsset& asset, const RaycastMapFormatLimits& limits = {});
[[nodiscard]] Result<RaycastMapAsset>
deserializeRaycastMapAsset(std::string_view text, const RaycastMapFormatLimits& limits = {});

} // namespace fabgl::rendering
