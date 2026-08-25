#pragma once

#include "fabgl/core/guid.h"
#include "fabgl/core/result.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <typeindex>
#include <utility>
#include <vector>

namespace fabgl {

enum class AssetStreamState : std::uint8_t {
    Unloaded,
    Queued,
    Loading,
    Resident,
    Failed,
};

enum class AssetTransitionState : std::uint8_t {
    Idle,
    Loading,
    Ready,
    Failed,
};

struct AssetStreamingLimits final {
    std::size_t maximumTrackedAssets = 4096U;
    std::size_t maximumQueuedAssets = 4096U;
    std::size_t maximumDependenciesPerAsset = 256U;
    std::size_t maximumResidentAssets = 512U;
    // Zero keeps the desktop-style unlimited mode available. Constrained targets
    // should always provide an explicit non-zero budget.
    std::size_t maximumResidentBytes = 128U * 1024U * 1024U;
    std::size_t maximumTransitionRoots = 4096U;
    std::size_t maximumDiagnostics = 128U;
};

struct AssetLoadPayload final {
    std::shared_ptr<void> resource;
    std::type_index type{typeid(void)};
    std::size_t residentBytes = 0U;

    template <typename T>
    [[nodiscard]] static AssetLoadPayload make(std::shared_ptr<T> value,
                                               const std::size_t bytes) {
        return {std::move(value), std::type_index(typeid(T)), bytes};
    }
};

using AssetDependencyResolver =
    std::function<Result<std::vector<AssetGuid>>(AssetGuid asset)>;
using AssetResourceLookup =
    std::function<std::shared_ptr<void>(AssetGuid asset, std::type_index requestedType)>;
using AssetResourceLoader =
    std::function<Result<AssetLoadPayload>(AssetGuid asset,
                                           const AssetResourceLookup& dependencies)>;

struct AssetStreamingCallbacks final {
    AssetDependencyResolver dependencies;
    AssetResourceLoader load;
};

struct AssetStreamingDiagnostic final {
    std::uint64_t sequence = 0U;
    AssetGuid asset{};
    Error error{};
};

struct AssetStreamingStats final {
    std::size_t trackedAssets = 0U;
    std::size_t queuedAssets = 0U;
    std::size_t residentAssets = 0U;
    std::size_t referencedAssets = 0U;
    std::size_t residentBytes = 0U;
    std::size_t residentByteBudget = 0U;
    std::uint64_t requests = 0U;
    std::uint64_t requestHits = 0U;
    std::uint64_t loads = 0U;
    std::uint64_t loadFailures = 0U;
    std::uint64_t evictions = 0U;
    std::uint64_t resourceHits = 0U;
    std::uint64_t resourceMisses = 0U;
    std::uint64_t transitionsCommitted = 0U;
    std::uint64_t transitionsCancelled = 0U;
};

struct AssetStreamStatus final {
    AssetStreamState state = AssetStreamState::Unloaded;
    std::size_t referenceCount = 0U;
    std::size_t residentBytes = 0U;
    std::vector<AssetGuid> dependencies;
    std::optional<Error> lastError;
};

// Deterministic, single-thread-pumped resource residency manager. Dependency
// discovery is lazy, loads are topologically ordered, and every queue/cache
// collection is bounded by AssetStreamingLimits. The class is intentionally
// independent of filesystem and asset formats.
class AssetStreamingManager final {
  public:
    [[nodiscard]] static Result<AssetStreamingManager>
    create(AssetStreamingCallbacks callbacks, const AssetStreamingLimits& limits = {});

    AssetStreamingManager(const AssetStreamingManager&) = default;
    AssetStreamingManager& operator=(const AssetStreamingManager&) = default;
    AssetStreamingManager(AssetStreamingManager&&) noexcept = default;
    AssetStreamingManager& operator=(AssetStreamingManager&&) noexcept = default;

    // Queues an unpinned prefetch. The dependency closure remains temporarily
    // pinned until this root request reaches Resident or Failed.
    [[nodiscard]] Result<void> request(AssetGuid asset);
    [[nodiscard]] Result<void> retry(AssetGuid asset);

    // Explicit references pin the complete dependency closure against eviction.
    [[nodiscard]] Result<void> acquire(AssetGuid asset);
    [[nodiscard]] Result<void> release(AssetGuid asset);

    // Performs at most maximumLoads loader calls. No worker thread is created;
    // callers can bind this directly to EngineLoop::assetStreamingUpdate.
    [[nodiscard]] Result<std::size_t> pump(std::size_t maximumLoads = 1U);

    template <typename T> [[nodiscard]] std::shared_ptr<T> get(AssetGuid asset) {
        return std::static_pointer_cast<T>(getErased(asset, std::type_index(typeid(T))));
    }

    [[nodiscard]] Result<void> beginTransition(std::vector<AssetGuid> targetRoots);
    [[nodiscard]] Result<void> commitTransition();
    void cancelTransition() noexcept;
    [[nodiscard]] AssetTransitionState transitionState() const noexcept;
    [[nodiscard]] std::vector<AssetGuid> activeTransitionRoots() const;
    [[nodiscard]] std::vector<AssetGuid> pendingTransitionRoots() const;

    // Removes every unreferenced resident item immediately. Normal budget
    // pressure uses deterministic LRU eviction instead.
    [[nodiscard]] std::size_t evictUnused();

    [[nodiscard]] std::optional<AssetStreamStatus> status(AssetGuid asset) const;
    [[nodiscard]] AssetStreamingStats stats() const noexcept;
    [[nodiscard]] std::vector<AssetStreamingDiagnostic> diagnostics() const;

  private:
    struct State;
    explicit AssetStreamingManager(std::shared_ptr<State> state) : state_(std::move(state)) {}

    [[nodiscard]] std::shared_ptr<void> getErased(AssetGuid asset,
                                                   std::type_index requestedType);

    std::shared_ptr<State> state_;
};

} // namespace fabgl
