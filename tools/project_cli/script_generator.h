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
};

[[nodiscard]] Result<GeneratedScript> generateGameplayScript(std::string_view className);
[[nodiscard]] Result<void> writeGameplayScript(const std::string& projectDirectory,
                                               std::string_view className);

} // namespace fabgl::project
