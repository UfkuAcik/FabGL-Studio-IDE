#pragma once

#include <fabgl/project/project_asset_library.h>
#include <fabgl/resources/asset_streaming_manager.h>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fabgl {
class Scene;
}

namespace fabgl::project {

struct Manifest;

struct ProjectAssetStreamingConfig final {
    ProjectAssetLibraryLimits decoding;
    AssetStreamingLimits residency;
    std::size_t loadsPerUpdate = 4U;
};

// Filesystem-backed adapter between a project manifest and the engine's
// deterministic residency manager. Each resident root owns a selected
// ProjectAssetLibrary containing only that root and its hard dependency
// closure. Scene presentation, animation, and audio resolve through the same
// cache, so eviction changes real runtime availability rather than a demo-only
// counter.
class ProjectAssetStreamingRuntime final {
  public:
    [[nodiscard]] static Result<ProjectAssetStreamingRuntime>
    create(std::string projectRoot, Manifest manifest,
           const ProjectAssetStreamingConfig& config = {});

    [[nodiscard]] static Result<std::vector<AssetGuid>>
    collectSceneRoots(const Scene& scene, const Manifest& manifest);

    [[nodiscard]] Result<void> beginTransition(std::vector<AssetGuid> roots);
    [[nodiscard]] Result<void> loadTransitionBlocking(std::vector<AssetGuid> roots);
    [[nodiscard]] Result<std::size_t> update();
    [[nodiscard]] std::size_t evictUnused();

    [[nodiscard]] rendering::ScenePresentationResources resources() const;
    [[nodiscard]] Result<std::unique_ptr<AnimatorController>>
    createAnimator(AssetGuid controller) const;
    [[nodiscard]] std::shared_ptr<const ProjectAudioClip>
    audioClip(AssetGuid guid) const noexcept;

    [[nodiscard]] AssetTransitionState transitionState() const noexcept;
    [[nodiscard]] std::vector<AssetGuid> activeRoots() const;
    [[nodiscard]] std::vector<AssetGuid> pendingRoots() const;
    [[nodiscard]] AssetStreamingStats stats() const noexcept;
    [[nodiscard]] std::size_t residentAssetCount(std::string_view type = {}) const;
    [[nodiscard]] std::vector<AssetStreamingDiagnostic> diagnostics() const;

  private:
    struct State;
    explicit ProjectAssetStreamingRuntime(std::shared_ptr<State> state)
        : state_(std::move(state)) {}

    std::shared_ptr<State> state_;
};

} // namespace fabgl::project
