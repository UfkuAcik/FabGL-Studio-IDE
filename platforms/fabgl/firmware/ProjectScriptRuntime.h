#pragma once

#include "ProjectRuntime.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace fabgl_project_scripts {

static constexpr std::uint32_t kAbiVersion = 1U;
static constexpr std::size_t kMaximumScripts = 16U;
static constexpr std::size_t kMaximumScriptNameBytes = 47U;

using StartCallback = void (*)(fabgl_project_runtime::RuntimeProject&) noexcept;
using UpdateCallback = void (*)(fabgl_project_runtime::RuntimeProject&,
                                float deltaSeconds) noexcept;

struct Descriptor final {
    std::uint32_t structureSize = sizeof(Descriptor);
    const char* name = nullptr;
    StartCallback onStart = nullptr;
    UpdateCallback onUpdate = nullptr;

    Descriptor() noexcept = default;
    constexpr Descriptor(std::uint32_t descriptorSize, const char* descriptorName,
                         StartCallback startCallback, UpdateCallback updateCallback) noexcept
        : structureSize(descriptorSize), name(descriptorName), onStart(startCallback),
          onUpdate(updateCallback) {}
};

struct ModuleView final {
    std::uint32_t structureSize = sizeof(ModuleView);
    std::uint32_t abiVersion = kAbiVersion;
    const Descriptor* descriptors = nullptr;
    std::size_t descriptorCount = 0U;
};

using GetModuleFunction = bool (*)(ModuleView*) noexcept;

enum class Error : std::uint8_t {
    None,
    MissingEntryPoint,
    InvalidAbi,
    InvalidDescriptor,
    DuplicateName,
};

struct Binding final {
    fabgl_project_runtime::Text<48> name;
    StartCallback onStart = nullptr;
    UpdateCallback onUpdate = nullptr;
};

class Runtime final {
  public:
    bool initialize(GetModuleFunction getModule, fabgl_project_runtime::RuntimeProject& project,
                    bool moduleRequired = false) noexcept {
        count_ = 0U;
        updateCount_ = 0U;
        error_ = Error::None;
        initialized_ = false;
        if (getModule == nullptr) {
            if (moduleRequired) {
                error_ = Error::MissingEntryPoint;
                return false;
            }
            initialized_ = true;
            return true;
        }
        ModuleView view;
        if (!getModule(&view) || view.structureSize != sizeof(ModuleView) ||
            view.abiVersion != kAbiVersion || view.descriptors == nullptr ||
            view.descriptorCount == 0U || view.descriptorCount > kMaximumScripts) {
            error_ = Error::InvalidAbi;
            return false;
        }
        for (std::size_t index = 0U; index < view.descriptorCount; ++index) {
            const auto& source = view.descriptors[index];
            if (source.structureSize != sizeof(Descriptor) || source.name == nullptr ||
                (source.onStart == nullptr && source.onUpdate == nullptr)) {
                error_ = Error::InvalidDescriptor;
                return false;
            }
            Binding binding;
            std::size_t length = 0U;
            while (length <= kMaximumScriptNameBytes && source.name[length] != '\0') {
                if (static_cast<unsigned char>(source.name[length]) < 0x20U ||
                    !binding.name.push(source.name[length])) {
                    error_ = Error::InvalidDescriptor;
                    return false;
                }
                ++length;
            }
            if (length == 0U || length > kMaximumScriptNameBytes) {
                error_ = Error::InvalidDescriptor;
                return false;
            }
            for (std::size_t previous = 0U; previous < count_; ++previous) {
                if (std::strcmp(bindings_[previous].name.value, binding.name.value) == 0) {
                    error_ = Error::DuplicateName;
                    return false;
                }
            }
            binding.onStart = source.onStart;
            binding.onUpdate = source.onUpdate;
            bindings_[count_++] = binding;
        }
        initialized_ = true;
        for (std::size_t index = 0U; index < count_; ++index) {
            if (bindings_[index].onStart != nullptr)
                bindings_[index].onStart(project);
        }
        return true;
    }

    void update(fabgl_project_runtime::RuntimeProject& project,
                float requestedDeltaSeconds) noexcept {
        if (!initialized_)
            return;
        const auto deltaSeconds = std::max(0.0F, std::min(0.1F, requestedDeltaSeconds));
        for (std::size_t index = 0U; index < count_; ++index) {
            if (bindings_[index].onUpdate != nullptr)
                bindings_[index].onUpdate(project, deltaSeconds);
        }
        ++updateCount_;
    }

    [[nodiscard]] bool initialized() const noexcept {
        return initialized_;
    }
    [[nodiscard]] std::size_t count() const noexcept {
        return count_;
    }
    [[nodiscard]] std::uint32_t updateCount() const noexcept {
        return updateCount_;
    }
    [[nodiscard]] Error error() const noexcept {
        return error_;
    }

  private:
    Binding bindings_[kMaximumScripts]{};
    std::size_t count_ = 0U;
    std::uint32_t updateCount_ = 0U;
    Error error_ = Error::None;
    bool initialized_ = false;
};

} // namespace fabgl_project_scripts

#define FGL_ESP32_SCRIPT_MODULE(descriptorArray)                                                   \
    extern "C" bool fabglProjectGetEsp32ScriptsV1(                                                 \
        ::fabgl_project_scripts::ModuleView* output) noexcept {                                    \
        if (output == nullptr)                                                                     \
            return false;                                                                          \
        output->structureSize = sizeof(*output);                                                   \
        output->abiVersion = ::fabgl_project_scripts::kAbiVersion;                                 \
        output->descriptors = (descriptorArray);                                                   \
        output->descriptorCount = sizeof(descriptorArray) / sizeof((descriptorArray)[0]);          \
        return true;                                                                               \
    }
