#include <fabgl/assets/asset_pack.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace fabgl::assets {

namespace {

constexpr std::uint16_t PackVersion = 1;
constexpr std::size_t HeaderSize = 32;
constexpr std::size_t IndexEntrySize = 40;

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

void appendU64(std::vector<std::uint8_t>& output, std::uint64_t value) {
    for (unsigned int shift = 0; shift < 64U; shift += 8U) {
        output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void writeU32(std::vector<std::uint8_t>& output, std::size_t offset, std::uint32_t value) {
    for (unsigned int shift = 0; shift < 32U; shift += 8U) {
        output[offset + shift / 8U] = static_cast<std::uint8_t>((value >> shift) & 0xFFU);
    }
}

void writeU64(std::vector<std::uint8_t>& output, std::size_t offset, std::uint64_t value) {
    for (unsigned int shift = 0; shift < 64U; shift += 8U) {
        output[offset + shift / 8U] = static_cast<std::uint8_t>((value >> shift) & 0xFFU);
    }
}

bool readU16(const std::vector<std::uint8_t>& input, std::size_t offset, std::uint16_t& value) {
    if (offset > input.size() || input.size() - offset < 2U) {
        return false;
    }
    value = static_cast<std::uint16_t>(static_cast<std::uint32_t>(input[offset]) |
                                       (static_cast<std::uint32_t>(input[offset + 1U]) << 8U));
    return true;
}

bool readU32(const std::vector<std::uint8_t>& input, std::size_t offset, std::uint32_t& value) {
    if (offset > input.size() || input.size() - offset < 4U) {
        return false;
    }
    value = 0;
    for (unsigned int shift = 0; shift < 32U; shift += 8U) {
        value |= static_cast<std::uint32_t>(input[offset + shift / 8U]) << shift;
    }
    return true;
}

bool readU64(const std::vector<std::uint8_t>& input, std::size_t offset, std::uint64_t& value) {
    if (offset > input.size() || input.size() - offset < 8U) {
        return false;
    }
    value = 0;
    for (unsigned int shift = 0; shift < 64U; shift += 8U) {
        value |= static_cast<std::uint64_t>(input[offset + shift / 8U]) << shift;
    }
    return true;
}

[[nodiscard]] bool powerOfTwo(std::uint32_t value) noexcept {
    return value != 0U && (value & (value - 1U)) == 0U;
}

[[nodiscard]] std::size_t alignUp(std::size_t value, std::uint32_t alignment) noexcept {
    const auto mask = static_cast<std::size_t>(alignment - 1U);
    return (value + mask) & ~mask;
}

Error invalidPack(std::string message) {
    return Error(ErrorCode::InvalidFormat, std::move(message));
}

} // namespace

std::uint64_t checksum64(const std::uint8_t* bytes, std::size_t size) noexcept {
    constexpr std::uint64_t offset = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    auto hash = offset;
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= prime;
    }
    return hash;
}

Result<AssetPack> buildPack(std::vector<PackInput> inputs, std::uint32_t alignment) {
    if (!powerOfTwo(alignment) || alignment > 4096U) {
        return Result<AssetPack>::failure(
            Error(ErrorCode::InvalidArgument, "pack alignment must be a power of two up to 4096"));
    }
    std::sort(inputs.begin(), inputs.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.guid < rhs.guid; });
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        if (inputs[index].guid.isNil()) {
            return Result<AssetPack>::failure(
                Error(ErrorCode::InvalidArgument, "pack input GUID cannot be nil"));
        }
        if (index > 0U && inputs[index - 1U].guid == inputs[index].guid) {
            return Result<AssetPack>::failure(
                Error(ErrorCode::AlreadyExists, "duplicate asset GUID in pack")
                    .addContext("guid", inputs[index].guid.toString()));
        }
    }
    if (inputs.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return Result<AssetPack>::failure(
            Error(ErrorCode::CapacityExceeded, "too many pack entries"));
    }

    const auto rawDataOffset = HeaderSize + inputs.size() * IndexEntrySize;
    const auto dataOffset = alignUp(rawDataOffset, alignment);
    if (dataOffset > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return Result<AssetPack>::failure(
            Error(ErrorCode::CapacityExceeded, "pack index is too large"));
    }

    AssetPack pack;
    pack.bytes.reserve(dataOffset);
    pack.bytes.insert(pack.bytes.end(), {'F', 'G', 'L', 'P'});
    appendU16(pack.bytes, PackVersion);
    appendU16(pack.bytes, 0U);
    appendU32(pack.bytes, static_cast<std::uint32_t>(inputs.size()));
    appendU32(pack.bytes, alignment);
    appendU32(pack.bytes, static_cast<std::uint32_t>(IndexEntrySize));
    appendU32(pack.bytes, static_cast<std::uint32_t>(dataOffset));
    appendU64(pack.bytes, 0U);

    const auto indexStart = pack.bytes.size();
    pack.bytes.resize(dataOffset, 0U);
    pack.index.reserve(inputs.size());
    std::size_t cursor = dataOffset;
    for (std::size_t inputIndex = 0; inputIndex < inputs.size(); ++inputIndex) {
        cursor = alignUp(cursor, alignment);
        if (cursor > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) ||
            inputs[inputIndex].payload.size() >
                static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) ||
            inputs[inputIndex].payload.size() >
                static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) - cursor) {
            return Result<AssetPack>::failure(
                Error(ErrorCode::CapacityExceeded, "asset pack exceeds 4 GiB"));
        }
        pack.bytes.resize(cursor, 0U);
        const auto offset = static_cast<std::uint32_t>(cursor);
        const auto size = static_cast<std::uint32_t>(inputs[inputIndex].payload.size());
        const auto payloadChecksum =
            checksum64(inputs[inputIndex].payload.data(), inputs[inputIndex].payload.size());
        pack.bytes.insert(pack.bytes.end(), inputs[inputIndex].payload.begin(),
                          inputs[inputIndex].payload.end());
        cursor = pack.bytes.size();
        pack.index.push_back({inputs[inputIndex].guid, inputs[inputIndex].typeId,
                              inputs[inputIndex].storage, offset, size, payloadChecksum});

        auto entryOffset = indexStart + inputIndex * IndexEntrySize;
        const auto& guidBytes = inputs[inputIndex].guid.bytes();
        std::copy(guidBytes.begin(), guidBytes.end(),
                  pack.bytes.begin() +
                      static_cast<std::vector<std::uint8_t>::difference_type>(entryOffset));
        entryOffset += 16U;
        writeU32(pack.bytes, entryOffset, inputs[inputIndex].typeId);
        entryOffset += 4U;
        pack.bytes[entryOffset] = static_cast<std::uint8_t>(inputs[inputIndex].storage);
        entryOffset += 4U;
        writeU32(pack.bytes, entryOffset, offset);
        entryOffset += 4U;
        writeU32(pack.bytes, entryOffset, size);
        entryOffset += 4U;
        writeU64(pack.bytes, entryOffset, payloadChecksum);
    }
    pack.buildChecksum = checksum64(pack.bytes.data() + HeaderSize, pack.bytes.size() - HeaderSize);
    writeU64(pack.bytes, 24U, pack.buildChecksum);
    return Result<AssetPack>::success(std::move(pack));
}

Result<AssetPack> inspectPack(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < HeaderSize || bytes[0] != 'F' || bytes[1] != 'G' || bytes[2] != 'L' ||
        bytes[3] != 'P') {
        return Result<AssetPack>::failure(invalidPack("invalid asset pack magic/header"));
    }
    std::uint16_t version = 0;
    std::uint32_t count = 0;
    std::uint32_t alignment = 0;
    std::uint32_t entrySize = 0;
    std::uint32_t dataOffset = 0;
    std::uint64_t declaredChecksum = 0;
    if (!readU16(bytes, 4U, version) || !readU32(bytes, 8U, count) ||
        !readU32(bytes, 12U, alignment) || !readU32(bytes, 16U, entrySize) ||
        !readU32(bytes, 20U, dataOffset) || !readU64(bytes, 24U, declaredChecksum)) {
        return Result<AssetPack>::failure(invalidPack("truncated asset pack header"));
    }
    if (version != PackVersion) {
        return Result<AssetPack>::failure(
            Error(ErrorCode::UnsupportedVersion, "unsupported asset pack version")
                .addContext("version", std::to_string(version)));
    }
    if (!powerOfTwo(alignment) || alignment > 4096U || entrySize != IndexEntrySize ||
        dataOffset < HeaderSize || dataOffset > bytes.size()) {
        return Result<AssetPack>::failure(invalidPack("invalid asset pack layout"));
    }
    const auto indexBytes = static_cast<std::uint64_t>(count) * IndexEntrySize;
    if (indexBytes > static_cast<std::uint64_t>(dataOffset - HeaderSize)) {
        return Result<AssetPack>::failure(invalidPack("asset pack index exceeds data offset"));
    }
    const auto actualChecksum = checksum64(bytes.data() + HeaderSize, bytes.size() - HeaderSize);
    if (actualChecksum != declaredChecksum) {
        return Result<AssetPack>::failure(invalidPack("asset pack build checksum mismatch"));
    }

    AssetPack pack;
    pack.bytes = bytes;
    pack.buildChecksum = actualChecksum;
    pack.index.reserve(count);
    AssetGuid previous;
    for (std::uint32_t index = 0; index < count; ++index) {
        auto entryOffset = HeaderSize + static_cast<std::size_t>(index) * IndexEntrySize;
        detail::GuidBytes guidBytes{};
        std::copy_n(bytes.begin() +
                        static_cast<std::vector<std::uint8_t>::difference_type>(entryOffset),
                    16, guidBytes.begin());
        const AssetGuid guid(guidBytes);
        entryOffset += 16U;
        std::uint32_t typeId = 0;
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
        std::uint64_t payloadChecksum = 0;
        if (!readU32(bytes, entryOffset, typeId)) {
            return Result<AssetPack>::failure(invalidPack("truncated asset index entry"));
        }
        entryOffset += 4U;
        const auto storageValue = bytes[entryOffset];
        entryOffset += 4U;
        if (!readU32(bytes, entryOffset, offset) || !readU32(bytes, entryOffset + 4U, size) ||
            !readU64(bytes, entryOffset + 8U, payloadChecksum) || storageValue > 3U ||
            guid.isNil()) {
            return Result<AssetPack>::failure(invalidPack("invalid asset index entry"));
        }
        if (index > 0U && !(previous < guid)) {
            return Result<AssetPack>::failure(invalidPack("asset index is not strictly sorted"));
        }
        if (offset < dataOffset || offset > bytes.size() || size > bytes.size() - offset ||
            (offset & (alignment - 1U)) != 0U) {
            return Result<AssetPack>::failure(
                invalidPack("asset payload range/alignment is invalid"));
        }
        if (checksum64(bytes.data() + offset, size) != payloadChecksum) {
            return Result<AssetPack>::failure(invalidPack("asset payload checksum mismatch"));
        }
        pack.index.push_back(
            {guid, typeId, static_cast<StorageClass>(storageValue), offset, size, payloadChecksum});
        previous = guid;
    }
    return Result<AssetPack>::success(std::move(pack));
}

} // namespace fabgl::assets
