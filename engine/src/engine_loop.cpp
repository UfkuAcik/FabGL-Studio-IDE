#include "fabgl/runtime/engine_loop.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace fabgl {
namespace {

Result<void> runPhase(const std::function<Result<void>()>& callback, const char* phase) {
    if (!callback)
        return Result<void>::success();
    auto result = callback();
    if (!result) {
        return Result<void>::failure(result.error().withContext("phase", phase));
    }
    return Result<void>::success();
}

Result<void> runTimedPhase(const std::function<Result<void>(double)>& callback, double deltaSeconds,
                           const char* phase) {
    if (!callback)
        return Result<void>::success();
    auto result = callback(deltaSeconds);
    if (!result) {
        return Result<void>::failure(result.error().withContext("phase", phase));
    }
    return Result<void>::success();
}

} // namespace

EngineLoop::EngineLoop() = default;

EngineLoop::EngineLoop(EngineLoopConfig config) : config_(config) {}

EngineLoop::~EngineLoop() {
    shutdown();
}

Result<void> EngineLoop::validateConfig(const EngineLoopConfig& config) const {
    if (!std::isfinite(config.fixedStepSeconds) || config.fixedStepSeconds <= 0.0) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "fixed step must be finite and positive"));
    }
    if (!std::isfinite(config.maximumFrameDeltaSeconds) || config.maximumFrameDeltaSeconds <= 0.0) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "maximum frame delta must be finite and positive"));
    }
    if (config.maximumCatchUpSteps == 0) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "maximum catch-up steps must be positive"));
    }
    return Result<void>::success();
}

Result<void> EngineLoop::setConfig(EngineLoopConfig config) {
    if (initialized_) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "cannot change engine-loop config while initialized"));
    }
    auto valid = validateConfig(config);
    if (!valid)
        return valid;
    config_ = config;
    return Result<void>::success();
}

void EngineLoop::setCallbacks(EngineLoopCallbacks callbacks) {
    callbacks_ = std::move(callbacks);
}

Result<void> EngineLoop::initialize() {
    if (initialized_)
        return Result<void>::success();
    auto valid = validateConfig(config_);
    if (!valid)
        return valid;

    auto result = runPhase(callbacks_.initialize, "initialization");
    if (!result)
        return result;
    result = runPhase(callbacks_.loadResources, "resource_loading");
    if (!result)
        return result;
    result = runPhase(callbacks_.loadScene, "scene_loading");
    if (!result)
        return result;

    accumulatorSeconds_ = 0.0;
    nextFrameIndex_ = 1;
    lastMetrics_ = {};
    initialized_ = true;
    return Result<void>::success();
}

Result<FrameMetrics> EngineLoop::tick(double frameDeltaSeconds) {
    if (!initialized_) {
        return Result<FrameMetrics>::failure(
            Error(ErrorCode::InvalidState, "engine loop is not initialized"));
    }
    if (!std::isfinite(frameDeltaSeconds) || frameDeltaSeconds < 0.0) {
        return Result<FrameMetrics>::failure(
            Error(ErrorCode::InvalidArgument, "frame delta must be finite and non-negative"));
    }

    const auto cpuStart = std::chrono::steady_clock::now();
    FrameMetrics metrics;
    metrics.frameIndex = nextFrameIndex_;
    metrics.inputDeltaSeconds = frameDeltaSeconds;
    metrics.fixedStepSeconds = config_.fixedStepSeconds;
    metrics.simulatedDeltaSeconds = std::min(frameDeltaSeconds, config_.maximumFrameDeltaSeconds);
    metrics.frameDeltaClamped = metrics.simulatedDeltaSeconds != frameDeltaSeconds;
    accumulatorSeconds_ += metrics.simulatedDeltaSeconds;

    while (accumulatorSeconds_ + 1.0e-12 >= config_.fixedStepSeconds &&
           metrics.fixedUpdateCount < config_.maximumCatchUpSteps) {
        auto phase =
            runTimedPhase(callbacks_.fixedUpdate, config_.fixedStepSeconds, "fixed_update");
        if (!phase)
            return Result<FrameMetrics>::failure(phase.error());
        phase = runTimedPhase(callbacks_.physicsUpdate, config_.fixedStepSeconds, "physics_update");
        if (!phase)
            return Result<FrameMetrics>::failure(phase.error());
        accumulatorSeconds_ -= config_.fixedStepSeconds;
        ++metrics.fixedUpdateCount;
    }

    if (accumulatorSeconds_ + 1.0e-12 >= config_.fixedStepSeconds) {
        metrics.catchUpLimited = true;
        const auto dropped = std::floor(accumulatorSeconds_ / config_.fixedStepSeconds);
        metrics.droppedFixedUpdateCount =
            dropped > static_cast<double>(std::numeric_limits<std::uint32_t>::max())
                ? std::numeric_limits<std::uint32_t>::max()
                : static_cast<std::uint32_t>(dropped);
        accumulatorSeconds_ = std::fmod(accumulatorSeconds_, config_.fixedStepSeconds);
    }

    auto phase =
        runTimedPhase(callbacks_.variableUpdate, metrics.simulatedDeltaSeconds, "variable_update");
    if (!phase)
        return Result<FrameMetrics>::failure(phase.error());
    phase = runTimedPhase(callbacks_.animationUpdate, metrics.simulatedDeltaSeconds,
                          "animation_update");
    if (!phase)
        return Result<FrameMetrics>::failure(phase.error());
    phase = runTimedPhase(callbacks_.audioUpdate, metrics.simulatedDeltaSeconds, "audio_update");
    if (!phase)
        return Result<FrameMetrics>::failure(phase.error());

    metrics.accumulatorSeconds = accumulatorSeconds_;
    metrics.interpolationAlpha = accumulatorSeconds_ / config_.fixedStepSeconds;
    phase =
        runTimedPhase(callbacks_.renderSubmission, metrics.interpolationAlpha, "render_submission");
    if (!phase)
        return Result<FrameMetrics>::failure(phase.error());
    phase = runPhase(callbacks_.render, "rendering");
    if (!phase)
        return Result<FrameMetrics>::failure(phase.error());
    phase = runPhase(callbacks_.present, "present");
    if (!phase)
        return Result<FrameMetrics>::failure(phase.error());

    const auto cpuEnd = std::chrono::steady_clock::now();
    metrics.measuredCpuSeconds = std::chrono::duration<double>(cpuEnd - cpuStart).count();
    lastMetrics_ = metrics;
    ++nextFrameIndex_;
    return Result<FrameMetrics>::success(metrics);
}

void EngineLoop::shutdown() {
    if (!initialized_)
        return;
    if (callbacks_.shutdown)
        callbacks_.shutdown();
    initialized_ = false;
    accumulatorSeconds_ = 0.0;
}

} // namespace fabgl
