#include "test_harness.h"

#include <fabgl/resources/asset_streaming_manager.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

fabgl::AssetGuid guid(const char* value) {
    auto parsed = fabgl::AssetGuid::parse(value);
    FGL_CHECK(parsed);
    return parsed.value();
}

fabgl::AssetStreamingCallbacks callbacks(
    const std::map<fabgl::AssetGuid, std::vector<fabgl::AssetGuid>>& dependencies,
    std::vector<fabgl::AssetGuid>& loadOrder, const std::size_t bytes = 4U,
    const fabgl::AssetGuid failed = {}) {
    fabgl::AssetStreamingCallbacks result;
    result.dependencies = [dependencies](const fabgl::AssetGuid asset) {
        const auto found = dependencies.find(asset);
        if (found == dependencies.end()) {
            return fabgl::Result<std::vector<fabgl::AssetGuid>>::failure(
                fabgl::Error(fabgl::ErrorCode::NotFound, "test asset is unknown"));
        }
        return fabgl::Result<std::vector<fabgl::AssetGuid>>::success(found->second);
    };
    result.load = [&loadOrder, bytes, failed](
                      const fabgl::AssetGuid asset, const fabgl::AssetResourceLookup&) {
        loadOrder.push_back(asset);
        if (asset == failed) {
            return fabgl::Result<fabgl::AssetLoadPayload>::failure(
                fabgl::Error(fabgl::ErrorCode::InvalidFormat, "test asset is corrupt"));
        }
        return fabgl::Result<fabgl::AssetLoadPayload>::success(
            fabgl::AssetLoadPayload::make(std::make_shared<std::string>(asset.toString()), bytes));
    };
    return result;
}

} // namespace

FGL_TEST(asset_streaming_loads_dependency_dag_deterministically_and_pins_the_closure) {
    const auto root = guid("10000000-0000-4000-8000-000000000001");
    const auto first = guid("10000000-0000-4000-8000-000000000002");
    const auto second = guid("10000000-0000-4000-8000-000000000003");
    const auto leaf = guid("10000000-0000-4000-8000-000000000004");
    std::map<fabgl::AssetGuid, std::vector<fabgl::AssetGuid>> graph{
        {root, {second, first}}, {first, {leaf}}, {second, {}}, {leaf, {}}};
    std::vector<fabgl::AssetGuid> order;
    auto created = fabgl::AssetStreamingManager::create(callbacks(graph, order));
    FGL_CHECK(created);
    auto manager = std::move(created.value());

    FGL_CHECK(manager.acquire(root));
    FGL_CHECK(manager.status(root)->state == fabgl::AssetStreamState::Queued);
    FGL_CHECK(manager.status(root)->referenceCount == 2U); // explicit + pending request
    FGL_CHECK(manager.pump(8U));
    FGL_CHECK(order == std::vector<fabgl::AssetGuid>({leaf, first, second, root}));
    FGL_CHECK(manager.get<std::string>(root) != nullptr);
    FGL_CHECK(manager.status(root)->referenceCount == 1U);
    FGL_CHECK(manager.status(leaf)->referenceCount == 1U);
    FGL_CHECK(manager.release(root));
    FGL_CHECK(manager.status(root)->referenceCount == 0U);
    FGL_CHECK(manager.status(leaf)->referenceCount == 0U);
}

FGL_TEST(asset_streaming_uses_deterministic_lru_eviction_and_type_safe_lookup) {
    const auto first = guid("20000000-0000-4000-8000-000000000001");
    const auto second = guid("20000000-0000-4000-8000-000000000002");
    const auto third = guid("20000000-0000-4000-8000-000000000003");
    const std::map<fabgl::AssetGuid, std::vector<fabgl::AssetGuid>> graph{
        {first, {}}, {second, {}}, {third, {}}};
    std::vector<fabgl::AssetGuid> order;
    fabgl::AssetStreamingLimits limits;
    limits.maximumResidentBytes = 8U;
    auto created = fabgl::AssetStreamingManager::create(callbacks(graph, order), limits);
    FGL_CHECK(created);
    auto manager = std::move(created.value());
    for (const auto asset : {first, second}) {
        FGL_CHECK(manager.acquire(asset));
        FGL_CHECK(manager.pump());
        FGL_CHECK(manager.release(asset));
    }
    FGL_CHECK(manager.get<std::string>(first) != nullptr); // first becomes newest
    FGL_CHECK(manager.get<int>(first) == nullptr);
    FGL_CHECK(manager.acquire(third));
    FGL_CHECK(manager.pump());
    FGL_CHECK(manager.status(first)->state == fabgl::AssetStreamState::Resident);
    FGL_CHECK(manager.status(second)->state == fabgl::AssetStreamState::Unloaded);
    FGL_CHECK(manager.status(third)->state == fabgl::AssetStreamState::Resident);
    FGL_CHECK(manager.stats().evictions == 1U);
}

FGL_TEST(asset_streaming_transition_is_transactional_when_a_new_asset_fails) {
    const auto stable = guid("30000000-0000-4000-8000-000000000001");
    const auto corrupt = guid("30000000-0000-4000-8000-000000000002");
    const std::map<fabgl::AssetGuid, std::vector<fabgl::AssetGuid>> graph{
        {stable, {}}, {corrupt, {}}};
    std::vector<fabgl::AssetGuid> order;
    auto created =
        fabgl::AssetStreamingManager::create(callbacks(graph, order, 4U, corrupt));
    FGL_CHECK(created);
    auto manager = std::move(created.value());

    FGL_CHECK(manager.beginTransition({stable}));
    FGL_CHECK(manager.pump());
    FGL_CHECK(manager.transitionState() == fabgl::AssetTransitionState::Ready);
    FGL_CHECK(manager.commitTransition());
    FGL_CHECK(manager.status(stable)->referenceCount == 1U);

    FGL_CHECK(manager.beginTransition({corrupt}));
    auto pumped = manager.pump();
    FGL_CHECK(!pumped && pumped.error().code() == fabgl::ErrorCode::InvalidFormat);
    FGL_CHECK(manager.transitionState() == fabgl::AssetTransitionState::Failed);
    FGL_CHECK(manager.status(stable)->referenceCount == 1U);
    FGL_CHECK(manager.status(corrupt)->referenceCount == 1U);
    manager.cancelTransition();
    FGL_CHECK(manager.activeTransitionRoots() == std::vector<fabgl::AssetGuid>({stable}));
    FGL_CHECK(manager.status(stable)->referenceCount == 1U);
    FGL_CHECK(manager.status(corrupt)->referenceCount == 0U);
    FGL_CHECK(manager.diagnostics().size() == 1U);
}

FGL_TEST(asset_streaming_rejects_cycles_and_can_retry_a_transient_failure) {
    const auto first = guid("40000000-0000-4000-8000-000000000001");
    const auto second = guid("40000000-0000-4000-8000-000000000002");
    std::vector<fabgl::AssetGuid> order;
    auto cycle = fabgl::AssetStreamingManager::create(
        callbacks({{first, {second}}, {second, {first}}}, order));
    FGL_CHECK(cycle);
    auto cyclicRequest = cycle.value().request(first);
    FGL_CHECK(!cyclicRequest && cyclicRequest.error().code() == fabgl::ErrorCode::CycleDetected);

    bool fail = true;
    fabgl::AssetStreamingCallbacks transient;
    transient.dependencies = [first](const fabgl::AssetGuid requested) {
        if (requested != first)
            return fabgl::Result<std::vector<fabgl::AssetGuid>>::failure(
                fabgl::Error(fabgl::ErrorCode::NotFound, "unknown"));
        return fabgl::Result<std::vector<fabgl::AssetGuid>>::success({});
    };
    transient.load = [&fail](const fabgl::AssetGuid,
                             const fabgl::AssetResourceLookup&) {
        if (fail) {
            return fabgl::Result<fabgl::AssetLoadPayload>::failure(
                fabgl::Error(fabgl::ErrorCode::IoError, "temporary read failure"));
        }
        return fabgl::Result<fabgl::AssetLoadPayload>::success(
            fabgl::AssetLoadPayload::make(std::make_shared<int>(7), sizeof(int)));
    };
    auto retryable = fabgl::AssetStreamingManager::create(std::move(transient));
    FGL_CHECK(retryable);
    auto manager = std::move(retryable.value());
    FGL_CHECK(manager.request(first));
    FGL_CHECK(!manager.pump());
    fail = false;
    FGL_CHECK(manager.retry(first));
    FGL_CHECK(manager.pump());
    FGL_CHECK(manager.get<int>(first) != nullptr && *manager.get<int>(first) == 7);
}
