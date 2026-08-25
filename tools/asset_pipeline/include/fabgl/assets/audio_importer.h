#pragma once

#include <fabgl/core/result.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fabgl::assets {

struct AudioImportSettings final {
    std::uint32_t targetSampleRate = 22050;
    bool normalize = true;
    bool trimSilence = true;
    float silenceThreshold = 0.01F;
    bool streaming = false;
    std::uint32_t loopStart = 0;
    std::uint32_t loopEnd = 0;
};

struct AudioClip final {
    std::uint32_t sampleRate = 0;
    std::vector<std::int16_t> samples;
    bool streaming = false;
    std::uint32_t loopStart = 0;
    std::uint32_t loopEnd = 0;

    [[nodiscard]] bool valid() const noexcept;
};

enum class AudioEncoding : std::uint8_t { Pcm16 = 0, Delta8 = 1 };

// Validated, allocation-free view of an encoded .fgla stream. Runtime users can
// keep the encoded payload resident and decode bounded frame windows instead of
// expanding the entire clip to PCM/floats.
struct AudioClipInfo final {
    std::uint32_t sampleRate = 0;
    std::uint32_t sampleCount = 0;
    bool streaming = false;
    std::uint32_t loopStart = 0;
    std::uint32_t loopEnd = 0;
    AudioEncoding encoding = AudioEncoding::Pcm16;
};

[[nodiscard]] Result<AudioClip> importWav(const std::vector<std::uint8_t>& bytes,
                                          const AudioImportSettings& settings);
[[nodiscard]] std::vector<std::uint8_t>
encodeAudioClip(const AudioClip& clip, AudioEncoding encoding = AudioEncoding::Pcm16);
[[nodiscard]] Result<AudioClipInfo> inspectAudioClip(const std::vector<std::uint8_t>& bytes);

// Decodes at most frameCount mono PCM16 frames into output without allocating.
// The return value is the number of frames produced. A zero result means either
// the request begins at end-of-stream or the bytes/info pair is invalid.
[[nodiscard]] std::size_t decodeAudioClipFrames(const std::vector<std::uint8_t>& bytes,
                                                const AudioClipInfo& info, std::size_t firstFrame,
                                                std::int16_t* output,
                                                std::size_t frameCount) noexcept;
[[nodiscard]] Result<AudioClip> decodeAudioClip(const std::vector<std::uint8_t>& bytes);

} // namespace fabgl::assets
