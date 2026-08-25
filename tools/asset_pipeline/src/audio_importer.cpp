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

[[nodiscard]] bool parseAudioClipInfo(const std::vector<std::uint8_t>& bytes,
                                      AudioClipInfo& info) noexcept {
    if (bytes.size() < 24U || !fourCc(bytes, 0U, "FGLA")) {
        return false;
    }
    std::uint16_t version = 0U;
    std::uint16_t flags = 0U;
    if (!readU16(bytes, 4U, version) || !readU16(bytes, 6U, flags) ||
        !readU32(bytes, 8U, info.sampleRate) || !readU32(bytes, 12U, info.sampleCount) ||
        !readU32(bytes, 16U, info.loopStart) || !readU32(bytes, 20U, info.loopEnd) ||
        (version != 1U && version != 2U) || (flags & static_cast<std::uint16_t>(~3U)) != 0U ||
        (version == 1U && (flags & 2U) != 0U) || info.sampleCount == 0U ||
        info.sampleCount > 100000000U || info.loopStart > info.loopEnd ||
        info.loopEnd > info.sampleCount || info.sampleRate < 4000U || info.sampleRate > 192000U) {
        return false;
    }
    info.streaming = (flags & 1U) != 0U;
    info.encoding =
        version == 2U && (flags & 2U) != 0U ? AudioEncoding::Delta8 : AudioEncoding::Pcm16;

    std::size_t payloadBytes = 0U;
    if (info.encoding == AudioEncoding::Pcm16) {
        payloadBytes = static_cast<std::size_t>(info.sampleCount) * 2U;
    } else {
        constexpr std::size_t BlockSamples = 128U;
        constexpr std::size_t FullBlockBytes = 129U;
        const auto sampleCount = static_cast<std::size_t>(info.sampleCount);
        const auto fullBlocks = sampleCount / BlockSamples;
        const auto remaining = sampleCount % BlockSamples;
        payloadBytes = fullBlocks * FullBlockBytes + (remaining == 0U ? 0U : remaining + 1U);
    }
    return payloadBytes <= bytes.size() && bytes.size() == 24U + payloadBytes;
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

std::vector<std::uint8_t> encodeAudioClip(const AudioClip& clip, const AudioEncoding encoding) {
    if (!clip.valid() ||
        clip.samples.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return {};
    }
    std::vector<std::uint8_t> output;
    output.reserve(24U + clip.samples.size() * 2U);
    output.insert(output.end(), {'F', 'G', 'L', 'A'});
    appendU16(output, encoding == AudioEncoding::Pcm16 ? 1U : 2U);
    const auto flags = static_cast<std::uint16_t>((clip.streaming ? 1U : 0U) |
                                                  (encoding == AudioEncoding::Delta8 ? 2U : 0U));
    appendU16(output, flags);
    appendU32(output, clip.sampleRate);
    appendU32(output, static_cast<std::uint32_t>(clip.samples.size()));
    appendU32(output, clip.loopStart);
    appendU32(output, clip.loopEnd);
    if (encoding == AudioEncoding::Pcm16) {
        for (const auto sample : clip.samples) {
            appendU16(output, static_cast<std::uint16_t>(sample));
        }
        return output;
    }

    constexpr std::size_t BlockSamples = 128U;
    constexpr int QuantizationStep = 256;
    for (std::size_t blockStart = 0; blockStart < clip.samples.size(); blockStart += BlockSamples) {
        auto reconstructed = static_cast<int>(clip.samples[blockStart]);
        appendU16(output, static_cast<std::uint16_t>(clip.samples[blockStart]));
        const auto blockEnd = std::min(blockStart + BlockSamples, clip.samples.size());
        for (auto index = blockStart + 1U; index < blockEnd; ++index) {
            const auto difference = static_cast<int>(clip.samples[index]) - reconstructed;
            const auto quantized =
                std::clamp(static_cast<int>(std::lround(static_cast<double>(difference) /
                                                        static_cast<double>(QuantizationStep))),
                           -128, 127);
            output.push_back(static_cast<std::uint8_t>(quantized & 0xFF));
            reconstructed = std::clamp(reconstructed + quantized * QuantizationStep,
                                       static_cast<int>(std::numeric_limits<std::int16_t>::min()),
                                       static_cast<int>(std::numeric_limits<std::int16_t>::max()));
        }
    }
    return output;
}

Result<AudioClipInfo> inspectAudioClip(const std::vector<std::uint8_t>& bytes) {
    AudioClipInfo info;
    if (!parseAudioClipInfo(bytes, info)) {
        return Result<AudioClipInfo>::failure(
            Error(ErrorCode::InvalidFormat, "FabGL Studio audio stream is invalid"));
    }
    return Result<AudioClipInfo>::success(info);
}

std::size_t decodeAudioClipFrames(const std::vector<std::uint8_t>& bytes, const AudioClipInfo& info,
                                  const std::size_t firstFrame, std::int16_t* output,
                                  const std::size_t frameCount) noexcept {
    AudioClipInfo encoded;
    if (!parseAudioClipInfo(bytes, encoded) || encoded.sampleRate != info.sampleRate ||
        encoded.sampleCount != info.sampleCount || encoded.streaming != info.streaming ||
        encoded.loopStart != info.loopStart || encoded.loopEnd != info.loopEnd ||
        encoded.encoding != info.encoding || firstFrame >= encoded.sampleCount ||
        (output == nullptr && frameCount != 0U)) {
        return 0U;
    }
    const auto available = static_cast<std::size_t>(encoded.sampleCount) - firstFrame;
    const auto requested = std::min(frameCount, available);
    if (requested == 0U) {
        return 0U;
    }
    if (encoded.encoding == AudioEncoding::Pcm16) {
        auto offset = 24U + firstFrame * 2U;
        for (std::size_t index = 0U; index < requested; ++index) {
            std::uint16_t sample = 0U;
            if (!readU16(bytes, offset, sample)) {
                return index;
            }
            output[index] = static_cast<std::int16_t>(sample);
            offset += 2U;
        }
        return requested;
    }

    constexpr std::size_t BlockSamples = 128U;
    constexpr std::size_t FullBlockBytes = 129U;
    constexpr int QuantizationStep = 256;
    const auto lastFrame = firstFrame + requested;
    auto blockIndex = firstFrame / BlockSamples;
    auto produced = std::size_t{0U};
    while (blockIndex * BlockSamples < lastFrame) {
        const auto blockFirstFrame = blockIndex * BlockSamples;
        const auto blockSamples =
            std::min(BlockSamples, static_cast<std::size_t>(encoded.sampleCount) - blockFirstFrame);
        auto offset = 24U + blockIndex * FullBlockBytes;
        std::uint16_t anchor = 0U;
        if (!readU16(bytes, offset, anchor)) {
            return produced;
        }
        offset += 2U;
        auto reconstructed = static_cast<int>(static_cast<std::int16_t>(anchor));
        for (std::size_t inBlock = 0U; inBlock < blockSamples; ++inBlock) {
            if (inBlock != 0U) {
                if (offset >= bytes.size()) {
                    return produced;
                }
                const auto byte = static_cast<int>(bytes[offset++]);
                const auto delta = byte <= 127 ? byte : byte - 256;
                reconstructed =
                    std::clamp(reconstructed + delta * QuantizationStep,
                               static_cast<int>(std::numeric_limits<std::int16_t>::min()),
                               static_cast<int>(std::numeric_limits<std::int16_t>::max()));
            }
            const auto frame = blockFirstFrame + inBlock;
            if (frame >= firstFrame && frame < lastFrame) {
                output[produced++] = static_cast<std::int16_t>(reconstructed);
            }
        }
        ++blockIndex;
    }
    return produced;
}

Result<AudioClip> decodeAudioClip(const std::vector<std::uint8_t>& bytes) {
    auto inspected = inspectAudioClip(bytes);
    if (!inspected) {
        return Result<AudioClip>::failure(inspected.error());
    }
    AudioClip clip;
    clip.sampleRate = inspected.value().sampleRate;
    clip.streaming = inspected.value().streaming;
    clip.loopStart = inspected.value().loopStart;
    clip.loopEnd = inspected.value().loopEnd;
    clip.samples.resize(inspected.value().sampleCount);
    const auto decoded = decodeAudioClipFrames(bytes, inspected.value(), 0U, clip.samples.data(),
                                               clip.samples.size());
    if (decoded != clip.samples.size()) {
        return Result<AudioClip>::failure(
            Error(ErrorCode::InvalidFormat, "FabGL Studio audio payload is truncated"));
    }
    return Result<AudioClip>::success(std::move(clip));
}

} // namespace fabgl::assets
