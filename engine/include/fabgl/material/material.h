#pragma once

#include <fabgl/core/guid.h>
#include <fabgl/math/types.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fabgl {

enum class RendererBackend : std::uint8_t {
    Renderer2D = 0,
    Raycast,
    Racer,
    LowPoly,
};

enum class RendererCompatibility : std::uint32_t {
    None = 0U,
    Renderer2D = 1U << 0U,
    Raycast = 1U << 1U,
    Racer = 1U << 2U,
    LowPoly = 1U << 3U,
    All = (1U << 0U) | (1U << 1U) | (1U << 2U) | (1U << 3U),
};

[[nodiscard]] constexpr RendererCompatibility operator|(RendererCompatibility lhs,
                                                        RendererCompatibility rhs) noexcept {
    return static_cast<RendererCompatibility>(static_cast<std::uint32_t>(lhs) |
                                              static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr RendererCompatibility operator&(RendererCompatibility lhs,
                                                        RendererCompatibility rhs) noexcept {
    return static_cast<RendererCompatibility>(static_cast<std::uint32_t>(lhs) &
                                              static_cast<std::uint32_t>(rhs));
}

enum class MaterialColorMode : std::uint8_t {
    Texture,
    Flat,
    Vertex,
};

enum class MaterialDitherMode : std::uint8_t {
    None,
    Ordered2x2,
    Ordered4x4,
};

enum class MaterialSamplingMode : std::uint8_t {
    Nearest,
    Bilinear,
};

enum class MaterialLightingMode : std::uint8_t {
    Unlit,
    Flat,
    Vertex,
};

enum class MaterialBlendMode : std::uint8_t {
    Opaque,
    Alpha,
    Additive,
    Multiply,
};

struct Material final {
    std::optional<AssetGuid> baseTexture;
    std::optional<AssetGuid> paletteAsset;
    std::vector<Color> palette;
    std::optional<std::uint8_t> transparentIndex;

    Color tint{255U, 255U, 255U, 255U};
    Color flatColor{255U, 255U, 255U, 255U};
    Color emissive{0U, 0U, 0U, 255U};
    std::uint8_t emissiveStrength = 0U;

    MaterialColorMode colorMode = MaterialColorMode::Texture;
    MaterialDitherMode dither = MaterialDitherMode::None;
    MaterialSamplingMode sampling = MaterialSamplingMode::Nearest;
    MaterialLightingMode lighting = MaterialLightingMode::Unlit;
    MaterialBlendMode blend = MaterialBlendMode::Opaque;

    bool participatesInFog = true;
    bool billboard = false;
    bool doubleSided = false;
    RendererCompatibility compatibleRenderers = RendererCompatibility::All;
};

enum class MaterialIssueSeverity : std::uint8_t {
    Warning,
    Error,
};

struct MaterialValidationIssue final {
    MaterialIssueSeverity severity = MaterialIssueSeverity::Warning;
    std::string code;
    std::string message;
};

struct MaterialValidationReport final {
    std::vector<MaterialValidationIssue> issues;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool hasWarnings() const noexcept;
};

[[nodiscard]] constexpr RendererCompatibility rendererCompatibilityBit(
    RendererBackend renderer) noexcept {
    switch (renderer) {
    case RendererBackend::Renderer2D:
        return RendererCompatibility::Renderer2D;
    case RendererBackend::Raycast:
        return RendererCompatibility::Raycast;
    case RendererBackend::Racer:
        return RendererCompatibility::Racer;
    case RendererBackend::LowPoly:
        return RendererCompatibility::LowPoly;
    }
    return RendererCompatibility::None;
}

[[nodiscard]] MaterialValidationReport validateMaterial(const Material& material,
                                                        RendererBackend renderer);

struct MaterialCostContext final {
    RendererBackend renderer = RendererBackend::Renderer2D;
    std::uint32_t textureWidth = 0U;
    std::uint32_t textureHeight = 0U;
    std::uint8_t sourceBytesPerPixel = 4U;
    bool indexedTexture = false;
    std::uint32_t vertexCount = 0U;
};

struct MaterialCostEstimate final {
    std::size_t persistentRamBytes = 0U;
    std::size_t flashBytes = 0U;
    std::uint32_t operationsPerPixel = 0U;
};

// This is deliberately a deterministic upper-bound model, not a platform timer. It lets the
// editor compare configurations without depending on the host CPU or ESP32 clock state.
[[nodiscard]] MaterialCostEstimate estimateMaterialCost(const Material& material,
                                                        const MaterialCostContext& context) noexcept;

} // namespace fabgl
