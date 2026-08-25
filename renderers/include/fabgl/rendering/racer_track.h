#pragma once

#include <fabgl/core/guid.h>
#include <fabgl/core/result.h>
#include <fabgl/rendering/racer_types.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fabgl::rendering {

struct RacerCheckpoint final {
    std::uint32_t segment = 0U;
    std::string name;
};

struct RacerRoadsideObject final {
    std::uint16_t id = 0U;
    std::uint32_t segment = 0U;
    float lateral = 0.0F;
    float scale = 1.0F;
    AssetGuid sprite;
    Color tint{255, 255, 255, 255};
};

struct RacerBackgroundLayer final {
    std::uint16_t id = 0U;
    AssetGuid sprite;
    float parallax = 0.0F;
    float verticalOffset = 0.0F;
    float scale = 1.0F;
    Color tint{255, 255, 255, 255};
};

struct RacerOpponentSpawn final {
    std::uint16_t id = 0U;
    std::uint32_t segment = 0U;
    float lateral = 0.0F;
    float targetSpeed = 40.0F;
    float skill = 0.5F;
    AssetGuid sprite;
};

struct RacerTrackAsset final {
    AssetGuid guid;
    std::string name;
    float segmentLength = 1.0F;
    std::uint32_t startSegment = 0U;
    std::uint32_t finishSegment = 0U;
    RacerWeatherMetadata weather;
    std::vector<RoadSegment> segments;
    // Checkpoint vector order is race order and is preserved in the file.
    std::vector<RacerCheckpoint> checkpoints;
    std::vector<RacerRoadsideObject> roadsideObjects;
    std::vector<RacerBackgroundLayer> backgroundLayers;
    std::vector<RacerOpponentSpawn> opponentSpawns;
};

struct RacerTrackFormatLimits final {
    std::size_t maximumSourceBytes = 4U * 1024U * 1024U;
    std::size_t maximumSegments = 8192U;
    std::size_t maximumCheckpoints = 256U;
    std::size_t maximumRoadsideObjects = 1024U;
    std::size_t maximumBackgroundLayers = 16U;
    std::size_t maximumOpponentSpawns = 128U;
    std::size_t maximumStringBytes = 1024U;
};

[[nodiscard]] Result<void> validateRacerTrack(const RacerTrackAsset& track,
                                              const RacerTrackFormatLimits& limits = {});
[[nodiscard]] Result<std::string> serializeRacerTrack(const RacerTrackAsset& track,
                                                      const RacerTrackFormatLimits& limits = {});
[[nodiscard]] Result<RacerTrackAsset>
deserializeRacerTrack(std::string_view text, const RacerTrackFormatLimits& limits = {});

} // namespace fabgl::rendering
