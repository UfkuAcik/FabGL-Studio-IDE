#include "fabgl/packages/package_manifest.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
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
            if (left[index].size() != right[index].size())
                return left[index].size() < right[index].size() ? -1 : 1;
            return left[index] < right[index] ? -1 : 1;
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

bool validPackageId(std::string_view id) noexcept {
    if (id.empty() || id.size() > 80U || !std::isalnum(static_cast<unsigned char>(id.front())))
        return false;
    for (const auto character : id) {
        const auto byte = static_cast<unsigned char>(character);
        if (!std::islower(byte) && !std::isdigit(byte) && character != '-' && character != '.' &&
            character != '_') {
            return false;
        }
    }
    return true;
}

bool safeRelativePath(std::string_view path) {
    if (path.empty() || path.size() > 240U || path.front() == '/' || path.front() == '\\' ||
        path.find(':') != std::string_view::npos || path.find('\0') != std::string_view::npos) {
        return false;
    }
    for (const auto character : path) {
        if (static_cast<unsigned char>(character) < 0x20U)
            return false;
    }
    std::string normalized(path);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    for (const auto& segment : split(normalized, '/')) {
        if (segment.empty() || segment == "." || segment == "..")
            return false;
    }
    return true;
}

bool validDisplayText(std::string_view value, std::size_t maximumLength) noexcept {
    if (value.empty() || value.size() > maximumLength)
        return false;
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20U || byte == 0x7FU)
            return false;
    }
    return true;
}

bool validSpdxLicense(std::string_view value) noexcept {
    if (value == "NOASSERTION")
        return true;
    if (value.empty() || value.size() > 80U)
        return false;
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte) == 0 && character != '-' && character != '.' && character != '+')
            return false;
    }
    return true;
}

Result<void> visitPackage(const std::string& name,
                          const std::map<std::string, PackageManifest>& packages,
                          std::map<std::string, int>& marks, std::vector<std::string>& order);

template <typename IsTrusted>
Result<std::vector<std::string>>
validatePackages(const std::map<std::string, PackageManifest>& packages,
                 const SemVersion* engineVersion, IsTrusted&& isTrusted) {
    for (const auto& entry : packages) {
        const auto& package = entry.second;
        if (engineVersion != nullptr && !package.engineCompatibility.matches(*engineVersion)) {
            return Result<std::vector<std::string>>::failure(
                Error(ErrorCode::UnsupportedVersion,
                      "package is incompatible with the current engine version")
                    .addContext("package", entry.first)
                    .addContext("engine", engineVersion->toString())
                    .addContext("required", package.engineCompatibility.toString()));
        }
        if (package.containsExecutableCode && !isTrusted(package)) {
            return Result<std::vector<std::string>>::failure(
                Error(ErrorCode::InvalidState, "untrusted executable package is blocked")
                    .addContext("package", entry.first));
        }
        for (const auto& dependency : package.dependencies) {
            const auto installed = packages.find(dependency.name);
            if (installed == packages.end()) {
                return Result<std::vector<std::string>>::failure(
                    Error(ErrorCode::NotFound, "package dependency is missing")
                        .addContext("package", entry.first)
                        .addContext("dependency", dependency.name));
            }
            if (!dependency.requirement.matches(installed->second.version)) {
                return Result<std::vector<std::string>>::failure(
                    Error(ErrorCode::UnsupportedVersion,
                          "package dependency version does not match")
                        .addContext("package", entry.first)
                        .addContext("dependency", dependency.name));
            }
        }
    }

    std::map<std::string, int> marks;
    std::vector<std::string> order;
    for (const auto& entry : packages) {
        auto visited = visitPackage(entry.first, packages, marks, order);
        if (!visited)
            return Result<std::vector<std::string>>::failure(visited.error());
    }
    return Result<std::vector<std::string>>::success(std::move(order));
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
    if (text.empty() || text.size() > 128U) {
        return Result<SemVersion>::failure(
            Error(ErrorCode::InvalidFormat, "semantic version length is invalid"));
    }
    const auto plus = text.find('+');
    if (plus != std::string_view::npos) {
        const auto build = text.substr(plus + 1U);
        if (build.empty()) {
            return Result<SemVersion>::failure(
                Error(ErrorCode::InvalidFormat, "empty build metadata"));
        }
        for (const auto& identifier : split(build, '.')) {
            if (identifier.empty()) {
                return Result<SemVersion>::failure(
                    Error(ErrorCode::InvalidFormat, "empty build metadata segment"));
            }
            for (const auto character : identifier) {
                if (std::isalnum(static_cast<unsigned char>(character)) == 0 && character != '-') {
                    return Result<SemVersion>::failure(
                        Error(ErrorCode::InvalidFormat, "invalid build metadata character"));
                }
            }
        }
        text = text.substr(0, plus);
    }
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

std::string VersionRequirement::toString() const {
    switch (kind) {
    case VersionRequirementKind::Any:
        return "*";
    case VersionRequirementKind::Exact:
        return version.toString();
    case VersionRequirementKind::AtLeast:
        return ">=" + version.toString();
    case VersionRequirementKind::Compatible:
        return "^" + version.toString();
    }
    return "*";
}

std::string_view packageEntryPointKindName(PackageEntryPointKind kind) noexcept {
    switch (kind) {
    case PackageEntryPointKind::EditorPlugin:
        return "editor-plugin";
    case PackageEntryPointKind::RuntimeModule:
        return "runtime-module";
    case PackageEntryPointKind::AssetImporter:
        return "asset-importer";
    case PackageEntryPointKind::CustomInspector:
        return "custom-inspector";
    case PackageEntryPointKind::CustomWindow:
        return "custom-window";
    case PackageEntryPointKind::BuildStep:
        return "build-step";
    case PackageEntryPointKind::RendererExtension:
        return "renderer-extension";
    case PackageEntryPointKind::Framework:
        return "framework";
    }
    return "runtime-module";
}

Result<PackageEntryPointKind> parsePackageEntryPointKind(std::string_view text) {
    for (const auto kind :
         {PackageEntryPointKind::EditorPlugin, PackageEntryPointKind::RuntimeModule,
          PackageEntryPointKind::AssetImporter, PackageEntryPointKind::CustomInspector,
          PackageEntryPointKind::CustomWindow, PackageEntryPointKind::BuildStep,
          PackageEntryPointKind::RendererExtension, PackageEntryPointKind::Framework}) {
        if (text == packageEntryPointKindName(kind))
            return Result<PackageEntryPointKind>::success(kind);
    }
    return Result<PackageEntryPointKind>::failure(
        Error(ErrorCode::InvalidFormat, "unknown package entry point type")
            .addContext("type", std::string(text)));
}

Result<PackageManifest> PackageManifestParser::parse(std::string_view text) {
    PackageManifest manifest;
    bool hasSchema = false;
    bool hasId = false;
    bool hasLegacyName = false;
    bool hasDisplayName = false;
    bool hasVersion = false;
    bool hasPath = false;
    bool hasEngine = false;
    bool hasAuthor = false;
    bool hasLicense = false;
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
        if (key == "schema" && !hasSchema) {
            auto schema = parseVersionNumber(value);
            if (!schema || (schema.value() != 1U && schema.value() != 2U)) {
                return Result<PackageManifest>::failure(
                    Error(ErrorCode::UnsupportedVersion, "unsupported package manifest schema"));
            }
            manifest.schemaVersion = static_cast<int>(schema.value());
            hasSchema = true;
        } else if (key == "id" && !hasId && !hasLegacyName) {
            manifest.id = value;
            hasId = true;
        } else if (key == "name" && !hasLegacyName && !hasId) {
            manifest.name = value;
            hasLegacyName = true;
        } else if (key == "displayName" && !hasDisplayName) {
            manifest.displayName = value;
            hasDisplayName = true;
        } else if (key == "version" && !hasVersion) {
            auto version = SemVersion::parse(value);
            if (!version)
                return Result<PackageManifest>::failure(version.error());
            manifest.version = std::move(version.value());
            hasVersion = true;
        } else if (key == "path" && !hasPath) {
            manifest.localPath = value;
            hasPath = true;
        } else if (key == "engine" && !hasEngine) {
            auto requirement = VersionRequirement::parse(value);
            if (!requirement)
                return Result<PackageManifest>::failure(requirement.error());
            manifest.engineCompatibility = std::move(requirement.value());
            hasEngine = true;
        } else if (key == "author" && !hasAuthor) {
            manifest.author = value;
            hasAuthor = true;
        } else if (key == "license" && !hasLicense) {
            manifest.spdxLicense = value;
            hasLicense = true;
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
        } else if (key == "entry") {
            const auto colon = value.find(':');
            if (colon == std::string::npos) {
                return Result<PackageManifest>::failure(
                    Error(ErrorCode::InvalidFormat, "invalid package entry point"));
            }
            auto kind = parsePackageEntryPointKind(std::string_view(value).substr(0U, colon));
            if (!kind)
                return Result<PackageManifest>::failure(kind.error());
            manifest.entryPoints.push_back({kind.value(), value.substr(colon + 1U)});
            manifest.containsExecutableCode = true;
        } else {
            return Result<PackageManifest>::failure(
                Error(ErrorCode::InvalidFormat, "duplicate or unknown package manifest field")
                    .addContext("field", key));
        }
    }
    if (hasId) {
        if (!hasSchema || manifest.schemaVersion != 2 || !hasDisplayName || !hasVersion ||
            !hasEngine || !hasAuthor || !hasLicense) {
            return Result<PackageManifest>::failure(
                Error(ErrorCode::InvalidFormat, "schema 2 package manifest is incomplete"));
        }
        manifest.name = manifest.id;
        if (!hasPath)
            manifest.localPath = "Packages/" + manifest.id;
    } else {
        if (!hasLegacyName || !hasVersion || !hasPath ||
            (hasSchema && manifest.schemaVersion != 1)) {
            return Result<PackageManifest>::failure(
                Error(ErrorCode::InvalidFormat, "legacy package manifest is incomplete"));
        }
        manifest.schemaVersion = 1;
        manifest.id = manifest.name;
        manifest.displayName = manifest.name;
        manifest.author = "Unknown";
        manifest.spdxLicense = "NOASSERTION";
    }
    auto valid = validateLocalManifest(manifest);
    if (!valid)
        return Result<PackageManifest>::failure(valid.error());
    return Result<PackageManifest>::success(std::move(manifest));
}

Result<std::string> PackageManifestParser::serializeCanonical(const PackageManifest& manifest) {
    auto valid = validateLocalManifest(manifest);
    if (!valid)
        return Result<std::string>::failure(valid.error());
    const auto id = std::string(manifest.stableId());
    const auto displayName = manifest.displayName.empty() ? id : manifest.displayName;
    const auto author = manifest.author.empty() ? std::string("Unknown") : manifest.author;
    const auto license =
        manifest.spdxLicense.empty() ? std::string("NOASSERTION") : manifest.spdxLicense;
    std::vector<PackageDependency> dependencies = manifest.dependencies;
    std::sort(dependencies.begin(), dependencies.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.name < rhs.name; });
    std::vector<PackageEntryPoint> entryPoints = manifest.entryPoints;
    std::sort(entryPoints.begin(), entryPoints.end(), [](const auto& lhs, const auto& rhs) {
        const auto leftKind = packageEntryPointKindName(lhs.kind);
        const auto rightKind = packageEntryPointKindName(rhs.kind);
        return leftKind == rightKind ? lhs.path < rhs.path : leftKind < rightKind;
    });
    std::ostringstream output;
    output << "schema=2\n"
           << "id=" << id << '\n'
           << "displayName=" << displayName << '\n'
           << "version=" << manifest.version.toString() << '\n'
           << "engine=" << manifest.engineCompatibility.toString() << '\n'
           << "author=" << author << '\n'
           << "license=" << license << '\n'
           << "path=Packages/" << id << '\n'
           << "executable="
           << (manifest.containsExecutableCode || !entryPoints.empty() ? "true" : "false") << '\n';
    for (const auto& dependency : dependencies)
        output << "dependency=" << dependency.name << '@' << dependency.requirement.toString()
               << '\n';
    for (const auto& entryPoint : entryPoints)
        output << "entry=" << packageEntryPointKindName(entryPoint.kind) << ':' << entryPoint.path
               << '\n';
    return Result<std::string>::success(output.str());
}

Result<void> PackageManifestParser::validateLocalManifest(const PackageManifest& manifest) {
    const auto id = manifest.stableId();
    if (!validPackageId(id) || (!manifest.name.empty() && manifest.name != id) ||
        !safeRelativePath(manifest.localPath)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "package id or local path is invalid"));
    }
    if (manifest.schemaVersion != 1 && manifest.schemaVersion != 2) {
        return Result<void>::failure(
            Error(ErrorCode::UnsupportedVersion, "package manifest schema is unsupported"));
    }
    if (manifest.schemaVersion == 2 &&
        (!validDisplayText(manifest.displayName, 120U) ||
         !validDisplayText(manifest.author, 160U) || !validSpdxLicense(manifest.spdxLicense))) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "package identity metadata is invalid"));
    }
    std::set<std::string> dependencies;
    for (const auto& dependency : manifest.dependencies) {
        if (!validPackageId(dependency.name) || dependency.name == id ||
            !dependencies.insert(dependency.name).second) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "package dependency is invalid"));
        }
    }
    std::set<std::string> entryPoints;
    for (const auto& entryPoint : manifest.entryPoints) {
        const auto key =
            std::string(packageEntryPointKindName(entryPoint.kind)) + ':' + entryPoint.path;
        if (!safeRelativePath(entryPoint.path) || !entryPoints.insert(key).second) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "package entry point is invalid"));
        }
    }
    if (!manifest.entryPoints.empty() && !manifest.containsExecutableCode) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "package entry points require executable=true"));
    }
    return Result<void>::success();
}

Result<void> PackageRegistry::add(PackageManifest manifest) {
    auto valid = PackageManifestParser::validateLocalManifest(manifest);
    if (!valid)
        return valid;
    const auto id = std::string(manifest.stableId());
    if (packages_.find(id) != packages_.end()) {
        return Result<void>::failure(Error(ErrorCode::AlreadyExists, "package is already installed")
                                         .addContext("package", id));
    }
    packages_.emplace(id, std::move(manifest));
    return Result<void>::success();
}

Result<std::vector<std::string>> PackageRegistry::validate(bool allowExecutableCode) const {
    return validatePackages(packages_, nullptr, [allowExecutableCode](const auto&) {
        // Legacy callers can explicitly authorize all executable packages. A package's own
        // trust field is metadata only and must never authorize its code.
        return allowExecutableCode;
    });
}

Result<std::vector<std::string>>
PackageRegistry::validate(const SemVersion& engineVersion,
                          const std::set<std::string>& trustedExecutablePackages) const {
    return validatePackages(
        packages_, &engineVersion, [&trustedExecutablePackages](const auto& package) {
            return trustedExecutablePackages.find(std::string(package.stableId())) !=
                   trustedExecutablePackages.end();
        });
}

} // namespace fabgl
