#pragma once

#include <fabgl/core/result.h>

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

[[nodiscard]] Result<AudioClip> importWav(const std::vector<std::uint8_t>& bytes,
                                          const AudioImportSettings& settings);
[[nodiscard]] std::vector<std::uint8_t> encodeAudioClip(const AudioClip& clip);

} // namespace fabgl::assets
