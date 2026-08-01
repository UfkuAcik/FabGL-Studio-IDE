#pragma once

#include <fabgl/core/result.h>

#include <cstdint>
#include <string>
#include <vector>

namespace fabgl::assets {

[[nodiscard]] Result<std::vector<std::uint8_t>> readBinaryFile(const std::string& utf8Path);
[[nodiscard]] Result<void> writeBinaryFileAtomic(const std::string& utf8Path,
                                                 const std::vector<std::uint8_t>& bytes);
[[nodiscard]] Result<std::string> readTextFile(const std::string& utf8Path);
[[nodiscard]] Result<void> createDirectories(const std::string& utf8Path);
[[nodiscard]] bool isSafeRelativePath(const std::string& path) noexcept;

} // namespace fabgl::assets
