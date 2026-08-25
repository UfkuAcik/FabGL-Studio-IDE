#pragma once

namespace fgl::studio {

enum class PreviewKind { None, StudioPlaying, StudioPaused, ExternalPlayer };

enum class PreviewRestartAction {
    None,
    StopStudio,
    StopExternalPlayer,
    BuildPc,
    StartStudioPlaying,
    StartStudioPaused,
    StartExternalPlayer,
    ReportFailure
};

// Deterministic coordinator for the supported native-script fallback: stop the
// preview, rebuild the PC gameplay module, then start a fresh preview process.
// Requests arriving during a build are coalesced into one additional build so
// a preview never starts with an older module than the last saved source.
class PreviewRestartController final {
  public:
    enum class Phase { Idle, StoppingPreview, Building };

    [[nodiscard]] PreviewRestartAction request(PreviewKind activePreview) noexcept {
        if (phase_ == Phase::Building) {
            dirtyDuringBuild_ = true;
            return PreviewRestartAction::None;
        }
        if (phase_ == Phase::StoppingPreview || activePreview == PreviewKind::None) {
            return PreviewRestartAction::None;
        }
        target_ = activePreview;
        phase_ = Phase::StoppingPreview;
        return target_ == PreviewKind::ExternalPlayer
                   ? PreviewRestartAction::StopExternalPlayer
                   : PreviewRestartAction::StopStudio;
    }

    [[nodiscard]] PreviewRestartAction previewStopped() noexcept {
        if (phase_ != Phase::StoppingPreview) {
            return PreviewRestartAction::None;
        }
        phase_ = Phase::Building;
        dirtyDuringBuild_ = false;
        return PreviewRestartAction::BuildPc;
    }

    [[nodiscard]] PreviewRestartAction buildFinished(const bool succeeded) noexcept {
        if (phase_ != Phase::Building) {
            return PreviewRestartAction::None;
        }
        if (!succeeded) {
            clear();
            return PreviewRestartAction::ReportFailure;
        }
        if (dirtyDuringBuild_) {
            dirtyDuringBuild_ = false;
            return PreviewRestartAction::BuildPc;
        }
        const auto target = target_;
        clear();
        switch (target) {
        case PreviewKind::StudioPlaying:
            return PreviewRestartAction::StartStudioPlaying;
        case PreviewKind::StudioPaused:
            return PreviewRestartAction::StartStudioPaused;
        case PreviewKind::ExternalPlayer:
            return PreviewRestartAction::StartExternalPlayer;
        case PreviewKind::None:
            return PreviewRestartAction::None;
        }
        return PreviewRestartAction::None;
    }

    void clear() noexcept {
        phase_ = Phase::Idle;
        target_ = PreviewKind::None;
        dirtyDuringBuild_ = false;
    }

    [[nodiscard]] Phase phase() const noexcept {
        return phase_;
    }
    [[nodiscard]] PreviewKind target() const noexcept {
        return target_;
    }
    [[nodiscard]] bool pending() const noexcept {
        return phase_ != Phase::Idle;
    }
    [[nodiscard]] bool rebuildDeferred() const noexcept {
        return dirtyDuringBuild_;
    }

  private:
    Phase phase_ = Phase::Idle;
    PreviewKind target_ = PreviewKind::None;
    bool dirtyDuringBuild_ = false;
};

} // namespace fgl::studio
