#pragma once

#include <fabgl/core/guid.h>
#include <fabgl/core/result.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fabgl::assets {

enum class AssetKind : std::uint16_t {
    Binary = 0,
    Image,
    Audio,
    Font,
    Tilemap,
    Tileset,
    SpriteAtlas,
    Animation,
    Material,
    Scene,
    Prefab,
    Script,
    VisualScript,
    RaycastMap,
    RacerTrack,
    LowPolyMesh,
    Json,
};

enum class AssetTarget : std::uint8_t { Pc = 0, Esp32Flash, Esp32Psram, Esp32Sd };

struct AssetImportRequest final {
    AssetGuid guid;
    std::string sourcePath;
    std::string relativePath;
    std::vector<std::uint8_t> sourceBytes;
    std::string normalizedSettings;
    std::vector<std::uint64_t> dependencyCacheKeys;
    AssetTarget target = AssetTarget::Pc;
    std::uint32_t pipelineVersion = 1;
};

struct ImportedAsset final {
    AssetGuid guid;
    AssetKind kind = AssetKind::Binary;
    std::vector<std::uint8_t> payload;
    std::vector<std::uint8_t> thumbnail;
    std::vector<AssetGuid> dependencies;
    std::uint64_t cacheKey = 0;
    std::size_t flashBytes = 0;
    std::size_t internalRamBytes = 0;
    std::size_t psramBytes = 0;
    std::size_t sdBytes = 0;
    std::uint32_t estimatedDecodeMicros = 0;
    std::uint64_t estimatedRenderPixelsPerFrame = 0U;
};

class IAssetImporter {
  public:
    virtual ~IAssetImporter() = default;

    [[nodiscard]] virtual std::string_view id() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t version() const noexcept = 0;
    [[nodiscard]] virtual AssetKind kind() const noexcept = 0;
    [[nodiscard]] virtual std::vector<std::string> extensions() const = 0;
    [[nodiscard]] virtual Result<ImportedAsset> import(const AssetImportRequest& request) const = 0;
};

// Registry ownership is explicit so importer plugins can be unloaded only after the registry is
// destroyed. Extensions are normalized to lower-case without a leading dot.
class AssetImporterRegistry final {
  public:
    [[nodiscard]] Result<void> add(std::unique_ptr<IAssetImporter> importer);
    [[nodiscard]] const IAssetImporter* findById(std::string_view id) const noexcept;
    [[nodiscard]] const IAssetImporter* findForPath(std::string_view path) const noexcept;
    [[nodiscard]] Result<ImportedAsset> import(const AssetImportRequest& request) const;
    [[nodiscard]] std::size_t size() const noexcept {
        return importers_.size();
    }

  private:
    std::vector<std::unique_ptr<IAssetImporter>> importers_;
    std::unordered_map<std::string, const IAssetImporter*> byId_;
    std::unordered_map<std::string, const IAssetImporter*> byExtension_;
};

[[nodiscard]] std::uint64_t assetImportCacheKey(const AssetImportRequest& request,
                                                std::string_view importerId,
                                                std::uint32_t importerVersion) noexcept;

} // namespace fabgl::assets
