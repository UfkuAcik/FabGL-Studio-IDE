#include "test_harness.h"

#include "fabgl/extensions/extension_registry.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace fabgl;

namespace {

class TestExtension final : public IExtension {
  public:
    TestExtension(ExtensionIdentity identity, std::vector<std::string>& events,
                  bool failActivation = false)
        : identity_(std::move(identity)), events_(events), failActivation_(failActivation) {}

    const ExtensionIdentity& identity() const noexcept override {
        return identity_;
    }

    Result<void> activate(ExtensionHost& host) override {
        events_.push_back("activate:" + identity_.qualifiedId());
        auto hook = host.registerHook(
            "dispatch", [id = identity_.qualifiedId()](const ExtensionInvocation& invocation) {
                return Result<std::string>::success(id + ':' + invocation.operation + ':' +
                                                    invocation.payload);
            });
        if (!hook) {
            return hook;
        }
        return failActivation_
                   ? Result<void>::failure(Error(ErrorCode::InvalidState, "requested failure"))
                   : Result<void>::success();
    }

    void deactivate(ExtensionHost&) noexcept override {
        events_.push_back("deactivate:" + identity_.qualifiedId());
    }

  private:
    ExtensionIdentity identity_;
    std::vector<std::string>& events_;
    bool failActivation_ = false;
};

ExtensionIdentity identity(std::string packageId, std::string extensionId,
                           PackageEntryPointKind kind, std::vector<std::string> dependencies = {},
                           bool builtIn = false) {
    return {std::move(packageId),
            std::move(extensionId),
            {1U, 2U, 3U, {}},
            kind,
            builtIn ? std::string{} : std::string(64U, 'a'),
            std::move(dependencies),
            builtIn};
}

} // namespace

FGL_TEST(extension_registry_activates_all_entry_point_kinds_in_dependency_order) {
    std::vector<std::string> events;
    ExtensionRegistry registry;
    const std::vector<PackageEntryPointKind> kinds{
        PackageEntryPointKind::EditorPlugin,      PackageEntryPointKind::RuntimeModule,
        PackageEntryPointKind::AssetImporter,     PackageEntryPointKind::CustomInspector,
        PackageEntryPointKind::CustomWindow,      PackageEntryPointKind::BuildStep,
        PackageEntryPointKind::RendererExtension, PackageEntryPointKind::Framework};
    std::string previous;
    for (std::size_t index = 0U; index < kinds.size(); ++index) {
        const auto extensionId = "entry-" + std::to_string(index);
        std::vector<std::string> dependencies;
        if (!previous.empty()) {
            dependencies.push_back(previous);
        }
        auto descriptor = identity("org.example.tools", extensionId, kinds[index], dependencies);
        previous = descriptor.qualifiedId();
        FGL_CHECK(registry.add(std::make_unique<TestExtension>(std::move(descriptor), events)));
    }
    ExtensionHost host(false, true, [](const ExtensionIdentity& extension) {
        return extension.contentSha256 == std::string(64U, 'a');
    });
    FGL_CHECK(registry.activateAll(host));
    FGL_CHECK(registry.activeCount() == kinds.size());
    FGL_CHECK(host.hookCount() == kinds.size());
    FGL_CHECK(events.front() == "activate:org.example.tools/entry-0");
    const auto response =
        host.invoke("org.example.tools/entry-7", "dispatch", {"build", "esp32", nullptr});
    FGL_CHECK(response && response.value() == "org.example.tools/entry-7:build:esp32");
    registry.deactivateAll(host);
    FGL_CHECK(registry.activeCount() == 0U && host.hookCount() == 0U);
    FGL_CHECK(events.back() == "deactivate:org.example.tools/entry-0");
}

FGL_TEST(extension_registry_fails_closed_for_trust_safe_mode_cycles_and_partial_activation) {
    std::vector<std::string> events;
    ExtensionRegistry registry;
    FGL_CHECK(registry.add(std::make_unique<TestExtension>(
        identity("org.example", "base", PackageEntryPointKind::RuntimeModule), events)));
    ExtensionHost untrusted(false, true, [](const ExtensionIdentity&) { return false; });
    FGL_CHECK(!registry.activateAll(untrusted));
    FGL_CHECK(registry.activeCount() == 0U && untrusted.hookCount() == 0U);
    ExtensionHost safeMode(true, true, [](const ExtensionIdentity&) { return true; });
    FGL_CHECK(!registry.activateAll(safeMode));

    ExtensionRegistry rollback;
    FGL_CHECK(rollback.add(std::make_unique<TestExtension>(
        identity("org.rollback", "first", PackageEntryPointKind::EditorPlugin), events)));
    FGL_CHECK(rollback.add(std::make_unique<TestExtension>(
        identity("org.rollback", "second", PackageEntryPointKind::BuildStep,
                 {"org.rollback/first"}),
        events, true)));
    ExtensionHost trusted(false, true, [](const ExtensionIdentity&) { return true; });
    FGL_CHECK(!rollback.activateAll(trusted));
    FGL_CHECK(rollback.activeCount() == 0U && trusted.hookCount() == 0U);
    FGL_CHECK(events.size() == 4U);
    FGL_CHECK(events[0] == "activate:org.rollback/first");
    FGL_CHECK(events[1] == "activate:org.rollback/second");
    FGL_CHECK(events[2] == "deactivate:org.rollback/second");
    FGL_CHECK(events[3] == "deactivate:org.rollback/first");

    ExtensionRegistry cycle;
    FGL_CHECK(cycle.add(std::make_unique<TestExtension>(
        identity("org.cycle", "a", PackageEntryPointKind::Framework, {"org.cycle/b"}), events)));
    FGL_CHECK(cycle.add(std::make_unique<TestExtension>(
        identity("org.cycle", "b", PackageEntryPointKind::Framework, {"org.cycle/a"}), events)));
    const auto order = cycle.activationOrder();
    FGL_CHECK(!order && order.error().code() == ErrorCode::CycleDetected);
}
