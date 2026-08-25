#pragma once

#include <fabgl/animation/animation_authoring.h>
#include <fabgl/assets/audio_importer.h>
#include <fabgl/audio/audio_mixer.h>
#include <fabgl/core/result.h>
#include <fabgl/rendering/scene_presenter.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fabgl::project {

struct Manifest;

struct ProjectAssetLibraryLimits final {
    std::size_t maximumAssets = 4096U;
    std::size_t maximumAssetBytes = 64U * 1024U * 1024U;
    std::size_t maximumAggregateBytes = 256U * 1024U * 1024U;
    std::size_t maximumTileKinds = 4096U;
};

struct ProjectAssetLibraryStats final {
    // Visual assets available to ScenePresenter. Kept separate so the
    // player-facing visual_assets metric remains backwards compatible.
    std::uint32_t loadedAssets = 0U;
    std::uint32_t loadedAnimationClips = 0U;
    std::uint32_t loadedAnimatorControllers = 0U;
    std::uint32_t loadedAudioClips = 0U;
    std::uint32_t loadedMaterials = 0U;
    std::uint32_t skippedNonVisualAssets = 0U;
    std::uint32_t loadedAssetEntries = 0U;
    std::size_t sourceBytes = 0U;
    // Estimated heap owned by decoded runtime objects. It excludes allocator
    // bookkeeping and is intended for deterministic residency budgeting, not
    // as a replacement for platform heap telemetry.
    std::size_t estimatedResidentBytes = 0U;
};

// Runtime-owned mono audio. Preloaded clips own normalized float PCM; streaming
// clips retain their compact .fgla payload and decode fixed-size windows through
// AudioClipView. Callers keep the shared clip alive for every mixer voice.
struct ProjectAudioClip final {
    std::uint32_t sampleRate = 0U;
    std::vector<float> samples;
    bool streaming = false;
    std::uint32_t loopStart = 0U;
    std::uint32_t loopEnd = 0U;
    assets::AudioClipInfo encodedInfo{};
    std::vector<std::uint8_t> encodedBytes;

    [[nodiscard]] AudioClipView view() const noexcept;
    [[nodiscard]] std::size_t frameCount() const noexcept;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool usesStreamingReader() const noexcept;

  private:
    [[nodiscard]] static std::size_t readStreamingFrames(const void* context,
                                                         std::size_t firstFrame, float* output,
                                                         std::size_t frameCount) noexcept;
};

class ProjectAssetLibrary final {
  public:
    ProjectAssetLibrary();

    [[nodiscard]] static Result<ProjectAssetLibrary>
    load(const std::string& projectRoot, const Manifest& manifest,
         const ProjectAssetLibraryLimits& limits = {});

    // Loads only the requested roots and their validated hard dependencies.
    // The original load() remains the eager, backwards-compatible path.
    [[nodiscard]] static Result<ProjectAssetLibrary>
    loadSelected(const std::string& projectRoot, const Manifest& manifest,
                 const std::vector<AssetGuid>& roots, const ProjectAssetLibraryLimits& limits = {});

    [[nodiscard]] static Result<std::vector<AssetGuid>>
    directDependencies(const std::string& projectRoot, const Manifest& manifest, AssetGuid asset,
                       const ProjectAssetLibraryLimits& limits = {});
    [[nodiscard]] static bool supportsRuntimeType(std::string_view type) noexcept;

    [[nodiscard]] rendering::ScenePresentationResources resources() const;
    [[nodiscard]] std::shared_ptr<const AnimationClip> animationClip(AssetGuid guid) const noexcept;
    [[nodiscard]] Result<std::unique_ptr<AnimatorController>>
    createAnimator(AssetGuid controller) const;
    [[nodiscard]] std::shared_ptr<const ProjectAudioClip> audioClip(AssetGuid guid) const noexcept;
    [[nodiscard]] const ProjectAssetLibraryStats& stats() const noexcept;

  private:
    struct State;
    explicit ProjectAssetLibrary(std::shared_ptr<const State> state);

    [[nodiscard]] static Result<ProjectAssetLibrary>
    loadInternal(const std::string& projectRoot, const Manifest& manifest,
                 const std::vector<AssetGuid>* selected, const ProjectAssetLibraryLimits& limits);

    std::shared_ptr<const State> state_;
};

} // namespace fabgl::project
