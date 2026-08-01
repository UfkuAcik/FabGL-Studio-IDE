#include <fabgl/assets/audio_importer.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>

namespace fabgl::assets {

namespace {

bool fourCc(const std::vector<std::uint8_t>& bytes, std::size_t offset, const char* value) {
    return offset <= bytes.size() && bytes.size() - offset >= 4U &&
           std::memcmp(bytes.data() + offset, value, 4U) == 0;
}

bool readU16(const std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t& value) {
    if (offset > bytes.size() || bytes.size() - offset < 2U) {
        return false;
    }
    value = static_cast<std::uint16_t>(static_cast<std::uint32_t>(bytes[offset]) |
                                       (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U));
    return true;
}

bool readU32(const std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t& value) {
    if (offset > bytes.size() || bytes.size() - offset < 4U) {
        return false;
    }
    value = 0;
    for (unsigned int shift = 0; shift < 32U; shift += 8U) {
        value |= static_cast<std::uint32_t>(bytes[offset + shift / 8U]) << shift;
    }
    return true;
}

void appendU16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    const auto wide = static_cast<std::uint32_t>(value);
    output.push_back(static_cast<std::uint8_t>(wide & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((wide >> 8U) & 0xFFU));
}

void appendU32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    for (unsigned int shift = 0; shift < 32U; shift += 8U) {
        output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

float decodeSample(const std::uint8_t* sample, std::uint16_t bits) noexcept {
    switch (bits) {
    case 8:
        return (static_cast<float>(sample[0]) - 128.0F) / 128.0F;
    case 16: {
        const auto raw = static_cast<std::uint16_t>(sample[0]) |
                         static_cast<std::uint16_t>(static_cast<std::uint16_t>(sample[1]) << 8U);
        return static_cast<float>(static_cast<std::int16_t>(raw)) / 32768.0F;
    }
    case 24: {
        std::int32_t raw = static_cast<std::int32_t>(sample[0]) |
                           (static_cast<std::int32_t>(sample[1]) << 8) |
                           (static_cast<std::int32_t>(sample[2]) << 16);
        if ((raw & 0x00800000) != 0) {
            raw |= static_cast<std::int32_t>(0xFF000000U);
        }
        return static_cast<float>(raw) / 8388608.0F;
    }
    case 32: {
        const auto raw = static_cast<std::uint32_t>(sample[0]) |
                         (static_cast<std::uint32_t>(sample[1]) << 8U) |
                         (static_cast<std::uint32_t>(sample[2]) << 16U) |
                         (static_cast<std::uint32_t>(sample[3]) << 24U);
        return static_cast<float>(static_cast<std::int32_t>(raw)) / 2147483648.0F;
    }
    default:
        return 0.0F;
    }
}

} // namespace

bool AudioClip::valid() const noexcept {
    return sampleRate >= 4000U && sampleRate <= 192000U && !samples.empty() &&
           loopStart <= loopEnd && loopEnd <= samples.size();
}

Result<AudioClip> importWav(const std::vector<std::uint8_t>& bytes,
                            const AudioImportSettings& settings) {
    if (settings.targetSampleRate < 4000U || settings.targetSampleRate > 48000U ||
        !std::isfinite(settings.silenceThreshold) || settings.silenceThreshold < 0.0F ||
        settings.silenceThreshold > 1.0F) {
        return Result<AudioClip>::failure(
            Error(ErrorCode::InvalidArgument, "invalid audio import settings"));
    }
    if (bytes.size() < 12U || !fourCc(bytes, 0U, "RIFF") || !fourCc(bytes, 8U, "WAVE")) {
        return Result<AudioClip>::failure(
            Error(ErrorCode::InvalidFormat, "input is not a RIFF/WAVE file"));
    }

    std::uint16_t format = 0;
    std::uint16_t channels = 0;
    std::uint16_t bits = 0;
    std::uint32_t sourceRate = 0;
    std::size_t dataOffset = 0;
    std::size_t dataSize = 0;
    auto offset = std::size_t{12};
    while (offset <= bytes.size() && bytes.size() - offset >= 8U) {
        std::uint32_t chunkSize = 0;
        if (!readU32(bytes, offset + 4U, chunkSize)) {
            break;
        }
        const auto content = offset + 8U;
        if (content > bytes.size() || chunkSize > bytes.size() - content) {
            return Result<AudioClip>::failure(
                Error(ErrorCode::InvalidFormat, "WAV chunk exceeds file bounds"));
        }
        if (fourCc(bytes, offset, "fmt ")) {
            if (chunkSize < 16U || !readU16(bytes, content, format) ||
                !readU16(bytes, content + 2U, channels) ||
                !readU32(bytes, content + 4U, sourceRate) || !readU16(bytes, content + 14U, bits)) {
                return Result<AudioClip>::failure(
                    Error(ErrorCode::InvalidFormat, "invalid WAV fmt chunk"));
            }
        } else if (fourCc(bytes, offset, "data")) {
            dataOffset = content;
            dataSize = chunkSize;
        }
        offset = content + static_cast<std::size_t>(chunkSize) + (chunkSize & 1U);
    }
    if (format != 1U || (channels != 1U && channels != 2U) ||
        (bits != 8U && bits != 16U && bits != 24U && bits != 32U) || sourceRate < 4000U ||
        sourceRate > 192000U || dataSize == 0U) {
        return Result<AudioClip>::failure(
            Error(ErrorCode::InvalidFormat, "WAV must be PCM, mono/stereo, 8/16/24/32-bit"));
    }
    const auto bytesPerSample = static_cast<std::size_t>(bits / 8U);
    const auto frameSize = bytesPerSample * channels;
    if (frameSize == 0U || dataSize % frameSize != 0U) {
        return Result<AudioClip>::failure(
            Error(ErrorCode::InvalidFormat, "WAV data is not frame aligned"));
    }
    const auto frameCount = dataSize / frameSize;
    if (frameCount == 0U || frameCount > 100000000U) {
        return Result<AudioClip>::failure(
            Error(ErrorCode::CapacityExceeded, "WAV sample count is invalid"));
    }

    std::vector<float> mono(frameCount);
    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        auto mixed = 0.0F;
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
            const auto sampleOffset =
                dataOffset + frame * frameSize + static_cast<std::size_t>(channel) * bytesPerSample;
            mixed += decodeSample(bytes.data() + sampleOffset, bits);
        }
        mono[frame] = mixed / static_cast<float>(channels);
    }

    const auto ratio =
        static_cast<double>(settings.targetSampleRate) / static_cast<double>(sourceRate);
    const auto resampledCount = std::max<std::size_t>(
        1U, static_cast<std::size_t>(std::llround(static_cast<double>(mono.size()) * ratio)));
    if (resampledCount > 100000000U) {
        return Result<AudioClip>::failure(
            Error(ErrorCode::CapacityExceeded, "resampled audio is too large"));
    }
    std::vector<float> resampled(resampledCount);
    for (std::size_t index = 0; index < resampled.size(); ++index) {
        const auto sourcePosition = static_cast<double>(index) / ratio;
        const auto lower = std::min(static_cast<std::size_t>(sourcePosition), mono.size() - 1U);
        const auto upper = std::min(lower + 1U, mono.size() - 1U);
        const auto fraction = static_cast<float>(sourcePosition - static_cast<double>(lower));
        resampled[index] = mono[lower] + (mono[upper] - mono[lower]) * fraction;
    }

    if (settings.trimSilence) {
        auto first = std::find_if(resampled.begin(), resampled.end(), [&](float sample) {
            return std::fabs(sample) > settings.silenceThreshold;
        });
        auto last = std::find_if(resampled.rbegin(), resampled.rend(), [&](float sample) {
                        return std::fabs(sample) > settings.silenceThreshold;
                    }).base();
        if (first < last) {
            resampled = std::vector<float>(first, last);
        } else {
            resampled.assign(1U, 0.0F);
        }
    }

    if (settings.normalize) {
        auto peak = 0.0F;
        for (const auto sample : resampled) {
            peak = std::max(peak, std::fabs(sample));
        }
        if (peak > 0.000001F) {
            const auto gain = 0.98F / peak;
            for (auto& sample : resampled) {
                sample *= gain;
            }
        }
    }

    AudioClip clip;
    clip.sampleRate = settings.targetSampleRate;
    clip.streaming = settings.streaming;
    clip.samples.reserve(resampled.size());
    for (const auto sample : resampled) {
        const auto clamped = std::clamp(sample, -1.0F, 1.0F);
        clip.samples.push_back(static_cast<std::int16_t>(std::lround(clamped * 32767.0F)));
    }
    clip.loopStart = std::min<std::uint32_t>(settings.loopStart,
                                             static_cast<std::uint32_t>(clip.samples.size()));
    clip.loopEnd = settings.loopEnd == 0U
                       ? static_cast<std::uint32_t>(clip.samples.size())
                       : std::min<std::uint32_t>(settings.loopEnd,
                                                 static_cast<std::uint32_t>(clip.samples.size()));
    if (clip.loopStart > clip.loopEnd) {
        return Result<AudioClip>::failure(
            Error(ErrorCode::InvalidArgument, "loop start is after loop end"));
    }
    return Result<AudioClip>::success(std::move(clip));
}

std::vector<std::uint8_t> encodeAudioClip(const AudioClip& clip) {
    if (!clip.valid() ||
        clip.samples.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return {};
    }
    std::vector<std::uint8_t> output;
    output.reserve(24U + clip.samples.size() * 2U);
    output.insert(output.end(), {'F', 'G', 'L', 'A'});
    appendU16(output, 1U);
    appendU16(output, clip.streaming ? 1U : 0U);
    appendU32(output, clip.sampleRate);
    appendU32(output, static_cast<std::uint32_t>(clip.samples.size()));
    appendU32(output, clip.loopStart);
    appendU32(output, clip.loopEnd);
    for (const auto sample : clip.samples) {
        appendU16(output, static_cast<std::uint16_t>(sample));
    }
    return output;
}

} // namespace fabgl::assets
