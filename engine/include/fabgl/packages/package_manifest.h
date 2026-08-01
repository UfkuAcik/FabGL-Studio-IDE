#pragma once

#include "fabgl/core/result.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace fabgl {

struct SemVersion final {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;
    std::string prerelease;

    [[nodiscard]] static Result<SemVersion> parse(std::string_view text);
    [[nodiscard]] std::string toString() const;

    friend bool operator==(const SemVersion& lhs, const SemVersion& rhs) noexcept;
    friend bool operator!=(const SemVersion& lhs, const SemVersion& rhs) noexcept {
        return !(lhs == rhs);
    }
    friend bool operator<(const SemVersion& lhs, const SemVersion& rhs);
    friend bool operator>(const SemVersion& lhs, const SemVersion& rhs) {
        return rhs < lhs;
    }
    friend bool operator<=(const SemVersion& lhs, const SemVersion& rhs) {
        return !(rhs < lhs);
    }
    friend bool operator>=(const SemVersion& lhs, const SemVersion& rhs) {
        return !(lhs < rhs);
    }
};

enum class VersionRequirementKind {
    Any,
    Exact,
    AtLeast,
    Compatible,
};

struct VersionRequirement final {
    VersionRequirementKind kind = VersionRequirementKind::Any;
    SemVersion version;

    [[nodiscard]] static Result<VersionRequirement> parse(std::string_view text);
    [[nodiscard]] bool matches(const SemVersion& candidate) const;
};

enum class PackageTrust {
    Untrusted,
    Trusted,
    BuiltIn,
};

struct PackageDependency final {
    std::string name;
    VersionRequirement requirement;
};

struct PackageManifest final {
    std::string name;
    SemVersion version;
    std::string localPath;
    PackageTrust trust = PackageTrust::Untrusted;
    bool containsExecutableCode = false;
    std::vector<PackageDependency> dependencies;
};

class PackageManifestParser final {
  public:
    [[nodiscard]] static Result<PackageManifest> parse(std::string_view text);
    [[nodiscard]] static Result<void> validateLocalManifest(const PackageManifest& manifest);
};

class PackageRegistry final {
  public:
    [[nodiscard]] Result<void> add(PackageManifest manifest);
    [[nodiscard]] Result<std::vector<std::string>>
    validate(bool allowUntrustedExecutableCode) const;

  private:
    std::map<std::string, PackageManifest> packages_;
};

} // namespace fabgl
