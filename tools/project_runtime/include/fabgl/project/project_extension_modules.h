#pragma once

#include <fabgl/core/result.h>
#include <fabgl/extensions/extension_registry.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fabgl {
class Scene;
class SceneRuntime;
} // namespace fabgl

namespace fabgl::project {

inline constexpr std::uint32_t ProjectExtensionHostApiVersion = 1U;
inline constexpr std::string_view ProjectOpenExtensionCapability = "fabgl.project.open";
inline constexpr std::string_view ProjectCloseExtensionCapability = "fabgl.project.close";
inline constexpr std::string_view RuntimeStartExtensionCapability = "fabgl.runtime.start";
inline constexpr std::string_view RuntimeStopExtensionCapability = "fabgl.runtime.stop";
inline constexpr std::uint32_t ProjectExtensionServiceApiVersion = 1U;

[[nodiscard]] constexpr std::string_view
projectExtensionRegistrationCapability(const PackageEntryPointKind kind) noexcept {
    switch (kind) {
    case PackageEntryPointKind::EditorPlugin:
        return "fabgl.editor-plugin.register";
    case PackageEntryPointKind::RuntimeModule:
        return "fabgl.runtime-module.register";
    case PackageEntryPointKind::AssetImporter:
        return "fabgl.asset-importer.register";
    case PackageEntryPointKind::CustomInspector:
        return "fabgl.custom-inspector.register";
    case PackageEntryPointKind::CustomWindow:
        return "fabgl.custom-window.register";
    case PackageEntryPointKind::BuildStep:
        return "fabgl.build-step.register";
    case PackageEntryPointKind::RendererExtension:
        return "fabgl.renderer-extension.register";
    case PackageEntryPointKind::Framework:
        return "fabgl.framework.register";
    }
    return {};
}

enum class ProjectExtensionHostKind : std::uint32_t {
    Studio = 1U,
    Player = 2U,
};

struct ProjectExtensionServiceDescriptor final {
    std::uint32_t structureSize = sizeof(ProjectExtensionServiceDescriptor);
    std::uint32_t apiVersion = ProjectExtensionServiceApiVersion;
    PackageEntryPointKind kind = PackageEntryPointKind::RuntimeModule;
    const char* serviceId = nullptr;
    const char* displayName = nullptr;
    const char* dispatchCapability = nullptr;
    std::int32_t priority = 0;
};

using ProjectExtensionServiceRegistrar =
    Result<void> (*)(void*, const ProjectExtensionServiceDescriptor*) noexcept;

// A host-owned registration table exposed only while a type-specific *.register hook executes.
// The extension may call registerService repeatedly, but must not retain the table or hostState.
struct ProjectExtensionHostServices final {
    std::uint32_t structureSize = sizeof(ProjectExtensionHostServices);
    std::uint32_t apiVersion = ProjectExtensionServiceApiVersion;
    void* hostState = nullptr;
    ProjectExtensionServiceRegistrar registerService = nullptr;
};

// Read-only host view passed through ExtensionInvocation::hostContext for the four standardized
// lifecycle capabilities above. The view and its UTF-8 strings are valid only for the duration
// of the hook call. Native extensions must reject an unknown apiVersion/structureSize and must
// not retain any pointer from this structure.
struct ProjectExtensionHostContext final {
    std::uint32_t structureSize = sizeof(ProjectExtensionHostContext);
    std::uint32_t apiVersion = ProjectExtensionHostApiVersion;
    ProjectExtensionHostKind hostKind = ProjectExtensionHostKind::Player;
    const char* projectManifestPath = nullptr;
    std::size_t projectManifestPathBytes = 0U;
    const char* projectRoot = nullptr;
    std::size_t projectRootBytes = 0U;
    const Scene* scene = nullptr;
    const SceneRuntime* sceneRuntime = nullptr;
    const ProjectExtensionHostServices* services = nullptr;
};

struct ProjectExtensionServiceInfo final {
    std::string extensionId;
    std::string serviceId;
    std::string displayName;
    std::string dispatchCapability;
    PackageEntryPointKind kind = PackageEntryPointKind::RuntimeModule;
    std::int32_t priority = 0;

    [[nodiscard]] std::string qualifiedId() const {
        return extensionId + ':' + serviceId;
    }
};

struct ProjectExtensionModuleLimits final {
    std::size_t maximumModules = 64U;
    std::size_t maximumDescriptorsPerModule = 64U;
    std::size_t maximumExtensionIdBytes = 128U;
    std::size_t maximumServices = 512U;
    std::size_t maximumServiceIdBytes = 96U;
    std::size_t maximumServiceDisplayNameBytes = 160U;
};

struct ProjectExtensionLoadOptions final {
    bool safeMode = false;
    bool extensionsEnabled = true;
    // Empty accepts all eight package entry-point kinds. A runtime host can
    // restrict this to RuntimeModule/RendererExtension/Framework.
    std::vector<PackageEntryPointKind> acceptedKinds;
    ProjectExtensionModuleLimits limits;
};

struct ProjectExtensionModuleStats final {
    std::uint32_t discoveredPackages = 0U;
    std::uint32_t discoveredEntries = 0U;
    std::uint32_t loadedModules = 0U;
    std::uint32_t registeredExtensions = 0U;
    std::uint32_t registeredServices = 0U;
    std::uint32_t skippedSourceEntries = 0U;
    std::uint32_t skippedKinds = 0U;
    std::uint32_t skippedDisabledEntries = 0U;
};

// Discovers content-bound trusted package binaries from the canonical project
// Packages lock, validates the module ABI, and keeps every library resident until
// its extensions have been deactivated and destroyed.
class ProjectExtensionModules final {
  public:
    ProjectExtensionModules();
    ~ProjectExtensionModules();
    ProjectExtensionModules(ProjectExtensionModules&&) noexcept;
    ProjectExtensionModules& operator=(ProjectExtensionModules&&) noexcept;
    ProjectExtensionModules(const ProjectExtensionModules&) = delete;
    ProjectExtensionModules& operator=(const ProjectExtensionModules&) = delete;

    [[nodiscard]] static Result<ProjectExtensionModules>
    load(const std::string& projectManifestPath,
         const ProjectExtensionLoadOptions& options = {});

    [[nodiscard]] Result<void> activate();
    void deactivate() noexcept;
    [[nodiscard]] Result<std::string>
    invoke(std::string_view qualifiedExtensionId, std::string_view capability,
           const ExtensionInvocation& invocation) const;
    // Invokes only extensions that registered the capability. Missing hooks are intentionally
    // skipped; the first registered-hook failure aborts deterministically with extension context.
    [[nodiscard]] Result<void> invokeAll(std::string_view capability,
                                         const ExtensionInvocation& invocation) const;
    // Calls the type-specific registration hook for each active extension. Descriptors are copied
    // into host ownership and can later be dispatched without exposing editor implementation
    // pointers across the module boundary.
    [[nodiscard]] Result<void> registerServices(ProjectExtensionHostContext context);
    [[nodiscard]] Result<std::string>
    invokeService(std::string_view qualifiedServiceId,
                  const ExtensionInvocation& invocation) const;

    [[nodiscard]] const ProjectExtensionModuleStats& stats() const noexcept;
    [[nodiscard]] const std::vector<std::string>& activeExtensionIds() const noexcept;
    [[nodiscard]] const std::vector<ProjectExtensionServiceInfo>& services() const noexcept;
    [[nodiscard]] bool active() const noexcept;

  private:
    struct Implementation;
    explicit ProjectExtensionModules(std::unique_ptr<Implementation> implementation);

    std::unique_ptr<Implementation> implementation_;
};

} // namespace fabgl::project
