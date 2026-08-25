#pragma once

#include "fabgl/core/result.h"

#include <cstdint>
#include <functional>

namespace fabgl {

struct EngineLoopConfig final {
    double fixedStepSeconds = 1.0 / 60.0;
    double maximumFrameDeltaSeconds = 0.25;
    std::uint32_t maximumCatchUpSteps = 5;
};

struct FrameMetrics final {
    std::uint64_t frameIndex = 0;
    double inputDeltaSeconds = 0.0;
    double simulatedDeltaSeconds = 0.0;
    double fixedStepSeconds = 0.0;
    double accumulatorSeconds = 0.0;
    double interpolationAlpha = 0.0;
    double measuredCpuSeconds = 0.0;
    double fixedUpdateCpuSeconds = 0.0;
    double physicsCpuSeconds = 0.0;
    double updateCpuSeconds = 0.0;
    double aiCpuSeconds = 0.0;
    double animationCpuSeconds = 0.0;
    double audioCpuSeconds = 0.0;
    double assetStreamingCpuSeconds = 0.0;
    double renderSubmissionCpuSeconds = 0.0;
    double renderingCpuSeconds = 0.0;
    double presentCpuSeconds = 0.0;
    std::uint32_t fixedUpdateCount = 0;
    std::uint32_t droppedFixedUpdateCount = 0;
    bool frameDeltaClamped = false;
    bool catchUpLimited = false;

    [[nodiscard]] double profiledCpuSeconds() const noexcept {
        return fixedUpdateCpuSeconds + physicsCpuSeconds + updateCpuSeconds + aiCpuSeconds +
               animationCpuSeconds + audioCpuSeconds + assetStreamingCpuSeconds +
               renderSubmissionCpuSeconds + renderingCpuSeconds + presentCpuSeconds;
    }
};

struct EngineLoopCallbacks final {
    std::function<Result<void>()> initialize;
    std::function<Result<void>()> loadResources;
    std::function<Result<void>()> loadScene;
    std::function<Result<void>(double)> fixedUpdate;
    std::function<Result<void>(double)> physicsUpdate;
    std::function<Result<void>(double)> variableUpdate;
    std::function<Result<void>(double)> aiUpdate;
    std::function<Result<void>(double)> animationUpdate;
    std::function<Result<void>(double)> audioUpdate;
    std::function<Result<void>(double)> assetStreamingUpdate;
    std::function<Result<void>(double)> renderSubmission;
    std::function<Result<void>()> render;
    std::function<Result<void>()> present;
    std::function<void()> shutdown;
};

class EngineLoop final {
  public:
    EngineLoop();
    explicit EngineLoop(EngineLoopConfig config);
    ~EngineLoop();

    EngineLoop(const EngineLoop&) = delete;
    EngineLoop& operator=(const EngineLoop&) = delete;

    [[nodiscard]] Result<void> setConfig(EngineLoopConfig config);
    [[nodiscard]] const EngineLoopConfig& config() const noexcept {
        return config_;
    }
    void setCallbacks(EngineLoopCallbacks callbacks);

    [[nodiscard]] Result<void> initialize();
    [[nodiscard]] Result<FrameMetrics> tick(double frameDeltaSeconds);
    void shutdown();

    [[nodiscard]] bool initialized() const noexcept {
        return initialized_;
    }
    [[nodiscard]] const FrameMetrics& lastFrameMetrics() const noexcept {
        return lastMetrics_;
    }

  private:
    [[nodiscard]] Result<void> validateConfig(const EngineLoopConfig& config) const;

    EngineLoopConfig config_;
    EngineLoopCallbacks callbacks_;
    FrameMetrics lastMetrics_;
    double accumulatorSeconds_ = 0.0;
    std::uint64_t nextFrameIndex_ = 1;
    bool initialized_ = false;
};

} // namespace fabgl
