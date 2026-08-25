#pragma once

#include "fabgl/extensions/extension_registry.h"

#include <cstddef>
#include <cstdint>

namespace fabgl::extensions {

inline constexpr std::uint32_t ExtensionModuleAbiVersion = 1U;
inline constexpr const char* ExtensionModuleEntryPoint = "fabglStudioGetExtensionModuleV1";

using ExtensionFactory = IExtension* (*)() noexcept;
using ExtensionDestroyer = void (*)(IExtension*) noexcept;

struct ExtensionModuleDescriptor final {
    std::uint32_t structureSize = sizeof(ExtensionModuleDescriptor);
    const char* extensionId = nullptr;
    ExtensionFactory create = nullptr;
    ExtensionDestroyer destroy = nullptr;
};

struct ExtensionModuleView final {
    std::uint32_t structureSize = sizeof(ExtensionModuleView);
    std::uint32_t abiVersion = ExtensionModuleAbiVersion;
    const ExtensionModuleDescriptor* descriptors = nullptr;
    std::size_t descriptorCount = 0U;
};

using GetExtensionModuleFunction = bool (*)(ExtensionModuleView*) noexcept;

} // namespace fabgl::extensions

#if defined(_WIN32)
#define FGL_EXTENSION_MODULE_EXPORT extern "C" __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define FGL_EXTENSION_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#else
#define FGL_EXTENSION_MODULE_EXPORT extern "C"
#endif
