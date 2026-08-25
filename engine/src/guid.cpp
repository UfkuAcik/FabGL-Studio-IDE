#include "fabgl/core/guid.h"

#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <random>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// MinGW's bcrypt declarations depend on the Win32 base typedefs above.
#include <bcrypt.h>
#elif defined(__linux__)
#include <sys/random.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <cstdlib>
#include <unistd.h>
#endif

namespace fabgl::detail {
namespace {

int hexValue(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    const auto lower = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    if (lower >= 'a' && lower <= 'f') {
        return 10 + (lower - 'a');
    }
    return -1;
}

std::uint64_t fnv1a(std::string_view text, std::uint64_t basis) {
    std::uint64_t hash = basis;
    for (const auto character : text) {
        hash ^= static_cast<std::uint8_t>(character);
        hash *= 1099511628211ULL;
    }
    return hash;
}

bool fillFromSystemRandom(GuidBytes& bytes) noexcept {
#if defined(_WIN32)
    const auto status = BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                                        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return status >= 0;
#elif defined(__linux__)
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        const auto count = ::getrandom(bytes.data() + offset, bytes.size() - offset, 0);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        return false;
    }
    return true;
#elif defined(__APPLE__)
    arc4random_buf(bytes.data(), bytes.size());
    return true;
#else
    return false;
#endif
}

std::uint64_t currentProcessId() noexcept {
#if defined(_WIN32)
    return static_cast<std::uint64_t>(GetCurrentProcessId());
#elif defined(__linux__) || defined(__APPLE__)
    return static_cast<std::uint64_t>(::getpid());
#else
    return 0U;
#endif
}

std::uint32_t lowerHalf(std::uint64_t value) noexcept {
    return static_cast<std::uint32_t>(value & 0xFFFFFFFFULL);
}

std::uint32_t upperHalf(std::uint64_t value) noexcept {
    return static_cast<std::uint32_t>(value >> 32U);
}

void fillFromFallback(GuidBytes& bytes) {
    static std::atomic<std::uint64_t> sequence{0U};
    const auto serial = sequence.fetch_add(1U, std::memory_order_relaxed) + 1U;
    const auto wallClock = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const auto monotonicClock =
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto process = currentProcessId();
    const auto thread =
        static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&bytes));

    std::uint32_t randomOne = 0U;
    std::uint32_t randomTwo = 0U;
    try {
        std::random_device random;
        randomOne = static_cast<std::uint32_t>(random());
        randomTwo = static_cast<std::uint32_t>(random());
    } catch (...) {
        // Clock, process, address, thread and the atomic sequence still prevent the deterministic
        // random_device behavior found in older MinGW runtimes from repeating a whole stream.
    }

    std::seed_seq seed{lowerHalf(serial),
                       upperHalf(serial),
                       lowerHalf(wallClock),
                       upperHalf(wallClock),
                       lowerHalf(monotonicClock),
                       upperHalf(monotonicClock),
                       lowerHalf(process),
                       upperHalf(process),
                       lowerHalf(thread),
                       upperHalf(thread),
                       lowerHalf(address),
                       upperHalf(address),
                       randomOne,
                       randomTwo};
    std::mt19937_64 generator(seed);
    for (std::size_t offset = 0U; offset < bytes.size(); offset += sizeof(std::uint64_t)) {
        const auto value = generator();
        for (std::size_t index = 0U; index < sizeof(std::uint64_t); ++index)
            bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

} // namespace

GuidBytes generateGuidBytes() {
    GuidBytes bytes{};
    if (!fillFromSystemRandom(bytes))
        fillFromFallback(bytes);
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0FU) | 0x40U);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3FU) | 0x80U);
    return bytes;
}

GuidBytes stableGuidBytes(std::string_view stableName) {
    const auto first = fnv1a(stableName, 1469598103934665603ULL);
    const auto second = fnv1a(stableName, 1099511628211ULL ^ 0x9E3779B97F4A7C15ULL);
    GuidBytes bytes{};
    for (std::size_t index = 0; index < 8; ++index) {
        bytes[index] = static_cast<std::uint8_t>(first >> (index * 8U));
        bytes[index + 8] = static_cast<std::uint8_t>(second >> (index * 8U));
    }
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0FU) | 0x50U);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3FU) | 0x80U);
    return bytes;
}

Result<GuidBytes> parseGuidBytes(std::string_view text) {
    if (text.size() != 36 || text[8] != '-' || text[13] != '-' || text[18] != '-' ||
        text[23] != '-') {
        return Result<GuidBytes>::failure(
            Error(ErrorCode::InvalidFormat, "GUID must use canonical 8-4-4-4-12 form")
                .addContext("value", std::string(text)));
    }

    GuidBytes bytes{};
    std::size_t byteIndex = 0;
    for (std::size_t index = 0; index < text.size();) {
        if (text[index] == '-') {
            ++index;
            continue;
        }
        if (index + 1 >= text.size() || byteIndex >= bytes.size()) {
            return Result<GuidBytes>::failure(
                Error(ErrorCode::InvalidFormat, "GUID has an invalid length"));
        }
        const auto high = hexValue(text[index]);
        const auto low = hexValue(text[index + 1]);
        if (high < 0 || low < 0) {
            return Result<GuidBytes>::failure(
                Error(ErrorCode::InvalidFormat, "GUID contains a non-hexadecimal character")
                    .addContext("value", std::string(text)));
        }
        bytes[byteIndex++] = static_cast<std::uint8_t>((high << 4) | low);
        index += 2;
    }

    if (byteIndex != bytes.size()) {
        return Result<GuidBytes>::failure(
            Error(ErrorCode::InvalidFormat, "GUID does not contain 16 bytes"));
    }
    return Result<GuidBytes>::success(bytes);
}

std::string formatGuidBytes(const GuidBytes& bytes) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) {
            stream << '-';
        }
        stream << std::setw(2) << static_cast<unsigned int>(bytes[index]);
    }
    return stream.str();
}

} // namespace fabgl::detail
