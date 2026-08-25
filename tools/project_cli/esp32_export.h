#pragma once

#include <fabgl/core/result.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fabgl::project {

inline constexpr std::uint32_t Esp32ManifestPayloadType = 0x4D414E46U;
inline constexpr std::uint32_t Esp32ScenePayloadType = 0x53434E45U;
inline constexpr std::uint32_t Esp32AssetPayloadType = 0x41535354U;
inline constexpr std::size_t Esp32MaximumEmbeddedAssets = 64U;

struct Esp32ExportSummary final {
    std::string projectName;
    std::string previewDemo;
    std::string sketchFileName;
    std::size_t entityCount = 0;
    std::size_t assetCount = 0;
    std::size_t portableScriptFileCount = 0;
    bool scriptRuntime = false;
    std::size_t payloadSize = 0;
    std::uint64_t payloadChecksum = 0;
    std::uint64_t packBuildChecksum = 0;
    std::size_t externalAssetCount = 0;
    std::size_t externalPayloadSize = 0;
    std::uint64_t externalPayloadChecksum = 0;
    std::uint64_t externalPackBuildChecksum = 0;
};

struct Esp32PortableScriptSource final {
    // Project-relative path below Scripts (always begins with "ESP32/").
    std::string relativePath;
    std::vector<std::uint8_t> bytes;
};

// Reads the complete Scripts tree through the same bounded, non-linking filesystem boundary used
// by exportEsp32Project. Desktop C/C++ is never returned. If desktop gameplay exists, a portable
// Scripts/ESP32 module entry is mandatory so callers cannot silently drop gameplay while preparing
// an ESP32 project.
[[nodiscard]] Result<std::vector<Esp32PortableScriptSource>>
collectEsp32PortableScriptSources(const std::string& projectRootDirectory);

[[nodiscard]] Result<Esp32ExportSummary>
exportEsp32Project(const std::string& projectManifestPath,
                   const std::string& firmwareTemplateDirectory,
                   const std::string& outputSketchDirectory);

} // namespace fabgl::project
