#pragma once

#include <fabgl/assets/asset_importer.h>
#include <fabgl/core/result.h>

#include <QString>

#include <cstdint>
#include <string>
#include <utility>

namespace fgl::studio {

struct AssetBrowserImporterDescriptor final {
    AssetBrowserImporterDescriptor() = default;
    AssetBrowserImporterDescriptor(std::string importerId, std::uint32_t importerVersion,
                                   fabgl::assets::AssetKind assetKind, bool isSupported,
                                   std::string serviceId = {},
                                   std::string canonicalSettings = {})
        : id(std::move(importerId)), version(importerVersion), kind(assetKind),
          supported(isSupported), extensionServiceId(std::move(serviceId)),
          normalizedSettings(std::move(canonicalSettings)) {}

    std::string id;
    std::uint32_t version = 0U;
    fabgl::assets::AssetKind kind = fabgl::assets::AssetKind::Binary;
    bool supported = false;
    // Non-empty only for a trusted product extension selected through its bounded probe schema.
    std::string extensionServiceId;
    std::string normalizedSettings;
};

[[nodiscard]] AssetBrowserImporterDescriptor
assetBrowserImporterFor(const QString& relativePath, const QString& type);
[[nodiscard]] fabgl::Result<fabgl::assets::ImportedAsset>
importAssetForBrowser(const AssetBrowserImporterDescriptor& descriptor,
                      const fabgl::assets::AssetImportRequest& request, const QString& type);

} // namespace fgl::studio
