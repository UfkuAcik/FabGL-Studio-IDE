#include <fabgl/material/material.h>

#include <algorithm>
#include <limits>

namespace fabgl {

namespace {

void addIssue(MaterialValidationReport& report, MaterialIssueSeverity severity, const char* code,
              const char* message) {
    report.issues.push_back({severity, code, message});
}

[[nodiscard]] std::size_t saturatingMultiply(std::size_t lhs, std::size_t rhs) noexcept {
    if (rhs != 0U && lhs > std::numeric_limits<std::size_t>::max() / rhs) {
        return std::numeric_limits<std::size_t>::max();
    }
    return lhs * rhs;
}

[[nodiscard]] std::size_t saturatingAdd(std::size_t lhs, std::size_t rhs) noexcept {
    if (lhs > std::numeric_limits<std::size_t>::max() - rhs) {
        return std::numeric_limits<std::size_t>::max();
    }
    return lhs + rhs;
}

} // namespace

bool MaterialValidationReport::valid() const noexcept {
    return std::none_of(issues.begin(), issues.end(), [](const MaterialValidationIssue& issue) {
        return issue.severity == MaterialIssueSeverity::Error;
    });
}

bool MaterialValidationReport::hasWarnings() const noexcept {
    return std::any_of(issues.begin(), issues.end(), [](const MaterialValidationIssue& issue) {
        return issue.severity == MaterialIssueSeverity::Warning;
    });
}

MaterialValidationReport validateMaterial(const Material& material, RendererBackend renderer) {
    MaterialValidationReport report;
    if ((material.compatibleRenderers & rendererCompatibilityBit(renderer)) ==
        RendererCompatibility::None) {
        addIssue(report, MaterialIssueSeverity::Error, "renderer.incompatible",
                 "The material compatibility mask excludes the selected renderer.");
    }
    if (material.paletteAsset.has_value() && !material.palette.empty()) {
        addIssue(report, MaterialIssueSeverity::Error, "palette.ambiguous-source",
                 "Use either a palette asset reference or embedded palette data, not both.");
    }
    if (material.palette.size() > 256U) {
        addIssue(report, MaterialIssueSeverity::Error, "palette.too-large",
                 "Indexed materials support at most 256 palette entries.");
    }
    if (material.transparentIndex.has_value()) {
        if (!material.palette.empty() && *material.transparentIndex >= material.palette.size()) {
            addIssue(report, MaterialIssueSeverity::Error, "palette.transparent-index-range",
                     "The transparent index is outside the embedded palette.");
        } else if (material.palette.empty() && !material.paletteAsset.has_value()) {
            addIssue(report, MaterialIssueSeverity::Warning, "palette.transparent-index-unresolved",
                     "The transparent index cannot be checked until a palette is assigned.");
        }
    }
    if (material.colorMode == MaterialColorMode::Texture && !material.baseTexture.has_value() &&
        material.palette.empty() && !material.paletteAsset.has_value()) {
        addIssue(report, MaterialIssueSeverity::Warning, "texture.unassigned",
                 "Texture color mode has no texture or indexed palette source assigned.");
    }
    if (material.sampling == MaterialSamplingMode::Bilinear) {
        if (!material.palette.empty() || material.paletteAsset.has_value()) {
            addIssue(report, MaterialIssueSeverity::Warning, "sampling.indexed-bilinear",
                     "Indexed textures are sampled as RGBA before bilinear interpolation.");
        }
        if (renderer == RendererBackend::Raycast || renderer == RendererBackend::Racer) {
            addIssue(report, MaterialIssueSeverity::Warning, "sampling.bilinear-fallback",
                     "This renderer falls back to nearest sampling for its constrained hot path.");
        }
    }
    if (material.colorMode == MaterialColorMode::Vertex &&
        renderer != RendererBackend::LowPoly) {
        addIssue(report, MaterialIssueSeverity::Error, "color.vertex-unsupported",
                 "Vertex colors are only supported by the low-poly renderer.");
    }
    if (material.lighting == MaterialLightingMode::Vertex &&
        renderer != RendererBackend::LowPoly) {
        addIssue(report, MaterialIssueSeverity::Error, "lighting.vertex-unsupported",
                 "Vertex lighting is only supported by the low-poly renderer.");
    }
    if (material.billboard && renderer == RendererBackend::Racer) {
        addIssue(report, MaterialIssueSeverity::Warning, "billboard.racer-ignored",
                 "The racer renderer ignores the billboard flag for track surfaces.");
    }
    if (material.doubleSided && renderer != RendererBackend::LowPoly) {
        addIssue(report, MaterialIssueSeverity::Warning, "sidedness.not-applicable",
                 "Double-sided culling only changes low-poly triangle rendering.");
    }
    if (material.participatesInFog && renderer == RendererBackend::Renderer2D) {
        addIssue(report, MaterialIssueSeverity::Warning, "fog.no-depth",
                 "Renderer2D has no depth fog; the participation flag is ignored.");
    }
    if (material.blend != MaterialBlendMode::Opaque && renderer == RendererBackend::LowPoly) {
        addIssue(report, MaterialIssueSeverity::Warning, "blend.lowpoly-ordering",
                 "Low-poly transparency is triangle-sorted and may show intersecting-surface artifacts.");
    }
    return report;
}

MaterialCostEstimate estimateMaterialCost(const Material& material,
                                          const MaterialCostContext& context) noexcept {
    MaterialCostEstimate estimate;
    const auto pixelCount = saturatingMultiply(static_cast<std::size_t>(context.textureWidth),
                                               static_cast<std::size_t>(context.textureHeight));
    const auto sourceBytes = context.indexedTexture ? 1U : std::max<std::uint8_t>(1U, context.sourceBytesPerPixel);
    const auto textureBytes = saturatingMultiply(pixelCount, sourceBytes);
    const auto embeddedPaletteBytes = saturatingMultiply(material.palette.size(), sizeof(Color));

    // 48 bytes is the packed runtime state used by the embedded material instance. Asset GUIDs
    // and editor strings are intentionally excluded from RAM because they stay in the manifest.
    estimate.persistentRamBytes = saturatingAdd(48U, embeddedPaletteBytes);
    if (material.baseTexture.has_value() || context.textureWidth != 0U ||
        context.textureHeight != 0U) {
        estimate.persistentRamBytes = saturatingAdd(estimate.persistentRamBytes, textureBytes);
    }
    if (material.colorMode == MaterialColorMode::Vertex) {
        estimate.persistentRamBytes = saturatingAdd(
            estimate.persistentRamBytes,
            saturatingMultiply(static_cast<std::size_t>(context.vertexCount), sizeof(Color)));
    }

    estimate.flashBytes = saturatingAdd(32U, embeddedPaletteBytes);
    estimate.flashBytes = saturatingAdd(estimate.flashBytes, textureBytes);
    if (material.colorMode == MaterialColorMode::Vertex) {
        estimate.flashBytes = saturatingAdd(
            estimate.flashBytes,
            saturatingMultiply(static_cast<std::size_t>(context.vertexCount), sizeof(Color)));
    }

    std::uint32_t operations = 1U;
    if (material.colorMode == MaterialColorMode::Texture) {
        operations += material.sampling == MaterialSamplingMode::Bilinear ? 15U : 2U;
    } else if (material.colorMode == MaterialColorMode::Vertex) {
        operations += 5U;
    }
    if (material.tint != Color{255U, 255U, 255U, 255U}) {
        operations += 4U;
    }
    if (material.dither != MaterialDitherMode::None) {
        operations += 3U;
    }
    if (material.lighting == MaterialLightingMode::Flat) {
        operations += 7U;
    } else if (material.lighting == MaterialLightingMode::Vertex) {
        operations += 12U;
    }
    if (material.emissiveStrength != 0U) {
        operations += 4U;
    }
    if (material.participatesInFog && context.renderer != RendererBackend::Renderer2D) {
        operations += 5U;
    }
    switch (material.blend) {
    case MaterialBlendMode::Opaque:
        break;
    case MaterialBlendMode::Alpha:
        operations += 6U;
        break;
    case MaterialBlendMode::Additive:
        operations += 5U;
        break;
    case MaterialBlendMode::Multiply:
        operations += 7U;
        break;
    }
    estimate.operationsPerPixel = operations;
    return estimate;
}

} // namespace fabgl
