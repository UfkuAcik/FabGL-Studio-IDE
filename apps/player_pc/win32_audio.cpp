#include "win32_audio.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>

namespace fabgl::player {

Win32AudioOutput::~Win32AudioOutput() {
    close();
}

Error Win32AudioOutput::waveError(const char* operation, const MMRESULT code) {
    char description[MAXERRORLENGTH]{};
    const auto described = waveOutGetErrorTextA(code, description, MAXERRORLENGTH);
    return Error(ErrorCode::IoError,
                 described == MMSYSERR_NOERROR ? std::string(description)
                                               : std::string("Windows audio operation failed"))
        .addContext("operation", operation)
        .addContext("mmresult", std::to_string(code));
}

bool Win32AudioOutput::open(const std::uint32_t sampleRate, std::string& error) {
    close();
    if (sampleRate == 0U || sampleRate > 192'000U) {
        error = "audio sample rate must be between 1 and 192000 Hz";
        return false;
    }

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2U;
    format.nSamplesPerSec = sampleRate;
    format.wBitsPerSample = 16U;
    format.nBlockAlign = static_cast<WORD>(format.nChannels * (format.wBitsPerSample / 8U));
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    const auto opened = waveOutOpen(&output_, WAVE_MAPPER, &format, 0U, 0U, CALLBACK_NULL);
    if (opened != MMSYSERR_NOERROR) {
        error = waveError("waveOutOpen", opened).message();
        output_ = nullptr;
        return false;
    }
    sampleRate_ = sampleRate;
    return true;
}

void Win32AudioOutput::releaseBuffer(Buffer& buffer) noexcept {
    if (output_ != nullptr && buffer.prepared) {
        static_cast<void>(waveOutUnprepareHeader(output_, &buffer.header, sizeof(WAVEHDR)));
    }
    buffer.prepared = false;
    buffer.header = {};
    buffer.samples.clear();
}

void Win32AudioOutput::close() noexcept {
    if (output_ == nullptr) {
        return;
    }
    static_cast<void>(waveOutReset(output_));
    for (auto& buffer : buffers_) {
        releaseBuffer(buffer);
    }
    static_cast<void>(waveOutClose(output_));
    output_ = nullptr;
    sampleRate_ = 0U;
    nextBuffer_ = 0U;
}

Result<void> Win32AudioOutput::submitStereo(const float* interleavedSamples,
                                            const std::size_t frameCount,
                                            const std::uint32_t sampleRate) {
    if (output_ == nullptr) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "Windows audio output is not open"));
    }
    if (sampleRate != sampleRate_) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "audio mixer sample rate changed after open"));
    }
    if (frameCount != 0U && interleavedSamples == nullptr) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "audio sample pointer is null"));
    }
    if (frameCount > static_cast<std::size_t>(std::numeric_limits<DWORD>::max()) /
                         (2U * sizeof(std::int16_t))) {
        return Result<void>::failure(
            Error(ErrorCode::CapacityExceeded, "audio block is too large for waveOut"));
    }

    Buffer* selected = nullptr;
    for (std::size_t offset = 0; offset < buffers_.size(); ++offset) {
        const auto index = (nextBuffer_ + offset) % buffers_.size();
        auto& candidate = buffers_[index];
        if (!candidate.prepared || (candidate.header.dwFlags & WHDR_DONE) != 0U) {
            selected = &candidate;
            nextBuffer_ = (index + 1U) % buffers_.size();
            break;
        }
    }
    // Backpressure is normal when the window loop briefly runs ahead of the audio device. Drop
    // one block instead of terminating gameplay; a later block will reuse the first completed
    // buffer and keep latency bounded by the fixed ring size.
    if (selected == nullptr) {
        return Result<void>::success();
    }
    auto& buffer = *selected;
    if (buffer.prepared) {
        const auto unprepared = waveOutUnprepareHeader(output_, &buffer.header, sizeof(WAVEHDR));
        if (unprepared != MMSYSERR_NOERROR) {
            return Result<void>::failure(waveError("waveOutUnprepareHeader", unprepared));
        }
        buffer.prepared = false;
    }

    buffer.samples.resize(frameCount * 2U);
    for (std::size_t index = 0; index < buffer.samples.size(); ++index) {
        const auto sample = std::clamp(interleavedSamples[index], -1.0F, 1.0F);
        const auto scaled = std::lround(sample * 32767.0F);
        buffer.samples[index] = static_cast<std::int16_t>(scaled);
    }

    buffer.header = {};
    buffer.header.lpData = reinterpret_cast<LPSTR>(buffer.samples.data());
    buffer.header.dwBufferLength = static_cast<DWORD>(buffer.samples.size() * sizeof(std::int16_t));
    const auto prepared = waveOutPrepareHeader(output_, &buffer.header, sizeof(WAVEHDR));
    if (prepared != MMSYSERR_NOERROR) {
        return Result<void>::failure(waveError("waveOutPrepareHeader", prepared));
    }
    buffer.prepared = true;
    const auto written = waveOutWrite(output_, &buffer.header, sizeof(WAVEHDR));
    if (written != MMSYSERR_NOERROR) {
        releaseBuffer(buffer);
        return Result<void>::failure(waveError("waveOutWrite", written));
    }
    return Result<void>::success();
}

} // namespace fabgl::player
