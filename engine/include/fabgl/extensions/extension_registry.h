#pragma once

#include "fabgl/core/result.h"
#include "fabgl/packages/package_manifest.h"

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fabgl {

inline constexpr std::size_t MaximumExtensions = 256U;
inline constexpr std::size_t MaximumExtensionDependencies = 32U;
inline constexpr std::size_t MaximumExtensionHooks = 512U;

struct ExtensionIdentity final {
    std::string packageId;
    std::string extensionId;
    SemVersion version;
    PackageEntryPointKind kind = PackageEntryPointKind::RuntimeModule;
    std::string contentSha256;
    std::vector<std::string> dependencies;
    bool builtIn = false;

    [[nodiscard]] std::string qualifiedId() const;
};

struct ExtensionInvocation final {
    std::string operation;
    std::string payload;
    void* hostContext = nullptr;
};

using ExtensionHook = std::function<Result<std::string>(const ExtensionInvocation&)>;
using ExtensionTrustEvaluator = std::function<bool(const ExtensionIdentity&)>;

class ExtensionHost final {
  public:
    explicit ExtensionHost(bool safeMode = false, bool extensionsEnabled = true,
                           ExtensionTrustEvaluator trustEvaluator = {});

    [[nodiscard]] bool safeMode() const noexcept;
    [[nodiscard]] bool extensionsEnabled() const noexcept;
    [[nodiscard]] bool mayActivate(const ExtensionIdentity& identity) const;
    [[nodiscard]] Result<void> registerHook(std::string capability, ExtensionHook hook);
    [[nodiscard]] Result<std::string> invoke(std::string_view qualifiedExtensionId,
                                             std::string_view capability,
                                             const ExtensionInvocation& invocation) const;
    [[nodiscard]] bool hasHook(std::string_view qualifiedExtensionId,
                               std::string_view capability) const noexcept;
    [[nodiscard]] std::size_t hookCount() const noexcept;

  private:
    friend class ExtensionRegistry;

    struct HookKey final {
        std::string extension;
        std::string capability;

        friend bool operator<(const HookKey& lhs, const HookKey& rhs) noexcept {
            return lhs.extension < rhs.extension ||
                   (lhs.extension == rhs.extension && lhs.capability < rhs.capability);
        }
    };

    void beginRegistration(const ExtensionIdentity* identity) noexcept;
    void endRegistration() noexcept;
    void removeHooks(std::string_view qualifiedExtensionId) noexcept;

    bool safeMode_ = false;
    bool extensionsEnabled_ = true;
    ExtensionTrustEvaluator trustEvaluator_;
    const ExtensionIdentity* registering_ = nullptr;
    std::map<HookKey, ExtensionHook> hooks_;
};

class IExtension {
  public:
    virtual ~IExtension() = default;

    [[nodiscard]] virtual const ExtensionIdentity& identity() const noexcept = 0;
    [[nodiscard]] virtual Result<void> activate(ExtensionHost& host) = 0;
    virtual void deactivate(ExtensionHost& host) noexcept = 0;
};

class ExtensionRegistry final {
  public:
    [[nodiscard]] Result<void> add(std::unique_ptr<IExtension> extension);
    [[nodiscard]] const IExtension* find(std::string_view qualifiedId) const noexcept;
    [[nodiscard]] Result<std::vector<std::string>> activationOrder() const;
    [[nodiscard]] Result<void> activateAll(ExtensionHost& host);
    void deactivateAll(ExtensionHost& host) noexcept;

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t activeCount() const noexcept;

  private:
    std::map<std::string, std::unique_ptr<IExtension>, std::less<>> extensions_;
    std::vector<std::string> active_;
};

} // namespace fabgl
