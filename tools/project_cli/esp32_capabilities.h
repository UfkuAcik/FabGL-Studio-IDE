#pragma once

#include "project_format.h"

#include <fabgl/core/result.h>

#include <cstddef>
#include <string_view>

namespace fabgl {

class Scene;

namespace project {

// These limits mirror the allocation-free arrays in
// platforms/fabgl/firmware/ProjectRuntime.h. Keeping the target contract in
// the host tool prevents projects that can only fail after flashing.
inline constexpr std::string_view Esp32RuntimeProfileId =
    "olimex-esp32-sbc-fabgl-revb";
inline constexpr std::size_t Esp32RuntimeMaximumEntities = 48U;
inline constexpr std::size_t Esp32RuntimeMaximumAssets = 64U;
inline constexpr std::size_t Esp32RuntimeMaximumComponentsPerEntity = 64U;
inline constexpr std::size_t Esp32RuntimeMaximumInputValues = 48U;
inline constexpr std::size_t Esp32RuntimeMaximumInputBindings = 128U;

enum class Esp32ComponentCapability {
    Supported,
    KnownButNotPorted,
    Unknown,
};

enum class Esp32AssetCapability {
    Supported,
    VisualScriptVmUnavailable,
    Unsupported,
};

struct Esp32CapabilitySummary final {
    std::size_t entityCount = 0U;
    std::size_t componentCount = 0U;
    std::size_t assetCount = 0U;
    std::size_t inputValueCount = 0U;
    std::size_t inputBindingCount = 0U;
};

[[nodiscard]] Esp32ComponentCapability
classifyEsp32RuntimeComponent(std::string_view componentName);
[[nodiscard]] Esp32AssetCapability
classifyEsp32RuntimeAsset(std::string_view assetType, std::string_view assetPath) noexcept;

// Validates only the semantics implemented by ProjectRuntime.h/firmware.ino.
// It returns the first issue in manifest/scene order, so diagnostics are
// deterministic, and performs no issue accumulation with target-sized input.
[[nodiscard]] Result<Esp32CapabilitySummary>
validateEsp32TargetCapabilities(const Manifest& manifest, const Scene& startupScene);

} // namespace project
} // namespace fabgl
