#include <fabgl/assets/asset_importer.h>
#include <fabgl/assets/file_io.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

namespace fabgl::assets {
namespace {

constexpr std::uint64_t FnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t FnvPrime = 1099511628211ULL;

[[nodiscard]] std::string normalizedToken(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

[[nodiscard]] std::string extensionOf(std::string_view path) {
    const auto separator = path.find_last_of("/\\");
    const auto dot = path.find_last_of('.');
    if (dot == std::string_view::npos || dot + 1U == path.size() ||
        (separator != std::string_view::npos && dot < separator)) {
        return {};
    }
    return normalizedToken(path.substr(dot + 1U));
}

[[nodiscard]] std::string normalizedPath(std::string_view value) {
    auto result = normalizedToken(value);
    std::replace(result.begin(), result.end(), '\\', '/');
    while (result.find("//") != std::string::npos) {
        result.replace(result.find("//"), 2U, "/");
    }
    return result;
}

void hashBytes(std::uint64_t& hash, const std::uint8_t* bytes, std::size_t size) noexcept {
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= FnvPrime;
    }
}

void hashString(std::uint64_t& hash, std::string_view value) noexcept {
    hashBytes(hash, reinterpret_cast<const std::uint8_t*>(value.data()), value.size());
    const std::uint8_t terminator = 0;
    hashBytes(hash, &terminator, 1U);
}

void hashU32(std::uint64_t& hash, std::uint32_t value) noexcept {
    for (unsigned int shift = 0; shift < 32U; shift += 8U) {
        const auto byte = static_cast<std::uint8_t>((value >> shift) & 0xFFU);
        hashBytes(hash, &byte, 1U);
    }
}

void hashU64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned int shift = 0; shift < 64U; shift += 8U) {
        const auto byte = static_cast<std::uint8_t>((value >> shift) & 0xFFU);
        hashBytes(hash, &byte, 1U);
    }
}

} // namespace

Result<void> AssetImporterRegistry::add(std::unique_ptr<IAssetImporter> importer) {
    if (importer == nullptr || importer->id().empty() || importer->version() == 0U) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "asset importer must have an ID and version"));
    }
    const auto normalizedId = normalizedToken(importer->id());
    if (byId_.find(normalizedId) != byId_.end()) {
        return Result<void>::failure(Error(ErrorCode::AlreadyExists, "asset importer ID exists")
                                         .addContext("importer", normalizedId));
    }

    auto extensions = importer->extensions();
    if (extensions.empty()) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "asset importer must declare an extension"));
    }
    for (auto& extension : extensions) {
        while (!extension.empty() && extension.front() == '.') {
            extension.erase(extension.begin());
        }
        extension = normalizedToken(extension);
        if (extension.empty()) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "asset importer extension is empty"));
        }
        const auto existing = byExtension_.find(extension);
        if (existing != byExtension_.end()) {
            return Result<void>::failure(
                Error(ErrorCode::AlreadyExists, "asset importer extension exists")
                    .addContext("extension", extension)
                    .addContext("importer", std::string(existing->second->id())));
        }
    }
    std::sort(extensions.begin(), extensions.end());
    if (std::adjacent_find(extensions.begin(), extensions.end()) != extensions.end()) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "asset importer repeats an extension"));
    }

    const auto* pointer = importer.get();
    importers_.push_back(std::move(importer));
    byId_.emplace(normalizedId, pointer);
    for (const auto& extension : extensions) {
        byExtension_.emplace(extension, pointer);
    }
    return Result<void>::success();
}

const IAssetImporter* AssetImporterRegistry::findById(std::string_view id) const noexcept {
    const auto found = byId_.find(normalizedToken(id));
    return found == byId_.end() ? nullptr : found->second;
}

const IAssetImporter* AssetImporterRegistry::findForPath(std::string_view path) const noexcept {
    const auto found = byExtension_.find(extensionOf(path));
    return found == byExtension_.end() ? nullptr : found->second;
}

Result<ImportedAsset> AssetImporterRegistry::import(const AssetImportRequest& request) const {
    if (request.guid.isNil() || request.sourceBytes.empty() ||
        !isSafeRelativePath(request.relativePath)) {
        return Result<ImportedAsset>::failure(
            Error(ErrorCode::InvalidArgument, "asset import request is invalid")
                .addContext("path", request.relativePath));
    }
    const auto* importer = findForPath(request.relativePath);
    if (importer == nullptr) {
        return Result<ImportedAsset>::failure(
            Error(ErrorCode::NotFound, "no importer accepts the asset extension")
                .addContext("path", request.relativePath));
    }
    auto imported = importer->import(request);
    if (!imported) {
        return imported;
    }
    if (imported.value().payload.empty()) {
        return Result<ImportedAsset>::failure(
            Error(ErrorCode::InvalidFormat, "asset importer produced an empty payload")
                .addContext("importer", std::string(importer->id())));
    }
    imported.value().guid = request.guid;
    imported.value().kind = importer->kind();
    imported.value().cacheKey = assetImportCacheKey(request, importer->id(), importer->version());
    return imported;
}

std::uint64_t assetImportCacheKey(const AssetImportRequest& request, std::string_view importerId,
                                  std::uint32_t importerVersion) noexcept {
    auto hash = FnvOffset;
    const auto guid = request.guid.toString();
    hashString(hash, guid);
    hashString(hash, normalizedToken(importerId));
    hashU32(hash, importerVersion);
    hashString(hash, normalizedPath(request.relativePath));
    hashString(hash, request.normalizedSettings);
    const auto target = static_cast<std::uint8_t>(request.target);
    hashBytes(hash, &target, 1U);
    hashU32(hash, request.pipelineVersion);
    for (const auto dependency : request.dependencyCacheKeys) {
        hashU64(hash, dependency);
    }
    hashBytes(hash, request.sourceBytes.data(), request.sourceBytes.size());
    return hash;
}

} // namespace fabgl::assets
