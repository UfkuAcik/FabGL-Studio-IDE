#pragma once

#include "fabgl/core/guid.h"
#include "fabgl/core/result.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <typeindex>
#include <unordered_map>

namespace fabgl {

struct ResourceCacheStats final {
    std::size_t entryCount = 0;
    std::size_t usedBytes = 0;
    std::size_t capacityBytes = 0;
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    std::uint64_t evictions = 0;
};

class ResourceCache final {
  public:
    explicit ResourceCache(std::size_t capacityBytes = 0) : capacityBytes_(capacityBytes) {}

    template <typename T>
    [[nodiscard]] Result<void> put(AssetGuid id, std::shared_ptr<T> resource,
                                   std::size_t sizeBytes) {
        if (!resource) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "cached resource cannot be null"));
        }
        return putErased(id, std::move(resource), std::type_index(typeid(T)), sizeBytes);
    }

    template <typename T> [[nodiscard]] std::shared_ptr<T> get(AssetGuid id) {
        const auto erased = getErased(id, std::type_index(typeid(T)));
        return std::static_pointer_cast<T>(erased);
    }

    [[nodiscard]] bool contains(AssetGuid id) const;
    [[nodiscard]] bool erase(AssetGuid id);
    void clear();
    void setCapacityBytes(std::size_t capacityBytes);
    [[nodiscard]] ResourceCacheStats stats() const;

  private:
    struct Entry final {
        std::shared_ptr<void> resource;
        std::type_index type;
        std::size_t sizeBytes = 0;
        std::uint64_t lastAccess = 0;
    };

    [[nodiscard]] Result<void> putErased(AssetGuid id, std::shared_ptr<void> resource,
                                         std::type_index type, std::size_t sizeBytes);
    [[nodiscard]] std::shared_ptr<void> getErased(AssetGuid id, std::type_index requestedType);
    void evictToFit(std::size_t requiredBytes, const AssetGuid* protectedId);

    mutable std::mutex mutex_;
    std::unordered_map<AssetGuid, Entry, StrongGuidHash<AssetGuidTag>> entries_;
    std::size_t usedBytes_ = 0;
    std::size_t capacityBytes_ = 0;
    std::uint64_t accessCounter_ = 0;
    std::uint64_t hits_ = 0;
    std::uint64_t misses_ = 0;
    std::uint64_t evictions_ = 0;
};

} // namespace fabgl
