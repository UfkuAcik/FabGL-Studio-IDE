#pragma once

#include <fabgl/core/result.h>
#include <fabgl/project/project_extension_modules.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fabgl::project {

inline constexpr std::size_t ProjectExtensionMaximumDispatchPayloadBytes = 256U * 1024U;
inline constexpr std::size_t ProjectExtensionMaximumDispatchResponseBytes = 256U * 1024U;
inline constexpr std::uint64_t ProjectExtensionMaximumInvocationMilliseconds = 500U;

enum class ProjectExtensionServiceStateKind : std::uint8_t {
    Ready,
    RuntimeStarted,
    RuntimeStopped,
    Disabled,
};

struct ProjectExtensionServiceState final {
    ProjectExtensionServiceInfo service;
    ProjectExtensionServiceStateKind state = ProjectExtensionServiceStateKind::Ready;
    std::uint64_t successfulInvocations = 0U;
    std::uint64_t failedInvocations = 0U;
    std::string lastError;

    [[nodiscard]] bool enabled() const noexcept {
        return state != ProjectExtensionServiceStateKind::Disabled;
    }
};

struct ProjectExtensionServiceFailure final {
    std::string qualifiedServiceId;
    Error error;
};

struct ProjectExtensionDispatchReport final {
    std::uint32_t attempted = 0U;
    std::uint32_t succeeded = 0U;
    std::uint32_t skippedDisabled = 0U;
    std::vector<ProjectExtensionServiceFailure> failures;

    [[nodiscard]] bool ok() const noexcept { return failures.empty(); }
};

struct ProjectExtensionServiceHostStats final {
    std::uint32_t registeredServices = 0U;
    std::uint32_t enabledServices = 0U;
    std::uint32_t disabledServices = 0U;
    std::uint64_t successfulInvocations = 0U;
    std::uint64_t failedInvocations = 0U;
};

// Product-facing dispatcher for descriptors registered through ProjectExtensionModules.
//
// It deliberately exposes a small string/UTF-8 ABI instead of editor implementation pointers.
// The host validates every operation and payload before crossing the native module boundary.
// A service that returns an error is disabled for the remainder of the project session while
// other services continue deterministically; the caller receives and can report the error.
class ProjectExtensionServiceHost final {
  public:
    ProjectExtensionServiceHost() = default;

    [[nodiscard]] static Result<ProjectExtensionServiceHost>
    create(ProjectExtensionModules& modules, ProjectExtensionHostContext registrationContext);

    [[nodiscard]] Result<std::string>
    invoke(std::string_view qualifiedServiceId, std::string_view operation,
           std::string payload, const ProjectExtensionHostContext& context);

    // Product schema parsers use this when native code returned malformed or hostile output.
    // Rejection is session-scoped and deterministic, matching a hook failure.
    [[nodiscard]] Result<void> rejectResponse(std::string_view qualifiedServiceId,
                                              Error error);

    [[nodiscard]] ProjectExtensionDispatchReport
    invokeKind(PackageEntryPointKind kind, std::string_view operation, std::string payload,
               const ProjectExtensionHostContext& context);

    [[nodiscard]] ProjectExtensionDispatchReport
    runtimeStart(const ProjectExtensionHostContext& context, std::string_view hostName);
    [[nodiscard]] ProjectExtensionDispatchReport
    runtimeUpdate(const ProjectExtensionHostContext& context, double deltaSeconds);
    [[nodiscard]] ProjectExtensionDispatchReport
    runtimeStop(const ProjectExtensionHostContext& context, std::string_view reason);

    [[nodiscard]] ProjectExtensionDispatchReport
    buildStep(const ProjectExtensionHostContext& context, std::string_view phase,
              std::string_view target, std::string_view configuration, bool processSucceeded,
              int exitCode);

    [[nodiscard]] const std::vector<ProjectExtensionServiceState>& services() const noexcept {
        return services_;
    }
    [[nodiscard]] ProjectExtensionServiceHostStats stats() const noexcept;
    [[nodiscard]] bool active() const noexcept { return modules_ != nullptr; }

  private:
    explicit ProjectExtensionServiceHost(ProjectExtensionModules& modules);

    [[nodiscard]] ProjectExtensionServiceState*
    findService(std::string_view qualifiedServiceId) noexcept;

    ProjectExtensionModules* modules_ = nullptr;
    std::vector<ProjectExtensionServiceState> services_;
};

[[nodiscard]] bool projectExtensionServiceSupportsOperation(PackageEntryPointKind kind,
                                                             std::string_view operation) noexcept;
[[nodiscard]] std::string_view
projectExtensionServiceStateName(ProjectExtensionServiceStateKind state) noexcept;

} // namespace fabgl::project
