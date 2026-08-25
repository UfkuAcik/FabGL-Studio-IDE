#pragma once

// Arduino SD/FS adapter for ProjectSaveRuntime. Include this only from firmware;
// the codec itself remains Arduino-independent and is host-testable.

#include "ProjectSaveRuntime.h"

#include <FS.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace fabgl_project_save {

class SdStorageAdapter final {
  public:
    explicit SdStorageAdapter(fs::FS& filesystem) noexcept : filesystem_(&filesystem) {}

    [[nodiscard]] StorageCallbacks callbacks() noexcept {
        StorageCallbacks result;
        result.context = this;
        result.read = &readThunk;
        result.stage = &stageThunk;
        result.commit = &commitThunk;
        result.discard = &discardThunk;
        result.remove = &removeThunk;
        return result;
    }

    [[nodiscard]] std::uint64_t bytesRead() const noexcept { return bytesRead_; }

  private:
    static StorageStatus readThunk(void* context, const char* path, std::uint8_t* output,
                                   const std::size_t capacity, std::size_t* size) noexcept {
        return static_cast<SdStorageAdapter*>(context)->read(path, output, capacity, size);
    }
    static StorageStatus stageThunk(void* context, const char* path, const std::uint8_t* data,
                                    const std::size_t size) noexcept {
        return static_cast<SdStorageAdapter*>(context)->stage(path, data, size);
    }
    static StorageStatus commitThunk(void* context, const char* livePath, const char* temporaryPath,
                                     const char* backupPath) noexcept {
        return static_cast<SdStorageAdapter*>(context)->commit(livePath, temporaryPath, backupPath);
    }
    static void discardThunk(void* context, const char* path) noexcept {
        static_cast<SdStorageAdapter*>(context)->discard(path);
    }
    static StorageStatus removeThunk(void* context, const char* path) noexcept {
        return static_cast<SdStorageAdapter*>(context)->remove(path);
    }

    StorageStatus read(const char* path, std::uint8_t* output, const std::size_t capacity,
                       std::size_t* size) noexcept {
        if (filesystem_ == nullptr || path == nullptr || output == nullptr || size == nullptr)
            return StorageStatus::IoFailure;
        if (!filesystem_->exists(path))
            return StorageStatus::NotFound;
        File file = filesystem_->open(path, FILE_READ);
        if (!file)
            return StorageStatus::IoFailure;
        const std::size_t available = static_cast<std::size_t>(file.size());
        if (available > capacity || available > kMaximumFileBytes) {
            file.close();
            return StorageStatus::CapacityExceeded;
        }
        const auto received = file.read(output, available);
        addReadBytes(received);
        file.close();
        if (received != available)
            return StorageStatus::IoFailure;
        *size = available;
        return StorageStatus::Ok;
    }

    StorageStatus stage(const char* path, const std::uint8_t* data,
                        const std::size_t size) noexcept {
        if (filesystem_ == nullptr || path == nullptr || data == nullptr || size == 0U ||
            size > kMaximumFileBytes || !ensureParentDirectories(path)) {
            return StorageStatus::IoFailure;
        }
        if (filesystem_->exists(path) && !filesystem_->remove(path))
            return StorageStatus::IoFailure;
        File file = filesystem_->open(path, FILE_WRITE);
        if (!file)
            return StorageStatus::IoFailure;
        const auto written = file.write(data, size);
        file.flush();
        file.close();
        if (written != size) {
            filesystem_->remove(path);
            return StorageStatus::IoFailure;
        }
        // Re-open and verify the staged byte count before touching the live slot.
        File verification = filesystem_->open(path, FILE_READ);
        if (!verification)
            return StorageStatus::IoFailure;
        const auto stagedSize = static_cast<std::size_t>(verification.size());
        bool exact = stagedSize == size;
        std::uint8_t check[64]{};
        std::size_t offset = 0U;
        while (exact && offset < size) {
            const auto remaining = size - offset;
            const auto count = remaining < sizeof(check) ? remaining : sizeof(check);
            const auto received = verification.read(check, count);
            addReadBytes(received);
            if (received != count ||
                std::memcmp(check, data + offset, count) != 0) {
                exact = false;
                break;
            }
            offset += count;
        }
        verification.close();
        if (!exact) {
            filesystem_->remove(path);
            return StorageStatus::IoFailure;
        }
        return StorageStatus::Ok;
    }

    StorageStatus commit(const char* livePath, const char* temporaryPath,
                         const char* backupPath) noexcept {
        if (filesystem_ == nullptr || livePath == nullptr || temporaryPath == nullptr ||
            backupPath == nullptr || !filesystem_->exists(temporaryPath)) {
            return StorageStatus::CommitFailed;
        }

        const bool hadLive = filesystem_->exists(livePath);
        if (filesystem_->exists(backupPath) && !filesystem_->remove(backupPath))
            return StorageStatus::CommitFailed;
        if (hadLive && !filesystem_->rename(livePath, backupPath))
            return StorageStatus::CommitFailed;

        if (filesystem_->rename(temporaryPath, livePath)) {
            // Keep one previous generation as a bounded corruption/power-loss fallback.
            return StorageStatus::Ok;
        }

        // ESP32 Arduino FS does not promise a power-fail-atomic rename. Restore the old live path
        // immediately when the second rename fails; if that also fails, the previous generation
        // remains named .bak and SaveService can still recover it explicitly.
        if (hadLive) {
            if (filesystem_->exists(livePath))
                filesystem_->remove(livePath);
            if (!filesystem_->rename(backupPath, livePath))
                return StorageStatus::RollbackFailed;
        }
        return StorageStatus::CommitFailed;
    }

    void discard(const char* path) noexcept {
        if (filesystem_ != nullptr && path != nullptr && filesystem_->exists(path))
            filesystem_->remove(path);
    }

    StorageStatus remove(const char* path) noexcept {
        if (filesystem_ == nullptr || path == nullptr)
            return StorageStatus::IoFailure;
        if (!filesystem_->exists(path))
            return StorageStatus::NotFound;
        return filesystem_->remove(path) ? StorageStatus::Ok : StorageStatus::IoFailure;
    }

    bool ensureParentDirectories(const char* path) noexcept {
        const std::size_t length = std::strlen(path);
        if (length < 2U || length >= 96U || path[0] != '/')
            return false;
        char prefix[96]{};
        std::memcpy(prefix, path, length + 1U);
        for (std::size_t index = 1U; index < length; ++index) {
            if (prefix[index] != '/')
                continue;
            prefix[index] = '\0';
            if (!filesystem_->exists(prefix) && !filesystem_->mkdir(prefix))
                return false;
            prefix[index] = '/';
        }
        return true;
    }

    void addReadBytes(const std::size_t count) noexcept {
        const auto wide = static_cast<std::uint64_t>(count);
        bytesRead_ = wide > std::numeric_limits<std::uint64_t>::max() - bytesRead_
                         ? std::numeric_limits<std::uint64_t>::max()
                         : bytesRead_ + wide;
    }

    fs::FS* filesystem_ = nullptr;
    std::uint64_t bytesRead_ = 0U;
};

} // namespace fabgl_project_save
