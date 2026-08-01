#pragma once

#include <fabgl/core/guid.h>
#include <fabgl/core/result.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace fabgl::assets {

struct AssetRecord final {
    AssetGuid guid;
    std::string relativePath;
    std::string importer;
    std::uint64_t sourceFingerprint = 0;
    std::vector<AssetGuid> dependencies;
};

class AssetDatabase final {
  public:
    [[nodiscard]] Result<void> add(AssetRecord record);
    [[nodiscard]] Result<void> move(const AssetGuid& guid, std::string newRelativePath);
    [[nodiscard]] const AssetRecord* find(const AssetGuid& guid) const noexcept;
    [[nodiscard]] const AssetRecord* findByPath(const std::string& relativePath) const noexcept;
    [[nodiscard]] Result<std::vector<AssetGuid>> buildOrder() const;
    [[nodiscard]] std::size_t size() const noexcept {
        return records_.size();
    }

  private:
    std::unordered_map<AssetGuid, AssetRecord, StrongGuidHash<AssetGuidTag>> records_;
    std::unordered_map<std::string, AssetGuid> paths_;
};

} // namespace fabgl::assets
