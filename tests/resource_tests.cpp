#include "test_harness.h"

#include "fabgl/resources/resource_cache.h"

#include <memory>
#include <string>

using namespace fabgl;

FGL_TEST(resource_cache_is_type_safe_and_uses_lru_eviction) {
    ResourceCache cache(8);
    const auto first = AssetGuid::fromStableName("first");
    const auto second = AssetGuid::fromStableName("second");
    const auto third = AssetGuid::fromStableName("third");

    FGL_CHECK(cache.put(first, std::make_shared<std::string>("one"), 4));
    FGL_CHECK(cache.put(second, std::make_shared<std::string>("two"), 4));
    FGL_CHECK(cache.get<std::string>(first) != nullptr);
    FGL_CHECK(cache.put(third, std::make_shared<std::string>("three"), 4));

    FGL_CHECK(cache.contains(first));
    FGL_CHECK(!cache.contains(second));
    FGL_CHECK(cache.contains(third));
    FGL_CHECK(cache.get<int>(first) == nullptr);
    const auto stats = cache.stats();
    FGL_CHECK(stats.entryCount == 2);
    FGL_CHECK(stats.usedBytes == 8);
    FGL_CHECK(stats.hits == 1);
    FGL_CHECK(stats.misses == 1);
    FGL_CHECK(stats.evictions == 1);
}

FGL_TEST(resource_cache_rejects_oversize_and_supports_resize_clear_and_erase) {
    ResourceCache cache(4);
    const auto first = AssetGuid::fromStableName("first");
    const auto second = AssetGuid::fromStableName("second");
    FGL_CHECK(cache.put(first, std::make_shared<int>(7), 4));
    auto oversize = cache.put(second, std::make_shared<int>(9), 5);
    FGL_CHECK(!oversize);
    FGL_CHECK(oversize.error().code() == ErrorCode::CapacityExceeded);
    FGL_CHECK(cache.contains(first));

    cache.setCapacityBytes(2);
    FGL_CHECK(!cache.contains(first));
    FGL_CHECK(cache.stats().evictions == 1);
    FGL_CHECK(!cache.erase(first));
    cache.setCapacityBytes(0);
    FGL_CHECK(cache.put(first, std::make_shared<int>(7), 100));
    FGL_CHECK(cache.erase(first));
    cache.clear();
    FGL_CHECK(cache.stats().entryCount == 0);
}
