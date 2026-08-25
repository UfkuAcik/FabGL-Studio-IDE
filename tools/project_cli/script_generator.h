#pragma once

#include <fabgl/core/result.h>

#include <string>
#include <string_view>

namespace fabgl::project {

struct GeneratedScript final {
    std::string headerFileName;
    std::string sourceFileName;
    std::string header;
    std::string source;
    std::string portableHeaderFileName;
    std::string portableSourceFileName;
    std::string portableHeader;
    std::string portableSource;
};

inline constexpr std::string_view GameplayCMakeMarker =
    "# FabGL Studio managed gameplay CMake glue. Schema: 1";
inline constexpr std::string_view ProjectCMakeMarker =
    "# FabGL Studio managed project CMake. Schema: 1";
inline constexpr std::string_view Esp32ScriptModuleMarker =
    "// FabGL Studio managed ESP32 gameplay module. Schema: 1";

[[nodiscard]] Result<GeneratedScript> generateGameplayScript(std::string_view className);
[[nodiscard]] std::string generateGameplayCMakeGlue();
[[nodiscard]] std::string generateProjectCMake();
[[nodiscard]] Result<void> ensureGameplayBuildFiles(const std::string& projectDirectory);
[[nodiscard]] Result<void> writeGameplayScript(const std::string& projectDirectory,
                                               std::string_view className);

} // namespace fabgl::project
