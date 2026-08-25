#include "test_harness.h"

#include <fabgl/material/material.h>

#include <algorithm>
#include <string_view>

namespace {

bool hasIssue(const fabgl::MaterialValidationReport& report, std::string_view code,
              fabgl::MaterialIssueSeverity severity) {
    return std::any_of(report.issues.begin(), report.issues.end(), [&](const auto& issue) {
        return issue.code == code && issue.severity == severity;
    });
}

} // namespace

FGL_TEST(material_validation_reports_structured_target_and_combination_diagnostics) {
    fabgl::Material invalid;
    invalid.paletteAsset = fabgl::AssetGuid::fromStableName("palette:test");
    invalid.palette = {{0U, 0U, 0U, 255U}, {255U, 255U, 255U, 255U}};
    invalid.transparentIndex = 7U;
    invalid.colorMode = fabgl::MaterialColorMode::Vertex;
    invalid.lighting = fabgl::MaterialLightingMode::Vertex;
    invalid.doubleSided = true;
    invalid.compatibleRenderers = fabgl::RendererCompatibility::LowPoly;

    const auto report = fabgl::validateMaterial(invalid, fabgl::RendererBackend::Renderer2D);
    FGL_CHECK(!report.valid());
    FGL_CHECK(report.hasWarnings());
    FGL_CHECK(hasIssue(report, "renderer.incompatible", fabgl::MaterialIssueSeverity::Error));
    FGL_CHECK(hasIssue(report, "palette.ambiguous-source", fabgl::MaterialIssueSeverity::Error));
    FGL_CHECK(hasIssue(report, "palette.transparent-index-range",
                       fabgl::MaterialIssueSeverity::Error));
    FGL_CHECK(hasIssue(report, "color.vertex-unsupported", fabgl::MaterialIssueSeverity::Error));
    FGL_CHECK(hasIssue(report, "lighting.vertex-unsupported",
                       fabgl::MaterialIssueSeverity::Error));
    FGL_CHECK(hasIssue(report, "sidedness.not-applicable",
                       fabgl::MaterialIssueSeverity::Warning));
}

FGL_TEST(material_validation_exposes_supported_fallbacks_as_warnings_not_errors) {
    fabgl::Material material;
    material.baseTexture = fabgl::AssetGuid::fromStableName("texture:test");
    material.sampling = fabgl::MaterialSamplingMode::Bilinear;
    material.blend = fabgl::MaterialBlendMode::Alpha;
    material.participatesInFog = false;

    const auto raycast = fabgl::validateMaterial(material, fabgl::RendererBackend::Raycast);
    FGL_CHECK(raycast.valid());
    FGL_CHECK(hasIssue(raycast, "sampling.bilinear-fallback",
                       fabgl::MaterialIssueSeverity::Warning));

    const auto lowPoly = fabgl::validateMaterial(material, fabgl::RendererBackend::LowPoly);
    FGL_CHECK(lowPoly.valid());
    FGL_CHECK(hasIssue(lowPoly, "blend.lowpoly-ordering",
                       fabgl::MaterialIssueSeverity::Warning));
}

FGL_TEST(material_cost_estimator_is_deterministic_and_feature_sensitive) {
    fabgl::Material material;
    material.baseTexture = fabgl::AssetGuid::fromStableName("texture:cost");
    material.palette = {{0U, 0U, 0U, 255U}, {255U, 255U, 255U, 255U}};
    material.tint = {128U, 255U, 255U, 255U};
    material.dither = fabgl::MaterialDitherMode::Ordered4x4;
    material.sampling = fabgl::MaterialSamplingMode::Bilinear;
    material.lighting = fabgl::MaterialLightingMode::Flat;
    material.emissiveStrength = 64U;
    material.blend = fabgl::MaterialBlendMode::Alpha;

    const fabgl::MaterialCostContext context{fabgl::RendererBackend::LowPoly, 8U, 4U, 4U,
                                             true, 0U};
    const auto first = fabgl::estimateMaterialCost(material, context);
    const auto second = fabgl::estimateMaterialCost(material, context);
    FGL_CHECK(first.persistentRamBytes == 88U);
    FGL_CHECK(first.flashBytes == 72U);
    FGL_CHECK(first.operationsPerPixel == 45U);
    FGL_CHECK(first.persistentRamBytes == second.persistentRamBytes);
    FGL_CHECK(first.flashBytes == second.flashBytes);
    FGL_CHECK(first.operationsPerPixel == second.operationsPerPixel);

    material.sampling = fabgl::MaterialSamplingMode::Nearest;
    material.dither = fabgl::MaterialDitherMode::None;
    const auto cheaper = fabgl::estimateMaterialCost(material, context);
    FGL_CHECK(cheaper.operationsPerPixel < first.operationsPerPixel);
    FGL_CHECK(cheaper.flashBytes == first.flashBytes);
}
