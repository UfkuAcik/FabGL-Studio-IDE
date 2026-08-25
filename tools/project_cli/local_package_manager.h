#pragma once

#include <fabgl/core/result.h>
#include <fabgl/packages/package_manifest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fabgl::project {

struct LocalPackageLimits final {
    std::size_t maximumFiles = 1024U;
    std::uint64_t maximumFileBytes = 16ULL * 1024ULL * 1024ULL;
    std::uint64_t maximumTotalBytes = 64ULL * 1024ULL * 1024ULL;
    std::size_t maximumDepth = 32U;
};

struct LocalPackageInfo final {
    PackageManifest manifest;
    std::string directory;
    std::string contentSha256;
    std::size_t fileCount = 0U;
    std::uint64_t totalBytes = 0U;
    bool executableTrusted = false;
};

struct LocalPackageInstallOptions final {
    bool allowExecutable = false;
    LocalPackageLimits limits;
};

[[nodiscard]] SemVersion currentPackageEngineVersion();
[[nodiscard]] Result<LocalPackageInfo>
installLocalPackage(const std::string& projectManifestPath, const std::string& sourceDirectory,
                    const LocalPackageInstallOptions& options = {});
[[nodiscard]] Result<std::vector<LocalPackageInfo>>
listLocalPackages(const std::string& projectManifestPath, const LocalPackageLimits& limits = {});
[[nodiscard]] Result<std::vector<std::string>>
validateLocalPackages(const std::string& projectManifestPath,
                      const LocalPackageLimits& limits = {});
[[nodiscard]] Result<void> removeLocalPackage(const std::string& projectManifestPath,
                                              const std::string& packageId,
                                              const LocalPackageLimits& limits = {});

} // namespace fabgl::project
