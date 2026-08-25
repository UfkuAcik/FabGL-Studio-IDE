#pragma once

#include <fabgl/audio/audio_mixer.h>
#include <fabgl/core/guid.h>
#include <fabgl/core/result.h>
#include <fabgl/project/project_asset_library.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace fabgl {
class Scene;
}

namespace fabgl::project {

using ProjectAudioClipResolver =
    std::function<std::shared_ptr<const ProjectAudioClip>(AssetGuid)>;

struct ProjectSceneAudioStats final {
    std::uint32_t listeners = 0U;
    std::uint32_t sources = 0U;
    std::uint32_t voicesStarted = 0U;
};

// Binds reflected AudioListener/AudioSource components to the shared mixer.
// Clip ownership is retained here because AudioMixer deliberately accepts a
// lightweight, non-owning AudioClipView.
class ProjectSceneAudioRuntime final {
  public:
    ProjectSceneAudioRuntime(Scene& scene, const ProjectAssetLibrary& assets,
                             AudioMixer& mixer);
    ProjectSceneAudioRuntime(Scene& scene, ProjectAudioClipResolver clipResolver,
                             AudioMixer& mixer);
    ~ProjectSceneAudioRuntime();

    ProjectSceneAudioRuntime(const ProjectSceneAudioRuntime&) = delete;
    ProjectSceneAudioRuntime& operator=(const ProjectSceneAudioRuntime&) = delete;

    [[nodiscard]] Result<void> initialize();
    void shutdown() noexcept;

    [[nodiscard]] bool initialized() const noexcept {
        return initialized_;
    }
    [[nodiscard]] std::size_t activeVoiceCount() const noexcept;
    [[nodiscard]] const ProjectSceneAudioStats& stats() const noexcept {
        return stats_;
    }

  private:
    struct Binding final {
        EntityGuid entity;
        std::shared_ptr<const ProjectAudioClip> clip;
        AudioVoiceId voice;
    };

    Scene* scene_ = nullptr;
    ProjectAudioClipResolver clipResolver_;
    AudioMixer* mixer_ = nullptr;
    std::vector<Binding> bindings_;
    ProjectSceneAudioStats stats_;
    bool initialized_ = false;
};

} // namespace fabgl::project
