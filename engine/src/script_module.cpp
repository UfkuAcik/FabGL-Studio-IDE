#include "fabgl/scripting/script_module.h"

#include <algorithm>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace fabgl::scripting::detail {

namespace {

struct OwnedDescriptor final {
    std::string typeName;
    ComponentTypeGuid typeId;
    ScriptComponentFactory create = nullptr;
};

std::mutex& registryMutex() {
    static std::mutex mutex;
    return mutex;
}

std::deque<OwnedDescriptor>& registry() {
    static std::deque<OwnedDescriptor> descriptors;
    return descriptors;
}

std::vector<ScriptModuleDescriptor>& exportedRegistry() {
    static std::vector<ScriptModuleDescriptor> descriptors;
    return descriptors;
}

} // namespace

bool registerScriptFactory(const std::string_view typeName, const ComponentTypeGuid typeId,
                           const ScriptComponentFactory factory) noexcept {
    if (typeName.empty() || typeName.size() > 255U || typeId.isNil() || factory == nullptr) {
        return false;
    }
    try {
        std::scoped_lock lock(registryMutex());
        auto& descriptors = registry();
        if (std::any_of(descriptors.begin(), descriptors.end(), [&](const auto& descriptor) {
                return descriptor.typeName == typeName || descriptor.typeId == typeId;
            })) {
            return false;
        }
        descriptors.push_back({std::string(typeName), typeId, factory});
        return true;
    } catch (...) {
        return false;
    }
}

bool exportRegisteredScriptModule(ScriptModuleView* output) noexcept {
    if (output == nullptr || output->structureSize != sizeof(ScriptModuleView) ||
        output->abiVersion != ScriptModuleAbiVersion) {
        return false;
    }
    try {
        std::scoped_lock lock(registryMutex());
        auto& exported = exportedRegistry();
        exported.clear();
        exported.reserve(registry().size());
        for (const auto& descriptor : registry()) {
            exported.push_back({sizeof(ScriptModuleDescriptor), descriptor.typeName.c_str(),
                                descriptor.typeId, descriptor.create});
        }
        output->descriptors = exported.empty() ? nullptr : exported.data();
        output->descriptorCount = exported.size();
        return !exported.empty();
    } catch (...) {
        output->descriptors = nullptr;
        output->descriptorCount = 0U;
        return false;
    }
}

} // namespace fabgl::scripting::detail
