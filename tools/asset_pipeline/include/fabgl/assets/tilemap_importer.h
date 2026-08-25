#pragma once

#include <fabgl/assets/asset_importer.h>
#include <fabgl/core/guid.h>
#include <fabgl/core/result.h>
#include <fabgl/math/types.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fabgl::assets {

struct TilemapLimits final {
    std::uint32_t maximumWidth = 4096;
    std::uint32_t maximumHeight = 4096;
    std::uint32_t maximumCells = 1'048'576;
    std::uint32_t maximumLayers = 32;
    std::uint32_t maximumObjects = 4096;
    std::uint32_t maximumChunks = 4096;
    std::uint32_t maximumAnimations = 256;
    std::uint32_t maximumAnimationFrames = 4096;
    std::uint32_t maximumTilesets = 64;
    std::uint32_t maximumStringBytes = 1024;
    std::size_t maximumEncodedBytes = 64U * 1024U * 1024U;
};

enum class TilemapLayerKind : std::uint8_t { Tiles = 0U, Collision = 1U, Objects = 2U };

struct TilemapLayer final {
    std::string name;
    TilemapLayerKind kind = TilemapLayerKind::Tiles;
    std::vector<std::uint32_t> cells;
    float parallaxX = 1.0F;
    float parallaxY = 1.0F;
    std::uint8_t opacity = 255U;
    bool visible = true;
};

struct TilemapObject final {
    std::uint32_t id = 0U;
    std::uint16_t layer = 0U;
    std::string type;
    Rect bounds{};
    AssetGuid asset{};
};

struct TilemapChunk final {
    std::uint16_t layer = 0U;
    std::uint32_t x = 0U;
    std::uint32_t y = 0U;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::vector<std::uint32_t> cells;
};

struct TileAnimationFrame final {
    std::uint32_t tile = 0U;
    std::uint32_t durationMilliseconds = 100U;
};

struct TileAnimation final {
    std::uint32_t outputTile = 0U;
    std::vector<TileAnimationFrame> frames;
};

struct TilemapTilesetReference final {
    AssetGuid tileset{};
    std::uint32_t firstTile = 0U;
    std::uint32_t tileCount = 0U;
};

struct Tilemap final {
    // The first three members deliberately preserve the original v1 aggregate API.
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint32_t> tiles;
    AssetGuid guid{};
    std::uint16_t tileWidth = 8U;
    std::uint16_t tileHeight = 8U;
    std::vector<TilemapLayer> layers;
    std::vector<TilemapObject> objects;
    std::vector<TilemapChunk> chunks;
    std::vector<TileAnimation> animations;
    std::vector<TilemapTilesetReference> tilesets;

    [[nodiscard]] bool valid(const TilemapLimits& limits = {}) const noexcept;
};

struct TilesetLimits final {
    std::uint32_t maximumTiles = 65'535U;
    std::uint32_t maximumCollisionTiles = 65'535U;
    std::uint32_t maximumStringBytes = 1024U;
    std::size_t maximumEncodedBytes = 4U * 1024U * 1024U;
};

struct Tileset final {
    AssetGuid guid{};
    std::string name;
    AssetGuid sourceImage{};
    std::uint16_t tileWidth = 0U;
    std::uint16_t tileHeight = 0U;
    std::uint16_t margin = 0U;
    std::uint16_t spacing = 0U;
    std::uint32_t tileCount = 0U;
    std::uint32_t columns = 0U;
    std::vector<std::uint32_t> collisionTiles;

    [[nodiscard]] bool valid(const TilesetLimits& limits = {}) const noexcept;
};

[[nodiscard]] Result<Tilemap> importCsvTilemap(std::string_view source,
                                               const TilemapLimits& limits = {});
[[nodiscard]] Result<Tilemap> importJsonTilemap(std::string_view source,
                                                const TilemapLimits& limits = {});
[[nodiscard]] Result<std::vector<std::uint8_t>> encodeTilemap(const Tilemap& tilemap);
[[nodiscard]] Result<Tilemap> inspectTilemap(const std::vector<std::uint8_t>& bytes,
                                             const TilemapLimits& limits = {});
[[nodiscard]] Result<std::vector<std::uint8_t>> encodeTileset(const Tileset& tileset);
[[nodiscard]] Result<Tileset> inspectTileset(const std::vector<std::uint8_t>& bytes,
                                             const TilesetLimits& limits = {});

class CsvTilemapImporter final : public IAssetImporter {
  public:
    [[nodiscard]] std::string_view id() const noexcept override;
    [[nodiscard]] std::uint32_t version() const noexcept override;
    [[nodiscard]] AssetKind kind() const noexcept override;
    [[nodiscard]] std::vector<std::string> extensions() const override;
    [[nodiscard]] Result<ImportedAsset> import(const AssetImportRequest& request) const override;
};

class JsonTilemapImporter final : public IAssetImporter {
  public:
    [[nodiscard]] std::string_view id() const noexcept override;
    [[nodiscard]] std::uint32_t version() const noexcept override;
    [[nodiscard]] AssetKind kind() const noexcept override;
    [[nodiscard]] std::vector<std::string> extensions() const override;
    [[nodiscard]] Result<ImportedAsset> import(const AssetImportRequest& request) const override;
};

} // namespace fabgl::assets
