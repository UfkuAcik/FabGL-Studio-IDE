#pragma once

#include <fabgl/core/guid.h>
#include <fabgl/core/result.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fabgl::assets {

struct AssetRecord final {
    AssetRecord() = default;
    AssetRecord(AssetGuid assetGuid, std::string path, std::string importerId,
                std::uint64_t fingerprint, std::vector<AssetGuid> assetDependencies)
        : guid(assetGuid), relativePath(std::move(path)), importer(std::move(importerId)),
          sourceFingerprint(fingerprint), dependencies(std::move(assetDependencies)) {}

    AssetGuid guid;
    std::string relativePath;
    std::string importer;
    std::uint64_t sourceFingerprint = 0;
    std::vector<AssetGuid> dependencies;
    std::uint64_t settingsFingerprint = 0;
    std::uint64_t importedSourceFingerprint = 0;
    std::uint64_t importedSettingsFingerprint = 0;
    std::uint64_t lastImportCacheKey = 0;
    std::uint32_t importerVersion = 1;
    std::uint32_t importedVersion = 0;
    bool missing = false;
};

struct AssetSourceState final {
    std::string relativePath;
    std::uint64_t fingerprint = 0;
};

struct AssetSyncResult final {
    std::vector<AssetGuid> dirty;
    std::vector<AssetGuid> unchanged;
    std::vector<AssetGuid> missing;
    std::vector<std::string> untrackedPaths;
};

class AssetDatabase final {
  public:
    [[nodiscard]] Result<void> add(AssetRecord record);
    [[nodiscard]] Result<void> move(const AssetGuid& guid, std::string newRelativePath);
    [[nodiscard]] Result<void> setImportConfiguration(const AssetGuid& guid,
                                                      std::uint32_t importerVersion,
                                                      std::uint64_t settingsFingerprint);
    [[nodiscard]] Result<void> markImported(const AssetGuid& guid,
                                            std::uint64_t expectedSourceFingerprint,
                                            std::uint64_t cacheKey);
    [[nodiscard]] Result<AssetSyncResult>
    synchronizeSources(const std::vector<AssetSourceState>& sources);
    [[nodiscard]] const AssetRecord* find(const AssetGuid& guid) const noexcept;
    [[nodiscard]] const AssetRecord* findByPath(const std::string& relativePath) const noexcept;
    [[nodiscard]] Result<std::vector<AssetGuid>> buildOrder() const;
    [[nodiscard]] bool needsImport(const AssetGuid& guid) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept {
        return records_.size();
    }

  private:
    std::unordered_map<AssetGuid, AssetRecord, StrongGuidHash<AssetGuidTag>> records_;
    std::unordered_map<std::string, AssetGuid> paths_;
};

} // namespace fabgl::assets
