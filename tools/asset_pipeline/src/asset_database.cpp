#include <fabgl/assets/asset_database.h>
#include <fabgl/assets/file_io.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <unordered_map>

namespace fabgl::assets {

namespace {

std::string normalizePath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    while (path.rfind("./", 0) == 0) {
        path.erase(0, 2);
    }
    while (path.find("//") != std::string::npos) {
        path.replace(path.find("//"), 2, "/");
    }
    std::transform(path.begin(), path.end(), path.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return path;
}

} // namespace

Result<void> AssetDatabase::add(AssetRecord record) {
    if (record.guid.isNil()) {
        return Result<void>::failure(Error(ErrorCode::InvalidArgument, "asset GUID cannot be nil"));
    }
    if (!isSafeRelativePath(record.relativePath)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "asset path must be safe and relative")
                .addContext("path", record.relativePath));
    }
    if (record.importer.empty()) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "asset importer cannot be empty"));
    }
    const auto normalized = normalizePath(record.relativePath);
    if (records_.find(record.guid) != records_.end()) {
        return Result<void>::failure(Error(ErrorCode::AlreadyExists, "asset GUID already exists")
                                         .addContext("guid", record.guid.toString()));
    }
    if (paths_.find(normalized) != paths_.end()) {
        return Result<void>::failure(Error(ErrorCode::AlreadyExists, "asset path already exists")
                                         .addContext("path", record.relativePath));
    }
    record.relativePath = std::move(normalized);
    paths_.emplace(record.relativePath, record.guid);
    records_.emplace(record.guid, std::move(record));
    return Result<void>::success();
}

Result<void> AssetDatabase::move(const AssetGuid& guid, std::string newRelativePath) {
    auto record = records_.find(guid);
    if (record == records_.end()) {
        return Result<void>::failure(Error(ErrorCode::NotFound, "asset GUID was not found")
                                         .addContext("guid", guid.toString()));
    }
    if (!isSafeRelativePath(newRelativePath)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "asset path must be safe and relative")
                .addContext("path", newRelativePath));
    }
    newRelativePath = normalizePath(std::move(newRelativePath));
    const auto collision = paths_.find(newRelativePath);
    if (collision != paths_.end() && collision->second != guid) {
        return Result<void>::failure(Error(ErrorCode::AlreadyExists, "asset path already exists")
                                         .addContext("path", newRelativePath));
    }
    paths_.erase(record->second.relativePath);
    record->second.relativePath = std::move(newRelativePath);
    paths_.emplace(record->second.relativePath, guid);
    return Result<void>::success();
}

Result<void> AssetDatabase::setImportConfiguration(const AssetGuid& guid,
                                                   const std::uint32_t importerVersion,
                                                   const std::uint64_t settingsFingerprint) {
    auto record = records_.find(guid);
    if (record == records_.end()) {
        return Result<void>::failure(Error(ErrorCode::NotFound, "asset GUID was not found")
                                         .addContext("guid", guid.toString()));
    }
    if (importerVersion == 0U) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "asset importer version cannot be zero"));
    }
    record->second.importerVersion = importerVersion;
    record->second.settingsFingerprint = settingsFingerprint;
    return Result<void>::success();
}

Result<void> AssetDatabase::markImported(const AssetGuid& guid,
                                         const std::uint64_t expectedSourceFingerprint,
                                         const std::uint64_t cacheKey) {
    auto record = records_.find(guid);
    if (record == records_.end()) {
        return Result<void>::failure(Error(ErrorCode::NotFound, "asset GUID was not found")
                                         .addContext("guid", guid.toString()));
    }
    if (record->second.missing || record->second.sourceFingerprint != expectedSourceFingerprint) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "asset changed while it was being imported")
                .addContext("guid", guid.toString()));
    }
    if (cacheKey == 0U) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "asset import cache key cannot be zero"));
    }
    record->second.importedSourceFingerprint = record->second.sourceFingerprint;
    record->second.importedSettingsFingerprint = record->second.settingsFingerprint;
    record->second.importedVersion = record->second.importerVersion;
    record->second.lastImportCacheKey = cacheKey;
    return Result<void>::success();
}

Result<AssetSyncResult>
AssetDatabase::synchronizeSources(const std::vector<AssetSourceState>& sources) {
    std::unordered_map<std::string, std::uint64_t> observed;
    observed.reserve(sources.size());
    for (const auto& source : sources) {
        if (!isSafeRelativePath(source.relativePath)) {
            return Result<AssetSyncResult>::failure(
                Error(ErrorCode::InvalidArgument, "source snapshot contains an unsafe path")
                    .addContext("path", source.relativePath));
        }
        const auto path = normalizePath(source.relativePath);
        if (!observed.emplace(path, source.fingerprint).second) {
            return Result<AssetSyncResult>::failure(
                Error(ErrorCode::AlreadyExists, "source snapshot repeats a normalized path")
                    .addContext("path", path));
        }
    }

    AssetSyncResult result;
    for (auto& pair : records_) {
        auto& record = pair.second;
        const auto source = observed.find(record.relativePath);
        if (source == observed.end()) {
            record.missing = true;
            result.missing.push_back(pair.first);
            continue;
        }
        record.missing = false;
        record.sourceFingerprint = source->second;
        (needsImport(pair.first) ? result.dirty : result.unchanged).push_back(pair.first);
        observed.erase(source);
    }
    for (const auto& pair : observed) {
        result.untrackedPaths.push_back(pair.first);
    }
    const auto sortGuids = [](std::vector<AssetGuid>& values) {
        std::sort(values.begin(), values.end());
    };
    sortGuids(result.dirty);
    sortGuids(result.unchanged);
    sortGuids(result.missing);
    std::sort(result.untrackedPaths.begin(), result.untrackedPaths.end());
    return Result<AssetSyncResult>::success(std::move(result));
}

const AssetRecord* AssetDatabase::find(const AssetGuid& guid) const noexcept {
    const auto record = records_.find(guid);
    return record == records_.end() ? nullptr : &record->second;
}

const AssetRecord* AssetDatabase::findByPath(const std::string& relativePath) const noexcept {
    const auto path = paths_.find(normalizePath(relativePath));
    return path == paths_.end() ? nullptr : find(path->second);
}

bool AssetDatabase::needsImport(const AssetGuid& guid) const noexcept {
    const auto* record = find(guid);
    return record != nullptr && !record->missing &&
           (record->lastImportCacheKey == 0U ||
            record->sourceFingerprint != record->importedSourceFingerprint ||
            record->settingsFingerprint != record->importedSettingsFingerprint ||
            record->importerVersion != record->importedVersion);
}

Result<std::vector<AssetGuid>> AssetDatabase::buildOrder() const {
    enum class Visit : std::uint8_t { Unvisited, Visiting, Visited };
    std::unordered_map<AssetGuid, Visit, StrongGuidHash<AssetGuidTag>> visits;
    std::vector<AssetGuid> sortedIds;
    sortedIds.reserve(records_.size());
    for (const auto& pair : records_) {
        sortedIds.push_back(pair.first);
        visits.emplace(pair.first, Visit::Unvisited);
    }
    std::sort(sortedIds.begin(), sortedIds.end());

    std::vector<AssetGuid> result;
    result.reserve(records_.size());
    std::function<Result<void>(const AssetGuid&)> visit =
        [&](const AssetGuid& guid) -> Result<void> {
        auto state = visits.find(guid);
        if (state == visits.end()) {
            return Result<void>::failure(Error(ErrorCode::NotFound, "asset dependency is missing")
                                             .addContext("guid", guid.toString()));
        }
        if (state->second == Visit::Visited) {
            return Result<void>::success();
        }
        if (state->second == Visit::Visiting) {
            return Result<void>::failure(
                Error(ErrorCode::CycleDetected, "asset dependency cycle detected")
                    .addContext("guid", guid.toString()));
        }
        state->second = Visit::Visiting;
        const auto& record = records_.at(guid);
        auto dependencies = record.dependencies;
        std::sort(dependencies.begin(), dependencies.end());
        for (const auto& dependency : dependencies) {
            auto visited = visit(dependency);
            if (!visited) {
                return visited;
            }
        }
        state->second = Visit::Visited;
        result.push_back(guid);
        return Result<void>::success();
    };

    for (const auto& guid : sortedIds) {
        auto visited = visit(guid);
        if (!visited) {
            return Result<std::vector<AssetGuid>>::failure(visited.error());
        }
    }
    return Result<std::vector<AssetGuid>>::success(std::move(result));
}

} // namespace fabgl::assets
