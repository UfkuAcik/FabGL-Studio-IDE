#include "fabgl/resources/resource_cache.h"

#include <limits>

namespace fabgl {

Result<void> ResourceCache::putErased(AssetGuid id, std::shared_ptr<void> resource,
                                      std::type_index type, std::size_t sizeBytes) {
    if (id.isNil()) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "resource GUID cannot be nil"));
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (capacityBytes_ != 0 && sizeBytes > capacityBytes_) {
        return Result<void>::failure(
            Error(ErrorCode::CapacityExceeded, "resource is larger than the cache capacity")
                .addContext("resource_bytes", std::to_string(sizeBytes))
                .addContext("capacity_bytes", std::to_string(capacityBytes_)));
    }

    const auto existing = entries_.find(id);
    if (existing != entries_.end()) {
        usedBytes_ -= existing->second.sizeBytes;
    }
    evictToFit(sizeBytes, existing == entries_.end() ? nullptr : &id);

    if (existing != entries_.end()) {
        existing->second = Entry{std::move(resource), type, sizeBytes, ++accessCounter_};
    } else {
        entries_.emplace(id, Entry{std::move(resource), type, sizeBytes, ++accessCounter_});
    }
    usedBytes_ += sizeBytes;
    return Result<void>::success();
}

std::shared_ptr<void> ResourceCache::getErased(AssetGuid id, std::type_index requestedType) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto iterator = entries_.find(id);
    if (iterator == entries_.end() || iterator->second.type != requestedType) {
        ++misses_;
        return {};
    }
    ++hits_;
    iterator->second.lastAccess = ++accessCounter_;
    return iterator->second.resource;
}

bool ResourceCache::contains(AssetGuid id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.find(id) != entries_.end();
}

bool ResourceCache::erase(AssetGuid id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto iterator = entries_.find(id);
    if (iterator == entries_.end()) {
        return false;
    }
    usedBytes_ -= iterator->second.sizeBytes;
    entries_.erase(iterator);
    return true;
}

void ResourceCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    usedBytes_ = 0;
}

void ResourceCache::setCapacityBytes(std::size_t capacityBytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    capacityBytes_ = capacityBytes;
    evictToFit(0, nullptr);
}

ResourceCacheStats ResourceCache::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {entries_.size(), usedBytes_, capacityBytes_, hits_, misses_, evictions_};
}

void ResourceCache::evictToFit(std::size_t requiredBytes, const AssetGuid* protectedId) {
    if (capacityBytes_ == 0) {
        return;
    }
    while (usedBytes_ + requiredBytes > capacityBytes_) {
        auto oldest = entries_.end();
        auto oldestAccess = std::numeric_limits<std::uint64_t>::max();
        for (auto iterator = entries_.begin(); iterator != entries_.end(); ++iterator) {
            if (protectedId != nullptr && iterator->first == *protectedId) {
                continue;
            }
            if (iterator->second.lastAccess < oldestAccess) {
                oldest = iterator;
                oldestAccess = iterator->second.lastAccess;
            }
        }
        if (oldest == entries_.end()) {
            break;
        }
        usedBytes_ -= oldest->second.sizeBytes;
        entries_.erase(oldest);
        ++evictions_;
    }
}

} // namespace fabgl
