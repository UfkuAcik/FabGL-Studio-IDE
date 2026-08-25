#include "test_harness.h"

#include <performance_budget.h>
#include <project_format.h>

using namespace fabgl::project;

FGL_TEST(performance_budget_presets_are_target_specific_deterministic_and_valid) {
    const auto pcSafe =
        performanceBudgetPreset(PerformanceTarget::Pc, PerformanceBudgetProfile::Safe);
    const auto pcBalanced =
        performanceBudgetPreset(PerformanceTarget::Pc, PerformanceBudgetProfile::Balanced);
    const auto pcMaximum =
        performanceBudgetPreset(PerformanceTarget::Pc, PerformanceBudgetProfile::Maximum);
    const auto esp32Safe =
        performanceBudgetPreset(PerformanceTarget::Esp32, PerformanceBudgetProfile::Safe);
    FGL_CHECK(validPerformanceBudget(pcSafe));
    FGL_CHECK(validPerformanceBudget(pcBalanced));
    FGL_CHECK(validPerformanceBudget(pcMaximum));
    FGL_CHECK(validPerformanceBudget(esp32Safe));
    FGL_CHECK(pcSafe.entities < pcBalanced.entities);
    FGL_CHECK(pcBalanced.entities < pcMaximum.entities);
    FGL_CHECK(pcSafe.internalRamBytes > esp32Safe.internalRamBytes);
    FGL_CHECK(esp32Safe.psramBytes == 0U);

    PerformanceBudgetProfile decoded = PerformanceBudgetProfile::Maximum;
    FGL_CHECK(parsePerformanceBudgetProfile("custom", decoded));
    FGL_CHECK(decoded == PerformanceBudgetProfile::Custom);
    FGL_CHECK(!parsePerformanceBudgetProfile("ultra", decoded));
}

FGL_TEST(performance_budget_custom_selection_and_diagnostics_preserve_measurement_sources) {
    PerformanceBudgetSettings settings;
    settings.esp32Profile = PerformanceBudgetProfile::Custom;
    settings.esp32Custom =
        performanceBudgetPreset(PerformanceTarget::Esp32, PerformanceBudgetProfile::Balanced);
    settings.esp32Custom.drawCalls = 10U;
    settings.esp32Custom.internalRamBytes = 1000U;
    const auto selected = selectedPerformanceBudget(settings, PerformanceTarget::Esp32);
    FGL_CHECK(selected.drawCalls == 10U);

    const std::vector<PerformanceObservation> observations = {
        {PerformanceMetric::DrawCalls, 11.0, PerformanceObservationSource::EstimatedEsp32},
        {PerformanceMetric::InternalRamBytes, 1500.0,
         PerformanceObservationSource::MeasuredEsp32},
        {PerformanceMetric::Triangles, 1000000.0,
         PerformanceObservationSource::Unavailable},
    };
    const auto diagnostics = evaluatePerformanceBudget(selected, observations);
    FGL_CHECK(diagnostics.size() == 2U);
    FGL_CHECK(diagnostics[0].severity == PerformanceBudgetSeverity::Warning);
    FGL_CHECK(diagnostics[0].source == PerformanceObservationSource::EstimatedEsp32);
    FGL_CHECK(!diagnostics[0].recommendation.empty());
    FGL_CHECK(diagnostics[1].severity == PerformanceBudgetSeverity::Error);
    FGL_CHECK(diagnostics[1].source == PerformanceObservationSource::MeasuredEsp32);
}

FGL_TEST(project_manifest_round_trips_versioned_performance_profiles_and_custom_values) {
    Manifest manifest;
    manifest.projectGuid = "14382751-d153-4d9c-807d-fb33d5e276d6";
    manifest.name = "Performance Test";
    manifest.performance.pcProfile = PerformanceBudgetProfile::Maximum;
    manifest.performance.esp32Profile = PerformanceBudgetProfile::Custom;
    manifest.performance.esp32Custom =
        performanceBudgetPreset(PerformanceTarget::Esp32, PerformanceBudgetProfile::Balanced);
    manifest.performance.esp32Custom.particles = 777U;
    manifest.performance.esp32Custom.sdBytes = 0U;

    const auto encoded = serializeManifest(manifest);
    FGL_CHECK(encoded);
    FGL_CHECK(encoded.value().find("\"performance\"") != std::string::npos);
    FGL_CHECK(encoded.value().find("\"esp32Profile\": \"custom\"") != std::string::npos);
    const auto decoded = parseManifest(encoded.value());
    FGL_CHECK(decoded);
    FGL_CHECK(decoded.value().performance == manifest.performance);

    manifest.performance.esp32Custom.internalRamBytes = 0U;
    FGL_CHECK(!serializeManifest(manifest));

    auto future = encoded.value();
    const auto version = future.find("\"version\": 1", future.find("\"performance\""));
    FGL_CHECK(version != std::string::npos);
    future.replace(version, std::string("\"version\": 1").size(), "\"version\": 2");
    FGL_CHECK(!parseManifest(future));
}
