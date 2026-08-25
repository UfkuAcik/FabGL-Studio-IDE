#include <fabgl/project/project_script_modules.h>

#include <fabgl/scene/builtin_components.h>
#include <fabgl/scene/entity.h>
#include <fabgl/scene/scene.h>
#include <fabgl/scripting/script_component.h>
#include <fabgl/scripting/script_module.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace fabgl::project {

namespace {

#if defined(_WIN32)
using ModuleHandle = HMODULE;

[[nodiscard]] Result<std::wstring> utf8ToWide(const std::string& value) {
    if (value.empty() || value.find('\0') != std::string::npos) {
        return Result<std::wstring>::failure(
            Error(ErrorCode::InvalidArgument, "script module path is empty or contains NUL"));
    }
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) {
        return Result<std::wstring>::failure(
            Error(ErrorCode::InvalidArgument, "script module path is not valid UTF-8"));
    }
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), count);
    return Result<std::wstring>::success(std::move(result));
}

[[nodiscard]] Result<ModuleHandle> openModule(const std::string& path) {
    auto wide = utf8ToWide(path);
    if (!wide) {
        return Result<ModuleHandle>::failure(wide.error());
    }
    const auto required = GetFullPathNameW(wide.value().c_str(), 0U, nullptr, nullptr);
    if (required == 0U || required > 32'768U) {
        return Result<ModuleHandle>::failure(
            Error(ErrorCode::InvalidArgument, "script module path cannot be resolved")
                .addContext("path", path));
    }
    std::wstring absolute(static_cast<std::size_t>(required), L'\0');
    const auto written = GetFullPathNameW(wide.value().c_str(), required, absolute.data(), nullptr);
    if (written == 0U || written >= required) {
        return Result<ModuleHandle>::failure(
            Error(ErrorCode::InvalidArgument, "script module path cannot be resolved")
                .addContext("path", path));
    }
    absolute.resize(static_cast<std::size_t>(written));
    const auto attributes = GetFileAttributesW(absolute.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0U) {
        return Result<ModuleHandle>::failure(
            Error(ErrorCode::InvalidArgument,
                  "script module must be a regular non-reparse file")
                .addContext("path", path));
    }
    const auto handle = LoadLibraryExW(absolute.c_str(), nullptr,
                                       LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                           LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (handle == nullptr) {
        return Result<ModuleHandle>::failure(
            Error(ErrorCode::IoError, "unable to load native gameplay module")
                .addContext("path", path)
                .addContext("win32", std::to_string(GetLastError())));
    }
    return Result<ModuleHandle>::success(handle);
}

[[nodiscard]] scripting::GetScriptModuleFunction entryPoint(const ModuleHandle handle) noexcept {
    const auto address = GetProcAddress(handle, scripting::ScriptModuleEntryPoint);
    static_assert(sizeof(address) == sizeof(scripting::GetScriptModuleFunction));
    return std::bit_cast<scripting::GetScriptModuleFunction>(address);
}

void closeModule(const ModuleHandle handle) noexcept {
    if (handle != nullptr) {
        FreeLibrary(handle);
    }
}
#else
using ModuleHandle = void*;

[[nodiscard]] Result<ModuleHandle> openModule(const std::string& path) {
    if (path.empty() || path.find('\0') != std::string::npos) {
        return Result<ModuleHandle>::failure(
            Error(ErrorCode::InvalidArgument, "script module path is empty or contains NUL"));
    }
    dlerror();
    const auto handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        const auto* message = dlerror();
        return Result<ModuleHandle>::failure(
            Error(ErrorCode::IoError, "unable to load native gameplay module")
                .addContext("path", path)
                .addContext("loader", message == nullptr ? "unknown" : message));
    }
    return Result<ModuleHandle>::success(handle);
}

[[nodiscard]] scripting::GetScriptModuleFunction entryPoint(const ModuleHandle handle) noexcept {
    dlerror();
    const auto address = dlsym(handle, scripting::ScriptModuleEntryPoint);
    static_assert(sizeof(address) == sizeof(scripting::GetScriptModuleFunction));
    return std::bit_cast<scripting::GetScriptModuleFunction>(address);
}

void closeModule(const ModuleHandle handle) noexcept {
    if (handle != nullptr) {
        dlclose(handle);
    }
}
#endif

struct OwnedDescriptor final {
    std::string typeName;
    ComponentTypeGuid typeId;
    scripting::ScriptComponentFactory create = nullptr;
};

} // namespace

struct ProjectScriptModules::Implementation final {
    ~Implementation() {
        for (auto iterator = handles.rbegin(); iterator != handles.rend(); ++iterator) {
            closeModule(*iterator);
        }
    }

    std::vector<ModuleHandle> handles;
    std::map<std::string, OwnedDescriptor, std::less<>> descriptors;
    ProjectScriptModuleStats stats;
};

ProjectScriptModules::ProjectScriptModules() : implementation_(std::make_unique<Implementation>()) {}
ProjectScriptModules::~ProjectScriptModules() = default;
ProjectScriptModules::ProjectScriptModules(ProjectScriptModules&&) noexcept = default;
ProjectScriptModules& ProjectScriptModules::operator=(ProjectScriptModules&&) noexcept = default;

ProjectScriptModules::ProjectScriptModules(std::unique_ptr<Implementation> implementation)
    : implementation_(std::move(implementation)) {}

Result<ProjectScriptModules>
ProjectScriptModules::load(const std::vector<std::string>& modulePaths,
                           const ProjectScriptModuleLimits& limits) {
    if (modulePaths.empty() || modulePaths.size() > limits.maximumModules ||
        limits.maximumDescriptorsPerModule == 0U || limits.maximumTypeNameBytes == 0U) {
        return Result<ProjectScriptModules>::failure(
            Error(ErrorCode::InvalidArgument, "native gameplay module request exceeds limits"));
    }
    auto implementation = std::make_unique<Implementation>();
    for (const auto& path : modulePaths) {
        auto loaded = openModule(path);
        if (!loaded) {
            return Result<ProjectScriptModules>::failure(loaded.error());
        }
        const auto handle = loaded.value();
        implementation->handles.push_back(handle);
        const auto getModule = entryPoint(handle);
        if (getModule == nullptr) {
            return Result<ProjectScriptModules>::failure(
                Error(ErrorCode::InvalidFormat, "gameplay module entry point is missing")
                    .addContext("path", path)
                    .addContext("entryPoint", scripting::ScriptModuleEntryPoint));
        }
        scripting::ScriptModuleView view;
        if (!getModule(&view) || view.structureSize != sizeof(scripting::ScriptModuleView) ||
            view.abiVersion != scripting::ScriptModuleAbiVersion || view.descriptors == nullptr ||
            view.descriptorCount == 0U ||
            view.descriptorCount > limits.maximumDescriptorsPerModule) {
            return Result<ProjectScriptModules>::failure(
                Error(ErrorCode::UnsupportedVersion, "gameplay module ABI or descriptor table is invalid")
                    .addContext("path", path));
        }
        for (std::size_t index = 0U; index < view.descriptorCount; ++index) {
            const auto& descriptor = view.descriptors[index];
            if (descriptor.structureSize != sizeof(scripting::ScriptModuleDescriptor) ||
                descriptor.typeName == nullptr || descriptor.typeId.isNil() ||
                descriptor.create == nullptr) {
                return Result<ProjectScriptModules>::failure(
                    Error(ErrorCode::InvalidFormat, "gameplay module descriptor is invalid")
                        .addContext("path", path)
                        .addContext("descriptor", std::to_string(index)));
            }
            const std::string typeName(descriptor.typeName);
            if (typeName.empty() || typeName.size() > limits.maximumTypeNameBytes ||
                typeName.find('\0') != std::string::npos) {
                return Result<ProjectScriptModules>::failure(
                    Error(ErrorCode::InvalidFormat, "gameplay module type name is invalid")
                        .addContext("path", path));
            }
            const auto inserted = implementation->descriptors.emplace(
                typeName, OwnedDescriptor{typeName, descriptor.typeId, descriptor.create});
            if (!inserted.second) {
                return Result<ProjectScriptModules>::failure(
                    Error(ErrorCode::AlreadyExists, "duplicate gameplay script type")
                        .addContext("type", typeName));
            }
        }
        ++implementation->stats.loadedModules;
    }
    implementation->stats.registeredTypes =
        static_cast<std::uint32_t>(implementation->descriptors.size());
    return Result<ProjectScriptModules>::success(
        ProjectScriptModules(std::move(implementation)));
}

Result<std::uint32_t> ProjectScriptModules::attach(Scene& scene) {
    struct Pending final {
        Entity* entity = nullptr;
        ComponentTypeGuid placeholderType;
        std::string typeName;
        bool enabled = true;
    };
    std::vector<Pending> pending;
    for (auto* entity : scene.entities()) {
        for (auto* component : entity->components()) {
            auto* data = dynamic_cast<DataComponent*>(component);
            if (data == nullptr || data->typeName() != "fabgl.ScriptComponent") {
                continue;
            }
            auto classValue = data->get("class");
            if (!classValue) {
                return Result<std::uint32_t>::failure(classValue.error());
            }
            const auto* typeName = std::get_if<std::string>(&classValue.value());
            if (typeName == nullptr || typeName->empty()) {
                return Result<std::uint32_t>::failure(
                    Error(ErrorCode::InvalidFormat, "ScriptComponent class is empty")
                        .addContext("entity", entity->id().toString()));
            }
            pending.push_back({entity, data->typeId(), *typeName, data->enabled()});
        }
    }

    for (const auto& item : pending) {
        const auto found = implementation_->descriptors.find(item.typeName);
        if (found == implementation_->descriptors.end()) {
            return Result<std::uint32_t>::failure(
                Error(ErrorCode::NotFound, "gameplay script class is not registered")
                    .addContext("entity", item.entity->id().toString())
                    .addContext("class", item.typeName));
        }
        if (item.entity->getComponent(found->second.typeId) != nullptr) {
            return Result<std::uint32_t>::failure(
                Error(ErrorCode::AlreadyExists, "entity already has gameplay script type")
                    .addContext("entity", item.entity->id().toString())
                    .addContext("class", item.typeName));
        }
        std::unique_ptr<Component> component(found->second.create());
        auto* script = dynamic_cast<scripting::ScriptComponent*>(component.get());
        if (script == nullptr || script->typeName() != item.typeName ||
            script->typeId() != found->second.typeId || !script->apiCompatible()) {
            return Result<std::uint32_t>::failure(
                Error(ErrorCode::TypeMismatch, "gameplay script factory violated its descriptor")
                    .addContext("class", item.typeName));
        }
        component->setEnabled(item.enabled);
        auto removed = item.entity->removeComponent(item.placeholderType);
        if (!removed) {
            return Result<std::uint32_t>::failure(removed.error());
        }
        auto attached = item.entity->addComponent(std::move(component));
        if (!attached) {
            return Result<std::uint32_t>::failure(attached.error());
        }
        ++implementation_->stats.attachedComponents;
    }
    return Result<std::uint32_t>::success(static_cast<std::uint32_t>(pending.size()));
}

const ProjectScriptModuleStats& ProjectScriptModules::stats() const noexcept {
    return implementation_->stats;
}

} // namespace fabgl::project
