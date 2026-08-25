#pragma once

// Fixed-memory, double-window storage reader used by ESP32 SD assets and host
// regressions.  Physical reads are only permitted while accessAllowed() is
// true; callers prefetch in the lifecycle AssetStreaming phase and can then
// consume cached bytes in render/audio hot paths without filesystem access.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace fabgl_bounded_storage {

using ReadCallback = std::size_t (*)(void* context, std::size_t offset,
                                     std::uint8_t* output, std::size_t count) noexcept;

struct Source final {
    void* context;
    std::size_t size;
    ReadCallback read;

    Source() noexcept : context(nullptr), size(0U), read(nullptr) {}
};

struct Stats final {
    std::uint64_t physicalReadCalls;
    std::uint64_t physicalReadBytes;
    std::uint64_t cacheHits;
    std::uint64_t cacheMisses;
    std::uint64_t prohibitedMisses;

    Stats() noexcept
        : physicalReadCalls(0U), physicalReadBytes(0U), cacheHits(0U), cacheMisses(0U),
          prohibitedMisses(0U) {}
};

template <std::size_t WindowBytes> class DoubleWindowReader final {
    static_assert(WindowBytes > 0U, "storage window must not be empty");

  public:
    DoubleWindowReader() noexcept : source_(), windows_(), nextWindow_(0U), stats_() {}

    explicit DoubleWindowReader(const Source source) noexcept
        : source_(source), windows_(), nextWindow_(0U), stats_() {}

    void bind(const Source source) noexcept {
        source_ = source;
        invalidate();
        stats_ = Stats();
    }

    void invalidate() noexcept {
        windows_[0].valid = false;
        windows_[1].valid = false;
        nextWindow_ = 0U;
        failed_ = false;
    }

    void setAccessAllowed(const bool allowed) noexcept {
        accessAllowed_ = allowed;
    }

    [[nodiscard]] bool accessAllowed() const noexcept {
        return accessAllowed_;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return source_.size;
    }

    [[nodiscard]] bool failed() const noexcept {
        return failed_;
    }

    [[nodiscard]] const Stats& stats() const noexcept {
        return stats_;
    }

    bool prefetch(const std::size_t offset, const std::size_t count) const noexcept {
        if (count == 0U)
            return offset <= source_.size;
        if (offset > source_.size || count > source_.size - offset)
            return fail();
        const std::size_t firstWindow = offset / WindowBytes;
        const std::size_t lastWindow = (offset + count - 1U) / WindowBytes;
        // prefetch() promises that the complete range remains hot after it
        // returns. A larger sequential stream must call prefetch/read a window
        // at a time in AssetStreaming; silently evicting the first window here
        // would violate the hot-path no-I/O contract.
        if (lastWindow - firstWindow >= 2U)
            return fail();
        std::size_t cursor = offset;
        const std::size_t end = offset + count;
        while (cursor < end) {
            Window* window = find(cursor);
            if (window == nullptr) {
                if (!load(cursor))
                    return false;
                window = find(cursor);
            }
            const std::size_t available = window->length - (cursor - window->offset);
            cursor += available < end - cursor ? available : end - cursor;
        }
        return true;
    }

    bool read(const std::size_t offset, std::uint8_t* output,
              const std::size_t count) const noexcept {
        if (count == 0U)
            return offset <= source_.size;
        if (output == nullptr || offset > source_.size || count > source_.size - offset)
            return fail();
        std::size_t cursor = offset;
        std::size_t written = 0U;
        while (written < count) {
            Window* window = find(cursor);
            if (window == nullptr) {
                ++stats_.cacheMisses;
                if (!accessAllowed_) {
                    ++stats_.prohibitedMisses;
                    return fail();
                }
                if (!load(cursor))
                    return false;
                window = find(cursor);
            } else {
                ++stats_.cacheHits;
            }
            const std::size_t inside = cursor - window->offset;
            const std::size_t available = window->length - inside;
            const std::size_t chunk = available < count - written ? available : count - written;
            std::memcpy(output + written, window->bytes + inside, chunk);
            cursor += chunk;
            written += chunk;
        }
        return true;
    }

    [[nodiscard]] std::uint8_t byte(const std::size_t offset) const noexcept {
        std::uint8_t result = 0U;
        if (!read(offset, &result, 1U))
            return 0U;
        return result;
    }

  private:
    struct Window final {
        std::uint8_t bytes[WindowBytes];
        std::size_t offset;
        std::size_t length;
        bool valid;

        Window() noexcept : bytes(), offset(0U), length(0U), valid(false) {}
    };

    [[nodiscard]] Window* find(const std::size_t offset) const noexcept {
        for (std::size_t index = 0U; index < 2U; ++index) {
            Window& window = windows_[index];
            if (window.valid && offset >= window.offset &&
                offset - window.offset < window.length)
                return &window;
        }
        return nullptr;
    }

    bool load(const std::size_t requestedOffset) const noexcept {
        if (!accessAllowed_) {
            ++stats_.prohibitedMisses;
            return fail();
        }
        if (source_.read == nullptr || requestedOffset >= source_.size)
            return fail();
        const std::size_t aligned = requestedOffset - requestedOffset % WindowBytes;
        const std::size_t remaining = source_.size - aligned;
        const std::size_t requested = remaining < WindowBytes ? remaining : WindowBytes;
        Window& window = windows_[nextWindow_];
        const std::size_t received =
            source_.read(source_.context, aligned, window.bytes, requested);
        ++stats_.physicalReadCalls;
        stats_.physicalReadBytes += static_cast<std::uint64_t>(received);
        if (received != requested) {
            window.valid = false;
            return fail();
        }
        window.offset = aligned;
        window.length = received;
        window.valid = true;
        nextWindow_ = (nextWindow_ + 1U) % 2U;
        return true;
    }

    bool fail() const noexcept {
        failed_ = true;
        return false;
    }

    Source source_;
    mutable Window windows_[2];
    mutable std::size_t nextWindow_;
    mutable Stats stats_;
    bool accessAllowed_ = true;
    mutable bool failed_ = false;
};

} // namespace fabgl_bounded_storage
