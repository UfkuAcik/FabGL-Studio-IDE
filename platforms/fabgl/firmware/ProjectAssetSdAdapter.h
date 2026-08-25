#pragma once

#include "BoundedStorageReader.h"

#include <FS.h>

#include <cstddef>
#include <cstdint>

namespace fabgl_project_assets {

// Arduino FS bridge for the portable double-window reader. The adapter owns a
// single open, read-only file; seek/read calls happen only when the lifecycle
// AssetStreaming phase temporarily enables misses on DoubleWindowReader.
class SdStorageAdapter final {
  public:
    explicit SdStorageAdapter(fs::FS& filesystem) noexcept : filesystem_(&filesystem) {}

    bool open(const char* path) noexcept {
        close();
        if (path == nullptr)
            return false;
        file_ = filesystem_->open(path, FILE_READ);
        if (!file_ || file_.isDirectory()) {
            close();
            return false;
        }
        const auto length = file_.size();
        if (length == 0U || length >= 0x80000000ULL) {
            close();
            return false;
        }
        size_ = static_cast<std::size_t>(length);
        return true;
    }

    void close() noexcept {
        if (file_)
            file_.close();
        size_ = 0U;
    }

    [[nodiscard]] bool opened() const noexcept {
        return static_cast<bool>(file_) && size_ != 0U;
    }

    [[nodiscard]] fabgl_bounded_storage::Source source() noexcept {
        fabgl_bounded_storage::Source result;
        if (!opened())
            return result;
        result.context = this;
        result.size = size_;
        result.read = &readThunk;
        return result;
    }

  private:
    static std::size_t readThunk(void* context, const std::size_t offset,
                                 std::uint8_t* output, const std::size_t count) noexcept {
        return static_cast<SdStorageAdapter*>(context)->read(offset, output, count);
    }

    std::size_t read(const std::size_t offset, std::uint8_t* output,
                     const std::size_t count) noexcept {
        if (!opened() || output == nullptr || offset > size_ || count > size_ - offset ||
            !file_.seek(static_cast<std::uint32_t>(offset), SeekSet))
            return 0U;
        const auto received = file_.read(output, count);
        return static_cast<std::size_t>(received);
    }

    fs::FS* filesystem_ = nullptr;
    fs::File file_;
    std::size_t size_ = 0U;
};

} // namespace fabgl_project_assets
