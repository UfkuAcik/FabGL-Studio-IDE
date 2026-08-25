#include "test_harness.h"

#include <fabgl/rendering/raycast_map_asset.h>

#include <string>

namespace {

[[nodiscard]] std::string replaceFirst(std::string value, const std::string& needle,
                                       const std::string& replacement) {
    const auto offset = value.find(needle);
    FGL_CHECK(offset != std::string::npos);
    value.replace(offset, needle.size(), replacement);
    return value;
}

} // namespace

FGL_TEST(raycast_map_asset_v1_is_canonical_and_round_trips) {
    fabgl::rendering::RaycastMapAsset source;
    source.guid = fabgl::AssetGuid::fromStableName("tests.raycast-map.canonical");
    source.map = fabgl::rendering::makeDemoRaycastMap();
    auto encoded = fabgl::rendering::serializeRaycastMapAsset(source);
    FGL_CHECK(encoded);
    FGL_CHECK(encoded.value().rfind("fglray 1\n", 0U) == 0U);
    auto decoded = fabgl::rendering::deserializeRaycastMapAsset(encoded.value());
    FGL_CHECK(decoded);
    FGL_CHECK(decoded.value().guid == source.guid);
    FGL_CHECK(decoded.value().map.cells == source.map.cells);
    FGL_CHECK(decoded.value().map.wallPalette == source.map.wallPalette);
    auto second = fabgl::rendering::serializeRaycastMapAsset(decoded.value());
    FGL_CHECK(second);
    FGL_CHECK(second.value() == encoded.value());
}

FGL_TEST(raycast_map_asset_v1_rejects_corruption_versions_and_limits) {
    fabgl::rendering::RaycastMapAsset source;
    source.guid = fabgl::AssetGuid::fromStableName("tests.raycast-map.corruption");
    source.map = fabgl::rendering::makeDemoRaycastMap();
    auto encoded = fabgl::rendering::serializeRaycastMapAsset(source);
    FGL_CHECK(encoded);
    FGL_CHECK(!fabgl::rendering::deserializeRaycastMapAsset(
        replaceFirst(encoded.value(), "fglray 1", "fglray 99")));
    FGL_CHECK(!fabgl::rendering::deserializeRaycastMapAsset(
        replaceFirst(encoded.value(), "cells 256", "cells 255")));
    FGL_CHECK(!fabgl::rendering::deserializeRaycastMapAsset(encoded.value() + "trailing\n"));
    auto openBoundary = source;
    openBoundary.map.cells.front() = 0U;
    FGL_CHECK(!fabgl::rendering::serializeRaycastMapAsset(openBoundary));
    fabgl::rendering::RaycastMapFormatLimits limits;
    limits.maximumWidth = 8;
    FGL_CHECK(!fabgl::rendering::deserializeRaycastMapAsset(encoded.value(), limits));
}
