#pragma once

#include <fabgl/core/guid.h>
#include <fabgl/core/result.h>
#include <fabgl/assets/asset_importer.h>
#include <fabgl/assets/audio_importer.h>
#include <fabgl/assets/image_pipeline.h>
#include <fabgl/packages/package_manifest.h>

#include "performance_budget.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fabgl::project {

struct InputBindingDefinition final {
    std::string control;
    float scale = 1.0F;
    float threshold = 0.5F;
};

struct InputValueDefinition final {
    std::string name;
    std::vector<InputBindingDefinition> bindings;
};

struct InputContextDefinition final {
    std::string name;
    int priority = 0;
    bool enabled = true;
    std::vector<InputValueDefinition> actions;
    std::vector<InputValueDefinition> axes;
};

struct ProjectPackageDependency final {
    std::string id;
    VersionRequirement version;
};

struct ProjectAssetEntry final {
    ProjectAssetEntry() = default;
    ProjectAssetEntry(AssetGuid guidValue, std::string pathValue, std::string typeValue,
                      std::vector<AssetGuid> dependencyValues = {})
        : guid(guidValue), path(std::move(pathValue)), type(std::move(typeValue)),
          dependencies(std::move(dependencyValues)) {}

    AssetGuid guid;
    std::string path;
    // Stable dispatch identifier such as "racer.track", "image", or "audio".
    std::string type;
    // Canonical importer contract shared by Studio, project prepare, and the
    // ESP32 packer. Older v2 manifests may omit it and keep these defaults.
    std::string importSettings = "{}";
    assets::AssetTarget esp32Target = assets::AssetTarget::Esp32Flash;
    std::vector<AssetGuid> dependencies;
    bool hasImportMetadata = false;
};

struct ProjectAudioImportOptions final {
    assets::AudioImportSettings settings;
    assets::AudioEncoding encoding = assets::AudioEncoding::Delta8;
};

struct TargetProfileSelection final {
    std::string pc = "pc.default";
    std::string esp32 = "olimex-esp32-sbc-fabgl-revb";
};

struct Manifest final {
    static constexpr int CurrentVersion = 2;

    int sourceVersion = CurrentVersion;
    std::string projectGuid;
    std::string name;
    std::string projectRoot = ".";
    std::string startupScene = "Scenes/Main.fglscene";
    std::string previewDemo;
    std::string buildProgram = "cmake";
    std::vector<std::string> buildArguments{"--build", "out/build/dev"};
    std::vector<ProjectAssetEntry> assets;
    std::vector<InputContextDefinition> inputContexts;
    std::vector<ProjectPackageDependency> packageDependencies;
    TargetProfileSelection targetProfiles;
    PerformanceBudgetSettings performance;
};

[[nodiscard]] Result<Manifest> parseManifest(std::string_view json);
[[nodiscard]] Result<std::string> serializeManifest(const Manifest& manifest);
// Migrates the private v1 Asset Browser index used by older Studio builds into
// the canonical manifest model. Existing canonical import blocks win; records
// absent from the manifest are added so command-line builds do not lose assets.
[[nodiscard]] Result<bool> mergeLegacyAssetIndex(std::string_view json, Manifest& manifest);
[[nodiscard]] Result<assets::ImageImportSettings>
decodeProjectImageImportSettings(std::string_view json);
[[nodiscard]] Result<ProjectAudioImportOptions>
decodeProjectAudioImportSettings(std::string_view json);

} // namespace fabgl::project
