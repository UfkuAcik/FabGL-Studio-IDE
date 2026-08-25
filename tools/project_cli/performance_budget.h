#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fabgl::project {

enum class PerformanceTarget : std::uint8_t { Pc = 0, Esp32 };
enum class PerformanceBudgetProfile : std::uint8_t { Safe = 0, Balanced, Maximum, Custom };

enum class PerformanceMetric : std::uint8_t {
    FrameTotalMilliseconds = 0,
    FixedUpdateMilliseconds,
    UpdateMilliseconds,
    PhysicsMilliseconds,
    AnimationMilliseconds,
    AiMilliseconds,
    RenderMilliseconds,
    AudioMilliseconds,
    AssetStreamingMilliseconds,
    Entities,
    Components,
    DrawCalls,
    Sprites,
    Triangles,
    Rays,
    Particles,
    AudioVoices,
    AssetResidentBytes,
    InternalRamBytes,
    PsramBytes,
    FlashBytes,
    SdBytes,
};

enum class PerformanceUnit : std::uint8_t { Milliseconds = 0, Count, Bytes };
enum class PerformanceObservationSource : std::uint8_t {
    MeasuredPc = 0,
    EstimatedEsp32,
    MeasuredEsp32,
    Unavailable,
};
enum class PerformanceBudgetSeverity : std::uint8_t { Warning = 0, Error };

struct PerformanceBudgetValues final {
    double frameTotalMilliseconds = 16.67;
    double fixedUpdateMilliseconds = 1.5;
    double updateMilliseconds = 2.5;
    double physicsMilliseconds = 2.0;
    double animationMilliseconds = 1.5;
    double aiMilliseconds = 1.0;
    double renderMilliseconds = 6.0;
    double audioMilliseconds = 0.75;
    double assetStreamingMilliseconds = 0.75;
    std::uint32_t entities = 2048U;
    std::uint32_t components = 8192U;
    std::uint32_t drawCalls = 512U;
    std::uint32_t sprites = 8192U;
    std::uint32_t triangles = 50000U;
    std::uint32_t rays = 1024U;
    std::uint32_t particles = 4096U;
    std::uint32_t audioVoices = 64U;
    std::uint64_t assetResidentBytes = 256U * 1024U * 1024U;
    std::uint64_t internalRamBytes = 512U * 1024U * 1024U;
    std::uint64_t psramBytes = 0U;
    std::uint64_t flashBytes = 1024U * 1024U * 1024U;
    std::uint64_t sdBytes = 4ULL * 1024ULL * 1024ULL * 1024ULL;

    friend bool operator==(const PerformanceBudgetValues&,
                           const PerformanceBudgetValues&) = default;
};

[[nodiscard]] constexpr PerformanceBudgetValues
performanceBudgetPreset(const PerformanceTarget target,
                        const PerformanceBudgetProfile profile) noexcept {
    if (target == PerformanceTarget::Pc) {
        if (profile == PerformanceBudgetProfile::Safe) {
            return {12.0,  1.0,  1.75, 1.25, 1.0, 0.75, 4.0, 0.5, 0.5,
                    1024U, 4096U, 256U, 4096U, 20000U, 512U, 2048U, 32U,
                    128U * 1024U * 1024U, 256U * 1024U * 1024U, 0U,
                    512U * 1024U * 1024U, 2ULL * 1024ULL * 1024ULL * 1024ULL};
        }
        if (profile == PerformanceBudgetProfile::Maximum) {
            return {33.33, 3.0, 5.0,  4.0, 3.0, 2.5, 12.0, 1.5, 2.0,
                    8192U, 32768U, 2048U, 32768U, 250000U, 4096U, 32768U, 128U,
                    1024ULL * 1024ULL * 1024ULL, 2ULL * 1024ULL * 1024ULL * 1024ULL, 0U,
                    4ULL * 1024ULL * 1024ULL * 1024ULL,
                    16ULL * 1024ULL * 1024ULL * 1024ULL};
        }
        return {};
    }

    if (profile == PerformanceBudgetProfile::Safe) {
        return {16.67, 1.0,  2.0,  1.5, 1.0, 0.75, 7.0, 0.5, 0.5,
                96U,   384U, 32U,  256U, 256U, 64U, 256U, 4U,
                128U * 1024U, 160U * 1024U, 0U, 2U * 1024U * 1024U,
                16U * 1024U * 1024U};
    }
    if (profile == PerformanceBudgetProfile::Maximum) {
        return {50.0,  4.0,   7.0,   5.0, 4.0, 3.0, 22.0, 1.5, 2.5,
                768U,  3072U, 192U, 1536U, 2048U, 384U, 2048U, 12U,
                3U * 1024U * 1024U, 256U * 1024U, 4U * 1024U * 1024U,
                4U * 1024U * 1024U, 256U * 1024U * 1024U};
    }
    return {33.33, 2.0,  4.0,  3.0, 2.0, 1.5, 14.0, 1.0, 1.0,
            256U,  1024U, 96U, 768U, 1024U, 192U, 1024U, 8U,
            512U * 1024U, 192U * 1024U, 4U * 1024U * 1024U,
            4U * 1024U * 1024U, 64U * 1024U * 1024U};
}

struct PerformanceBudgetSettings final {
    static constexpr int CurrentVersion = 1;

    int version = CurrentVersion;
    PerformanceBudgetProfile pcProfile = PerformanceBudgetProfile::Balanced;
    PerformanceBudgetProfile esp32Profile = PerformanceBudgetProfile::Safe;
    PerformanceBudgetValues pcCustom =
        performanceBudgetPreset(PerformanceTarget::Pc, PerformanceBudgetProfile::Balanced);
    PerformanceBudgetValues esp32Custom =
        performanceBudgetPreset(PerformanceTarget::Esp32, PerformanceBudgetProfile::Safe);

    friend bool operator==(const PerformanceBudgetSettings&,
                           const PerformanceBudgetSettings&) = default;
};

[[nodiscard]] constexpr PerformanceBudgetValues
selectedPerformanceBudget(const PerformanceBudgetSettings& settings,
                          const PerformanceTarget target) noexcept {
    const auto profile = target == PerformanceTarget::Pc ? settings.pcProfile
                                                          : settings.esp32Profile;
    if (profile == PerformanceBudgetProfile::Custom) {
        return target == PerformanceTarget::Pc ? settings.pcCustom : settings.esp32Custom;
    }
    return performanceBudgetPreset(target, profile);
}

[[nodiscard]] constexpr std::string_view
performanceBudgetProfileId(const PerformanceBudgetProfile profile) noexcept {
    switch (profile) {
    case PerformanceBudgetProfile::Safe:
        return "safe";
    case PerformanceBudgetProfile::Balanced:
        return "balanced";
    case PerformanceBudgetProfile::Maximum:
        return "maximum";
    case PerformanceBudgetProfile::Custom:
        return "custom";
    }
    return "balanced";
}

[[nodiscard]] constexpr bool
validPerformanceBudgetProfile(const PerformanceBudgetProfile profile) noexcept {
    return static_cast<std::uint8_t>(profile) <=
           static_cast<std::uint8_t>(PerformanceBudgetProfile::Custom);
}

[[nodiscard]] constexpr bool
parsePerformanceBudgetProfile(const std::string_view id,
                              PerformanceBudgetProfile& profile) noexcept {
    if (id == "safe")
        profile = PerformanceBudgetProfile::Safe;
    else if (id == "balanced")
        profile = PerformanceBudgetProfile::Balanced;
    else if (id == "maximum")
        profile = PerformanceBudgetProfile::Maximum;
    else if (id == "custom")
        profile = PerformanceBudgetProfile::Custom;
    else
        return false;
    return true;
}

[[nodiscard]] inline bool validPerformanceBudget(const PerformanceBudgetValues& budget) noexcept {
    const auto validTime = [](const double value) {
        return std::isfinite(value) && value > 0.0 && value <= 10000.0;
    };
    return validTime(budget.frameTotalMilliseconds) &&
           validTime(budget.fixedUpdateMilliseconds) && validTime(budget.updateMilliseconds) &&
           validTime(budget.physicsMilliseconds) && validTime(budget.animationMilliseconds) &&
           validTime(budget.aiMilliseconds) && validTime(budget.renderMilliseconds) &&
           validTime(budget.audioMilliseconds) && validTime(budget.assetStreamingMilliseconds) &&
           budget.entities > 0U && budget.components > 0U && budget.drawCalls > 0U &&
           budget.sprites > 0U && budget.triangles > 0U && budget.rays > 0U &&
           budget.particles > 0U && budget.audioVoices > 0U && budget.assetResidentBytes > 0U &&
           budget.internalRamBytes > 0U && budget.flashBytes > 0U;
}

struct PerformanceObservation final {
    PerformanceMetric metric = PerformanceMetric::FrameTotalMilliseconds;
    double value = 0.0;
    PerformanceObservationSource source = PerformanceObservationSource::Unavailable;
};

struct PerformanceBudgetDiagnostic final {
    PerformanceBudgetSeverity severity = PerformanceBudgetSeverity::Warning;
    PerformanceMetric metric = PerformanceMetric::FrameTotalMilliseconds;
    double value = 0.0;
    double limit = 0.0;
    PerformanceObservationSource source = PerformanceObservationSource::Unavailable;
    std::string recommendation;
};

[[nodiscard]] constexpr double performanceBudgetLimit(const PerformanceBudgetValues& budget,
                                                       const PerformanceMetric metric) noexcept {
    switch (metric) {
    case PerformanceMetric::FrameTotalMilliseconds:
        return budget.frameTotalMilliseconds;
    case PerformanceMetric::FixedUpdateMilliseconds:
        return budget.fixedUpdateMilliseconds;
    case PerformanceMetric::UpdateMilliseconds:
        return budget.updateMilliseconds;
    case PerformanceMetric::PhysicsMilliseconds:
        return budget.physicsMilliseconds;
    case PerformanceMetric::AnimationMilliseconds:
        return budget.animationMilliseconds;
    case PerformanceMetric::AiMilliseconds:
        return budget.aiMilliseconds;
    case PerformanceMetric::RenderMilliseconds:
        return budget.renderMilliseconds;
    case PerformanceMetric::AudioMilliseconds:
        return budget.audioMilliseconds;
    case PerformanceMetric::AssetStreamingMilliseconds:
        return budget.assetStreamingMilliseconds;
    case PerformanceMetric::Entities:
        return static_cast<double>(budget.entities);
    case PerformanceMetric::Components:
        return static_cast<double>(budget.components);
    case PerformanceMetric::DrawCalls:
        return static_cast<double>(budget.drawCalls);
    case PerformanceMetric::Sprites:
        return static_cast<double>(budget.sprites);
    case PerformanceMetric::Triangles:
        return static_cast<double>(budget.triangles);
    case PerformanceMetric::Rays:
        return static_cast<double>(budget.rays);
    case PerformanceMetric::Particles:
        return static_cast<double>(budget.particles);
    case PerformanceMetric::AudioVoices:
        return static_cast<double>(budget.audioVoices);
    case PerformanceMetric::AssetResidentBytes:
        return static_cast<double>(budget.assetResidentBytes);
    case PerformanceMetric::InternalRamBytes:
        return static_cast<double>(budget.internalRamBytes);
    case PerformanceMetric::PsramBytes:
        return static_cast<double>(budget.psramBytes);
    case PerformanceMetric::FlashBytes:
        return static_cast<double>(budget.flashBytes);
    case PerformanceMetric::SdBytes:
        return static_cast<double>(budget.sdBytes);
    }
    return 0.0;
}

[[nodiscard]] constexpr PerformanceUnit performanceMetricUnit(const PerformanceMetric metric) {
    if (metric <= PerformanceMetric::AssetStreamingMilliseconds)
        return PerformanceUnit::Milliseconds;
    if (metric <= PerformanceMetric::AudioVoices)
        return PerformanceUnit::Count;
    return PerformanceUnit::Bytes;
}

[[nodiscard]] inline std::string
performanceOptimizationRecommendation(const PerformanceMetric metric) {
    switch (metric) {
    case PerformanceMetric::PhysicsMilliseconds:
        return "Reduce active colliders, solver work, or fixed-update frequency.";
    case PerformanceMetric::AnimationMilliseconds:
        return "Cull off-screen animators or reduce simultaneously evaluated tracks.";
    case PerformanceMetric::AiMilliseconds:
        return "Stagger AI updates and reduce path-search or perception frequency.";
    case PerformanceMetric::RenderMilliseconds:
    case PerformanceMetric::DrawCalls:
    case PerformanceMetric::Sprites:
    case PerformanceMetric::Triangles:
    case PerformanceMetric::Rays:
    case PerformanceMetric::Particles:
        return "Cull invisible work, batch compatible draws, and reduce scene detail.";
    case PerformanceMetric::AudioMilliseconds:
    case PerformanceMetric::AudioVoices:
        return "Reduce simultaneous voices or pre-convert expensive audio assets.";
    case PerformanceMetric::AssetStreamingMilliseconds:
        return "Preload the transition working set or split large dependency groups.";
    case PerformanceMetric::AssetResidentBytes:
    case PerformanceMetric::InternalRamBytes:
    case PerformanceMetric::PsramBytes:
    case PerformanceMetric::FlashBytes:
    case PerformanceMetric::SdBytes:
        return "Compress or downscale assets and move eligible payloads to streaming storage.";
    case PerformanceMetric::Entities:
    case PerformanceMetric::Components:
        return "Pool short-lived objects and disable or remove inactive scene content.";
    case PerformanceMetric::FrameTotalMilliseconds:
    case PerformanceMetric::FixedUpdateMilliseconds:
    case PerformanceMetric::UpdateMilliseconds:
        return "Profile the largest subsystem and reduce its per-frame work.";
    }
    return "Inspect the metric history and reduce the dominant workload.";
}

[[nodiscard]] inline std::vector<PerformanceBudgetDiagnostic>
evaluatePerformanceBudget(const PerformanceBudgetValues& budget,
                          const std::vector<PerformanceObservation>& observations) {
    std::vector<PerformanceBudgetDiagnostic> diagnostics;
    diagnostics.reserve(observations.size());
    for (const auto& observation : observations) {
        if (observation.source == PerformanceObservationSource::Unavailable ||
            !std::isfinite(observation.value) || observation.value < 0.0)
            continue;
        const double limit = performanceBudgetLimit(budget, observation.metric);
        if (limit <= 0.0 || observation.value <= limit)
            continue;
        diagnostics.push_back(
            {observation.value > limit * 1.25 ? PerformanceBudgetSeverity::Error
                                             : PerformanceBudgetSeverity::Warning,
             observation.metric, observation.value, limit, observation.source,
             performanceOptimizationRecommendation(observation.metric)});
    }
    return diagnostics;
}

} // namespace fabgl::project
