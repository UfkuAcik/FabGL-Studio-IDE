#include <fabgl/project/project_asset_library.h>

#include <fabgl/assets/audio_importer.h>
#include <fabgl/assets/file_io.h>
#include <fabgl/assets/image_pipeline.h>
#include <fabgl/assets/mesh_importer.h>
#include <fabgl/assets/tilemap_importer.h>
#include <fabgl/material/material.h>
#include <fabgl/rendering/racer_track.h>
#include <fabgl/rendering/raycast_map_asset.h>
#include <fabgl/serialization/material_serializer.h>

#include <project_format.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fabgl::project {

namespace {

[[nodiscard]] std::string joinPath(const std::string& left, const std::string& right) {
    return left + (left.empty() || left.back() == '/' || left.back() == '\\' ? "" : "/") + right;
}

[[nodiscard]] Error assetError(const Error& source, const ProjectAssetEntry& entry) {
    return source.withContext("assetGuid", entry.guid.toString())
        .withContext("assetPath", entry.path)
        .withContext("assetType", entry.type);
}

[[nodiscard]] const ProjectAssetEntry* findManifestAsset(const Manifest& manifest,
                                                         const AssetGuid guid) noexcept {
    const auto found = std::find_if(manifest.assets.begin(), manifest.assets.end(),
                                    [guid](const auto& asset) { return asset.guid == guid; });
    return found == manifest.assets.end() ? nullptr : &*found;
}

[[nodiscard]] Result<void> validateManifestReference(const Manifest& manifest, const AssetGuid guid,
                                                     const std::string_view expectedType,
                                                     const std::string_view relationship,
                                                     const ProjectAssetEntry& owner) {
    const auto* referenced = findManifestAsset(manifest, guid);
    if (referenced == nullptr) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidFormat, "runtime asset references an unknown GUID")
                .addContext("assetPath", owner.path)
                .addContext("relationship", std::string(relationship))
                .addContext("referenced", guid.toString()));
    }
    if (!expectedType.empty() && referenced->type != expectedType) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidFormat, "runtime asset references the wrong asset type")
                .addContext("assetPath", owner.path)
                .addContext("relationship", std::string(relationship))
                .addContext("referenced", guid.toString())
                .addContext("expectedType", std::string(expectedType))
                .addContext("actualType", referenced->type));
    }
    return Result<void>::success();
}

[[nodiscard]] rendering::Sprite spriteFrom(const assets::IndexedImage& image) {
    rendering::Sprite result;
    result.width = image.width;
    result.height = image.height;
    result.pixels.reserve(image.indices.size());
    for (const auto index : image.indices) {
        auto color = image.palette[static_cast<std::size_t>(index)];
        if (image.transparentIndex != 255U && index == image.transparentIndex) {
            color.a = 0U;
        }
        result.pixels.push_back(color);
    }
    return result;
}

[[nodiscard]] rendering::Sprite transparentTile(const int width, const int height) {
    rendering::Sprite result;
    result.width = width;
    result.height = height;
    result.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
                         {0U, 0U, 0U, 0U});
    return result;
}

[[nodiscard]] Result<rendering::Sprite> sliceTile(const rendering::Sprite& source,
                                                  const assets::Tileset& tileset,
                                                  const std::uint32_t tile) {
    const auto column = tile % tileset.columns;
    const auto row = tile / tileset.columns;
    const auto x = static_cast<std::uint64_t>(tileset.margin) +
                   static_cast<std::uint64_t>(column) *
                       (static_cast<std::uint64_t>(tileset.tileWidth) + tileset.spacing);
    const auto y = static_cast<std::uint64_t>(tileset.margin) +
                   static_cast<std::uint64_t>(row) *
                       (static_cast<std::uint64_t>(tileset.tileHeight) + tileset.spacing);
    if (x + tileset.tileWidth > static_cast<std::uint64_t>(source.width) ||
        y + tileset.tileHeight > static_cast<std::uint64_t>(source.height)) {
        return Result<rendering::Sprite>::failure(
            Error(ErrorCode::InvalidFormat, "tileset rectangle exceeds its source image")
                .addContext("tile", std::to_string(tile)));
    }
    rendering::Sprite result;
    result.width = tileset.tileWidth;
    result.height = tileset.tileHeight;
    result.pixels.reserve(static_cast<std::size_t>(result.width) *
                          static_cast<std::size_t>(result.height));
    for (auto rowOffset = 0; rowOffset < result.height; ++rowOffset) {
        const auto begin =
            source.pixels.begin() +
            static_cast<std::ptrdiff_t>((static_cast<int>(y) + rowOffset) * source.width +
                                        static_cast<int>(x));
        result.pixels.insert(result.pixels.end(), begin, begin + result.width);
    }
    return Result<rendering::Sprite>::success(std::move(result));
}

[[nodiscard]] Result<rendering::Tilemap>
tilemapFrom(const assets::Tilemap& source, const std::size_t maximumTileKinds,
            const std::map<AssetGuid, assets::Tileset>& tilesets,
            const std::map<AssetGuid, std::shared_ptr<const rendering::Sprite>>& sprites) {
    rendering::Tilemap result;
    if (source.width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        source.height > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        source.tileWidth != source.tileHeight || source.tileWidth == 0U || source.tileWidth > 1024U)
        return Result<rendering::Tilemap>::failure(
            Error(ErrorCode::InvalidFormat, "tilemap dimensions are unsupported by Renderer2D"));
    result.width = static_cast<int>(source.width);
    result.height = static_cast<int>(source.height);
    result.tileSize = source.tileWidth;
    std::uint32_t highest = 0U;
    const auto convertCells = [&highest](const std::vector<std::uint32_t>& cells,
                                         std::vector<std::uint16_t>& output) {
        output.reserve(cells.size());
        for (const auto value : cells) {
            highest = std::max(highest, value);
            if (value >= std::numeric_limits<std::uint16_t>::max())
                return false;
            output.push_back(static_cast<std::uint16_t>(value));
        }
        return true;
    };
    if (source.layers.empty()) {
        if (!convertCells(source.tiles, result.cells))
            return Result<rendering::Tilemap>::failure(
                Error(ErrorCode::CapacityExceeded, "legacy tilemap uses an unsupported tile ID"));
    } else {
        result.layers.reserve(source.layers.size());
        for (const auto& sourceLayer : source.layers) {
            rendering::TilemapLayer layer;
            if (!convertCells(sourceLayer.cells, layer.cells))
                return Result<rendering::Tilemap>::failure(Error(
                    ErrorCode::CapacityExceeded, "tilemap layer uses an unsupported tile ID"));
            layer.kind = static_cast<rendering::TilemapLayerKind>(sourceLayer.kind);
            layer.parallax = {sourceLayer.parallaxX, sourceLayer.parallaxY};
            layer.opacity = sourceLayer.opacity;
            layer.visible = sourceLayer.visible;
            result.layers.push_back(std::move(layer));
        }
    }
    result.objects.reserve(source.objects.size());
    for (const auto& sourceObject : source.objects) {
        std::uint32_t typeHash = 2166136261U;
        for (const auto character : sourceObject.type) {
            typeHash ^= static_cast<std::uint8_t>(character);
            typeHash *= 16777619U;
        }
        result.objects.push_back({sourceObject.id, sourceObject.layer,
                                  static_cast<std::uint16_t>(typeHash & 0xFFFFU),
                                  sourceObject.bounds, sourceObject.asset});
    }
    result.chunks.reserve(source.chunks.size());
    for (const auto& sourceChunk : source.chunks) {
        if (sourceChunk.x > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
            sourceChunk.y > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
            sourceChunk.width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
            sourceChunk.height > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
            return Result<rendering::Tilemap>::failure(Error(
                ErrorCode::CapacityExceeded, "tilemap chunk dimensions exceed runtime limits"));
        result.chunks.push_back(
            {sourceChunk.layer, static_cast<int>(sourceChunk.x), static_cast<int>(sourceChunk.y),
             static_cast<int>(sourceChunk.width), static_cast<int>(sourceChunk.height)});
    }
    result.animations.reserve(source.animations.size());
    for (const auto& sourceAnimation : source.animations) {
        if (sourceAnimation.outputTile >= std::numeric_limits<std::uint16_t>::max())
            return Result<rendering::Tilemap>::failure(
                Error(ErrorCode::CapacityExceeded, "tile animation output exceeds runtime limits"));
        rendering::TileAnimation animation;
        animation.sourceTile = static_cast<std::uint16_t>(sourceAnimation.outputTile);
        animation.frames.reserve(sourceAnimation.frames.size());
        animation.frameDurationsSeconds.reserve(sourceAnimation.frames.size());
        for (const auto& frame : sourceAnimation.frames) {
            if (frame.tile >= std::numeric_limits<std::uint16_t>::max())
                return Result<rendering::Tilemap>::failure(Error(
                    ErrorCode::CapacityExceeded, "tile animation frame exceeds runtime limits"));
            highest = std::max(highest, frame.tile);
            animation.frames.push_back(static_cast<std::uint16_t>(frame.tile));
            animation.frameDurationsSeconds.push_back(
                static_cast<float>(frame.durationMilliseconds) / 1000.0F);
        }
        animation.frameSeconds = animation.frameDurationsSeconds.front();
        result.animations.push_back(std::move(animation));
    }

    for (const auto& reference : source.tilesets) {
        const auto end = static_cast<std::uint64_t>(reference.firstTile) + reference.tileCount;
        if (end == 0U || end > std::numeric_limits<std::uint16_t>::max())
            return Result<rendering::Tilemap>::failure(
                Error(ErrorCode::CapacityExceeded, "tileset mapping exceeds runtime tile IDs"));
        highest = std::max(highest, static_cast<std::uint32_t>(end - 1U));
    }
    const auto kinds = static_cast<std::size_t>(highest) + 1U;
    if (kinds == 0U || kinds > maximumTileKinds ||
        kinds > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()))
        return Result<rendering::Tilemap>::failure(
            Error(ErrorCode::CapacityExceeded, "tilemap tile table exceeds runtime limits"));
    result.tiles.assign(kinds, transparentTile(result.tileSize, result.tileSize));

    for (const auto& reference : source.tilesets) {
        const auto tileset = tilesets.find(reference.tileset);
        if (tileset == tilesets.end())
            return Result<rendering::Tilemap>::failure(
                Error(ErrorCode::NotFound, "tilemap references an unloaded tileset")
                    .addContext("tileset", reference.tileset.toString()));
        if (reference.tileCount != tileset->second.tileCount ||
            tileset->second.tileWidth != source.tileWidth ||
            tileset->second.tileHeight != source.tileHeight)
            return Result<rendering::Tilemap>::failure(
                Error(ErrorCode::InvalidFormat, "tilemap and tileset metadata disagree")
                    .addContext("tileset", reference.tileset.toString()));
        const auto image = sprites.find(tileset->second.sourceImage);
        if (image == sprites.end())
            return Result<rendering::Tilemap>::failure(
                Error(ErrorCode::NotFound, "tileset source image is not loaded")
                    .addContext("image", tileset->second.sourceImage.toString()));
        for (std::uint32_t local = 0U; local < reference.tileCount; ++local) {
            auto tile = sliceTile(*image->second, tileset->second, local);
            if (!tile)
                return Result<rendering::Tilemap>::failure(tile.error());
            result.tiles[static_cast<std::size_t>(reference.firstTile) + local] =
                std::move(tile.value());
        }
        for (const auto collision : tileset->second.collisionTiles)
            result.solidTiles.push_back(
                static_cast<std::uint16_t>(reference.firstTile + collision));
    }
    std::sort(result.solidTiles.begin(), result.solidTiles.end());
    result.solidTiles.erase(std::unique(result.solidTiles.begin(), result.solidTiles.end()),
                            result.solidTiles.end());
    return Result<rendering::Tilemap>::success(std::move(result));
}

[[nodiscard]] rendering::LowPolyMesh meshFrom(const assets::LowPolyMesh& source) {
    rendering::LowPolyMesh result;
    result.vertices.reserve(source.positions.size());
    for (std::size_t index = 0U; index < source.positions.size(); ++index) {
        const auto& position = source.positions[index];
        const auto tone = static_cast<std::uint8_t>(80U + (index * 37U) % 160U);
        const auto uv = source.textureCoordinates.empty()
                            ? assets::MeshTextureCoordinate{}
                            : source.textureCoordinates[index];
        result.vertices.push_back({{position.x, position.y, position.z},
                                   {tone, static_cast<std::uint8_t>(255U - tone / 2U),
                                    static_cast<std::uint8_t>(100U + tone / 3U), 255U},
                                   {uv.u, uv.v}});
    }
    result.triangles.reserve(source.indices.size() / 3U);
    for (std::size_t index = 0U; index + 2U < source.indices.size(); index += 3U) {
        const auto triangleIndex = index / 3U;
        const auto tone = static_cast<std::uint8_t>(70U + (triangleIndex * 29U) % 170U);
        result.triangles.push_back({source.indices[index],
                                    source.indices[index + 1U],
                                    source.indices[index + 2U],
                                    {tone, static_cast<std::uint8_t>(120U + tone / 2U),
                                     static_cast<std::uint8_t>(245U - tone / 2U), 255U}});
    }
    return result;
}

[[nodiscard]] bool visualType(const std::string_view type) noexcept {
    return type == "image" || type == "tilemap" || type == "mesh" || type == "racer.track" ||
           type == "raycast.map";
}

[[nodiscard]] bool runtimeType(const std::string_view type) noexcept {
    return visualType(type) || type == "animation.clip" || type == "animation.controller" ||
           type == "audio" || type == "tileset" || type == "material";
}

void addEstimate(std::size_t& total, const std::size_t value) noexcept {
    total = value > std::numeric_limits<std::size_t>::max() - total
                ? std::numeric_limits<std::size_t>::max()
                : total + value;
}

[[nodiscard]] std::size_t spriteEstimate(const rendering::Sprite& sprite) noexcept {
    return sizeof(rendering::Sprite) + sprite.pixels.size() * sizeof(Color) +
           sprite.indices.size() * sizeof(std::uint8_t);
}

[[nodiscard]] std::size_t tilemapEstimate(const rendering::Tilemap& tilemap) noexcept {
    std::size_t result = sizeof(rendering::Tilemap) + tilemap.cells.size() * sizeof(std::uint16_t) +
                         tilemap.layers.size() * sizeof(rendering::TilemapLayer) +
                         tilemap.objects.size() * sizeof(rendering::TilemapObject) +
                         tilemap.animations.size() * sizeof(rendering::TileAnimation) +
                         tilemap.chunks.size() * sizeof(rendering::TilemapChunk) +
                         tilemap.solidTiles.size() * sizeof(std::uint16_t);
    for (const auto& tile : tilemap.tiles)
        addEstimate(result, spriteEstimate(tile));
    for (const auto& layer : tilemap.layers)
        addEstimate(result, layer.cells.size() * sizeof(std::uint16_t));
    for (const auto& animation : tilemap.animations) {
        addEstimate(result, animation.frames.size() * sizeof(std::uint16_t));
        addEstimate(result, animation.frameDurationsSeconds.size() * sizeof(float));
    }
    return result;
}

[[nodiscard]] std::size_t animationClipEstimate(const AnimationClipAsset& clip) noexcept {
    std::size_t result =
        sizeof(AnimationClip) + clip.name.size() + clip.events.size() * sizeof(AnimationEvent);
    for (const auto& [path, curve] : clip.tracks) {
        addEstimate(result, path.size() + sizeof(AnimationCurve) +
                                curve.keys().size() * sizeof(AnimationKey));
    }
    for (const auto& event : clip.events)
        addEstimate(result, event.name.size());
    return result;
}

[[nodiscard]] std::size_t controllerEstimate(const AnimatorControllerAsset& controller) noexcept {
    std::size_t result = sizeof(AnimatorControllerAsset) + controller.name.size() +
                         controller.initialState.size() +
                         controller.parameters.size() * sizeof(AnimatorParameterDefinition) +
                         controller.states.size() * sizeof(AnimatorStateDefinition) +
                         controller.transitions.size() * sizeof(AnimatorTransitionDefinition);
    for (const auto& [name, parameter] : controller.parameters) {
        static_cast<void>(parameter);
        addEstimate(result, name.size());
    }
    for (const auto& [name, state] : controller.states) {
        static_cast<void>(state);
        addEstimate(result, name.size());
    }
    for (const auto& transition : controller.transitions) {
        addEstimate(result, transition.fromState.size() + transition.toState.size());
        for (const auto& condition : transition.conditions)
            addEstimate(result, sizeof(AnimationCondition) + condition.parameter.size());
    }
    return result;
}

} // namespace

struct ProjectAssetLibrary::State final {
    std::map<AssetGuid, std::shared_ptr<const rendering::Sprite>> sprites;
    std::map<AssetGuid, std::shared_ptr<const Material>> materials;
    std::map<AssetGuid, std::shared_ptr<const rendering::Tilemap>> tilemaps;
    std::map<AssetGuid, assets::Tileset> tilesets;
    std::map<AssetGuid, assets::Tilemap> pendingTilemaps;
    std::map<AssetGuid, std::string> pendingTilemapPaths;
    std::map<AssetGuid, std::shared_ptr<const rendering::LowPolyMesh>> meshes;
    std::map<AssetGuid, std::shared_ptr<const rendering::RacerTrackAsset>> racerTracks;
    std::map<AssetGuid, std::shared_ptr<const rendering::RaycastMap>> raycastMaps;
    std::map<AssetGuid, std::shared_ptr<const AnimationClip>> animationClips;
    std::map<AssetGuid, AnimatorControllerAsset> animatorControllers;
    std::map<AssetGuid, std::shared_ptr<const ProjectAudioClip>> audioClips;
    ProjectAssetLibraryStats stats;
};

AudioClipView ProjectAudioClip::view() const noexcept {
    if (usesStreamingReader()) {
        return {nullptr,    frameCount(), 1U,
                sampleRate, this,         &ProjectAudioClip::readStreamingFrames};
    }
    return {samples.data(), samples.size(), 1U, sampleRate};
}

std::size_t ProjectAudioClip::frameCount() const noexcept {
    return usesStreamingReader() ? static_cast<std::size_t>(encodedInfo.sampleCount)
                                 : samples.size();
}

bool ProjectAudioClip::valid() const noexcept {
    if (sampleRate < 4000U || sampleRate > 192000U || loopStart > loopEnd ||
        loopEnd > frameCount()) {
        return false;
    }
    if (usesStreamingReader()) {
        return encodedInfo.sampleRate == sampleRate && encodedInfo.streaming &&
               encodedInfo.loopStart == loopStart && encodedInfo.loopEnd == loopEnd;
    }
    return !samples.empty();
}

bool ProjectAudioClip::usesStreamingReader() const noexcept {
    return streaming && !encodedBytes.empty() && encodedInfo.sampleCount != 0U;
}

std::size_t ProjectAudioClip::readStreamingFrames(const void* context, const std::size_t firstFrame,
                                                  float* output,
                                                  const std::size_t requestedFrames) noexcept {
    const auto* clip = static_cast<const ProjectAudioClip*>(context);
    if (clip == nullptr || output == nullptr || !clip->usesStreamingReader() ||
        firstFrame >= clip->encodedInfo.sampleCount) {
        return 0U;
    }
    constexpr std::size_t DecodeBlockFrames = 128U;
    std::array<std::int16_t, DecodeBlockFrames> decoded{};
    const auto available = static_cast<std::size_t>(clip->encodedInfo.sampleCount) - firstFrame;
    const auto target = std::min(requestedFrames, available);
    auto produced = std::size_t{0U};
    while (produced < target) {
        const auto blockFrames = std::min(DecodeBlockFrames, target - produced);
        const auto read =
            assets::decodeAudioClipFrames(clip->encodedBytes, clip->encodedInfo,
                                          firstFrame + produced, decoded.data(), blockFrames);
        for (std::size_t index = 0U; index < read; ++index) {
            output[produced + index] = static_cast<float>(decoded[index]) / 32768.0F;
        }
        produced += read;
        if (read != blockFrames) {
            break;
        }
    }
    return produced;
}

ProjectAssetLibrary::ProjectAssetLibrary() : state_(std::make_shared<State>()) {}

ProjectAssetLibrary::ProjectAssetLibrary(std::shared_ptr<const State> state)
    : state_(std::move(state)) {}

Result<ProjectAssetLibrary> ProjectAssetLibrary::load(const std::string& projectRoot,
                                                      const Manifest& manifest,
                                                      const ProjectAssetLibraryLimits& limits) {
    return loadInternal(projectRoot, manifest, nullptr, limits);
}

Result<ProjectAssetLibrary>
ProjectAssetLibrary::loadSelected(const std::string& projectRoot, const Manifest& manifest,
                                  const std::vector<AssetGuid>& roots,
                                  const ProjectAssetLibraryLimits& limits) {
    if (roots.size() > limits.maximumAssets) {
        return Result<ProjectAssetLibrary>::failure(
            Error(ErrorCode::CapacityExceeded, "selected runtime asset roots exceed limits"));
    }
    std::set<AssetGuid> visiting;
    std::set<AssetGuid> selected;
    std::function<Result<void>(AssetGuid)> visit = [&](const AssetGuid asset) -> Result<void> {
        if (selected.contains(asset))
            return Result<void>::success();
        if (!visiting.insert(asset).second) {
            return Result<void>::failure(
                Error(ErrorCode::CycleDetected, "runtime asset dependency cycle detected")
                    .addContext("asset", asset.toString()));
        }
        auto dependencies = directDependencies(projectRoot, manifest, asset, limits);
        if (!dependencies)
            return Result<void>::failure(dependencies.error());
        for (const auto dependency : dependencies.value()) {
            auto visited = visit(dependency);
            if (!visited)
                return visited;
        }
        visiting.erase(asset);
        selected.insert(asset);
        if (selected.size() > limits.maximumAssets) {
            return Result<void>::failure(Error(ErrorCode::CapacityExceeded,
                                               "runtime asset dependency closure exceeds limits"));
        }
        return Result<void>::success();
    };
    for (const auto root : roots) {
        auto visited = visit(root);
        if (!visited)
            return Result<ProjectAssetLibrary>::failure(visited.error());
    }
    const std::vector<AssetGuid> selectedAssets(selected.begin(), selected.end());
    return loadInternal(projectRoot, manifest, &selectedAssets, limits);
}

bool ProjectAssetLibrary::supportsRuntimeType(const std::string_view type) noexcept {
    return runtimeType(type);
}

Result<std::vector<AssetGuid>>
ProjectAssetLibrary::directDependencies(const std::string& projectRoot, const Manifest& manifest,
                                        const AssetGuid asset,
                                        const ProjectAssetLibraryLimits& limits) {
    if (projectRoot.empty() || asset.isNil() || manifest.assets.size() > limits.maximumAssets ||
        limits.maximumAssetBytes == 0U) {
        return Result<std::vector<AssetGuid>>::failure(
            Error(ErrorCode::InvalidArgument, "runtime dependency request is invalid"));
    }
    const auto* entry = findManifestAsset(manifest, asset);
    if (entry == nullptr) {
        return Result<std::vector<AssetGuid>>::failure(
            Error(ErrorCode::NotFound, "runtime asset GUID is absent from the manifest")
                .addContext("asset", asset.toString()));
    }
    if (!runtimeType(entry->type)) {
        return Result<std::vector<AssetGuid>>::failure(
            Error(ErrorCode::InvalidArgument, "asset type has no project runtime loader")
                .addContext("asset", asset.toString())
                .addContext("assetType", entry->type));
    }
    if (!assets::isSafeRelativePath(entry->path)) {
        return Result<std::vector<AssetGuid>>::failure(
            Error(ErrorCode::InvalidArgument, "runtime asset dependency path is unsafe")
                .addContext("assetPath", entry->path));
    }
    auto bytes = assets::readBinaryFile(joinPath(projectRoot, entry->path));
    if (!bytes)
        return Result<std::vector<AssetGuid>>::failure(assetError(bytes.error(), *entry));
    if (bytes.value().size() > limits.maximumAssetBytes) {
        return Result<std::vector<AssetGuid>>::failure(
            Error(ErrorCode::CapacityExceeded, "runtime dependency asset exceeds limits")
                .addContext("assetPath", entry->path));
    }

    std::vector<AssetGuid> dependencies;
    const auto append = [&](const AssetGuid dependency, const std::string_view expectedType,
                            const std::string_view relationship) -> Result<void> {
        auto valid =
            validateManifestReference(manifest, dependency, expectedType, relationship, *entry);
        if (!valid)
            return valid;
        const auto* referenced = findManifestAsset(manifest, dependency);
        if (referenced != nullptr && runtimeType(referenced->type))
            dependencies.push_back(dependency);
        return Result<void>::success();
    };

    for (const auto dependency : entry->dependencies) {
        auto added = append(dependency, {}, "manifest.asset.import.dependencies");
        if (!added)
            return Result<std::vector<AssetGuid>>::failure(added.error());
    }

    if (entry->type == "tilemap") {
        auto decoded = assets::inspectTilemap(bytes.value());
        if (!decoded)
            return Result<std::vector<AssetGuid>>::failure(assetError(decoded.error(), *entry));
        for (const auto& reference : decoded.value().tilesets) {
            auto added = append(reference.tileset, "tileset", "tilemap.tileset");
            if (!added)
                return Result<std::vector<AssetGuid>>::failure(added.error());
        }
        for (const auto& object : decoded.value().objects) {
            if (object.asset.isNil())
                continue;
            auto added = append(object.asset, {}, "tilemap.object.asset");
            if (!added)
                return Result<std::vector<AssetGuid>>::failure(added.error());
        }
    } else if (entry->type == "tileset") {
        auto decoded = assets::inspectTileset(bytes.value());
        if (!decoded)
            return Result<std::vector<AssetGuid>>::failure(assetError(decoded.error(), *entry));
        auto added = append(decoded.value().sourceImage, "image", "tileset.sourceImage");
        if (!added)
            return Result<std::vector<AssetGuid>>::failure(added.error());
    } else if (entry->type == "material") {
        const std::string text(bytes.value().begin(), bytes.value().end());
        auto decoded = MaterialSerializer::deserialize(text);
        if (!decoded)
            return Result<std::vector<AssetGuid>>::failure(assetError(decoded.error(), *entry));
        if (decoded.value().id != entry->guid)
            return Result<std::vector<AssetGuid>>::failure(
                Error(ErrorCode::InvalidFormat, "material GUID does not match manifest")
                    .addContext("assetPath", entry->path));
        if (decoded.value().material.baseTexture) {
            auto added = append(*decoded.value().material.baseTexture, "image",
                                "material.baseTexture");
            if (!added)
                return Result<std::vector<AssetGuid>>::failure(added.error());
        }
        if (decoded.value().material.paletteAsset) {
            auto added = append(*decoded.value().material.paletteAsset, {},
                                "material.paletteAsset");
            if (!added)
                return Result<std::vector<AssetGuid>>::failure(added.error());
        }
    } else if (entry->type == "animation.controller") {
        const std::string text(bytes.value().begin(), bytes.value().end());
        auto decoded = deserializeAnimatorControllerAsset(text);
        if (!decoded)
            return Result<std::vector<AssetGuid>>::failure(assetError(decoded.error(), *entry));
        for (const auto& [name, state] : decoded.value().states) {
            static_cast<void>(name);
            auto added = append(state.clip, "animation.clip", "animation.controller.state.clip");
            if (!added)
                return Result<std::vector<AssetGuid>>::failure(added.error());
        }
    } else if (entry->type == "racer.track") {
        const std::string text(bytes.value().begin(), bytes.value().end());
        auto decoded = rendering::deserializeRacerTrack(text);
        if (!decoded)
            return Result<std::vector<AssetGuid>>::failure(assetError(decoded.error(), *entry));
        for (const auto& object : decoded.value().roadsideObjects) {
            auto added = append(object.sprite, "image", "racer.track.roadside.sprite");
            if (!added)
                return Result<std::vector<AssetGuid>>::failure(added.error());
        }
        for (const auto& layer : decoded.value().backgroundLayers) {
            auto added = append(layer.sprite, "image", "racer.track.background.sprite");
            if (!added)
                return Result<std::vector<AssetGuid>>::failure(added.error());
        }
        for (const auto& opponent : decoded.value().opponentSpawns) {
            auto added = append(opponent.sprite, "image", "racer.track.opponent.sprite");
            if (!added)
                return Result<std::vector<AssetGuid>>::failure(added.error());
        }
    }
    std::sort(dependencies.begin(), dependencies.end());
    dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
    if (dependencies.size() > limits.maximumAssets) {
        return Result<std::vector<AssetGuid>>::failure(
            Error(ErrorCode::CapacityExceeded, "runtime asset dependency count exceeds limits"));
    }
    return Result<std::vector<AssetGuid>>::success(std::move(dependencies));
}

Result<ProjectAssetLibrary>
ProjectAssetLibrary::loadInternal(const std::string& projectRoot, const Manifest& manifest,
                                  const std::vector<AssetGuid>* selected,
                                  const ProjectAssetLibraryLimits& limits) {
    if (projectRoot.empty() || manifest.assets.size() > limits.maximumAssets ||
        limits.maximumAssetBytes == 0U || limits.maximumAggregateBytes == 0U ||
        limits.maximumTileKinds == 0U) {
        return Result<ProjectAssetLibrary>::failure(
            Error(ErrorCode::InvalidArgument, "project asset library arguments exceed limits"));
    }
    auto state = std::make_shared<State>();
    const std::set<AssetGuid> selectedSet =
        selected == nullptr ? std::set<AssetGuid>{}
                            : std::set<AssetGuid>(selected->begin(), selected->end());
    for (const auto& entry : manifest.assets) {
        if (!runtimeType(entry.type)) {
            ++state->stats.skippedNonVisualAssets;
            continue;
        }
        if (selected != nullptr && !selectedSet.contains(entry.guid))
            continue;
        if (entry.guid.isNil() || !assets::isSafeRelativePath(entry.path)) {
            return Result<ProjectAssetLibrary>::failure(
                Error(ErrorCode::InvalidArgument, "runtime asset entry is unsafe")
                    .addContext("assetPath", entry.path));
        }
        auto bytes = assets::readBinaryFile(joinPath(projectRoot, entry.path));
        if (!bytes) {
            return Result<ProjectAssetLibrary>::failure(assetError(bytes.error(), entry));
        }
        if (bytes.value().size() > limits.maximumAssetBytes ||
            bytes.value().size() > limits.maximumAggregateBytes ||
            state->stats.sourceBytes > limits.maximumAggregateBytes - bytes.value().size()) {
            return Result<ProjectAssetLibrary>::failure(
                Error(ErrorCode::CapacityExceeded, "runtime asset bytes exceed project limits")
                    .addContext("assetPath", entry.path));
        }
        state->stats.sourceBytes += bytes.value().size();
        addEstimate(state->stats.estimatedResidentBytes,
                    std::max<std::size_t>(bytes.value().size(), 1U));

        if (entry.type == "image") {
            auto decoded = assets::decodeIndexedImage(bytes.value());
            if (!decoded) {
                return Result<ProjectAssetLibrary>::failure(assetError(decoded.error(), entry));
            }
            auto sprite = std::make_shared<rendering::Sprite>(spriteFrom(decoded.value()));
            addEstimate(state->stats.estimatedResidentBytes, spriteEstimate(*sprite));
            state->sprites.emplace(entry.guid, std::move(sprite));
        } else if (entry.type == "tilemap") {
            auto decoded = assets::inspectTilemap(bytes.value());
            if (!decoded) {
                return Result<ProjectAssetLibrary>::failure(assetError(decoded.error(), entry));
            }
            if (!decoded.value().guid.isNil() && decoded.value().guid != entry.guid) {
                return Result<ProjectAssetLibrary>::failure(
                    Error(ErrorCode::InvalidFormat, "tilemap GUID does not match manifest")
                        .addContext("assetPath", entry.path));
            }
            for (const auto& reference : decoded.value().tilesets) {
                auto validReference = validateManifestReference(
                    manifest, reference.tileset, "tileset", "tilemap.tileset", entry);
                if (!validReference)
                    return Result<ProjectAssetLibrary>::failure(validReference.error());
            }
            for (const auto& object : decoded.value().objects) {
                if (object.asset.isNil())
                    continue;
                auto validReference = validateManifestReference(manifest, object.asset, {},
                                                                "tilemap.object.asset", entry);
                if (!validReference)
                    return Result<ProjectAssetLibrary>::failure(validReference.error());
            }
            state->pendingTilemaps.emplace(entry.guid, std::move(decoded.value()));
            state->pendingTilemapPaths.emplace(entry.guid, entry.path);
        } else if (entry.type == "tileset") {
            auto decoded = assets::inspectTileset(bytes.value());
            if (!decoded)
                return Result<ProjectAssetLibrary>::failure(assetError(decoded.error(), entry));
            if (decoded.value().guid != entry.guid)
                return Result<ProjectAssetLibrary>::failure(
                    Error(ErrorCode::InvalidFormat, "tileset GUID does not match manifest")
                        .addContext("assetPath", entry.path));
            auto validReference = validateManifestReference(manifest, decoded.value().sourceImage,
                                                            "image", "tileset.sourceImage", entry);
            if (!validReference)
                return Result<ProjectAssetLibrary>::failure(validReference.error());
            state->tilesets.emplace(entry.guid, std::move(decoded.value()));
        } else if (entry.type == "material") {
            const std::string text(bytes.value().begin(), bytes.value().end());
            auto decoded = MaterialSerializer::deserialize(text);
            if (!decoded)
                return Result<ProjectAssetLibrary>::failure(assetError(decoded.error(), entry));
            if (decoded.value().id != entry.guid)
                return Result<ProjectAssetLibrary>::failure(
                    Error(ErrorCode::InvalidFormat, "material GUID does not match manifest")
                        .addContext("assetPath", entry.path));
            if (decoded.value().material.baseTexture) {
                auto validReference = validateManifestReference(
                    manifest, *decoded.value().material.baseTexture, "image",
                    "material.baseTexture", entry);
                if (!validReference)
                    return Result<ProjectAssetLibrary>::failure(validReference.error());
            }
            if (decoded.value().material.paletteAsset) {
                auto validReference = validateManifestReference(
                    manifest, *decoded.value().material.paletteAsset, {},
                    "material.paletteAsset", entry);
                if (!validReference)
                    return Result<ProjectAssetLibrary>::failure(validReference.error());
            }
            const bool supported =
                validateMaterial(decoded.value().material, RendererBackend::Renderer2D).valid() ||
                validateMaterial(decoded.value().material, RendererBackend::Raycast).valid() ||
                validateMaterial(decoded.value().material, RendererBackend::Racer).valid() ||
                validateMaterial(decoded.value().material, RendererBackend::LowPoly).valid();
            if (!supported)
                return Result<ProjectAssetLibrary>::failure(
                    Error(ErrorCode::InvalidFormat, "runtime material is unsupported")
                        .addContext("assetPath", entry.path));
            addEstimate(state->stats.estimatedResidentBytes,
                        sizeof(Material) + decoded.value().name.size() +
                            decoded.value().material.palette.size() * sizeof(Color));
            state->materials.emplace(
                entry.guid,
                std::make_shared<Material>(std::move(decoded.value().material)));
            ++state->stats.loadedMaterials;
        } else if (entry.type == "mesh") {
            auto decoded = assets::inspectLowPolyMesh(bytes.value());
            if (!decoded) {
                return Result<ProjectAssetLibrary>::failure(assetError(decoded.error(), entry));
            }
            state->meshes.emplace(
                entry.guid, std::make_shared<rendering::LowPolyMesh>(meshFrom(decoded.value())));
        } else if (entry.type == "racer.track") {
            const std::string text(bytes.value().begin(), bytes.value().end());
            auto decoded = rendering::deserializeRacerTrack(text);
            if (!decoded) {
                return Result<ProjectAssetLibrary>::failure(assetError(decoded.error(), entry));
            }
            if (decoded.value().guid != entry.guid) {
                return Result<ProjectAssetLibrary>::failure(
                    Error(ErrorCode::InvalidFormat, "racer track GUID does not match manifest")
                        .addContext("assetPath", entry.path));
            }
            state->racerTracks.emplace(entry.guid, std::make_shared<rendering::RacerTrackAsset>(
                                                       std::move(decoded.value())));
        } else if (entry.type == "raycast.map") {
            const std::string text(bytes.value().begin(), bytes.value().end());
            auto decoded = rendering::deserializeRaycastMapAsset(text);
            if (!decoded) {
                return Result<ProjectAssetLibrary>::failure(assetError(decoded.error(), entry));
            }
            if (decoded.value().guid != entry.guid) {
                return Result<ProjectAssetLibrary>::failure(
                    Error(ErrorCode::InvalidFormat, "raycast map GUID does not match manifest")
                        .addContext("assetPath", entry.path));
            }
            state->raycastMaps.emplace(entry.guid, std::make_shared<rendering::RaycastMap>(
                                                       std::move(decoded.value().map)));
        } else if (entry.type == "animation.clip") {
            const std::string text(bytes.value().begin(), bytes.value().end());
            auto decoded = deserializeAnimationClipAsset(text);
            if (!decoded) {
                return Result<ProjectAssetLibrary>::failure(assetError(decoded.error(), entry));
            }
            if (decoded.value().guid != entry.guid) {
                return Result<ProjectAssetLibrary>::failure(
                    Error(ErrorCode::InvalidFormat, "animation clip GUID does not match manifest")
                        .addContext("assetPath", entry.path));
            }
            addEstimate(state->stats.estimatedResidentBytes,
                        animationClipEstimate(decoded.value()));
            auto runtimeClip = buildAnimationClip(decoded.value());
            if (!runtimeClip) {
                return Result<ProjectAssetLibrary>::failure(assetError(runtimeClip.error(), entry));
            }
            state->animationClips.emplace(entry.guid, std::move(runtimeClip.value()));
            ++state->stats.loadedAnimationClips;
        } else if (entry.type == "animation.controller") {
            const std::string text(bytes.value().begin(), bytes.value().end());
            auto decoded = deserializeAnimatorControllerAsset(text);
            if (!decoded) {
                return Result<ProjectAssetLibrary>::failure(assetError(decoded.error(), entry));
            }
            if (decoded.value().guid != entry.guid) {
                return Result<ProjectAssetLibrary>::failure(
                    Error(ErrorCode::InvalidFormat,
                          "animator controller GUID does not match manifest")
                        .addContext("assetPath", entry.path));
            }
            addEstimate(state->stats.estimatedResidentBytes, controllerEstimate(decoded.value()));
            state->animatorControllers.emplace(entry.guid, std::move(decoded.value()));
            ++state->stats.loadedAnimatorControllers;
        } else {
            auto inspected = assets::inspectAudioClip(bytes.value());
            if (!inspected) {
                return Result<ProjectAssetLibrary>::failure(assetError(inspected.error(), entry));
            }
            auto clip = std::make_shared<ProjectAudioClip>();
            clip->sampleRate = inspected.value().sampleRate;
            clip->streaming = inspected.value().streaming;
            clip->loopStart = inspected.value().loopStart;
            clip->loopEnd = inspected.value().loopEnd;
            if (clip->streaming) {
                clip->encodedInfo = inspected.value();
                clip->encodedBytes = std::move(bytes.value());
            } else {
                auto decoded = assets::decodeAudioClip(bytes.value());
                if (!decoded) {
                    return Result<ProjectAssetLibrary>::failure(assetError(decoded.error(), entry));
                }
                clip->samples.reserve(decoded.value().samples.size());
                for (const auto sample : decoded.value().samples) {
                    clip->samples.push_back(static_cast<float>(sample) / 32768.0F);
                }
            }
            if (!clip->valid()) {
                return Result<ProjectAssetLibrary>::failure(
                    Error(ErrorCode::InvalidFormat, "runtime audio clip is invalid")
                        .addContext("assetPath", entry.path));
            }
            addEstimate(state->stats.estimatedResidentBytes,
                        sizeof(ProjectAudioClip) + clip->samples.size() * sizeof(float) +
                            clip->encodedBytes.size());
            state->audioClips.emplace(entry.guid, std::move(clip));
            ++state->stats.loadedAudioClips;
        }
        ++state->stats.loadedAssetEntries;
        if (visualType(entry.type))
            ++state->stats.loadedAssets;
    }
    for (auto& [guid, tilemap] : state->pendingTilemaps) {
        auto converted =
            tilemapFrom(tilemap, limits.maximumTileKinds, state->tilesets, state->sprites);
        if (!converted)
            return Result<ProjectAssetLibrary>::failure(
                converted.error().withContext("assetPath", state->pendingTilemapPaths.at(guid)));
        if (!converted.value().valid())
            return Result<ProjectAssetLibrary>::failure(
                Error(ErrorCode::InvalidFormat, "tilemap cannot be presented")
                    .addContext("assetPath", state->pendingTilemapPaths.at(guid)));
        state->tilemaps.emplace(guid,
                                std::make_shared<rendering::Tilemap>(std::move(converted.value())));
        addEstimate(state->stats.estimatedResidentBytes,
                    tilemapEstimate(*state->tilemaps.at(guid)));
    }
    state->pendingTilemaps.clear();
    state->pendingTilemapPaths.clear();
    return Result<ProjectAssetLibrary>::success(ProjectAssetLibrary(std::move(state)));
}

rendering::ScenePresentationResources ProjectAssetLibrary::resources() const {
    rendering::ScenePresentationResources result;
    const auto state = state_;
    result.sprite = [state](const AssetGuid guid) {
        const auto found = state->sprites.find(guid);
        return found == state->sprites.end() ? std::shared_ptr<const rendering::Sprite>{}
                                             : found->second;
    };
    result.material = [state](const AssetGuid guid) {
        const auto found = state->materials.find(guid);
        return found == state->materials.end() ? std::shared_ptr<const Material>{}
                                               : found->second;
    };
    result.tilemap = [state](const AssetGuid guid) {
        const auto found = state->tilemaps.find(guid);
        return found == state->tilemaps.end() ? std::shared_ptr<const rendering::Tilemap>{}
                                              : found->second;
    };
    result.mesh = [state](const AssetGuid guid) {
        const auto found = state->meshes.find(guid);
        return found == state->meshes.end() ? std::shared_ptr<const rendering::LowPolyMesh>{}
                                            : found->second;
    };
    result.racerTrack = [state](const AssetGuid guid) {
        const auto found = state->racerTracks.find(guid);
        return found == state->racerTracks.end()
                   ? std::shared_ptr<const rendering::RacerTrackAsset>{}
                   : found->second;
    };
    result.raycastMap = [state](const AssetGuid guid) {
        const auto found = state->raycastMaps.find(guid);
        return found == state->raycastMaps.end() ? std::shared_ptr<const rendering::RaycastMap>{}
                                                 : found->second;
    };
    return result;
}

std::shared_ptr<const AnimationClip>
ProjectAssetLibrary::animationClip(const AssetGuid guid) const noexcept {
    const auto found = state_->animationClips.find(guid);
    return found == state_->animationClips.end() ? std::shared_ptr<const AnimationClip>{}
                                                 : found->second;
}

Result<std::unique_ptr<AnimatorController>>
ProjectAssetLibrary::createAnimator(const AssetGuid controller) const {
    const auto found = state_->animatorControllers.find(controller);
    if (found == state_->animatorControllers.end()) {
        return Result<std::unique_ptr<AnimatorController>>::failure(
            Error(ErrorCode::NotFound, "animator controller is not loaded")
                .addContext("controller", controller.toString()));
    }
    const auto state = state_;
    AnimationClipResolver clips = [state](const AssetGuid clip) {
        const auto resolved = state->animationClips.find(clip);
        if (resolved == state->animationClips.end()) {
            return Result<std::shared_ptr<const AnimationClip>>::failure(
                Error(ErrorCode::NotFound, "animator clip is not loaded")
                    .addContext("clip", clip.toString()));
        }
        return Result<std::shared_ptr<const AnimationClip>>::success(resolved->second);
    };
    auto built = buildAnimatorController(found->second, clips);
    if (!built) {
        return Result<std::unique_ptr<AnimatorController>>::failure(
            built.error().withContext("controller", controller.toString()));
    }
    return built;
}

std::shared_ptr<const ProjectAudioClip>
ProjectAssetLibrary::audioClip(const AssetGuid guid) const noexcept {
    const auto found = state_->audioClips.find(guid);
    return found == state_->audioClips.end() ? std::shared_ptr<const ProjectAudioClip>{}
                                             : found->second;
}

const ProjectAssetLibraryStats& ProjectAssetLibrary::stats() const noexcept {
    return state_->stats;
}

} // namespace fabgl::project
