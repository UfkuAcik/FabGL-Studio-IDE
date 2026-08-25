#pragma once

#include <fabgl/core/result.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace fabgl::project {

enum class ProjectPrepareTarget : std::uint8_t { Pc = 0, Esp32 };

struct ProjectPrepareResult final {
    std::string packPath;
    std::string preparedProjectPath;
    std::size_t assetCount = 0U;
    std::size_t importedAssetCount = 0U;
    std::size_t validatedAssetCount = 0U;
    std::size_t visualGraphCount = 0U;
    std::size_t visualProgramCount = 0U;
    std::size_t portableScriptFileCount = 0U;
    std::size_t sourceBytes = 0U;
    std::size_t packedBytes = 0U;
    std::uint64_t packChecksum = 0U;
};

[[nodiscard]] Result<ProjectPrepareResult>
prepareProjectInputs(const std::string& projectManifestPath, const std::string& outputDirectory,
                     ProjectPrepareTarget target);

} // namespace fabgl::project
