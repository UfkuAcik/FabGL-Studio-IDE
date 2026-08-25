#pragma once

#include <fabgl/audio/audio_mixer.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>

namespace fabgl::player {

class Win32AudioOutput final : public IAudioOutputBackend {
  public:
    Win32AudioOutput() = default;
    ~Win32AudioOutput() override;

    Win32AudioOutput(const Win32AudioOutput&) = delete;
    Win32AudioOutput& operator=(const Win32AudioOutput&) = delete;

    [[nodiscard]] bool open(std::uint32_t sampleRate, std::string& error);
    void close() noexcept;
    [[nodiscard]] bool isOpen() const noexcept {
        return output_ != nullptr;
    }

    [[nodiscard]] Result<void> submitStereo(const float* interleavedSamples,
                                            std::size_t frameCount,
                                            std::uint32_t sampleRate) override;

  private:
    struct Buffer final {
        std::vector<std::int16_t> samples;
        WAVEHDR header{};
        bool prepared = false;
    };

    [[nodiscard]] static Error waveError(const char* operation, MMRESULT code);
    void releaseBuffer(Buffer& buffer) noexcept;

    static constexpr std::size_t BufferCount = 8U;
    HWAVEOUT output_ = nullptr;
    std::array<Buffer, BufferCount> buffers_{};
    std::size_t nextBuffer_ = 0U;
    std::uint32_t sampleRate_ = 0U;
};

} // namespace fabgl::player
