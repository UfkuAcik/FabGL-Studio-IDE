#include "fabgl/core/guid.h"

#include <array>
#include <cctype>
#include <iomanip>
#include <random>
#include <sstream>

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

} // namespace

GuidBytes generateGuidBytes() {
    thread_local std::mt19937_64 generator([] {
        std::random_device device;
        std::seed_seq seed{device(), device(), device(), device(), device(), device()};
        return std::mt19937_64(seed);
    }());

    GuidBytes bytes{};
    for (std::size_t offset = 0; offset < bytes.size(); offset += sizeof(std::uint64_t)) {
        const auto value = generator();
        for (std::size_t index = 0; index < sizeof(std::uint64_t); ++index) {
            bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8U));
        }
    }
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
