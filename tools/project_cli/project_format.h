#pragma once

#include <fabgl/core/result.h>

#include <string>
#include <string_view>
#include <vector>

namespace fabgl::project {

struct Manifest final {
    static constexpr int CurrentVersion = 1;

    int sourceVersion = CurrentVersion;
    std::string projectGuid;
    std::string name;
    std::string projectRoot = ".";
    std::string startupScene = "Scenes/Main.fglscene";
    std::string buildProgram = "cmake";
    std::vector<std::string> buildArguments{"--build", "out/build/dev"};
};

[[nodiscard]] Result<Manifest> parseManifest(std::string_view json);
[[nodiscard]] Result<std::string> serializeManifest(const Manifest& manifest);

} // namespace fabgl::project
