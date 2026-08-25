#pragma once

#include <fabgl/audio/audio_mixer.h>
#include <fabgl/core/result.h>
#include <fabgl/input/input_map.h>
#include <fabgl/project/project_asset_streaming.h>
#include <fabgl/project/project_scene_audio.h>
#include <fabgl/project/project_script_modules.h>
#include <fabgl/project/project_visual_host.h>
#include <fabgl/runtime/engine_loop.h>
#include <fabgl/runtime/scene_runtime.h>

#include <project_format.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fabgl {
class Scene;
namespace frameworks {
class SceneGameplayRuntime;
}
} // namespace fabgl

namespace fgl::studio {

struct StudioPlaySessionConfig final {
    std::string projectRoot;
    fabgl::project::Manifest manifest;
    std::vector<std::string> nativeScriptModules;
    fabgl::IAudioOutputBackend* audioOutput = nullptr;
};

struct StudioPlaySessionStats final {
    std::size_t animators = 0U;
    std::size_t visualScripts = 0U;
    std::size_t controlledGameplayEntities = 0U;
    std::size_t activeAudioVoices = 0U;
    std::uint32_t nativeScriptComponents = 0U;
    std::uint64_t gameplayUpdates = 0U;
    std::uint64_t mixedAudioFrames = 0U;
    std::size_t residentAssets = 0U;
    std::size_t residentAssetBytes = 0U;
    std::uint64_t streamedAssetLoads = 0U;
    std::uint64_t assetEvictions = 0U;
    std::uint64_t visualHostCalls = 0U;
    std::uint64_t visualHostFailures = 0U;
};

// Owns every transient layer used by in-editor project playback. Keeping these objects in one
// session makes their lifetime identical to the external player: native modules outlive the
// scene components they create, project assets outlive animator/audio views, and all runtime
// systems shut down before the isolated scene clone is destroyed.
class StudioPlaySession final {
  public:
    [[nodiscard]] static fabgl::Result<std::unique_ptr<StudioPlaySession>>
    create(std::unique_ptr<fabgl::Scene> scene, StudioPlaySessionConfig config);

    ~StudioPlaySession();

    StudioPlaySession(const StudioPlaySession&) = delete;
    StudioPlaySession& operator=(const StudioPlaySession&) = delete;
    StudioPlaySession(StudioPlaySession&&) = delete;
    StudioPlaySession& operator=(StudioPlaySession&&) = delete;

    [[nodiscard]] fabgl::Result<void> initialize();
    [[nodiscard]] fabgl::Result<fabgl::FrameMetrics> tick(double frameDeltaSeconds);
    void shutdown() noexcept;

    [[nodiscard]] fabgl::Result<void> setControlValue(std::string control, float value);
    void clearTransientControls() noexcept;

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] fabgl::Scene& scene() noexcept;
    [[nodiscard]] const fabgl::Scene& scene() const noexcept;
    [[nodiscard]] fabgl::SceneRuntime& runtime() noexcept;
    [[nodiscard]] const fabgl::SceneRuntime& runtime() const noexcept;
    [[nodiscard]] fabgl::rendering::ScenePresentationResources presentationResources() const;
    [[nodiscard]] StudioPlaySessionStats stats() const noexcept;

  private:
    StudioPlaySession(std::unique_ptr<fabgl::Scene> scene, StudioPlaySessionConfig config,
                      fabgl::project::ProjectAssetStreamingRuntime assets,
                      std::vector<fabgl::AssetGuid> sceneAssetRoots, fabgl::InputMap inputMap,
                      std::optional<fabgl::project::ProjectScriptModules> scriptModules);

    [[nodiscard]] fabgl::Result<void> configureAudioBuses();
    [[nodiscard]] fabgl::Result<void> configureVisualRuntime();
    void configureLoop();

    // Declaration order is intentional. Destruction is reversed, so the scene and every runtime
    // object using module code disappear before the native libraries are unloaded.
    std::optional<fabgl::project::ProjectScriptModules> scriptModules_;
    std::unique_ptr<fabgl::Scene> scene_;
    StudioPlaySessionConfig config_;
    fabgl::project::ProjectAssetStreamingRuntime assets_;
    std::vector<fabgl::AssetGuid> sceneAssetRoots_;
    fabgl::InputMap inputMap_;
    fabgl::AudioMixer audioMixer_;
    std::vector<float> audioDiscard_;
    std::optional<fabgl::project::ProjectVisualHost> visualHost_;
    std::unique_ptr<fabgl::SceneRuntime> runtime_;
    std::unique_ptr<fabgl::frameworks::SceneGameplayRuntime> gameplay_;
    std::unique_ptr<fabgl::project::ProjectSceneAudioRuntime> projectAudio_;
    fabgl::EngineLoop loop_;
    bool audioBusesConfigured_ = false;
    bool evictAfterTransition_ = false;
};

} // namespace fgl::studio
