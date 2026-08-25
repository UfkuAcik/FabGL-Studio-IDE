#include "fabgl/resources/asset_streaming_manager.h"

#include <algorithm>
#include <deque>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace fabgl {
namespace {

[[nodiscard]] bool validLimits(const AssetStreamingLimits& limits) noexcept {
    return limits.maximumTrackedAssets != 0U && limits.maximumQueuedAssets != 0U &&
           limits.maximumDependenciesPerAsset != 0U && limits.maximumResidentAssets != 0U &&
           limits.maximumTransitionRoots != 0U && limits.maximumDiagnostics != 0U;
}

[[nodiscard]] Error assetContext(Error error, const AssetGuid asset) {
    return error.withContext("asset", asset.toString());
}

} // namespace

struct AssetStreamingManager::State final {
    struct Record final {
        AssetStreamState state = AssetStreamState::Unloaded;
        bool dependenciesResolved = false;
        std::vector<AssetGuid> dependencies;
        std::shared_ptr<void> resource;
        std::type_index type{typeid(void)};
        std::size_t residentBytes = 0U;
        std::size_t explicitReferences = 0U;
        std::size_t pendingReferences = 0U;
        std::uint64_t lastAccess = 0U;
        std::optional<Error> lastError;
    };

    struct GraphPlan final {
        std::map<AssetGuid, std::vector<AssetGuid>> discovered;
        std::vector<AssetGuid> topologicalOrder;
    };

    explicit State(AssetStreamingCallbacks valueCallbacks, AssetStreamingLimits valueLimits)
        : callbacks(std::move(valueCallbacks)), limits(valueLimits) {
        statistics.residentByteBudget = limits.maximumResidentBytes;
    }

    [[nodiscard]] Result<GraphPlan> plan(const AssetGuid root) {
        if (root.isNil()) {
            return Result<GraphPlan>::failure(
                Error(ErrorCode::InvalidArgument, "streamed asset GUID cannot be nil"));
        }
        GraphPlan result;
        std::set<AssetGuid> visiting;
        std::set<AssetGuid> visited;
        std::function<Result<void>(AssetGuid)> visit = [&](const AssetGuid asset) -> Result<void> {
            if (visited.contains(asset))
                return Result<void>::success();
            if (!visiting.insert(asset).second) {
                return Result<void>::failure(
                    Error(ErrorCode::CycleDetected, "asset dependency cycle detected")
                        .addContext("asset", asset.toString())
                        .addContext("root", root.toString()));
            }

            std::vector<AssetGuid> dependencies;
            const auto existing = records.find(asset);
            if (existing != records.end() && existing->second.dependenciesResolved) {
                dependencies = existing->second.dependencies;
            } else {
                auto resolved = callbacks.dependencies(asset);
                if (!resolved) {
                    return Result<void>::failure(assetContext(resolved.error(), asset));
                }
                dependencies = std::move(resolved.value());
                if (dependencies.size() > limits.maximumDependenciesPerAsset) {
                    return Result<void>::failure(
                        Error(ErrorCode::CapacityExceeded,
                              "asset dependency count exceeds the streaming limit")
                            .addContext("asset", asset.toString())
                            .addContext("dependencies", std::to_string(dependencies.size())));
                }
                if (std::any_of(dependencies.begin(), dependencies.end(),
                                [](const AssetGuid dependency) { return dependency.isNil(); })) {
                    return Result<void>::failure(
                        Error(ErrorCode::InvalidFormat, "asset dependency GUID cannot be nil")
                            .addContext("asset", asset.toString()));
                }
                std::sort(dependencies.begin(), dependencies.end());
                const auto duplicate = std::adjacent_find(dependencies.begin(), dependencies.end());
                if (duplicate != dependencies.end()) {
                    return Result<void>::failure(
                        Error(ErrorCode::InvalidFormat, "asset dependency is listed more than once")
                            .addContext("asset", asset.toString())
                            .addContext("dependency", duplicate->toString()));
                }
                result.discovered.emplace(asset, dependencies);
            }

            if (visited.size() + visiting.size() > limits.maximumTrackedAssets) {
                return Result<void>::failure(
                    Error(ErrorCode::CapacityExceeded,
                          "asset dependency graph exceeds the tracked-asset limit")
                        .addContext("root", root.toString()));
            }
            for (const auto dependency : dependencies) {
                auto traversed = visit(dependency);
                if (!traversed)
                    return traversed;
            }
            visiting.erase(asset);
            visited.insert(asset);
            result.topologicalOrder.push_back(asset);
            return Result<void>::success();
        };
        auto traversed = visit(root);
        if (!traversed)
            return Result<GraphPlan>::failure(traversed.error());
        if (records.size() + result.discovered.size() > limits.maximumTrackedAssets) {
            return Result<GraphPlan>::failure(
                Error(ErrorCode::CapacityExceeded,
                      "streaming request exceeds the tracked-asset limit")
                    .addContext("root", root.toString()));
        }
        return Result<GraphPlan>::success(std::move(result));
    }

    void commitPlan(const GraphPlan& plan) {
        for (const auto& [asset, dependencies] : plan.discovered) {
            auto& record = records[asset];
            record.dependencies = dependencies;
            record.dependenciesResolved = true;
        }
    }

    [[nodiscard]] std::vector<AssetGuid> closure(const AssetGuid root) const {
        std::set<AssetGuid> visited;
        std::vector<AssetGuid> pending{root};
        while (!pending.empty()) {
            const auto asset = pending.back();
            pending.pop_back();
            if (!visited.insert(asset).second)
                continue;
            const auto record = records.find(asset);
            if (record == records.end())
                continue;
            pending.insert(pending.end(), record->second.dependencies.begin(),
                           record->second.dependencies.end());
        }
        return {visited.begin(), visited.end()};
    }

    [[nodiscard]] Result<void> incrementReferences(const AssetGuid root,
                                                   const bool pending) {
        const auto assets = closure(root);
        for (const auto asset : assets) {
            const auto& record = records.at(asset);
            const auto value = pending ? record.pendingReferences : record.explicitReferences;
            if (value == std::numeric_limits<std::size_t>::max()) {
                return Result<void>::failure(
                    Error(ErrorCode::CapacityExceeded, "asset reference count overflow")
                        .addContext("asset", asset.toString()));
            }
        }
        for (const auto asset : assets) {
            auto& record = records.at(asset);
            auto& value = pending ? record.pendingReferences : record.explicitReferences;
            ++value;
        }
        return Result<void>::success();
    }

    void decrementReferences(const AssetGuid root, const bool pending) noexcept {
        for (const auto asset : closure(root)) {
            auto& record = records.at(asset);
            auto& value = pending ? record.pendingReferences : record.explicitReferences;
            if (value != 0U)
                --value;
        }
    }

    [[nodiscard]] std::size_t referenceCount(const Record& record) const noexcept {
        return record.explicitReferences + record.pendingReferences;
    }

    void appendDiagnostic(const AssetGuid asset, const Error& error) {
        if (diagnosticLog.size() == limits.maximumDiagnostics)
            diagnosticLog.erase(diagnosticLog.begin());
        diagnosticLog.push_back({++diagnosticSequence, asset, error});
    }

    void finishPendingRequest(const AssetGuid root) noexcept {
        if (pendingRequests.erase(root) != 0U)
            decrementReferences(root, true);
    }

    void failPendingDependents(const AssetGuid failedAsset, const Error& cause) {
        const std::vector<AssetGuid> roots(pendingRequests.begin(), pendingRequests.end());
        for (const auto root : roots) {
            const auto assets = closure(root);
            if (!std::binary_search(assets.begin(), assets.end(), failedAsset))
                continue;
            if (root != failedAsset) {
                auto& rootRecord = records.at(root);
                if (rootRecord.state == AssetStreamState::Queued) {
                    rootRecord.state = AssetStreamState::Failed;
                    rootRecord.lastError =
                        Error(ErrorCode::InvalidState, "asset dependency failed to load")
                            .addContext("asset", root.toString())
                            .addContext("dependency", failedAsset.toString())
                            .addContext("cause", cause.message());
                    ++statistics.loadFailures;
                    appendDiagnostic(root, *rootRecord.lastError);
                }
            }
            finishPendingRequest(root);
        }
    }

    [[nodiscard]] bool transitionContains(const AssetGuid asset) const {
        if (transitionStatus == AssetTransitionState::Idle)
            return false;
        for (const auto root : pendingTransition) {
            const auto assets = closure(root);
            if (std::binary_search(assets.begin(), assets.end(), asset))
                return true;
        }
        return false;
    }

    void updateTransitionStatus() {
        if (transitionStatus == AssetTransitionState::Idle ||
            transitionStatus == AssetTransitionState::Failed)
            return;
        for (const auto root : pendingTransition) {
            const auto record = records.find(root);
            if (record == records.end() || record->second.state != AssetStreamState::Resident) {
                transitionStatus = AssetTransitionState::Loading;
                return;
            }
        }
        transitionStatus = AssetTransitionState::Ready;
    }

    [[nodiscard]] bool makeRoom(const std::size_t requiredBytes,
                                const AssetGuid protectedAsset) {
        const auto exceedsBytes = [&] {
            return limits.maximumResidentBytes != 0U &&
                   (requiredBytes > limits.maximumResidentBytes ||
                    residentBytes > limits.maximumResidentBytes - requiredBytes);
        };
        const auto exceedsEntries = [&] {
            return residentAssets >= limits.maximumResidentAssets;
        };
        while (exceedsBytes() || exceedsEntries()) {
            auto victim = records.end();
            for (auto iterator = records.begin(); iterator != records.end(); ++iterator) {
                const auto& record = iterator->second;
                if (iterator->first == protectedAsset ||
                    record.state != AssetStreamState::Resident || referenceCount(record) != 0U)
                    continue;
                if (victim == records.end() ||
                    record.lastAccess < victim->second.lastAccess ||
                    (record.lastAccess == victim->second.lastAccess &&
                     iterator->first < victim->first)) {
                    victim = iterator;
                }
            }
            if (victim == records.end())
                return false;
            residentBytes -= victim->second.residentBytes;
            --residentAssets;
            victim->second.resource.reset();
            victim->second.type = std::type_index(typeid(void));
            victim->second.residentBytes = 0U;
            victim->second.state = AssetStreamState::Unloaded;
            ++statistics.evictions;
        }
        return true;
    }

    [[nodiscard]] std::shared_ptr<void> getErased(const AssetGuid asset,
                                                   const std::type_index requestedType,
                                                   const bool countStats) {
        const auto found = records.find(asset);
        if (found == records.end() || found->second.state != AssetStreamState::Resident ||
            found->second.type != requestedType || !found->second.resource) {
            if (countStats)
                ++statistics.resourceMisses;
            return {};
        }
        if (countStats)
            ++statistics.resourceHits;
        found->second.lastAccess = ++accessSequence;
        return found->second.resource;
    }

    AssetStreamingCallbacks callbacks;
    AssetStreamingLimits limits;
    std::map<AssetGuid, Record> records;
    std::deque<AssetGuid> queue;
    std::set<AssetGuid> pendingRequests;
    std::vector<AssetGuid> activeTransition;
    std::vector<AssetGuid> pendingTransition;
    std::vector<AssetGuid> pendingTransitionAdditions;
    AssetTransitionState transitionStatus = AssetTransitionState::Idle;
    std::optional<Error> transitionError;
    std::vector<AssetStreamingDiagnostic> diagnosticLog;
    AssetStreamingStats statistics;
    std::size_t residentBytes = 0U;
    std::size_t residentAssets = 0U;
    std::uint64_t accessSequence = 0U;
    std::uint64_t diagnosticSequence = 0U;
};

Result<AssetStreamingManager>
AssetStreamingManager::create(AssetStreamingCallbacks callbacks,
                              const AssetStreamingLimits& limits) {
    if (!callbacks.dependencies || !callbacks.load || !validLimits(limits)) {
        return Result<AssetStreamingManager>::failure(
            Error(ErrorCode::InvalidArgument,
                  "asset streaming callbacks and bounded limits are required"));
    }
    return Result<AssetStreamingManager>::success(
        AssetStreamingManager(std::make_shared<State>(std::move(callbacks), limits)));
}

Result<void> AssetStreamingManager::request(const AssetGuid asset) {
    ++state_->statistics.requests;
    auto planned = state_->plan(asset);
    if (!planned)
        return Result<void>::failure(planned.error());
    state_->commitPlan(planned.value());

    const auto root = state_->records.find(asset);
    if (root != state_->records.end() && root->second.state == AssetStreamState::Resident) {
        root->second.lastAccess = ++state_->accessSequence;
        ++state_->statistics.requestHits;
        return Result<void>::success();
    }
    if (root != state_->records.end() && root->second.state == AssetStreamState::Failed) {
        return Result<void>::failure(*root->second.lastError);
    }
    if (state_->pendingRequests.contains(asset)) {
        ++state_->statistics.requestHits;
        return Result<void>::success();
    }

    std::size_t additionalQueued = 0U;
    for (const auto item : planned.value().topologicalOrder) {
        const auto& record = state_->records.at(item);
        if (record.state == AssetStreamState::Failed)
            return Result<void>::failure(*record.lastError);
        additionalQueued += record.state == AssetStreamState::Unloaded ? 1U : 0U;
    }
    if (state_->queue.size() + additionalQueued > state_->limits.maximumQueuedAssets) {
        return Result<void>::failure(
            Error(ErrorCode::CapacityExceeded, "asset streaming queue limit exceeded")
                .addContext("asset", asset.toString()));
    }
    auto pinned = state_->incrementReferences(asset, true);
    if (!pinned)
        return pinned;
    state_->pendingRequests.insert(asset);
    for (const auto item : planned.value().topologicalOrder) {
        auto& record = state_->records.at(item);
        if (record.state == AssetStreamState::Unloaded) {
            record.state = AssetStreamState::Queued;
            record.lastError.reset();
            state_->queue.push_back(item);
        }
    }
    return Result<void>::success();
}

Result<void> AssetStreamingManager::retry(const AssetGuid asset) {
    auto planned = state_->plan(asset);
    if (!planned)
        return Result<void>::failure(planned.error());
    state_->commitPlan(planned.value());
    for (const auto item : planned.value().topologicalOrder) {
        auto& record = state_->records.at(item);
        if (record.state == AssetStreamState::Failed) {
            record.state = AssetStreamState::Unloaded;
            record.lastError.reset();
        }
    }
    return request(asset);
}

Result<void> AssetStreamingManager::acquire(const AssetGuid asset) {
    auto requested = request(asset);
    if (!requested)
        return requested;
    return state_->incrementReferences(asset, false);
}

Result<void> AssetStreamingManager::release(const AssetGuid asset) {
    const auto root = state_->records.find(asset);
    if (root == state_->records.end() || !root->second.dependenciesResolved ||
        root->second.explicitReferences == 0U) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "asset release has no matching acquire")
                .addContext("asset", asset.toString()));
    }
    state_->decrementReferences(asset, false);
    return Result<void>::success();
}

Result<std::size_t> AssetStreamingManager::pump(const std::size_t maximumLoads) {
    if (maximumLoads == 0U) {
        return Result<std::size_t>::failure(
            Error(ErrorCode::InvalidArgument, "asset streaming pump limit must be positive"));
    }
    std::size_t loaded = 0U;
    while (loaded < maximumLoads && !state_->queue.empty()) {
        const auto asset = state_->queue.front();
        state_->queue.pop_front();
        auto& record = state_->records.at(asset);
        if (record.state != AssetStreamState::Queued)
            continue;

        std::optional<Error> dependencyError;
        bool dependencyQueued = false;
        for (const auto dependency : record.dependencies) {
            const auto& dependencyRecord = state_->records.at(dependency);
            if (dependencyRecord.state == AssetStreamState::Failed) {
                dependencyError = dependencyRecord.lastError;
                break;
            }
            if (dependencyRecord.state != AssetStreamState::Resident) {
                dependencyQueued = true;
                break;
            }
        }
        if (dependencyError) {
            record.state = AssetStreamState::Failed;
            record.lastError = Error(ErrorCode::InvalidState, "asset dependency failed to load")
                                   .addContext("asset", asset.toString())
                                   .addContext("cause", dependencyError->message());
            ++state_->statistics.loadFailures;
            state_->appendDiagnostic(asset, *record.lastError);
            state_->failPendingDependents(asset, *record.lastError);
            if (state_->transitionContains(asset)) {
                state_->transitionStatus = AssetTransitionState::Failed;
                state_->transitionError = record.lastError;
            }
            return Result<std::size_t>::failure(*record.lastError);
        }
        if (dependencyQueued) {
            state_->queue.push_back(asset);
            continue;
        }

        record.state = AssetStreamState::Loading;
        const AssetResourceLookup lookup = [state = state_](const AssetGuid dependency,
                                                            const std::type_index type) {
            return state->getErased(dependency, type, false);
        };
        auto payload = state_->callbacks.load(asset, lookup);
        ++loaded;
        if (!payload || !payload.value().resource || payload.value().residentBytes == 0U) {
            Error error = payload
                              ? Error(ErrorCode::InvalidState,
                                      "asset loader returned an empty or zero-sized resource")
                              : payload.error();
            error = assetContext(std::move(error), asset);
            record.state = AssetStreamState::Failed;
            record.lastError = error;
            ++state_->statistics.loadFailures;
            state_->appendDiagnostic(asset, error);
            state_->failPendingDependents(asset, error);
            if (state_->transitionContains(asset)) {
                state_->transitionStatus = AssetTransitionState::Failed;
                state_->transitionError = error;
            }
            return Result<std::size_t>::failure(error);
        }
        if (!state_->makeRoom(payload.value().residentBytes, asset)) {
            auto error = Error(ErrorCode::CapacityExceeded,
                               "referenced assets leave no room in the residency budget")
                             .addContext("asset", asset.toString())
                             .addContext("assetBytes",
                                         std::to_string(payload.value().residentBytes))
                             .addContext("residentBytes",
                                         std::to_string(state_->residentBytes))
                             .addContext("budgetBytes",
                                         std::to_string(state_->limits.maximumResidentBytes));
            record.state = AssetStreamState::Failed;
            record.lastError = error;
            ++state_->statistics.loadFailures;
            state_->appendDiagnostic(asset, error);
            state_->failPendingDependents(asset, error);
            if (state_->transitionContains(asset)) {
                state_->transitionStatus = AssetTransitionState::Failed;
                state_->transitionError = error;
            }
            return Result<std::size_t>::failure(error);
        }
        record.resource = std::move(payload.value().resource);
        record.type = payload.value().type;
        record.residentBytes = payload.value().residentBytes;
        record.lastAccess = ++state_->accessSequence;
        record.state = AssetStreamState::Resident;
        record.lastError.reset();
        state_->residentBytes += record.residentBytes;
        ++state_->residentAssets;
        ++state_->statistics.loads;
        state_->finishPendingRequest(asset);
        state_->updateTransitionStatus();
    }
    state_->updateTransitionStatus();
    return Result<std::size_t>::success(loaded);
}

std::shared_ptr<void> AssetStreamingManager::getErased(const AssetGuid asset,
                                                        const std::type_index requestedType) {
    return state_->getErased(asset, requestedType, true);
}

Result<void> AssetStreamingManager::beginTransition(std::vector<AssetGuid> targetRoots) {
    if (state_->transitionStatus != AssetTransitionState::Idle) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "an asset transition is already active"));
    }
    if (targetRoots.size() > state_->limits.maximumTransitionRoots ||
        std::any_of(targetRoots.begin(), targetRoots.end(),
                    [](const AssetGuid asset) { return asset.isNil(); })) {
        return Result<void>::failure(
            Error(ErrorCode::CapacityExceeded,
                  "asset transition roots exceed bounded limits or contain nil"));
    }
    std::sort(targetRoots.begin(), targetRoots.end());
    targetRoots.erase(std::unique(targetRoots.begin(), targetRoots.end()), targetRoots.end());

    std::vector<AssetGuid> additions;
    std::set_difference(targetRoots.begin(), targetRoots.end(), state_->activeTransition.begin(),
                        state_->activeTransition.end(), std::back_inserter(additions));
    for (const auto root : additions) {
        auto requested = request(root);
        if (!requested)
            return requested;
    }
    for (const auto root : additions) {
        auto referenced = state_->incrementReferences(root, false);
        if (!referenced) {
            for (const auto rollback : additions) {
                if (rollback == root)
                    break;
                state_->decrementReferences(rollback, false);
            }
            return referenced;
        }
    }
    state_->pendingTransition = std::move(targetRoots);
    state_->pendingTransitionAdditions = std::move(additions);
    state_->transitionStatus = AssetTransitionState::Loading;
    state_->transitionError.reset();
    state_->updateTransitionStatus();
    return Result<void>::success();
}

Result<void> AssetStreamingManager::commitTransition() {
    if (state_->transitionStatus == AssetTransitionState::Failed) {
        return Result<void>::failure(*state_->transitionError);
    }
    if (state_->transitionStatus != AssetTransitionState::Ready) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "asset transition is not ready to commit"));
    }
    std::vector<AssetGuid> removals;
    std::set_difference(state_->activeTransition.begin(), state_->activeTransition.end(),
                        state_->pendingTransition.begin(), state_->pendingTransition.end(),
                        std::back_inserter(removals));
    for (const auto root : removals)
        state_->decrementReferences(root, false);
    state_->activeTransition = std::move(state_->pendingTransition);
    state_->pendingTransition.clear();
    state_->pendingTransitionAdditions.clear();
    state_->transitionStatus = AssetTransitionState::Idle;
    state_->transitionError.reset();
    ++state_->statistics.transitionsCommitted;
    return Result<void>::success();
}

void AssetStreamingManager::cancelTransition() noexcept {
    if (state_->transitionStatus == AssetTransitionState::Idle)
        return;
    for (const auto root : state_->pendingTransitionAdditions)
        state_->decrementReferences(root, false);
    state_->pendingTransition.clear();
    state_->pendingTransitionAdditions.clear();
    state_->transitionStatus = AssetTransitionState::Idle;
    state_->transitionError.reset();
    ++state_->statistics.transitionsCancelled;
}

AssetTransitionState AssetStreamingManager::transitionState() const noexcept {
    return state_->transitionStatus;
}

std::vector<AssetGuid> AssetStreamingManager::activeTransitionRoots() const {
    return state_->activeTransition;
}

std::vector<AssetGuid> AssetStreamingManager::pendingTransitionRoots() const {
    return state_->pendingTransition;
}

std::size_t AssetStreamingManager::evictUnused() {
    std::size_t evicted = 0U;
    for (auto& [asset, record] : state_->records) {
        static_cast<void>(asset);
        if (record.state != AssetStreamState::Resident || state_->referenceCount(record) != 0U)
            continue;
        state_->residentBytes -= record.residentBytes;
        --state_->residentAssets;
        record.resource.reset();
        record.type = std::type_index(typeid(void));
        record.residentBytes = 0U;
        record.state = AssetStreamState::Unloaded;
        ++state_->statistics.evictions;
        ++evicted;
    }
    return evicted;
}

std::optional<AssetStreamStatus> AssetStreamingManager::status(const AssetGuid asset) const {
    const auto found = state_->records.find(asset);
    if (found == state_->records.end())
        return std::nullopt;
    return AssetStreamStatus{found->second.state, state_->referenceCount(found->second),
                             found->second.residentBytes, found->second.dependencies,
                             found->second.lastError};
}

AssetStreamingStats AssetStreamingManager::stats() const noexcept {
    auto result = state_->statistics;
    result.trackedAssets = state_->records.size();
    result.queuedAssets = static_cast<std::size_t>(std::count_if(
        state_->records.begin(), state_->records.end(), [](const auto& item) {
            return item.second.state == AssetStreamState::Queued ||
                   item.second.state == AssetStreamState::Loading;
        }));
    result.residentAssets = state_->residentAssets;
    result.referencedAssets = static_cast<std::size_t>(std::count_if(
        state_->records.begin(), state_->records.end(), [this](const auto& item) {
            return state_->referenceCount(item.second) != 0U;
        }));
    result.residentBytes = state_->residentBytes;
    return result;
}

std::vector<AssetStreamingDiagnostic> AssetStreamingManager::diagnostics() const {
    return state_->diagnosticLog;
}

} // namespace fabgl
