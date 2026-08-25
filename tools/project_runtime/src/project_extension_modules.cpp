#include <fabgl/project/project_extension_modules.h>

#include <fabgl/assets/file_io.h>
#include <fabgl/extensions/extension_module.h>
#include <local_package_manager.h>

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace fabgl::project {
namespace {

namespace fs = std::filesystem;

std::string pathText(const fs::path& path) {
    const auto value = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

fs::path pathFromUtf8(const std::string& value) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}

std::string lowercaseAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool sameOrInside(const fs::path& path, const fs::path& root) {
    auto child = lowercaseAscii(pathText(path.lexically_normal()));
    auto parent = lowercaseAscii(pathText(root.lexically_normal()));
#if !defined(_WIN32)
    child = pathText(path.lexically_normal());
    parent = pathText(root.lexically_normal());
#endif
    if (child == parent)
        return true;
    if (!parent.empty() && parent.back() != '/')
        parent.push_back('/');
    return child.rfind(parent, 0U) == 0U;
}

bool isReparsePoint(const fs::path& path) noexcept {
#if defined(_WIN32)
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U;
#else
    std::error_code error;
    return fs::is_symlink(fs::symlink_status(path, error));
#endif
}

Result<void> validateModulePath(const fs::path& packageRoot, const fs::path& modulePath) {
    if (!sameOrInside(modulePath, packageRoot)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "extension module escapes its package root")
                .addContext("path", pathText(modulePath)));
    }
    auto current = modulePath.root_path();
    for (const auto& segment : modulePath.relative_path()) {
        current /= segment;
        std::error_code error;
        const auto status = fs::symlink_status(current, error);
        if (error) {
            return Result<void>::failure(
                Error(ErrorCode::IoError, "unable to inspect extension module path")
                    .addContext("path", pathText(current))
                    .addContext("system", error.message()));
        }
        if (isReparsePoint(current)) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument,
                      "extension module path traverses a symlink, junction, or reparse point")
                    .addContext("path", pathText(current)));
        }
    }
    std::error_code error;
    if (!fs::is_regular_file(modulePath, error) || error) {
        return Result<void>::failure(
            Error(ErrorCode::NotFound, "extension module is not a regular file")
                .addContext("path", pathText(modulePath)));
    }
    return Result<void>::success();
}

bool dynamicLibraryPath(std::string_view path) {
    const auto extension = lowercaseAscii(pathText(pathFromUtf8(std::string(path)).extension()));
#if defined(_WIN32)
    return extension == ".dll";
#elif defined(__APPLE__)
    return extension == ".dylib" || extension == ".so";
#else
    return extension == ".so";
#endif
}

bool acceptsKind(const ProjectExtensionLoadOptions& options, PackageEntryPointKind kind) {
    return options.acceptedKinds.empty() ||
           std::find(options.acceptedKinds.begin(), options.acceptedKinds.end(), kind) !=
               options.acceptedKinds.end();
}

bool capabilityName(std::string_view value) noexcept {
    return !value.empty() && value.size() <= 96U &&
           std::all_of(value.cbegin(), value.cend(), [](const unsigned char character) {
               return std::islower(character) || std::isdigit(character) || character == '-' ||
                      character == '.';
           });
}

bool lifecycleCapability(std::string_view capability) noexcept {
    return capability == ProjectOpenExtensionCapability ||
           capability == ProjectCloseExtensionCapability ||
           capability == RuntimeStartExtensionCapability ||
           capability == RuntimeStopExtensionCapability;
}

bool boundedHostText(const char* text, const std::size_t size) noexcept {
    return text != nullptr && size > 0U && size <= 32'768U &&
           std::find(text, text + size, '\0') == text + size;
}

Result<void> validateLifecycleInvocation(const std::string_view capability,
                                         const ExtensionInvocation& invocation) {
    if (!lifecycleCapability(capability))
        return Result<void>::success();
    const auto* context = static_cast<const ProjectExtensionHostContext*>(invocation.hostContext);
    if (context == nullptr || context->structureSize != sizeof(ProjectExtensionHostContext) ||
        context->apiVersion != ProjectExtensionHostApiVersion ||
        (context->hostKind != ProjectExtensionHostKind::Studio &&
         context->hostKind != ProjectExtensionHostKind::Player) ||
        !boundedHostText(context->projectManifestPath, context->projectManifestPathBytes) ||
        !boundedHostText(context->projectRoot, context->projectRootBytes) ||
        context->scene == nullptr ||
        ((capability == RuntimeStartExtensionCapability ||
          capability == RuntimeStopExtensionCapability) &&
         context->sceneRuntime == nullptr)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument,
                  "extension lifecycle host context is missing or ABI-incompatible")
                .addContext("capability", std::string(capability)));
    }
    return Result<void>::success();
}

bool boundedDisplayName(std::string_view value) noexcept {
    return !value.empty() && std::none_of(value.cbegin(), value.cend(), [](const char raw) {
               const auto character = static_cast<unsigned char>(raw);
               return character < 0x20U || character == 0x7FU;
           });
}

Result<std::string> boundedCString(const char* value, std::size_t maximumBytes,
                                   std::string_view field) {
    if (value == nullptr)
        return Result<std::string>::failure(
            Error(ErrorCode::InvalidFormat, "extension module string is null")
                .addContext("field", std::string(field)));
    std::size_t size = 0U;
    while (size <= maximumBytes && value[size] != '\0')
        ++size;
    if (size == 0U || size > maximumBytes) {
        return Result<std::string>::failure(
            Error(ErrorCode::InvalidFormat, "extension module string exceeds its limit")
                .addContext("field", std::string(field)));
    }
    return Result<std::string>::success(std::string(value, size));
}

#if defined(_WIN32)
using ModuleHandle = HMODULE;

Result<ModuleHandle> openModule(const fs::path& path) {
    const auto handle = LoadLibraryExW(path.c_str(), nullptr,
                                       LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                           LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (handle == nullptr) {
        return Result<ModuleHandle>::failure(
            Error(ErrorCode::IoError, "unable to load extension module")
                .addContext("path", pathText(path))
                .addContext("win32", std::to_string(GetLastError())));
    }
    return Result<ModuleHandle>::success(handle);
}

extensions::GetExtensionModuleFunction entryPoint(ModuleHandle handle) noexcept {
    const auto address = GetProcAddress(handle, extensions::ExtensionModuleEntryPoint);
    static_assert(sizeof(address) == sizeof(extensions::GetExtensionModuleFunction));
    return std::bit_cast<extensions::GetExtensionModuleFunction>(address);
}

void closeModule(ModuleHandle handle) noexcept {
    if (handle != nullptr)
        FreeLibrary(handle);
}
#else
using ModuleHandle = void*;

Result<ModuleHandle> openModule(const fs::path& path) {
    dlerror();
    const auto text = pathText(path);
    const auto handle = dlopen(text.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        const auto* loaderError = dlerror();
        return Result<ModuleHandle>::failure(
            Error(ErrorCode::IoError, "unable to load extension module")
                .addContext("path", text)
                .addContext("loader", loaderError == nullptr ? "unknown" : loaderError));
    }
    return Result<ModuleHandle>::success(handle);
}

extensions::GetExtensionModuleFunction entryPoint(ModuleHandle handle) noexcept {
    dlerror();
    const auto address = dlsym(handle, extensions::ExtensionModuleEntryPoint);
    static_assert(sizeof(address) == sizeof(extensions::GetExtensionModuleFunction));
    return std::bit_cast<extensions::GetExtensionModuleFunction>(address);
}

void closeModule(ModuleHandle handle) noexcept {
    if (handle != nullptr)
        dlclose(handle);
}
#endif

class ModuleOwner final {
  public:
    explicit ModuleOwner(ModuleHandle handle) noexcept : handle_(handle) {}
    ~ModuleOwner() {
        closeModule(handle_);
    }

    ModuleOwner(ModuleOwner&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
    ModuleOwner& operator=(ModuleOwner&& other) noexcept {
        if (this != &other) {
            closeModule(handle_);
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }
    ModuleOwner(const ModuleOwner&) = delete;
    ModuleOwner& operator=(const ModuleOwner&) = delete;

  private:
    ModuleHandle handle_ = nullptr;
};

class LoadedExtensionProxy final : public IExtension {
  public:
    LoadedExtensionProxy(ExtensionIdentity identity, IExtension* implementation,
                         extensions::ExtensionDestroyer destroyer)
        : identity_(std::move(identity)), implementation_(implementation), destroyer_(destroyer) {}

    ~LoadedExtensionProxy() override {
        if (implementation_ != nullptr && destroyer_ != nullptr)
            destroyer_(implementation_);
    }

    LoadedExtensionProxy(const LoadedExtensionProxy&) = delete;
    LoadedExtensionProxy& operator=(const LoadedExtensionProxy&) = delete;

    const ExtensionIdentity& identity() const noexcept override {
        return identity_;
    }

    Result<void> activate(ExtensionHost& host) override {
        return implementation_->activate(host);
    }

    void deactivate(ExtensionHost& host) noexcept override {
        implementation_->deactivate(host);
    }

    void addDependency(std::string dependency) {
        identity_.dependencies.push_back(std::move(dependency));
    }

  private:
    ExtensionIdentity identity_;
    IExtension* implementation_ = nullptr;
    extensions::ExtensionDestroyer destroyer_ = nullptr;
};

struct PendingExtension final {
    std::string packageId;
    std::vector<PackageDependency> packageDependencies;
    std::unique_ptr<LoadedExtensionProxy> extension;
};

} // namespace

struct ProjectExtensionModules::Implementation final {
    struct ServiceRegistrationState final {
        Implementation* implementation = nullptr;
        std::string extensionId;
        PackageEntryPointKind kind = PackageEntryPointKind::RuntimeModule;
        std::optional<Error> failure;
    };

    Implementation(bool safeMode, bool extensionsEnabled, ProjectExtensionModuleLimits limitsValue)
        : host(safeMode, extensionsEnabled, [this](const ExtensionIdentity& identity) {
              const auto trusted = trustedDigests.find(identity.packageId);
              return trusted != trustedDigests.end() && trusted->second == identity.contentSha256;
          }),
          limits(std::move(limitsValue)) {}

    ~Implementation() {
        if (active)
            registry.deactivateAll(host);
    }

    std::vector<ModuleOwner> handles;
    std::map<std::string, std::string, std::less<>> trustedDigests;
    ExtensionHost host;
    ExtensionRegistry registry;
    ProjectExtensionModuleStats stats;
    ProjectExtensionModuleLimits limits;
    std::vector<std::string> activationOrder;
    std::map<std::string, PackageEntryPointKind, std::less<>> extensionKinds;
    std::vector<ProjectExtensionServiceInfo> services;
    bool servicesRegistered = false;
    bool active = false;

    Result<void> addService(ServiceRegistrationState& state,
                            const ProjectExtensionServiceDescriptor* descriptor) {
        if (descriptor == nullptr || descriptor->structureSize != sizeof(*descriptor) ||
            descriptor->apiVersion != ProjectExtensionServiceApiVersion ||
            descriptor->kind != state.kind || descriptor->priority < -100'000 ||
            descriptor->priority > 100'000) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidFormat, "extension service descriptor is invalid")
                    .addContext("extension", state.extensionId));
        }
        auto serviceId = boundedCString(descriptor->serviceId, limits.maximumServiceIdBytes,
                                        "serviceId");
        auto displayName = boundedCString(descriptor->displayName,
                                          limits.maximumServiceDisplayNameBytes, "displayName");
        auto dispatch = boundedCString(descriptor->dispatchCapability, 96U,
                                       "dispatchCapability");
        if (!serviceId)
            return Result<void>::failure(serviceId.error());
        if (!displayName)
            return Result<void>::failure(displayName.error());
        if (!dispatch)
            return Result<void>::failure(dispatch.error());
        if (!capabilityName(serviceId.value()) || !boundedDisplayName(displayName.value()) ||
            !capabilityName(dispatch.value()) ||
            !host.hasHook(state.extensionId, dispatch.value())) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument,
                      "extension service identity, label, or dispatch hook is invalid")
                    .addContext("extension", state.extensionId)
                    .addContext("service", serviceId.value())
                    .addContext("dispatch", dispatch.value()));
        }
        const auto duplicate = std::find_if(
            services.cbegin(), services.cend(), [&](const ProjectExtensionServiceInfo& service) {
                return service.extensionId == state.extensionId &&
                       service.serviceId == serviceId.value();
            });
        if (duplicate != services.cend()) {
            return Result<void>::failure(
                Error(ErrorCode::AlreadyExists, "extension service ID is already registered")
                    .addContext("extension", state.extensionId)
                    .addContext("service", serviceId.value()));
        }
        if (services.size() >= limits.maximumServices) {
            return Result<void>::failure(
                Error(ErrorCode::CapacityExceeded, "extension service count exceeds its limit"));
        }
        services.push_back({state.extensionId, std::move(serviceId.value()),
                            std::move(displayName.value()), std::move(dispatch.value()), state.kind,
                            descriptor->priority});
        return Result<void>::success();
    }

    static Result<void>
    registerService(void* opaqueState,
                    const ProjectExtensionServiceDescriptor* descriptor) noexcept {
        auto* state = static_cast<ServiceRegistrationState*>(opaqueState);
        if (state == nullptr || state->implementation == nullptr) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidState, "extension service registrar state is invalid"));
        }
        try {
            auto registered = state->implementation->addService(*state, descriptor);
            if (!registered && !state->failure)
                state->failure = registered.error();
            return registered;
        } catch (...) {
            auto error = Error(ErrorCode::InternalError,
                               "extension service registration crossed a failing host boundary")
                             .addContext("extension", state->extensionId);
            if (!state->failure)
                state->failure = error;
            return Result<void>::failure(std::move(error));
        }
    }
};

ProjectExtensionModules::ProjectExtensionModules()
    : implementation_(
          std::make_unique<Implementation>(false, true, ProjectExtensionModuleLimits{})) {}
ProjectExtensionModules::~ProjectExtensionModules() = default;
ProjectExtensionModules::ProjectExtensionModules(ProjectExtensionModules&&) noexcept = default;
ProjectExtensionModules&
ProjectExtensionModules::operator=(ProjectExtensionModules&&) noexcept = default;

ProjectExtensionModules::ProjectExtensionModules(std::unique_ptr<Implementation> implementation)
    : implementation_(std::move(implementation)) {}

Result<ProjectExtensionModules>
ProjectExtensionModules::load(const std::string& projectManifestPath,
                              const ProjectExtensionLoadOptions& options) {
    if (projectManifestPath.empty() || options.limits.maximumModules == 0U ||
        options.limits.maximumDescriptorsPerModule == 0U ||
        options.limits.maximumExtensionIdBytes == 0U ||
        options.limits.maximumExtensionIdBytes > 128U || options.limits.maximumServices == 0U ||
        options.limits.maximumServices > 4096U || options.limits.maximumServiceIdBytes == 0U ||
        options.limits.maximumServiceIdBytes > 96U ||
        options.limits.maximumServiceDisplayNameBytes == 0U ||
        options.limits.maximumServiceDisplayNameBytes > 512U) {
        return Result<ProjectExtensionModules>::failure(
            Error(ErrorCode::InvalidArgument, "extension module load limits are invalid"));
    }
    std::set<PackageEntryPointKind> accepted;
    for (const auto kind : options.acceptedKinds) {
        if (!accepted.insert(kind).second) {
            return Result<ProjectExtensionModules>::failure(
                Error(ErrorCode::InvalidArgument, "extension entry kind filter is duplicated"));
        }
    }

    auto packages = listLocalPackages(projectManifestPath);
    if (!packages)
        return Result<ProjectExtensionModules>::failure(packages.error());
    auto implementation = std::make_unique<Implementation>(
        options.safeMode, options.extensionsEnabled, options.limits);
    implementation->stats.discoveredPackages =
        static_cast<std::uint32_t>(packages.value().size());

    std::vector<PendingExtension> pending;
    std::set<std::string> modulePaths;
    std::map<std::string, std::vector<std::string>, std::less<>> packageExtensionIds;
    for (const auto& package : packages.value()) {
        const auto packageId = std::string(package.manifest.stableId());
        implementation->trustedDigests.emplace(packageId, package.contentSha256);
        const auto packageRoot = pathFromUtf8(package.directory).lexically_normal();
        for (const auto& entry : package.manifest.entryPoints) {
            ++implementation->stats.discoveredEntries;
            if (!acceptsKind(options, entry.kind)) {
                ++implementation->stats.skippedKinds;
                continue;
            }
            if (options.safeMode || !options.extensionsEnabled) {
                ++implementation->stats.skippedDisabledEntries;
                continue;
            }
            if (!dynamicLibraryPath(entry.path)) {
                ++implementation->stats.skippedSourceEntries;
                continue;
            }
            if (!package.executableTrusted) {
                return Result<ProjectExtensionModules>::failure(
                    Error(ErrorCode::InvalidState,
                          "extension package is not trusted for its exact installed content")
                        .addContext("package", packageId));
            }
            if (implementation->handles.size() >= options.limits.maximumModules) {
                return Result<ProjectExtensionModules>::failure(
                    Error(ErrorCode::CapacityExceeded, "extension module count exceeds its limit"));
            }
            if (!assets::isSafeRelativePath(entry.path)) {
                return Result<ProjectExtensionModules>::failure(
                    Error(ErrorCode::InvalidArgument, "extension entry path is unsafe")
                        .addContext("package", packageId)
                        .addContext("entry", entry.path));
            }
            const auto modulePath = (packageRoot / pathFromUtf8(entry.path)).lexically_normal();
            auto safePath = validateModulePath(packageRoot, modulePath);
            if (!safePath)
                return Result<ProjectExtensionModules>::failure(safePath.error());
            const auto modulePathKey = lowercaseAscii(pathText(modulePath));
            if (!modulePaths.insert(modulePathKey).second) {
                return Result<ProjectExtensionModules>::failure(
                    Error(ErrorCode::AlreadyExists,
                          "extension module path is declared more than once")
                        .addContext("path", pathText(modulePath)));
            }
            auto loaded = openModule(modulePath);
            if (!loaded)
                return Result<ProjectExtensionModules>::failure(loaded.error());
            implementation->handles.emplace_back(loaded.value());
            const auto getModule = entryPoint(loaded.value());
            if (getModule == nullptr) {
                return Result<ProjectExtensionModules>::failure(
                    Error(ErrorCode::InvalidFormat, "extension module entry point is missing")
                        .addContext("path", pathText(modulePath))
                        .addContext("entryPoint", extensions::ExtensionModuleEntryPoint));
            }
            extensions::ExtensionModuleView view;
            if (!getModule(&view) || view.structureSize != sizeof(view) ||
                view.abiVersion != extensions::ExtensionModuleAbiVersion ||
                view.descriptors == nullptr || view.descriptorCount == 0U ||
                view.descriptorCount > options.limits.maximumDescriptorsPerModule) {
                return Result<ProjectExtensionModules>::failure(
                    Error(ErrorCode::UnsupportedVersion,
                          "extension module ABI or descriptor table is invalid")
                        .addContext("path", pathText(modulePath)));
            }
            for (std::size_t index = 0U; index < view.descriptorCount; ++index) {
                const auto& descriptor = view.descriptors[index];
                if (descriptor.structureSize != sizeof(descriptor) || descriptor.create == nullptr ||
                    descriptor.destroy == nullptr) {
                    return Result<ProjectExtensionModules>::failure(
                        Error(ErrorCode::InvalidFormat, "extension module descriptor is invalid")
                            .addContext("path", pathText(modulePath))
                            .addContext("descriptor", std::to_string(index)));
                }
                auto extensionId = boundedCString(descriptor.extensionId,
                                                  options.limits.maximumExtensionIdBytes,
                                                  "extensionId");
                if (!extensionId) {
                    return Result<ProjectExtensionModules>::failure(extensionId.error());
                }
                IExtension* created = descriptor.create();
                if (created == nullptr) {
                    return Result<ProjectExtensionModules>::failure(
                        Error(ErrorCode::InvalidState, "extension factory returned null")
                            .addContext("package", packageId)
                            .addContext("extension", extensionId.value()));
                }
                ExtensionIdentity identity;
                identity.packageId = packageId;
                identity.extensionId = extensionId.value();
                identity.version = package.manifest.version;
                identity.kind = entry.kind;
                identity.contentSha256 = package.contentSha256;
                auto proxy = std::make_unique<LoadedExtensionProxy>(
                    std::move(identity), created, descriptor.destroy);
                const auto qualifiedId = proxy->identity().qualifiedId();
                implementation->extensionKinds.emplace(qualifiedId, entry.kind);
                packageExtensionIds[packageId].push_back(qualifiedId);
                pending.push_back(
                    {packageId, package.manifest.dependencies, std::move(proxy)});
            }
            ++implementation->stats.loadedModules;
        }
    }

    std::map<std::string, std::string, std::less<>> packageAnchors;
    for (auto& [packageId, extensions] : packageExtensionIds) {
        std::sort(extensions.begin(), extensions.end());
        packageAnchors.emplace(packageId, extensions.front());
    }
    for (auto& item : pending) {
        for (const auto& dependency : item.packageDependencies) {
            const auto anchor = packageAnchors.find(dependency.name);
            if (anchor != packageAnchors.end())
                item.extension->addDependency(anchor->second);
        }
        auto added = implementation->registry.add(std::move(item.extension));
        if (!added)
            return Result<ProjectExtensionModules>::failure(added.error());
        ++implementation->stats.registeredExtensions;
    }
    auto activationOrder = implementation->registry.activationOrder();
    if (!activationOrder)
        return Result<ProjectExtensionModules>::failure(activationOrder.error());
    implementation->activationOrder = std::move(activationOrder.value());
    return Result<ProjectExtensionModules>::success(
        ProjectExtensionModules(std::move(implementation)));
}

Result<void> ProjectExtensionModules::activate() {
    if (!implementation_) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "extension module host is not initialized"));
    }
    if (implementation_->active) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "extension modules are already active"));
    }
    auto activated = implementation_->registry.activateAll(implementation_->host);
    if (!activated)
        return activated;
    implementation_->active = true;
    return Result<void>::success();
}

void ProjectExtensionModules::deactivate() noexcept {
    if (implementation_ && implementation_->active) {
        implementation_->services.clear();
        implementation_->servicesRegistered = false;
        implementation_->registry.deactivateAll(implementation_->host);
        implementation_->active = false;
    }
}

Result<std::string>
ProjectExtensionModules::invoke(const std::string_view qualifiedExtensionId,
                                const std::string_view capability,
                                const ExtensionInvocation& invocation) const {
    if (!implementation_ || !implementation_->active) {
        return Result<std::string>::failure(
            Error(ErrorCode::InvalidState, "extension modules are not active"));
    }
    return implementation_->host.invoke(qualifiedExtensionId, capability, invocation);
}

Result<void> ProjectExtensionModules::invokeAll(const std::string_view capability,
                                                const ExtensionInvocation& invocation) const {
    if (!implementation_ || !implementation_->active) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "extension modules are not active"));
    }
    if (!capabilityName(capability)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "extension capability name is invalid"));
    }
    auto validInvocation = validateLifecycleInvocation(capability, invocation);
    if (!validInvocation)
        return validInvocation;
    for (const auto& extensionId : implementation_->activationOrder) {
        if (!implementation_->host.hasHook(extensionId, capability))
            continue;
        auto invoked = implementation_->host.invoke(extensionId, capability, invocation);
        if (!invoked) {
            return Result<void>::failure(
                invoked.error()
                    .withContext("extension", extensionId)
                    .withContext("capability", std::string(capability)));
        }
    }
    return Result<void>::success();
}

Result<void>
ProjectExtensionModules::registerServices(ProjectExtensionHostContext context) {
    if (!implementation_ || !implementation_->active) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "extension modules are not active"));
    }
    if (implementation_->servicesRegistered) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "extension services are already registered"));
    }
    ExtensionInvocation validationInvocation{"register", {}, &context};
    auto validContext =
        validateLifecycleInvocation(ProjectOpenExtensionCapability, validationInvocation);
    if (!validContext)
        return validContext;

    implementation_->services.clear();
    for (const auto& extensionId : implementation_->activationOrder) {
        const auto kind = implementation_->extensionKinds.find(extensionId);
        if (kind == implementation_->extensionKinds.end()) {
            implementation_->services.clear();
            return Result<void>::failure(
                Error(ErrorCode::InternalError, "active extension kind is unavailable")
                    .addContext("extension", extensionId));
        }
        const auto registrationCapability =
            projectExtensionRegistrationCapability(kind->second);
        if (!implementation_->host.hasHook(extensionId, registrationCapability))
            continue;

        Implementation::ServiceRegistrationState state{
            implementation_.get(), extensionId, kind->second, std::nullopt};
        const ProjectExtensionHostServices services{
            sizeof(ProjectExtensionHostServices), ProjectExtensionServiceApiVersion, &state,
            &Implementation::registerService};
        context.services = &services;
        auto invoked = implementation_->host.invoke(
            extensionId, registrationCapability,
            {"register", std::string(packageEntryPointKindName(kind->second)), &context});
        context.services = nullptr;
        if (!invoked || state.failure) {
            Error error = state.failure ? *state.failure : invoked.error();
            implementation_->services.clear();
            return Result<void>::failure(
                error.withContext("extension", extensionId)
                    .withContext("capability", std::string(registrationCapability)));
        }
    }
    std::sort(implementation_->services.begin(), implementation_->services.end(),
              [](const ProjectExtensionServiceInfo& left,
                 const ProjectExtensionServiceInfo& right) {
                  if (left.kind != right.kind)
                      return left.kind < right.kind;
                  if (left.priority != right.priority)
                      return left.priority > right.priority;
                  if (left.extensionId != right.extensionId)
                      return left.extensionId < right.extensionId;
                  return left.serviceId < right.serviceId;
              });
    implementation_->servicesRegistered = true;
    implementation_->stats.registeredServices =
        static_cast<std::uint32_t>(implementation_->services.size());
    return Result<void>::success();
}

Result<std::string>
ProjectExtensionModules::invokeService(const std::string_view qualifiedServiceId,
                                       const ExtensionInvocation& invocation) const {
    if (!implementation_ || !implementation_->active ||
        !implementation_->servicesRegistered) {
        return Result<std::string>::failure(
            Error(ErrorCode::InvalidState, "extension services are not active"));
    }
    if (qualifiedServiceId.empty() || qualifiedServiceId.size() > 225U) {
        return Result<std::string>::failure(
            Error(ErrorCode::InvalidArgument, "qualified extension service ID is invalid"));
    }
    const auto service = std::find_if(
        implementation_->services.cbegin(), implementation_->services.cend(),
        [qualifiedServiceId](const ProjectExtensionServiceInfo& candidate) {
            return candidate.qualifiedId() == qualifiedServiceId;
        });
    if (service == implementation_->services.cend()) {
        return Result<std::string>::failure(
            Error(ErrorCode::NotFound, "extension service was not registered")
                .addContext("service", std::string(qualifiedServiceId)));
    }
    auto invoked = implementation_->host.invoke(service->extensionId,
                                                 service->dispatchCapability, invocation);
    if (!invoked) {
        return Result<std::string>::failure(
            invoked.error()
                .withContext("extension", service->extensionId)
                .withContext("service", service->serviceId));
    }
    return invoked;
}

const ProjectExtensionModuleStats& ProjectExtensionModules::stats() const noexcept {
    static const ProjectExtensionModuleStats empty;
    return implementation_ ? implementation_->stats : empty;
}

const std::vector<std::string>& ProjectExtensionModules::activeExtensionIds() const noexcept {
    static const std::vector<std::string> empty;
    return implementation_ && implementation_->active ? implementation_->activationOrder : empty;
}

const std::vector<ProjectExtensionServiceInfo>&
ProjectExtensionModules::services() const noexcept {
    static const std::vector<ProjectExtensionServiceInfo> empty;
    return implementation_ && implementation_->active ? implementation_->services : empty;
}

bool ProjectExtensionModules::active() const noexcept {
    return implementation_ && implementation_->active;
}

} // namespace fabgl::project
