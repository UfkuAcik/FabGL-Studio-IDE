#include "fabgl/packages/package_manifest.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <set>
#include <sstream>
#include <utility>

namespace fabgl {
namespace {

Result<std::uint32_t> parseVersionNumber(std::string_view text) {
    if (text.empty())
        return Result<std::uint32_t>::failure(
            Error(ErrorCode::InvalidFormat, "empty version number"));
    if (text.size() > 1U && text.front() == '0') {
        return Result<std::uint32_t>::failure(
            Error(ErrorCode::InvalidFormat, "version number has a leading zero"));
    }
    std::uint64_t value = 0;
    for (const auto character : text) {
        if (!std::isdigit(static_cast<unsigned char>(character))) {
            return Result<std::uint32_t>::failure(
                Error(ErrorCode::InvalidFormat, "version contains a non-digit"));
        }
        value = value * 10U + static_cast<unsigned int>(character - '0');
        if (value > std::numeric_limits<std::uint32_t>::max()) {
            return Result<std::uint32_t>::failure(
                Error(ErrorCode::InvalidFormat, "version number is too large"));
        }
    }
    return Result<std::uint32_t>::success(static_cast<std::uint32_t>(value));
}

std::vector<std::string> split(std::string_view text, char delimiter) {
    std::vector<std::string> result;
    std::size_t start = 0;
    for (;;) {
        const auto end = text.find(delimiter, start);
        result.emplace_back(
            text.substr(start, end == std::string_view::npos ? text.size() - start : end - start));
        if (end == std::string_view::npos)
            break;
        start = end + 1U;
    }
    return result;
}

bool numericIdentifier(std::string_view identifier) noexcept {
    if (identifier.empty())
        return false;
    for (const auto character : identifier) {
        if (!std::isdigit(static_cast<unsigned char>(character)))
            return false;
    }
    return true;
}

int comparePrerelease(std::string_view lhs, std::string_view rhs) {
    if (lhs.empty() && rhs.empty())
        return 0;
    if (lhs.empty())
        return 1;
    if (rhs.empty())
        return -1;
    const auto left = split(lhs, '.');
    const auto right = split(rhs, '.');
    const auto count = std::min(left.size(), right.size());
    for (std::size_t index = 0; index < count; ++index) {
        if (left[index] == right[index])
            continue;
        const bool leftNumeric = numericIdentifier(left[index]);
        const bool rightNumeric = numericIdentifier(right[index]);
        if (leftNumeric && rightNumeric) {
            const auto leftValue = std::strtoull(left[index].c_str(), nullptr, 10);
            const auto rightValue = std::strtoull(right[index].c_str(), nullptr, 10);
            return leftValue < rightValue ? -1 : 1;
        }
        if (leftNumeric != rightNumeric)
            return leftNumeric ? -1 : 1;
        return left[index] < right[index] ? -1 : 1;
    }
    if (left.size() == right.size())
        return 0;
    return left.size() < right.size() ? -1 : 1;
}

std::string trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos)
        return {};
    const auto last = text.find_last_not_of(" \t\r");
    return std::string(text.substr(first, last - first + 1U));
}

bool validPackageName(std::string_view name) noexcept {
    if (name.empty() || name.size() > 80U ||
        !std::isalnum(static_cast<unsigned char>(name.front())))
        return false;
    for (const auto character : name) {
        const auto byte = static_cast<unsigned char>(character);
        if (!std::islower(byte) && !std::isdigit(byte) && character != '-' && character != '.' &&
            character != '_') {
            return false;
        }
    }
    return true;
}

bool safeRelativePath(std::string_view path) {
    if (path.empty() || path.front() == '/' || path.front() == '\\' ||
        path.find(':') != std::string_view::npos) {
        return false;
    }
    std::string normalized(path);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    for (const auto& segment : split(normalized, '/')) {
        if (segment.empty() || segment == "..")
            return false;
    }
    return true;
}

Result<void> visitPackage(const std::string& name,
                          const std::map<std::string, PackageManifest>& packages,
                          std::map<std::string, int>& marks, std::vector<std::string>& order) {
    const auto mark = marks.find(name);
    if (mark != marks.end() && mark->second == 1) {
        return Result<void>::failure(
            Error(ErrorCode::CycleDetected, "package dependency cycle detected")
                .addContext("package", name));
    }
    if (mark != marks.end() && mark->second == 2)
        return Result<void>::success();
    marks[name] = 1;
    const auto package = packages.find(name);
    for (const auto& dependency : package->second.dependencies) {
        auto visited = visitPackage(dependency.name, packages, marks, order);
        if (!visited)
            return visited;
    }
    marks[name] = 2;
    order.push_back(name);
    return Result<void>::success();
}

} // namespace

Result<SemVersion> SemVersion::parse(std::string_view text) {
    const auto plus = text.find('+');
    if (plus != std::string_view::npos)
        text = text.substr(0, plus);
    const auto dash = text.find('-');
    const auto core = text.substr(0, dash);
    const auto parts = split(core, '.');
    if (parts.size() != 3U) {
        return Result<SemVersion>::failure(
            Error(ErrorCode::InvalidFormat, "semantic version must have three numbers"));
    }
    auto major = parseVersionNumber(parts[0]);
    auto minor = parseVersionNumber(parts[1]);
    auto patch = parseVersionNumber(parts[2]);
    if (!major)
        return Result<SemVersion>::failure(major.error());
    if (!minor)
        return Result<SemVersion>::failure(minor.error());
    if (!patch)
        return Result<SemVersion>::failure(patch.error());
    std::string prerelease;
    if (dash != std::string_view::npos) {
        prerelease = std::string(text.substr(dash + 1U));
        if (prerelease.empty()) {
            return Result<SemVersion>::failure(
                Error(ErrorCode::InvalidFormat, "empty prerelease identifier"));
        }
        for (const auto& identifier : split(prerelease, '.')) {
            if (identifier.empty()) {
                return Result<SemVersion>::failure(
                    Error(ErrorCode::InvalidFormat, "empty prerelease segment"));
            }
            for (const auto character : identifier) {
                if (!std::isalnum(static_cast<unsigned char>(character)) && character != '-') {
                    return Result<SemVersion>::failure(
                        Error(ErrorCode::InvalidFormat, "invalid prerelease character"));
                }
            }
            if (numericIdentifier(identifier) && identifier.size() > 1U &&
                identifier.front() == '0') {
                return Result<SemVersion>::failure(Error(
                    ErrorCode::InvalidFormat, "numeric prerelease identifier has a leading zero"));
            }
        }
    }
    return Result<SemVersion>::success(
        {major.value(), minor.value(), patch.value(), std::move(prerelease)});
}

std::string SemVersion::toString() const {
    auto text = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    if (!prerelease.empty())
        text += "-" + prerelease;
    return text;
}

bool operator==(const SemVersion& lhs, const SemVersion& rhs) noexcept {
    return lhs.major == rhs.major && lhs.minor == rhs.minor && lhs.patch == rhs.patch &&
           lhs.prerelease == rhs.prerelease;
}

bool operator<(const SemVersion& lhs, const SemVersion& rhs) {
    if (lhs.major != rhs.major)
        return lhs.major < rhs.major;
    if (lhs.minor != rhs.minor)
        return lhs.minor < rhs.minor;
    if (lhs.patch != rhs.patch)
        return lhs.patch < rhs.patch;
    return comparePrerelease(lhs.prerelease, rhs.prerelease) < 0;
}

Result<VersionRequirement> VersionRequirement::parse(std::string_view text) {
    if (text == "*")
        return Result<VersionRequirement>::success({VersionRequirementKind::Any, {}});
    VersionRequirementKind kind = VersionRequirementKind::Exact;
    if (text.size() >= 2U && text.substr(0, 2) == ">=") {
        kind = VersionRequirementKind::AtLeast;
        text.remove_prefix(2);
    } else if (!text.empty() && text.front() == '^') {
        kind = VersionRequirementKind::Compatible;
        text.remove_prefix(1);
    }
    auto version = SemVersion::parse(text);
    if (!version)
        return Result<VersionRequirement>::failure(version.error());
    return Result<VersionRequirement>::success({kind, version.value()});
}

bool VersionRequirement::matches(const SemVersion& candidate) const {
    switch (kind) {
    case VersionRequirementKind::Any:
        return true;
    case VersionRequirementKind::Exact:
        return candidate == version;
    case VersionRequirementKind::AtLeast:
        return candidate >= version;
    case VersionRequirementKind::Compatible:
        if (candidate < version)
            return false;
        if (version.major != 0U)
            return candidate.major == version.major;
        if (version.minor != 0U)
            return candidate.major == 0U && candidate.minor == version.minor;
        return candidate.major == 0U && candidate.minor == 0U && candidate.patch == version.patch;
    }
    return false;
}

Result<PackageManifest> PackageManifestParser::parse(std::string_view text) {
    PackageManifest manifest;
    bool hasName = false;
    bool hasVersion = false;
    bool hasPath = false;
    bool hasTrust = false;
    bool hasExecutable = false;
    std::istringstream stream{std::string(text)};
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(stream, line)) {
        ++lineNumber;
        const auto cleaned = trim(line);
        if (cleaned.empty() || cleaned.front() == '#')
            continue;
        const auto separator = cleaned.find('=');
        if (separator == std::string::npos) {
            return Result<PackageManifest>::failure(
                Error(ErrorCode::InvalidFormat, "package manifest line is missing '='")
                    .addContext("line", std::to_string(lineNumber)));
        }
        const auto key = trim(std::string_view(cleaned).substr(0, separator));
        const auto value = trim(std::string_view(cleaned).substr(separator + 1U));
        if (key == "name" && !hasName) {
            manifest.name = value;
            hasName = true;
        } else if (key == "version" && !hasVersion) {
            auto version = SemVersion::parse(value);
            if (!version)
                return Result<PackageManifest>::failure(version.error());
            manifest.version = std::move(version.value());
            hasVersion = true;
        } else if (key == "path" && !hasPath) {
            manifest.localPath = value;
            hasPath = true;
        } else if (key == "trust" && !hasTrust) {
            if (value == "untrusted")
                manifest.trust = PackageTrust::Untrusted;
            else if (value == "trusted")
                manifest.trust = PackageTrust::Trusted;
            else if (value == "builtin")
                manifest.trust = PackageTrust::BuiltIn;
            else
                return Result<PackageManifest>::failure(
                    Error(ErrorCode::InvalidFormat, "invalid package trust"));
            hasTrust = true;
        } else if (key == "executable" && !hasExecutable) {
            if (value == "true")
                manifest.containsExecutableCode = true;
            else if (value == "false")
                manifest.containsExecutableCode = false;
            else
                return Result<PackageManifest>::failure(
                    Error(ErrorCode::InvalidFormat, "invalid executable flag"));
            hasExecutable = true;
        } else if (key == "dependency") {
            const auto at = value.find('@');
            if (at == std::string::npos) {
                return Result<PackageManifest>::failure(
                    Error(ErrorCode::InvalidFormat, "invalid package dependency"));
            }
            auto requirement = VersionRequirement::parse(std::string_view(value).substr(at + 1U));
            if (!requirement)
                return Result<PackageManifest>::failure(requirement.error());
            manifest.dependencies.push_back({value.substr(0, at), requirement.value()});
        } else {
            return Result<PackageManifest>::failure(
                Error(ErrorCode::InvalidFormat, "duplicate or unknown package manifest field")
                    .addContext("field", key));
        }
    }
    if (!hasName || !hasVersion || !hasPath) {
        return Result<PackageManifest>::failure(
            Error(ErrorCode::InvalidFormat, "package manifest is incomplete"));
    }
    auto valid = validateLocalManifest(manifest);
    if (!valid)
        return Result<PackageManifest>::failure(valid.error());
    return Result<PackageManifest>::success(std::move(manifest));
}

Result<void> PackageManifestParser::validateLocalManifest(const PackageManifest& manifest) {
    if (!validPackageName(manifest.name) || !safeRelativePath(manifest.localPath)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "package name or local path is invalid"));
    }
    std::set<std::string> dependencies;
    for (const auto& dependency : manifest.dependencies) {
        if (!validPackageName(dependency.name) || dependency.name == manifest.name ||
            !dependencies.insert(dependency.name).second) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "package dependency is invalid"));
        }
    }
    return Result<void>::success();
}

Result<void> PackageRegistry::add(PackageManifest manifest) {
    auto valid = PackageManifestParser::validateLocalManifest(manifest);
    if (!valid)
        return valid;
    if (packages_.find(manifest.name) != packages_.end()) {
        return Result<void>::failure(Error(ErrorCode::AlreadyExists, "package is already installed")
                                         .addContext("package", manifest.name));
    }
    packages_.emplace(manifest.name, std::move(manifest));
    return Result<void>::success();
}

Result<std::vector<std::string>>
PackageRegistry::validate(bool allowUntrustedExecutableCode) const {
    for (const auto& entry : packages_) {
        const auto& package = entry.second;
        if (package.containsExecutableCode && package.trust == PackageTrust::Untrusted &&
            !allowUntrustedExecutableCode) {
            return Result<std::vector<std::string>>::failure(
                Error(ErrorCode::InvalidState, "untrusted executable package is blocked")
                    .addContext("package", package.name));
        }
        for (const auto& dependency : package.dependencies) {
            const auto installed = packages_.find(dependency.name);
            if (installed == packages_.end()) {
                return Result<std::vector<std::string>>::failure(
                    Error(ErrorCode::NotFound, "package dependency is missing")
                        .addContext("package", package.name)
                        .addContext("dependency", dependency.name));
            }
            if (!dependency.requirement.matches(installed->second.version)) {
                return Result<std::vector<std::string>>::failure(
                    Error(ErrorCode::UnsupportedVersion,
                          "package dependency version does not match")
                        .addContext("package", package.name)
                        .addContext("dependency", dependency.name));
            }
        }
    }

    std::map<std::string, int> marks;
    std::vector<std::string> order;
    for (const auto& entry : packages_) {
        auto visited = visitPackage(entry.first, packages_, marks, order);
        if (!visited)
            return Result<std::vector<std::string>>::failure(visited.error());
    }
    return Result<std::vector<std::string>>::success(std::move(order));
}

} // namespace fabgl
