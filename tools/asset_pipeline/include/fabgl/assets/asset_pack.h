#pragma once

#include <fabgl/core/guid.h>
#include <fabgl/core/result.h>

#include <cstdint>
#include <string>
#include <vector>

namespace fabgl::assets {

enum class StorageClass : std::uint8_t { Flash = 0, InternalRam = 1, Psram = 2, Sd = 3 };

struct PackInput final {
    AssetGuid guid;
    std::uint32_t typeId = 0;
    StorageClass storage = StorageClass::Flash;
    std::vector<std::uint8_t> payload;
};

struct PackIndexEntry final {
    AssetGuid guid;
    std::uint32_t typeId = 0;
    StorageClass storage = StorageClass::Flash;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    std::uint64_t checksum = 0;
};

struct AssetPack final {
    std::vector<PackIndexEntry> index;
    std::vector<std::uint8_t> bytes;
    std::uint64_t buildChecksum = 0;
};

[[nodiscard]] Result<AssetPack> buildPack(std::vector<PackInput> inputs,
                                          std::uint32_t alignment = 16);
[[nodiscard]] Result<AssetPack> inspectPack(const std::vector<std::uint8_t>& bytes);
[[nodiscard]] std::uint64_t checksum64(const std::uint8_t* bytes, std::size_t size) noexcept;

} // namespace fabgl::assets
