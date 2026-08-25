#pragma once

#include <fabgl/core/guid.h>
#include <fabgl/core/result.h>
#include <fabgl/visual/visual_graph.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fabgl {

class AudioMixer;
class InputMap;
class Scene;
class SceneRuntime;

namespace project {

struct ProjectAudioClip;

struct ProjectVisualHostLimits final {
    std::size_t maximumRetainedAudioClips = 32U;
    std::size_t maximumDiagnostics = 128U;
    std::size_t maximumSceneLoadRequests = 16U;
    std::size_t maximumPayloadBytes = 256U;
};

struct ProjectVisualHostServices final {
    using AudioClipResolver =
        std::function<std::shared_ptr<const ProjectAudioClip>(AssetGuid)>;
    using SceneLoadHandler = std::function<Result<void>(AssetGuid)>;

    Scene* scene = nullptr;
    InputMap* input = nullptr;
    AudioMixer* audio = nullptr;
    AudioClipResolver audioClipResolver;
    // If omitted, validated requests are retained in a bounded queue and can
    // be consumed by the owning application with takeSceneLoadRequests().
    SceneLoadHandler sceneLoadHandler;
};

struct ProjectVisualHostDiagnostic final {
    std::uint64_t sequence = 0U;
    std::string callback;
    std::string payload;
    ErrorCode code = ErrorCode::None;
    std::string message;
    bool failure = false;
};

struct ProjectVisualHostStats final {
    std::uint64_t calls = 0U;
    std::uint64_t failures = 0U;
    std::uint64_t audioVoicesStarted = 0U;
    std::uint64_t sceneLoadRequests = 0U;
};

// Production host adapter for portable visual bytecode on desktop runtimes.
// Callback closures hold a weak state reference, so a copied callback table
// fails deterministically instead of dereferencing services after this host is
// destroyed. Service objects and a bound SceneRuntime must outlive the host.
class ProjectVisualHost final {
  public:
    // Opaque callback state; public only so translation-unit helpers can name
    // the incomplete type. Applications never construct or access it.
    struct State;

    [[nodiscard]] static Result<ProjectVisualHost>
    create(ProjectVisualHostServices services, ProjectVisualHostLimits limits = {});

    ProjectVisualHost(ProjectVisualHost&&) noexcept = default;
    ProjectVisualHost& operator=(ProjectVisualHost&& other) noexcept;
    ProjectVisualHost(const ProjectVisualHost&) = delete;
    ProjectVisualHost& operator=(const ProjectVisualHost&) = delete;
    ~ProjectVisualHost();

    [[nodiscard]] Result<void> bindRuntime(SceneRuntime& runtime);
    void unbindRuntime() noexcept;

    [[nodiscard]] const VisualHostCallbackTable& callbacks() const noexcept {
        return callbacks_;
    }
    [[nodiscard]] ProjectVisualHostStats stats() const noexcept;
    [[nodiscard]] std::vector<ProjectVisualHostDiagnostic> diagnostics() const;
    [[nodiscard]] std::vector<AssetGuid> takeSceneLoadRequests();
    [[nodiscard]] std::size_t retainedAudioClipCount() const noexcept;

    // Editor/project-build schema. It has the same signatures as the runtime
    // host but performs no scene, audio, animation, or UI mutation.
    [[nodiscard]] static const VisualHostCallbackTable& validationCallbacks();
    [[nodiscard]] static const std::vector<std::string>& safeFunctionCallbacks();

  private:
    ProjectVisualHost(std::shared_ptr<State> state, VisualHostCallbackTable callbacks)
        : state_(std::move(state)), callbacks_(std::move(callbacks)) {}

    std::shared_ptr<State> state_;
    VisualHostCallbackTable callbacks_;
};

} // namespace project
} // namespace fabgl
