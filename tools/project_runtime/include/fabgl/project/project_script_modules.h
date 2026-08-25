#pragma once

#include <fabgl/core/result.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fabgl {
class Scene;
}

namespace fabgl::project {

struct ProjectScriptModuleLimits final {
    std::size_t maximumModules = 32U;
    std::size_t maximumDescriptorsPerModule = 1024U;
    std::size_t maximumTypeNameBytes = 255U;
};

struct ProjectScriptModuleStats final {
    std::uint32_t loadedModules = 0U;
    std::uint32_t registeredTypes = 0U;
    std::uint32_t attachedComponents = 0U;
};

// Loads explicitly authorized native gameplay modules and keeps their code in
// memory for the entire lifetime of every component created from them.
class ProjectScriptModules final {
  public:
    ProjectScriptModules();
    ~ProjectScriptModules();
    ProjectScriptModules(ProjectScriptModules&&) noexcept;
    ProjectScriptModules& operator=(ProjectScriptModules&&) noexcept;
    ProjectScriptModules(const ProjectScriptModules&) = delete;
    ProjectScriptModules& operator=(const ProjectScriptModules&) = delete;

    [[nodiscard]] static Result<ProjectScriptModules>
    load(const std::vector<std::string>& modulePaths,
         const ProjectScriptModuleLimits& limits = {});

    [[nodiscard]] Result<std::uint32_t> attach(Scene& scene);
    [[nodiscard]] const ProjectScriptModuleStats& stats() const noexcept;

  private:
    struct Implementation;
    explicit ProjectScriptModules(std::unique_ptr<Implementation> implementation);

    std::unique_ptr<Implementation> implementation_;
};

} // namespace fabgl::project
