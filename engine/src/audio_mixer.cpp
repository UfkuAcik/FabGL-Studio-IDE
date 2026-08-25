#include "fabgl/audio/audio_mixer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace fabgl {
namespace {

[[nodiscard]] bool validVolume(float value) noexcept {
    return std::isfinite(value) && value >= 0.0F;
}

[[nodiscard]] bool validPan(float value) noexcept {
    return std::isfinite(value) && value >= -1.0F && value <= 1.0F;
}

[[nodiscard]] Result<void> validateBusSettings(const AudioBusSettings& settings) {
    if (!validVolume(settings.volume)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "audio bus volume must be finite and non-negative"));
    }
    if (!validPan(settings.pan)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "audio bus pan must be within [-1, 1]"));
    }
    return Result<void>::success();
}

} // namespace

AudioMixer::AudioMixer(AudioMixerConfig config) : config_(config) {
    if (config_.outputSampleRate == 0) {
        throw std::invalid_argument("audio mixer output sample rate cannot be zero");
    }
    if (config_.maximumBuses == 0) {
        throw std::invalid_argument("audio mixer must have room for the master bus");
    }
    if (config_.mixBlockFrames == 0 ||
        config_.mixBlockFrames > std::numeric_limits<std::size_t>::max() / 2U) {
        throw std::invalid_argument("audio mixer block size is invalid");
    }

    buses_.reserve(config_.maximumBuses);
    buses_.push_back({MasterAudioBus, {}});
    voices_.resize(config_.maximumVoices);
    mixBuffer_.resize(config_.mixBlockFrames * 2U);
}

Result<void> AudioMixer::createBus(AudioBusId id, AudioBusSettings settings) {
    if (id == MasterAudioBus) {
        return Result<void>::failure(
            Error(ErrorCode::AlreadyExists, "the master audio bus already exists"));
    }
    if (findBus(id) != nullptr) {
        return Result<void>::failure(Error(ErrorCode::AlreadyExists, "audio bus already exists")
                                         .addContext("bus", std::to_string(id.value)));
    }
    auto validation = validateBusSettings(settings);
    if (!validation) {
        return validation;
    }
    if (buses_.size() >= config_.maximumBuses) {
        return Result<void>::failure(
            Error(ErrorCode::CapacityExceeded, "audio bus capacity has been reached"));
    }
    buses_.push_back({id, settings});
    return Result<void>::success();
}

Result<void> AudioMixer::setBusVolume(AudioBusId id, float volume) {
    if (!validVolume(volume)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "audio bus volume must be finite and non-negative"));
    }
    auto* bus = findBus(id);
    if (bus == nullptr) {
        return Result<void>::failure(Error(ErrorCode::NotFound, "audio bus was not found"));
    }
    bus->settings.volume = volume;
    return Result<void>::success();
}

Result<void> AudioMixer::setBusPan(AudioBusId id, float pan) {
    if (!validPan(pan)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "audio bus pan must be within [-1, 1]"));
    }
    auto* bus = findBus(id);
    if (bus == nullptr) {
        return Result<void>::failure(Error(ErrorCode::NotFound, "audio bus was not found"));
    }
    bus->settings.pan = pan;
    return Result<void>::success();
}

Result<void> AudioMixer::setBusMuted(AudioBusId id, bool muted) {
    auto* bus = findBus(id);
    if (bus == nullptr) {
        return Result<void>::failure(Error(ErrorCode::NotFound, "audio bus was not found"));
    }
    bus->settings.muted = muted;
    return Result<void>::success();
}

const AudioBusSettings* AudioMixer::busSettings(AudioBusId id) const noexcept {
    const auto* bus = findBus(id);
    return bus == nullptr ? nullptr : &bus->settings;
}

Result<AudioVoiceId> AudioMixer::play(AudioClipView clip, AudioVoiceSettings settings) {
    if ((clip.interleavedSamples == nullptr && clip.frameReader == nullptr) ||
        clip.frameCount == 0) {
        return Result<AudioVoiceId>::failure(
            Error(ErrorCode::InvalidArgument, "audio clip must contain at least one frame"));
    }
    if (clip.channelCount != 1 && clip.channelCount != 2) {
        return Result<AudioVoiceId>::failure(
            Error(ErrorCode::InvalidArgument, "audio clip must be mono or stereo"));
    }
    if (clip.sampleRate == 0) {
        return Result<AudioVoiceId>::failure(
            Error(ErrorCode::InvalidArgument, "audio clip sample rate cannot be zero"));
    }
    if (!validVolume(settings.volume) || !validPan(settings.pan) ||
        !std::isfinite(settings.pitch) || settings.pitch <= 0.0F) {
        return Result<AudioVoiceId>::failure(
            Error(ErrorCode::InvalidArgument, "audio voice volume, pan, or pitch is invalid"));
    }
    if (findBus(settings.bus) == nullptr) {
        return Result<AudioVoiceId>::failure(
            Error(ErrorCode::NotFound, "audio voice references an unknown bus"));
    }

    Voice* selected = nullptr;
    for (auto& voice : voices_) {
        if (!voice.active) {
            selected = &voice;
            break;
        }
    }

    if (selected == nullptr) {
        Voice* victim = nullptr;
        for (auto& voice : voices_) {
            if (victim == nullptr || voice.settings.priority < victim->settings.priority ||
                (voice.settings.priority == victim->settings.priority &&
                 voice.startSequence < victim->startSequence)) {
                victim = &voice;
            }
        }
        if (victim == nullptr || settings.priority < victim->settings.priority) {
            ++voicesRejected_;
            return Result<AudioVoiceId>::failure(
                Error(ErrorCode::CapacityExceeded, "all audio voices have a higher priority"));
        }
        selected = victim;
        ++voicesStolen_;
    } else {
        ++activeVoiceCount_;
    }

    selected->id = allocateVoiceId();
    selected->clip = clip;
    selected->settings = settings;
    selected->framePosition = 0.0;
    selected->startSequence = ++startSequence_;
    selected->streamCacheFirstFrame = 0U;
    selected->streamCacheFrameCount = 0U;
    selected->active = true;
    ++voicesStarted_;
    return Result<AudioVoiceId>::success(selected->id);
}

bool AudioMixer::stop(AudioVoiceId id) noexcept {
    auto* voice = findVoice(id);
    if (voice == nullptr) {
        return false;
    }
    deactivate(*voice);
    return true;
}

void AudioMixer::stopAll() noexcept {
    for (auto& voice : voices_) {
        if (voice.active) {
            deactivate(voice);
        }
    }
}

bool AudioMixer::isPlaying(AudioVoiceId id) const noexcept {
    return findVoice(id) != nullptr;
}

Result<void> AudioMixer::mixTo(float* interleavedStereo, std::size_t frameCount) {
    if (interleavedStereo == nullptr && frameCount != 0) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "audio mix output cannot be null"));
    }
    if (frameCount > std::numeric_limits<std::size_t>::max() / 2U) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "audio mix frame count is too large"));
    }
    if (frameCount != 0) {
        std::fill(interleavedStereo, interleavedStereo + frameCount * 2U, 0.0F);
    }
    for (auto& voice : voices_) {
        if (voice.active) {
            mixVoice(voice, interleavedStereo, frameCount);
        }
    }
    for (std::size_t index = 0; index < frameCount * 2U; ++index) {
        interleavedStereo[index] = std::clamp(interleavedStereo[index], -1.0F, 1.0F);
    }
    mixedFrames_ += static_cast<std::uint64_t>(frameCount);
    return Result<void>::success();
}

Result<void> AudioMixer::render(std::size_t frameCount) {
    if (backend_ == nullptr) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "audio output backend is not configured"));
    }
    return render(frameCount, *backend_);
}

Result<void> AudioMixer::render(std::size_t frameCount, IAudioOutputBackend& backend) {
    if (frameCount > config_.mixBlockFrames) {
        return Result<void>::failure(
            Error(ErrorCode::CapacityExceeded, "audio render exceeds the configured mix block")
                .addContext("requested_frames", std::to_string(frameCount))
                .addContext("block_frames", std::to_string(config_.mixBlockFrames)));
    }
    auto mixed = mixTo(mixBuffer_.data(), frameCount);
    if (!mixed) {
        return mixed;
    }
    return backend.submitStereo(mixBuffer_.data(), frameCount, config_.outputSampleRate);
}

AudioMixerStats AudioMixer::stats() const noexcept {
    return {
        activeVoiceCount_, voices_.size(),      voicesStarted_,  voicesStolen_,    voicesRejected_,
        mixedFrames_,      streamCacheRefills_, streamedFrames_, streamUnderruns_,
    };
}

AudioMixer::Bus* AudioMixer::findBus(AudioBusId id) noexcept {
    for (auto& bus : buses_) {
        if (bus.id == id) {
            return &bus;
        }
    }
    return nullptr;
}

const AudioMixer::Bus* AudioMixer::findBus(AudioBusId id) const noexcept {
    for (const auto& bus : buses_) {
        if (bus.id == id) {
            return &bus;
        }
    }
    return nullptr;
}

AudioMixer::Voice* AudioMixer::findVoice(AudioVoiceId id) noexcept {
    if (!id.valid()) {
        return nullptr;
    }
    for (auto& voice : voices_) {
        if (voice.active && voice.id == id) {
            return &voice;
        }
    }
    return nullptr;
}

const AudioMixer::Voice* AudioMixer::findVoice(AudioVoiceId id) const noexcept {
    if (!id.valid()) {
        return nullptr;
    }
    for (const auto& voice : voices_) {
        if (voice.active && voice.id == id) {
            return &voice;
        }
    }
    return nullptr;
}

AudioVoiceId AudioMixer::allocateVoiceId() noexcept {
    for (;;) {
        const AudioVoiceId candidate{nextVoiceId_++};
        if (nextVoiceId_ == 0) {
            nextVoiceId_ = 1;
        }
        if (candidate.valid() && findVoice(candidate) == nullptr) {
            return candidate;
        }
    }
}

void AudioMixer::deactivate(Voice& voice) noexcept {
    if (!voice.active) {
        return;
    }
    voice.active = false;
    voice.id = {};
    if (activeVoiceCount_ != 0) {
        --activeVoiceCount_;
    }
}

float AudioMixer::readSample(Voice& voice, const std::size_t frame,
                             const std::size_t channel) noexcept {
    const auto channels = static_cast<std::size_t>(voice.clip.channelCount);
    if (voice.clip.interleavedSamples != nullptr) {
        return voice.clip.interleavedSamples[frame * channels + channel];
    }
    if (voice.clip.frameReader == nullptr || frame >= voice.clip.frameCount ||
        channel >= channels) {
        return 0.0F;
    }
    const auto cacheEnd = voice.streamCacheFirstFrame + voice.streamCacheFrameCount;
    if (voice.streamCacheFrameCount == 0U || frame < voice.streamCacheFirstFrame ||
        frame >= cacheEnd) {
        const auto requested = std::min(Voice::StreamCacheFrames, voice.clip.frameCount - frame);
        std::fill_n(voice.streamCache.data(), requested * channels, 0.0F);
        const auto read =
            std::min(requested, voice.clip.frameReader(voice.clip.readerContext, frame,
                                                       voice.streamCache.data(), requested));
        voice.streamCacheFirstFrame = frame;
        voice.streamCacheFrameCount = requested;
        ++streamCacheRefills_;
        streamedFrames_ += static_cast<std::uint64_t>(read);
        if (read != requested) {
            ++streamUnderruns_;
        }
    }
    return voice.streamCache[(frame - voice.streamCacheFirstFrame) * channels + channel];
}

void AudioMixer::mixVoice(Voice& voice, float* output, std::size_t frameCount) noexcept {
    const auto* voiceBus = findBus(voice.settings.bus);
    const auto* masterBus = findBus(MasterAudioBus);
    if (voiceBus == nullptr || masterBus == nullptr) {
        deactivate(voice);
        return;
    }

    const bool usesMasterDirectly = voice.settings.bus == MasterAudioBus;
    const bool muted =
        voiceBus->settings.muted || (!usesMasterDirectly && masterBus->settings.muted);
    const float masterVolume = usesMasterDirectly ? 1.0F : masterBus->settings.volume;
    const float volume =
        muted ? 0.0F : voice.settings.volume * voiceBus->settings.volume * masterVolume;
    const float masterPan = usesMasterDirectly ? 0.0F : masterBus->settings.pan;
    const float pan =
        std::clamp(voice.settings.pan + voiceBus->settings.pan + masterPan, -1.0F, 1.0F);
    const float leftGain = volume * (pan > 0.0F ? 1.0F - pan : 1.0F);
    const float rightGain = volume * (pan < 0.0F ? 1.0F + pan : 1.0F);
    const auto clipFrames = static_cast<double>(voice.clip.frameCount);
    const double sourceRate = static_cast<double>(voice.clip.sampleRate);
    const double outputRate = static_cast<double>(config_.outputSampleRate);
    const double step = sourceRate * static_cast<double>(voice.settings.pitch) / outputRate;

    for (std::size_t outputFrame = 0; outputFrame < frameCount; ++outputFrame) {
        if (voice.framePosition >= clipFrames) {
            if (!voice.settings.loop) {
                deactivate(voice);
                break;
            }
            voice.framePosition = std::fmod(voice.framePosition, clipFrames);
        }

        const auto firstFrame = static_cast<std::size_t>(voice.framePosition);
        std::size_t secondFrame = firstFrame + 1U;
        if (secondFrame >= voice.clip.frameCount) {
            secondFrame = voice.settings.loop ? 0U : firstFrame;
        }
        const auto fraction =
            static_cast<float>(voice.framePosition - static_cast<double>(firstFrame));
        const auto interpolate = [fraction](float first, float second) noexcept {
            return first + (second - first) * fraction;
        };

        float left = 0.0F;
        float right = 0.0F;
        if (voice.clip.channelCount == 1) {
            const auto firstSample = readSample(voice, firstFrame, 0);
            const auto secondSample = readSample(voice, secondFrame, 0);
            const float sample = interpolate(firstSample, secondSample);
            left = sample;
            right = sample;
        } else {
            const auto firstLeft = readSample(voice, firstFrame, 0);
            const auto firstRight = readSample(voice, firstFrame, 1);
            const auto secondLeft = readSample(voice, secondFrame, 0);
            const auto secondRight = readSample(voice, secondFrame, 1);
            left = interpolate(firstLeft, secondLeft);
            right = interpolate(firstRight, secondRight);
        }

        output[outputFrame * 2U] += left * leftGain;
        output[outputFrame * 2U + 1U] += right * rightGain;
        voice.framePosition += step;
        if (!voice.settings.loop && voice.framePosition >= clipFrames) {
            deactivate(voice);
            break;
        }
    }
}

} // namespace fabgl
