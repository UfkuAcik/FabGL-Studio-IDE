#pragma once

// Portable, allocation-free lifecycle scheduling contract shared verbatim by
// the desktop EngineLoop and the ESP32 firmware.  The contract deliberately
// depends only on C++11 library facilities: a target supplies one stack/static
// Hooks object with bool run(Phase, double), while the scheduler owns only
// scalar state and metrics.  It performs no allocation, logging or I/O.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace fabgl_lifecycle {

enum class Phase : std::uint8_t {
    Initialization,
    ResourceLoading,
    SceneLoading,
    FixedUpdate,
    PhysicsUpdate,
    VariableUpdate,
    AiUpdate,
    AnimationUpdate,
    AudioUpdate,
    AssetStreaming,
    RenderSubmission,
    Rendering,
    Present,
    Shutdown,
};

inline const char* phaseName(const Phase phase) noexcept {
    switch (phase) {
    case Phase::Initialization: return "initialization";
    case Phase::ResourceLoading: return "resource_loading";
    case Phase::SceneLoading: return "scene_loading";
    case Phase::FixedUpdate: return "fixed_update";
    case Phase::PhysicsUpdate: return "physics_update";
    case Phase::VariableUpdate: return "variable_update";
    case Phase::AiUpdate: return "ai_update";
    case Phase::AnimationUpdate: return "animation_update";
    case Phase::AudioUpdate: return "audio_update";
    case Phase::AssetStreaming: return "asset_streaming";
    case Phase::RenderSubmission: return "render_submission";
    case Phase::Rendering: return "rendering";
    case Phase::Present: return "present";
    case Phase::Shutdown: return "shutdown";
    }
    return "unknown";
}

struct Config final {
    double fixedStepSeconds;
    double maximumFrameDeltaSeconds;
    std::uint32_t maximumCatchUpSteps;

    Config() noexcept
        : fixedStepSeconds(1.0 / 60.0), maximumFrameDeltaSeconds(0.25),
          maximumCatchUpSteps(5U) {}
};

struct State final {
    double accumulatorSeconds;
    std::uint64_t nextFrameIndex;
    bool initialized;

    State() noexcept : accumulatorSeconds(0.0), nextFrameIndex(1U), initialized(false) {}
};

struct Frame final {
    std::uint64_t frameIndex;
    double inputDeltaSeconds;
    double simulatedDeltaSeconds;
    double fixedStepSeconds;
    double accumulatorSeconds;
    double interpolationAlpha;
    std::uint32_t fixedUpdateCount;
    std::uint32_t droppedFixedUpdateCount;
    bool frameDeltaClamped;
    bool catchUpLimited;

    Frame() noexcept
        : frameIndex(0U), inputDeltaSeconds(0.0), simulatedDeltaSeconds(0.0),
          fixedStepSeconds(0.0), accumulatorSeconds(0.0), interpolationAlpha(0.0),
          fixedUpdateCount(0U), droppedFixedUpdateCount(0U), frameDeltaClamped(false),
          catchUpLimited(false) {}
};

struct Outcome final {
    bool ok;
    Phase failedPhase;
    Frame frame;

    Outcome() noexcept : ok(true), failedPhase(Phase::Initialization), frame() {}
};

inline bool validConfig(const Config& config) noexcept {
    return std::isfinite(config.fixedStepSeconds) && config.fixedStepSeconds > 0.0 &&
           std::isfinite(config.maximumFrameDeltaSeconds) &&
           config.maximumFrameDeltaSeconds > 0.0 && config.maximumCatchUpSteps != 0U;
}

template <typename Hooks>
Outcome initialize(const Config& config, State& state, Hooks& hooks) {
    Outcome outcome;
    if (!validConfig(config)) {
        outcome.ok = false;
        return outcome;
    }
    if (state.initialized)
        return outcome;
    if (!hooks.run(Phase::Initialization, 0.0)) {
        outcome.ok = false;
        outcome.failedPhase = Phase::Initialization;
        return outcome;
    }
    if (!hooks.run(Phase::ResourceLoading, 0.0)) {
        hooks.run(Phase::Shutdown, 0.0);
        state = State();
        outcome.ok = false;
        outcome.failedPhase = Phase::ResourceLoading;
        return outcome;
    }
    if (!hooks.run(Phase::SceneLoading, 0.0)) {
        hooks.run(Phase::Shutdown, 0.0);
        state = State();
        outcome.ok = false;
        outcome.failedPhase = Phase::SceneLoading;
        return outcome;
    }
    state = State();
    state.initialized = true;
    return outcome;
}

template <typename Hooks>
Outcome tick(const Config& config, State& state, const double frameDeltaSeconds, Hooks& hooks) {
    Outcome outcome;
    if (!state.initialized || !validConfig(config) || !std::isfinite(frameDeltaSeconds) ||
        frameDeltaSeconds < 0.0) {
        outcome.ok = false;
        return outcome;
    }

    Frame& metrics = outcome.frame;
    metrics.frameIndex = state.nextFrameIndex;
    metrics.inputDeltaSeconds = frameDeltaSeconds;
    metrics.fixedStepSeconds = config.fixedStepSeconds;
    metrics.simulatedDeltaSeconds =
        frameDeltaSeconds < config.maximumFrameDeltaSeconds ? frameDeltaSeconds
                                                            : config.maximumFrameDeltaSeconds;
    metrics.frameDeltaClamped = metrics.simulatedDeltaSeconds != frameDeltaSeconds;
    state.accumulatorSeconds += metrics.simulatedDeltaSeconds;

    while (state.accumulatorSeconds + 1.0e-12 >= config.fixedStepSeconds &&
           metrics.fixedUpdateCount < config.maximumCatchUpSteps) {
        if (!hooks.run(Phase::FixedUpdate, config.fixedStepSeconds)) {
            outcome.ok = false;
            outcome.failedPhase = Phase::FixedUpdate;
            return outcome;
        }
        if (!hooks.run(Phase::PhysicsUpdate, config.fixedStepSeconds)) {
            outcome.ok = false;
            outcome.failedPhase = Phase::PhysicsUpdate;
            return outcome;
        }
        state.accumulatorSeconds -= config.fixedStepSeconds;
        ++metrics.fixedUpdateCount;
    }

    if (state.accumulatorSeconds + 1.0e-12 >= config.fixedStepSeconds) {
        metrics.catchUpLimited = true;
        const double dropped = std::floor(state.accumulatorSeconds / config.fixedStepSeconds);
        const double maximum =
            static_cast<double>((std::numeric_limits<std::uint32_t>::max)());
        metrics.droppedFixedUpdateCount =
            dropped > maximum ? (std::numeric_limits<std::uint32_t>::max)()
                              : static_cast<std::uint32_t>(dropped);
        state.accumulatorSeconds = std::fmod(state.accumulatorSeconds, config.fixedStepSeconds);
    }

    const Phase timedPhases[] = {Phase::VariableUpdate, Phase::AiUpdate,
                                 Phase::AnimationUpdate, Phase::AudioUpdate,
                                 Phase::AssetStreaming};
    for (std::size_t index = 0U; index < sizeof(timedPhases) / sizeof(timedPhases[0]); ++index) {
        if (!hooks.run(timedPhases[index], metrics.simulatedDeltaSeconds)) {
            outcome.ok = false;
            outcome.failedPhase = timedPhases[index];
            return outcome;
        }
    }

    metrics.accumulatorSeconds = state.accumulatorSeconds;
    metrics.interpolationAlpha = state.accumulatorSeconds / config.fixedStepSeconds;
    if (!hooks.run(Phase::RenderSubmission, metrics.interpolationAlpha)) {
        outcome.ok = false;
        outcome.failedPhase = Phase::RenderSubmission;
        return outcome;
    }
    if (!hooks.run(Phase::Rendering, 0.0)) {
        outcome.ok = false;
        outcome.failedPhase = Phase::Rendering;
        return outcome;
    }
    if (!hooks.run(Phase::Present, 0.0)) {
        outcome.ok = false;
        outcome.failedPhase = Phase::Present;
        return outcome;
    }

    ++state.nextFrameIndex;
    return outcome;
}

template <typename Hooks> void shutdown(State& state, Hooks& hooks) {
    if (!state.initialized)
        return;
    hooks.run(Phase::Shutdown, 0.0);
    state.initialized = false;
    state.accumulatorSeconds = 0.0;
}

} // namespace fabgl_lifecycle
