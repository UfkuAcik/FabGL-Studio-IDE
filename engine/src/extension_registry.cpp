#include "fabgl/extensions/extension_registry.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace fabgl {
namespace {

bool stableId(std::string_view value) noexcept {
    if (value.empty() || value.size() > 128U || value.front() == '.' || value.back() == '.') {
        return false;
    }
    bool previousDot = false;
    for (const char rawCharacter : value) {
        const auto character = static_cast<unsigned char>(rawCharacter);
        const bool dot = character == '.';
        if ((!std::islower(character) && !std::isdigit(character) && character != '-' && !dot) ||
            (dot && previousDot)) {
            return false;
        }
        previousDot = dot;
    }
    return true;
}

bool lowercaseHexDigest(std::string_view value) noexcept {
    return value.size() == 64U &&
           std::all_of(value.cbegin(), value.cend(), [](const char rawCharacter) {
               const auto character = static_cast<unsigned char>(rawCharacter);
               return std::isdigit(character) || (character >= 'a' && character <= 'f');
           });
}

bool capabilityName(std::string_view value) noexcept {
    if (value.empty() || value.size() > 96U) {
        return false;
    }
    return std::all_of(value.cbegin(), value.cend(), [](const char rawCharacter) {
        const auto character = static_cast<unsigned char>(rawCharacter);
        return std::islower(character) || std::isdigit(character) || character == '-' ||
               character == '.';
    });
}

Result<void> validateIdentity(const ExtensionIdentity& identity) {
    if (!stableId(identity.packageId) || !stableId(identity.extensionId)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "extension IDs must be stable lower-case IDs"));
    }
    if ((!identity.builtIn && !lowercaseHexDigest(identity.contentSha256)) ||
        identity.dependencies.size() > MaximumExtensionDependencies) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "extension identity or dependency bounds are invalid")
                .addContext("extension", identity.qualifiedId()));
    }
    std::set<std::string> dependencies;
    for (const auto& dependency : identity.dependencies) {
        const auto separator = dependency.find('/');
        if (separator == std::string::npos ||
            !stableId(std::string_view(dependency).substr(0U, separator)) ||
            !stableId(std::string_view(dependency).substr(separator + 1U)) ||
            dependency == identity.qualifiedId() || !dependencies.insert(dependency).second) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "extension dependency is invalid")
                    .addContext("extension", identity.qualifiedId())
                    .addContext("dependency", dependency));
        }
    }
    return Result<void>::success();
}

} // namespace

std::string ExtensionIdentity::qualifiedId() const {
    return packageId + '/' + extensionId;
}

ExtensionHost::ExtensionHost(const bool safeMode, const bool extensionsEnabled,
                             ExtensionTrustEvaluator trustEvaluator)
    : safeMode_(safeMode), extensionsEnabled_(extensionsEnabled),
      trustEvaluator_(std::move(trustEvaluator)) {}

bool ExtensionHost::safeMode() const noexcept {
    return safeMode_;
}

bool ExtensionHost::extensionsEnabled() const noexcept {
    return extensionsEnabled_;
}

bool ExtensionHost::mayActivate(const ExtensionIdentity& identity) const {
    if (identity.builtIn) {
        return true;
    }
    return !safeMode_ && extensionsEnabled_ && trustEvaluator_ && trustEvaluator_(identity);
}

Result<void> ExtensionHost::registerHook(std::string capability, ExtensionHook hook) {
    if (registering_ == nullptr) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "extension hook registration is outside activation"));
    }
    if (!capabilityName(capability) || !hook || hooks_.size() >= MaximumExtensionHooks) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "extension hook is invalid or exceeds host limits")
                .addContext("extension", registering_->qualifiedId())
                .addContext("capability", capability));
    }
    HookKey key{registering_->qualifiedId(), std::move(capability)};
    if (hooks_.find(key) != hooks_.end()) {
        return Result<void>::failure(
            Error(ErrorCode::AlreadyExists, "extension hook is already registered")
                .addContext("extension", key.extension)
                .addContext("capability", key.capability));
    }
    hooks_.emplace(std::move(key), std::move(hook));
    return Result<void>::success();
}

Result<std::string> ExtensionHost::invoke(const std::string_view qualifiedExtensionId,
                                          const std::string_view capability,
                                          const ExtensionInvocation& invocation) const {
    const auto found =
        hooks_.find(HookKey{std::string(qualifiedExtensionId), std::string(capability)});
    if (found == hooks_.end()) {
        return Result<std::string>::failure(
            Error(ErrorCode::NotFound, "extension capability was not registered")
                .addContext("extension", std::string(qualifiedExtensionId))
                .addContext("capability", std::string(capability)));
    }
    try {
        return found->second(invocation);
    } catch (...) {
        return Result<std::string>::failure(
            Error(ErrorCode::InternalError, "extension hook threw across the host boundary")
                .addContext("extension", std::string(qualifiedExtensionId)));
    }
}

bool ExtensionHost::hasHook(const std::string_view qualifiedExtensionId,
                            const std::string_view capability) const noexcept {
    return hooks_.find(HookKey{std::string(qualifiedExtensionId), std::string(capability)}) !=
           hooks_.end();
}

std::size_t ExtensionHost::hookCount() const noexcept {
    return hooks_.size();
}

void ExtensionHost::beginRegistration(const ExtensionIdentity* identity) noexcept {
    registering_ = identity;
}

void ExtensionHost::endRegistration() noexcept {
    registering_ = nullptr;
}

void ExtensionHost::removeHooks(const std::string_view qualifiedExtensionId) noexcept {
    std::erase_if(hooks_, [qualifiedExtensionId](const auto& entry) {
        return entry.first.extension == qualifiedExtensionId;
    });
}

Result<void> ExtensionRegistry::add(std::unique_ptr<IExtension> extension) {
    if (extension == nullptr || extensions_.size() >= MaximumExtensions) {
        return Result<void>::failure(
            Error(ErrorCode::CapacityExceeded, "extension registry capacity was exceeded"));
    }
    const auto valid = validateIdentity(extension->identity());
    if (!valid) {
        return valid;
    }
    const auto id = extension->identity().qualifiedId();
    if (extensions_.find(id) != extensions_.end()) {
        return Result<void>::failure(
            Error(ErrorCode::AlreadyExists, "extension ID is already registered")
                .addContext("extension", id));
    }
    extensions_.emplace(id, std::move(extension));
    return Result<void>::success();
}

const IExtension* ExtensionRegistry::find(const std::string_view qualifiedId) const noexcept {
    const auto found = extensions_.find(qualifiedId);
    return found == extensions_.end() ? nullptr : found->second.get();
}

Result<std::vector<std::string>> ExtensionRegistry::activationOrder() const {
    enum class Visit { Visiting, Visited };
    std::map<std::string, Visit, std::less<>> visited;
    std::vector<std::string> result;
    std::function<Result<void>(const std::string&)> visit = [&](const std::string& id) {
        const auto state = visited.find(id);
        if (state != visited.end()) {
            if (state->second == Visit::Visiting) {
                return Result<void>::failure(
                    Error(ErrorCode::CycleDetected, "extension dependency cycle detected")
                        .addContext("extension", id));
            }
            return Result<void>::success();
        }
        const auto found = extensions_.find(id);
        if (found == extensions_.end()) {
            return Result<void>::failure(
                Error(ErrorCode::NotFound, "extension dependency was not registered")
                    .addContext("extension", id));
        }
        visited.emplace(id, Visit::Visiting);
        for (const auto& dependency : found->second->identity().dependencies) {
            const auto dependencyResult = visit(dependency);
            if (!dependencyResult) {
                return dependencyResult;
            }
        }
        visited[id] = Visit::Visited;
        result.push_back(id);
        return Result<void>::success();
    };
    for (const auto& [id, extension] : extensions_) {
        static_cast<void>(extension);
        const auto visitedResult = visit(id);
        if (!visitedResult) {
            return Result<std::vector<std::string>>::failure(visitedResult.error());
        }
    }
    return Result<std::vector<std::string>>::success(std::move(result));
}

Result<void> ExtensionRegistry::activateAll(ExtensionHost& host) {
    if (!active_.empty()) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "extension registry is already active"));
    }
    const auto order = activationOrder();
    if (!order) {
        return Result<void>::failure(order.error());
    }
    for (const auto& id : order.value()) {
        auto& extension = *extensions_.at(id);
        if (!host.mayActivate(extension.identity())) {
            deactivateAll(host);
            return Result<void>::failure(
                Error(ErrorCode::InvalidState, "extension activation is not trusted")
                    .addContext("extension", id));
        }
        host.beginRegistration(&extension.identity());
        Result<void> activated = Result<void>::failure(
            Error(ErrorCode::InternalError, "extension activation did not complete"));
        try {
            activated = extension.activate(host);
        } catch (...) {
            activated = Result<void>::failure(
                Error(ErrorCode::InternalError, "extension activation threw across the boundary"));
        }
        host.endRegistration();
        if (!activated) {
            host.removeHooks(id);
            try {
                extension.deactivate(host);
            } catch (...) {
            }
            deactivateAll(host);
            return Result<void>::failure(activated.error().withContext("extension", id));
        }
        active_.push_back(id);
    }
    return Result<void>::success();
}

void ExtensionRegistry::deactivateAll(ExtensionHost& host) noexcept {
    host.endRegistration();
    for (auto iterator = active_.rbegin(); iterator != active_.rend(); ++iterator) {
        auto& extension = *extensions_.at(*iterator);
        try {
            extension.deactivate(host);
        } catch (...) {
        }
        host.removeHooks(*iterator);
    }
    active_.clear();
}

std::size_t ExtensionRegistry::size() const noexcept {
    return extensions_.size();
}

std::size_t ExtensionRegistry::activeCount() const noexcept {
    return active_.size();
}

} // namespace fabgl
