#pragma once

#include "fabgl/core/result.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fabgl {

struct AudioBusId final {
    std::uint16_t value = 0;

    friend constexpr bool operator==(AudioBusId lhs, AudioBusId rhs) noexcept {
        return lhs.value == rhs.value;
    }
    friend constexpr bool operator!=(AudioBusId lhs, AudioBusId rhs) noexcept {
        return !(lhs == rhs);
    }
};

inline constexpr AudioBusId MasterAudioBus{};

struct AudioVoiceId final {
    std::uint32_t value = 0;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return value != 0;
    }
    friend constexpr bool operator==(AudioVoiceId lhs, AudioVoiceId rhs) noexcept {
        return lhs.value == rhs.value;
    }
    friend constexpr bool operator!=(AudioVoiceId lhs, AudioVoiceId rhs) noexcept {
        return !(lhs == rhs);
    }
};

// The sample storage must remain alive while a voice using this view is active.
struct AudioClipView final {
    const float* interleavedSamples = nullptr;
    std::size_t frameCount = 0;
    std::uint8_t channelCount = 1;
    std::uint32_t sampleRate = 0;
};

struct AudioBusSettings final {
    float volume = 1.0F;
    float pan = 0.0F;
    bool muted = false;
};

struct AudioVoiceSettings final {
    AudioBusId bus = MasterAudioBus;
    float volume = 1.0F;
    float pan = 0.0F;
    float pitch = 1.0F;
    int priority = 0;
    bool loop = false;
};

struct AudioMixerConfig final {
    std::uint32_t outputSampleRate = 48'000;
    std::size_t maximumVoices = 8;
    std::size_t maximumBuses = 8;
    std::size_t mixBlockFrames = 512;
};

struct AudioMixerStats final {
    std::size_t activeVoices = 0;
    std::size_t maximumVoices = 0;
    std::uint64_t voicesStarted = 0;
    std::uint64_t voicesStolen = 0;
    std::uint64_t voicesRejected = 0;
    std::uint64_t mixedFrames = 0;
};

class IAudioOutputBackend {
  public:
    virtual ~IAudioOutputBackend() = default;

    // Samples are interleaved stereo floats in the closed range [-1, 1]. The pointer is only
    // valid for the duration of this call.
    [[nodiscard]] virtual Result<void> submitStereo(const float* interleavedSamples,
                                                    std::size_t frameCount,
                                                    std::uint32_t sampleRate) = 0;
};

class AudioMixer final {
  public:
    explicit AudioMixer(AudioMixerConfig config = {});

    [[nodiscard]] Result<void> createBus(AudioBusId id, AudioBusSettings settings = {});
    [[nodiscard]] Result<void> setBusVolume(AudioBusId id, float volume);
    [[nodiscard]] Result<void> setBusPan(AudioBusId id, float pan);
    [[nodiscard]] Result<void> setBusMuted(AudioBusId id, bool muted);
    [[nodiscard]] const AudioBusSettings* busSettings(AudioBusId id) const noexcept;

    [[nodiscard]] Result<AudioVoiceId> play(AudioClipView clip, AudioVoiceSettings settings = {});
    [[nodiscard]] bool stop(AudioVoiceId id) noexcept;
    void stopAll() noexcept;
    [[nodiscard]] bool isPlaying(AudioVoiceId id) const noexcept;

    // mixTo advances all voices and writes exactly frameCount stereo frames.
    [[nodiscard]] Result<void> mixTo(float* interleavedStereo, std::size_t frameCount);

    void setOutputBackend(IAudioOutputBackend* backend) noexcept {
        backend_ = backend;
    }
    [[nodiscard]] IAudioOutputBackend* outputBackend() const noexcept {
        return backend_;
    }
    [[nodiscard]] Result<void> render(std::size_t frameCount);
    [[nodiscard]] Result<void> render(std::size_t frameCount, IAudioOutputBackend& backend);

    [[nodiscard]] std::uint32_t outputSampleRate() const noexcept {
        return config_.outputSampleRate;
    }
    [[nodiscard]] std::size_t maximumVoices() const noexcept {
        return voices_.size();
    }
    [[nodiscard]] std::size_t maximumBuses() const noexcept {
        return config_.maximumBuses;
    }
    [[nodiscard]] std::size_t mixBlockFrames() const noexcept {
        return config_.mixBlockFrames;
    }
    [[nodiscard]] AudioMixerStats stats() const noexcept;

  private:
    struct Bus final {
        AudioBusId id{};
        AudioBusSettings settings{};
    };

    struct Voice final {
        AudioVoiceId id{};
        AudioClipView clip{};
        AudioVoiceSettings settings{};
        double framePosition = 0.0;
        std::uint64_t startSequence = 0;
        bool active = false;
    };

    [[nodiscard]] Bus* findBus(AudioBusId id) noexcept;
    [[nodiscard]] const Bus* findBus(AudioBusId id) const noexcept;
    [[nodiscard]] Voice* findVoice(AudioVoiceId id) noexcept;
    [[nodiscard]] const Voice* findVoice(AudioVoiceId id) const noexcept;
    [[nodiscard]] AudioVoiceId allocateVoiceId() noexcept;
    void deactivate(Voice& voice) noexcept;
    void mixVoice(Voice& voice, float* output, std::size_t frameCount) noexcept;

    AudioMixerConfig config_{};
    std::vector<Bus> buses_;
    std::vector<Voice> voices_;
    std::vector<float> mixBuffer_;
    IAudioOutputBackend* backend_ = nullptr;
    std::uint32_t nextVoiceId_ = 1;
    std::uint64_t startSequence_ = 0;
    std::size_t activeVoiceCount_ = 0;
    std::uint64_t voicesStarted_ = 0;
    std::uint64_t voicesStolen_ = 0;
    std::uint64_t voicesRejected_ = 0;
    std::uint64_t mixedFrames_ = 0;
};

} // namespace fabgl
