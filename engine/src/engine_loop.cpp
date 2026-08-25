#include "fabgl/runtime/engine_loop.h"

#include "LifecycleScheduler.h"

#include <chrono>
#include <cmath>
#include <optional>
#include <utility>

namespace fabgl {
namespace {

fabgl_lifecycle::Config schedulerConfig(const EngineLoopConfig& input) noexcept {
    fabgl_lifecycle::Config output;
    output.fixedStepSeconds = input.fixedStepSeconds;
    output.maximumFrameDeltaSeconds = input.maximumFrameDeltaSeconds;
    output.maximumCatchUpSteps = input.maximumCatchUpSteps;
    return output;
}

fabgl_lifecycle::State schedulerState(const double accumulator, const std::uint64_t nextFrame,
                                      const bool initialized) noexcept {
    fabgl_lifecycle::State output;
    output.accumulatorSeconds = accumulator;
    output.nextFrameIndex = nextFrame;
    output.initialized = initialized;
    return output;
}

class DesktopLifecycleHooks final {
  public:
    DesktopLifecycleHooks(EngineLoopCallbacks& callbacks, FrameMetrics* metrics) noexcept
        : callbacks_(callbacks), metrics_(metrics) {}

    bool run(const fabgl_lifecycle::Phase phase, const double value) {
        using fabgl_lifecycle::Phase;
        if (phase == Phase::Shutdown) {
            if (callbacks_.shutdown)
                callbacks_.shutdown();
            return true;
        }

        switch (phase) {
        case Phase::Initialization: return invoke(callbacks_.initialize, phase, nullptr);
        case Phase::ResourceLoading: return invoke(callbacks_.loadResources, phase, nullptr);
        case Phase::SceneLoading: return invoke(callbacks_.loadScene, phase, nullptr);
        case Phase::FixedUpdate:
            return invoke(callbacks_.fixedUpdate, value, phase,
                          metrics_ == nullptr ? nullptr : &metrics_->fixedUpdateCpuSeconds);
        case Phase::PhysicsUpdate:
            return invoke(callbacks_.physicsUpdate, value, phase,
                          metrics_ == nullptr ? nullptr : &metrics_->physicsCpuSeconds);
        case Phase::VariableUpdate:
            return invoke(callbacks_.variableUpdate, value, phase,
                          metrics_ == nullptr ? nullptr : &metrics_->updateCpuSeconds);
        case Phase::AiUpdate:
            return invoke(callbacks_.aiUpdate, value, phase,
                          metrics_ == nullptr ? nullptr : &metrics_->aiCpuSeconds);
        case Phase::AnimationUpdate:
            return invoke(callbacks_.animationUpdate, value, phase,
                          metrics_ == nullptr ? nullptr : &metrics_->animationCpuSeconds);
        case Phase::AudioUpdate:
            return invoke(callbacks_.audioUpdate, value, phase,
                          metrics_ == nullptr ? nullptr : &metrics_->audioCpuSeconds);
        case Phase::AssetStreaming:
            return invoke(callbacks_.assetStreamingUpdate, value, phase,
                          metrics_ == nullptr ? nullptr : &metrics_->assetStreamingCpuSeconds);
        case Phase::RenderSubmission:
            return invoke(callbacks_.renderSubmission, value, phase,
                          metrics_ == nullptr ? nullptr : &metrics_->renderSubmissionCpuSeconds);
        case Phase::Rendering:
            return invoke(callbacks_.render, phase,
                          metrics_ == nullptr ? nullptr : &metrics_->renderingCpuSeconds);
        case Phase::Present:
            return invoke(callbacks_.present, phase,
                          metrics_ == nullptr ? nullptr : &metrics_->presentCpuSeconds);
        case Phase::Shutdown: break;
        }
        return true;
    }

    [[nodiscard]] const std::optional<Error>& error() const noexcept {
        return error_;
    }

  private:
    bool invoke(const std::function<Result<void>()>& callback, const fabgl_lifecycle::Phase phase,
                double* measuredSeconds) {
        if (!callback)
            return true;
        const auto started = std::chrono::steady_clock::now();
        auto result = callback();
        const auto stopped = std::chrono::steady_clock::now();
        if (measuredSeconds != nullptr)
            *measuredSeconds += std::chrono::duration<double>(stopped - started).count();
        if (!result) {
            error_ = result.error().withContext("phase", fabgl_lifecycle::phaseName(phase));
            return false;
        }
        return true;
    }

    bool invoke(const std::function<Result<void>(double)>& callback, const double value,
                const fabgl_lifecycle::Phase phase, double* measuredSeconds) {
        if (!callback)
            return true;
        const auto started = std::chrono::steady_clock::now();
        auto result = callback(value);
        const auto stopped = std::chrono::steady_clock::now();
        if (measuredSeconds != nullptr)
            *measuredSeconds += std::chrono::duration<double>(stopped - started).count();
        if (!result) {
            error_ = result.error().withContext("phase", fabgl_lifecycle::phaseName(phase));
            return false;
        }
        return true;
    }

    EngineLoopCallbacks& callbacks_;
    FrameMetrics* metrics_ = nullptr;
    std::optional<Error> error_;
};

void copySchedulerMetrics(const fabgl_lifecycle::Frame& input, FrameMetrics& output) noexcept {
    output.frameIndex = input.frameIndex;
    output.inputDeltaSeconds = input.inputDeltaSeconds;
    output.simulatedDeltaSeconds = input.simulatedDeltaSeconds;
    output.fixedStepSeconds = input.fixedStepSeconds;
    output.accumulatorSeconds = input.accumulatorSeconds;
    output.interpolationAlpha = input.interpolationAlpha;
    output.fixedUpdateCount = input.fixedUpdateCount;
    output.droppedFixedUpdateCount = input.droppedFixedUpdateCount;
    output.frameDeltaClamped = input.frameDeltaClamped;
    output.catchUpLimited = input.catchUpLimited;
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

    auto state = schedulerState(accumulatorSeconds_, nextFrameIndex_, initialized_);
    DesktopLifecycleHooks hooks(callbacks_, nullptr);
    const auto outcome = fabgl_lifecycle::initialize(schedulerConfig(config_), state, hooks);
    accumulatorSeconds_ = state.accumulatorSeconds;
    nextFrameIndex_ = state.nextFrameIndex;
    initialized_ = state.initialized;
    lastMetrics_ = {};
    if (!outcome.ok) {
        if (hooks.error())
            return Result<void>::failure(*hooks.error());
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "portable lifecycle scheduler initialization failed")
                .addContext("phase", fabgl_lifecycle::phaseName(outcome.failedPhase)));
    }
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
    auto state = schedulerState(accumulatorSeconds_, nextFrameIndex_, initialized_);
    DesktopLifecycleHooks hooks(callbacks_, &metrics);
    const auto outcome =
        fabgl_lifecycle::tick(schedulerConfig(config_), state, frameDeltaSeconds, hooks);
    accumulatorSeconds_ = state.accumulatorSeconds;
    nextFrameIndex_ = state.nextFrameIndex;
    initialized_ = state.initialized;
    if (!outcome.ok) {
        if (hooks.error())
            return Result<FrameMetrics>::failure(*hooks.error());
        return Result<FrameMetrics>::failure(
            Error(ErrorCode::InvalidState, "portable lifecycle scheduler tick failed")
                .addContext("phase", fabgl_lifecycle::phaseName(outcome.failedPhase)));
    }
    copySchedulerMetrics(outcome.frame, metrics);

    const auto cpuEnd = std::chrono::steady_clock::now();
    metrics.measuredCpuSeconds = std::chrono::duration<double>(cpuEnd - cpuStart).count();
    lastMetrics_ = metrics;
    return Result<FrameMetrics>::success(metrics);
}

void EngineLoop::shutdown() {
    auto state = schedulerState(accumulatorSeconds_, nextFrameIndex_, initialized_);
    DesktopLifecycleHooks hooks(callbacks_, nullptr);
    fabgl_lifecycle::shutdown(state, hooks);
    accumulatorSeconds_ = state.accumulatorSeconds;
    nextFrameIndex_ = state.nextFrameIndex;
    initialized_ = state.initialized;
}

} // namespace fabgl
