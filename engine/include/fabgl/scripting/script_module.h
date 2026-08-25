#pragma once

#include "fabgl/scene/component.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#if defined(_WIN32)
#define FGL_SCRIPT_MODULE_EXPORT extern "C" __declspec(dllexport)
#elif defined(__GNUC__)
#define FGL_SCRIPT_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#else
#define FGL_SCRIPT_MODULE_EXPORT extern "C"
#endif

namespace fabgl::scripting {

inline constexpr std::uint32_t ScriptModuleAbiVersion = 1U;
inline constexpr const char* ScriptModuleEntryPoint = "fabglStudioGetScriptModuleV1";

using ScriptComponentFactory = Component* (*)() noexcept;

struct ScriptModuleDescriptor final {
    std::uint32_t structureSize = sizeof(ScriptModuleDescriptor);
    const char* typeName = nullptr;
    ComponentTypeGuid typeId;
    ScriptComponentFactory create = nullptr;
};

struct ScriptModuleView final {
    std::uint32_t structureSize = sizeof(ScriptModuleView);
    std::uint32_t abiVersion = ScriptModuleAbiVersion;
    const ScriptModuleDescriptor* descriptors = nullptr;
    std::size_t descriptorCount = 0U;
};

using GetScriptModuleFunction = bool (*)(ScriptModuleView*) noexcept;

namespace detail {

[[nodiscard]] bool registerScriptFactory(std::string_view typeName, ComponentTypeGuid typeId,
                                         ScriptComponentFactory factory) noexcept;
[[nodiscard]] bool exportRegisteredScriptModule(ScriptModuleView* output) noexcept;

template <typename Script> [[nodiscard]] Component* createRegisteredScript() noexcept {
    static_assert(std::is_base_of_v<Component, Script>);
    static_assert(std::is_default_constructible_v<Script>);
    try {
        return new Script();
    } catch (...) {
        return nullptr;
    }
}

template <typename Script> class ScriptAutoRegistrar final {
  public:
    ScriptAutoRegistrar() noexcept {
        static_assert(std::is_base_of_v<Component, Script>);
        static_assert(std::is_default_constructible_v<Script>);
        try {
            Script probe;
            registered_ = registerScriptFactory(probe.typeName(), probe.typeId(),
                                                &createRegisteredScript<Script>);
        } catch (...) {
            registered_ = false;
        }
    }

    [[nodiscard]] bool registered() const noexcept {
        return registered_;
    }

  private:
    bool registered_ = false;
};

} // namespace detail

} // namespace fabgl::scripting

#define FGL_SCRIPT_DETAIL_JOIN_INNER(left, right) left##right
#define FGL_SCRIPT_DETAIL_JOIN(left, right) FGL_SCRIPT_DETAIL_JOIN_INNER(left, right)
#define FABGL_REGISTER_SCRIPT(type)                                                            \
    namespace {                                                                                \
    [[maybe_unused]] const ::fabgl::scripting::detail::ScriptAutoRegistrar<type>               \
        FGL_SCRIPT_DETAIL_JOIN(fabglScriptRegistrar_, __COUNTER__);                            \
    }
