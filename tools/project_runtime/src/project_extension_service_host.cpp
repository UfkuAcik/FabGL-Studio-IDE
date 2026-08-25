#include <fabgl/project/project_extension_service_host.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace fabgl::project {
namespace {

[[nodiscard]] bool validContext(const ProjectExtensionHostContext& context) noexcept {
    return context.structureSize == sizeof(ProjectExtensionHostContext) &&
           context.apiVersion == ProjectExtensionHostApiVersion &&
           (context.hostKind == ProjectExtensionHostKind::Studio ||
            context.hostKind == ProjectExtensionHostKind::Player) &&
           context.projectManifestPath != nullptr && context.projectManifestPathBytes > 0U &&
           context.projectRoot != nullptr && context.projectRootBytes > 0U &&
           context.scene != nullptr;
}

[[nodiscard]] std::string errorText(const Error& error) {
    std::string text = error.message();
    for (const auto& item : error.context())
        text += " [" + item.key + '=' + item.value + ']';
    return text;
}

[[nodiscard]] std::string jsonString(const std::string_view value) {
    std::string output;
    output.reserve(value.size() + 2U);
    output.push_back('"');
    constexpr char Hex[] = "0123456789abcdef";
    for (const auto byte : value) {
        const auto character = static_cast<unsigned char>(byte);
        switch (character) {
        case '"':
            output += "\\\"";
            break;
        case '\\':
            output += "\\\\";
            break;
        case '\b':
            output += "\\b";
            break;
        case '\f':
            output += "\\f";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            if (character < 0x20U) {
                output += "\\u00";
                output.push_back(Hex[(character >> 4U) & 0x0FU]);
                output.push_back(Hex[character & 0x0FU]);
            } else {
                output.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    output.push_back('"');
    return output;
}

void appendReport(ProjectExtensionDispatchReport& destination,
                  ProjectExtensionDispatchReport source) {
    destination.attempted += source.attempted;
    destination.succeeded += source.succeeded;
    destination.skippedDisabled += source.skippedDisabled;
    destination.failures.insert(destination.failures.end(),
                                std::make_move_iterator(source.failures.begin()),
                                std::make_move_iterator(source.failures.end()));
}

[[nodiscard]] std::string runtimePayload(const std::string_view hostName) {
    return "{\"schema\":1,\"host\":" + jsonString(hostName) + '}';
}

} // namespace

bool projectExtensionServiceSupportsOperation(const PackageEntryPointKind kind,
                                              const std::string_view operation) noexcept {
    switch (kind) {
    case PackageEntryPointKind::EditorPlugin:
        return operation == "activate" || operation == "execute" || operation == "deactivate";
    case PackageEntryPointKind::RuntimeModule:
    case PackageEntryPointKind::RendererExtension:
    case PackageEntryPointKind::Framework:
        return operation == "startup" || operation == "update" || operation == "shutdown";
    case PackageEntryPointKind::AssetImporter:
        return operation == "probe" || operation == "import" || operation == "reimport";
    case PackageEntryPointKind::CustomInspector:
        return operation == "inspect" || operation == "apply";
    case PackageEntryPointKind::CustomWindow:
        return operation == "show" || operation == "hide" || operation == "refresh";
    case PackageEntryPointKind::BuildStep:
        return operation == "pre-build" || operation == "post-build";
    }
    return false;
}

std::string_view
projectExtensionServiceStateName(const ProjectExtensionServiceStateKind state) noexcept {
    switch (state) {
    case ProjectExtensionServiceStateKind::Ready:
        return "ready";
    case ProjectExtensionServiceStateKind::RuntimeStarted:
        return "runtime-started";
    case ProjectExtensionServiceStateKind::RuntimeStopped:
        return "runtime-stopped";
    case ProjectExtensionServiceStateKind::Disabled:
        return "disabled";
    }
    return "unknown";
}

ProjectExtensionServiceHost::ProjectExtensionServiceHost(ProjectExtensionModules& modules)
    : modules_(&modules) {}

Result<ProjectExtensionServiceHost>
ProjectExtensionServiceHost::create(ProjectExtensionModules& modules,
                                    ProjectExtensionHostContext registrationContext) {
    if (!modules.active()) {
        return Result<ProjectExtensionServiceHost>::failure(
            Error(ErrorCode::InvalidState, "extension modules are not active"));
    }
    if (!validContext(registrationContext)) {
        return Result<ProjectExtensionServiceHost>::failure(
            Error(ErrorCode::InvalidArgument,
                  "extension service registration context is invalid"));
    }
    auto registered = modules.registerServices(registrationContext);
    if (!registered)
        return Result<ProjectExtensionServiceHost>::failure(registered.error());

    ProjectExtensionServiceHost host(modules);
    host.services_.reserve(modules.services().size());
    for (const auto& service : modules.services()) {
        ProjectExtensionServiceState state;
        state.service = service;
        host.services_.push_back(std::move(state));
    }
    return Result<ProjectExtensionServiceHost>::success(std::move(host));
}

ProjectExtensionServiceState*
ProjectExtensionServiceHost::findService(const std::string_view qualifiedServiceId) noexcept {
    const auto found = std::find_if(
        services_.begin(), services_.end(), [qualifiedServiceId](const auto& state) {
            return state.service.qualifiedId() == qualifiedServiceId;
        });
    return found == services_.end() ? nullptr : &*found;
}

Result<std::string>
ProjectExtensionServiceHost::invoke(const std::string_view qualifiedServiceId,
                                    const std::string_view operation, std::string payload,
                                    const ProjectExtensionHostContext& context) {
    if (modules_ == nullptr || !modules_->active()) {
        return Result<std::string>::failure(
            Error(ErrorCode::InvalidState, "extension service host is not active"));
    }
    auto* state = findService(qualifiedServiceId);
    if (state == nullptr) {
        return Result<std::string>::failure(
            Error(ErrorCode::NotFound, "extension service was not registered")
                .addContext("service", std::string(qualifiedServiceId)));
    }
    if (!state->enabled()) {
        return Result<std::string>::failure(
            Error(ErrorCode::InvalidState, "extension service is disabled for this session")
                .addContext("service", state->service.qualifiedId())
                .addContext("lastError", state->lastError));
    }
    if (!projectExtensionServiceSupportsOperation(state->service.kind, operation)) {
        return Result<std::string>::failure(
            Error(ErrorCode::InvalidArgument,
                  "operation is not supported by this extension service kind")
                .addContext("service", state->service.qualifiedId())
                .addContext("kind", std::string(packageEntryPointKindName(state->service.kind)))
                .addContext("operation", std::string(operation)));
    }
    if (!validContext(context) ||
        ((operation == "startup" || operation == "update" || operation == "shutdown") &&
         context.sceneRuntime == nullptr)) {
        return Result<std::string>::failure(
            Error(ErrorCode::InvalidArgument, "extension service host context is invalid")
                .addContext("service", state->service.qualifiedId())
                .addContext("operation", std::string(operation)));
    }
    if (payload.size() > ProjectExtensionMaximumDispatchPayloadBytes) {
        return Result<std::string>::failure(
            Error(ErrorCode::CapacityExceeded, "extension service payload exceeds its limit")
                .addContext("service", state->service.qualifiedId())
                .addContext("bytes", std::to_string(payload.size())));
    }

    const auto started = std::chrono::steady_clock::now();
    auto invoked = modules_->invokeService(
        state->service.qualifiedId(),
        {std::string(operation), std::move(payload), const_cast<ProjectExtensionHostContext*>(&context)});
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    if (!invoked) {
        ++state->failedInvocations;
        state->lastError = errorText(invoked.error());
        state->state = ProjectExtensionServiceStateKind::Disabled;
        return invoked;
    }
    if (elapsed.count() < 0 ||
        static_cast<std::uint64_t>(elapsed.count()) >
            ProjectExtensionMaximumInvocationMilliseconds) {
        Error error(ErrorCode::InvalidState,
                    "extension service exceeded the synchronous invocation time limit");
        error.addContext("service", state->service.qualifiedId())
            .addContext("milliseconds", std::to_string(elapsed.count()))
            .addContext("limitMilliseconds",
                        std::to_string(ProjectExtensionMaximumInvocationMilliseconds));
        ++state->failedInvocations;
        state->lastError = errorText(error);
        state->state = ProjectExtensionServiceStateKind::Disabled;
        return Result<std::string>::failure(std::move(error));
    }
    if (invoked.value().size() > ProjectExtensionMaximumDispatchResponseBytes) {
        Error error(ErrorCode::CapacityExceeded,
                    "extension service response exceeds its limit");
        error.addContext("service", state->service.qualifiedId())
            .addContext("bytes", std::to_string(invoked.value().size()));
        ++state->failedInvocations;
        state->lastError = errorText(error);
        state->state = ProjectExtensionServiceStateKind::Disabled;
        return Result<std::string>::failure(std::move(error));
    }
    ++state->successfulInvocations;
    state->lastError.clear();
    if (operation == "startup")
        state->state = ProjectExtensionServiceStateKind::RuntimeStarted;
    else if (operation == "shutdown")
        state->state = ProjectExtensionServiceStateKind::RuntimeStopped;
    return invoked;
}

Result<void>
ProjectExtensionServiceHost::rejectResponse(const std::string_view qualifiedServiceId,
                                            Error error) {
    auto* state = findService(qualifiedServiceId);
    if (state == nullptr) {
        return Result<void>::failure(
            Error(ErrorCode::NotFound, "extension service was not registered")
                .addContext("service", std::string(qualifiedServiceId)));
    }
    ++state->failedInvocations;
    state->lastError = errorText(error);
    state->state = ProjectExtensionServiceStateKind::Disabled;
    return Result<void>::failure(std::move(error));
}

ProjectExtensionDispatchReport
ProjectExtensionServiceHost::invokeKind(const PackageEntryPointKind kind,
                                        const std::string_view operation, std::string payload,
                                        const ProjectExtensionHostContext& context) {
    ProjectExtensionDispatchReport report;
    std::vector<std::string> matchingIds;
    for (const auto& state : services_) {
        if (state.service.kind == kind)
            matchingIds.push_back(state.service.qualifiedId());
    }
    for (const auto& id : matchingIds) {
        auto* state = findService(id);
        if (state == nullptr)
            continue;
        if (!state->enabled()) {
            ++report.skippedDisabled;
            continue;
        }
        ++report.attempted;
        auto result = invoke(id, operation, payload, context);
        if (result) {
            ++report.succeeded;
        } else {
            report.failures.push_back({id, result.error()});
        }
    }
    return report;
}

ProjectExtensionDispatchReport
ProjectExtensionServiceHost::runtimeStart(const ProjectExtensionHostContext& context,
                                          const std::string_view hostName) {
    ProjectExtensionDispatchReport report;
    const auto payload = runtimePayload(hostName);
    for (const auto kind : {PackageEntryPointKind::RuntimeModule,
                            PackageEntryPointKind::RendererExtension,
                            PackageEntryPointKind::Framework}) {
        appendReport(report, invokeKind(kind, "startup", payload, context));
    }
    return report;
}

ProjectExtensionDispatchReport
ProjectExtensionServiceHost::runtimeUpdate(const ProjectExtensionHostContext& context,
                                           const double deltaSeconds) {
    ProjectExtensionDispatchReport report;
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0 || deltaSeconds > 1.0) {
        report.failures.push_back(
            {"<host>", Error(ErrorCode::InvalidArgument,
                              "extension runtime update delta is invalid")});
        return report;
    }
    std::ostringstream payload;
    payload << "{\"schema\":1,\"deltaSeconds\":" << std::setprecision(17) << deltaSeconds
            << '}';
    for (const auto kind : {PackageEntryPointKind::RuntimeModule,
                            PackageEntryPointKind::RendererExtension,
                            PackageEntryPointKind::Framework}) {
        appendReport(report, invokeKind(kind, "update", payload.str(), context));
    }
    return report;
}

ProjectExtensionDispatchReport
ProjectExtensionServiceHost::runtimeStop(const ProjectExtensionHostContext& context,
                                         const std::string_view reason) {
    ProjectExtensionDispatchReport report;
    const auto payload = "{\"schema\":1,\"reason\":" + jsonString(reason) + '}';
    // Shutdown in reverse layer order so renderer/framework resources outlive consumers.
    for (const auto kind : {PackageEntryPointKind::Framework,
                            PackageEntryPointKind::RendererExtension,
                            PackageEntryPointKind::RuntimeModule}) {
        appendReport(report, invokeKind(kind, "shutdown", payload, context));
    }
    return report;
}

ProjectExtensionDispatchReport
ProjectExtensionServiceHost::buildStep(const ProjectExtensionHostContext& context,
                                       const std::string_view phase, const std::string_view target,
                                       const std::string_view configuration,
                                       const bool processSucceeded, const int exitCode) {
    if (phase != "pre-build" && phase != "post-build") {
        ProjectExtensionDispatchReport report;
        report.failures.push_back(
            {"<host>", Error(ErrorCode::InvalidArgument,
                              "extension build phase must be pre-build or post-build")});
        return report;
    }
    std::string payload = "{\"schema\":1,\"target\":" + jsonString(target) +
                          ",\"configuration\":" + jsonString(configuration) +
                          ",\"processSucceeded\":" +
                          (processSucceeded ? std::string("true") : std::string("false")) +
                          ",\"exitCode\":" + std::to_string(exitCode) + '}';
    return invokeKind(PackageEntryPointKind::BuildStep, phase, std::move(payload), context);
}

ProjectExtensionServiceHostStats ProjectExtensionServiceHost::stats() const noexcept {
    ProjectExtensionServiceHostStats stats;
    stats.registeredServices = static_cast<std::uint32_t>(services_.size());
    for (const auto& state : services_) {
        if (state.enabled())
            ++stats.enabledServices;
        else
            ++stats.disabledServices;
        stats.successfulInvocations += state.successfulInvocations;
        stats.failedInvocations += state.failedInvocations;
    }
    return stats;
}

} // namespace fabgl::project
