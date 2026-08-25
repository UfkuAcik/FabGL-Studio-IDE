#include <fabgl/extensions/extension_module.h>
#include <fabgl/project/project_extension_modules.h>

#include <cstdlib>
#include <fstream>
#include <string>

namespace {

class FixtureExtension final : public fabgl::IExtension {
  public:
    const fabgl::ExtensionIdentity& identity() const noexcept override {
        static const fabgl::ExtensionIdentity identity{
            "fixture.package",
            "fixture",
            {1U, 0U, 0U, {}},
            fabgl::PackageEntryPointKind::EditorPlugin,
            std::string(64U, 'a'),
            {},
            false};
        return identity;
    }

    fabgl::Result<void> activate(fabgl::ExtensionHost& host) override {
        auto registered = host.registerHook(
            "fixture.echo", [](const fabgl::ExtensionInvocation& invocation) {
                if (invocation.payload == "fail-service") {
                    return fabgl::Result<std::string>::failure(
                        fabgl::Error(fabgl::ErrorCode::InvalidState,
                                     "fixture service failure requested"));
                }
                if (const auto* tracePath = std::getenv("FGL_EXTENSION_FIXTURE_SERVICE_TRACE");
                    tracePath != nullptr && tracePath[0] != '\0') {
                    std::ofstream trace(tracePath, std::ios::binary | std::ios::app);
                    if (!trace.is_open()) {
                        return fabgl::Result<std::string>::failure(
                            fabgl::Error(fabgl::ErrorCode::IoError,
                                         "fixture could not open its service trace"));
                    }
                    trace << invocation.operation << '|' << invocation.payload << '\n';
                    if (!trace.good()) {
                        return fabgl::Result<std::string>::failure(
                            fabgl::Error(fabgl::ErrorCode::IoError,
                                         "fixture could not write its service trace"));
                    }
                }
                return fabgl::Result<std::string>::success(invocation.operation + "|" +
                                                           invocation.payload);
            });
        if (!registered)
            return registered;
        for (const auto capability : {fabgl::project::ProjectOpenExtensionCapability,
                                      fabgl::project::ProjectCloseExtensionCapability,
                                      fabgl::project::RuntimeStartExtensionCapability,
                                      fabgl::project::RuntimeStopExtensionCapability}) {
            registered = host.registerHook(
                std::string(capability), [capability](const fabgl::ExtensionInvocation& invocation) {
                    const auto* context = static_cast<const fabgl::project::ProjectExtensionHostContext*>(
                        invocation.hostContext);
                    if (context == nullptr ||
                        context->structureSize !=
                            sizeof(fabgl::project::ProjectExtensionHostContext) ||
                        context->apiVersion != fabgl::project::ProjectExtensionHostApiVersion ||
                        context->projectManifestPath == nullptr ||
                        context->projectManifestPathBytes == 0U || context->projectRoot == nullptr ||
                        context->projectRootBytes == 0U || context->scene == nullptr ||
                        ((capability == fabgl::project::RuntimeStartExtensionCapability ||
                          capability == fabgl::project::RuntimeStopExtensionCapability) &&
                         context->sceneRuntime == nullptr)) {
                        return fabgl::Result<std::string>::failure(
                            fabgl::Error(fabgl::ErrorCode::InvalidArgument,
                                         "fixture received an invalid lifecycle context"));
                    }
                    if (invocation.payload == "fail") {
                        return fabgl::Result<std::string>::failure(
                            fabgl::Error(fabgl::ErrorCode::InvalidState,
                                         "fixture lifecycle failure requested"));
                    }
                    if (const auto* tracePath = std::getenv("FGL_EXTENSION_FIXTURE_TRACE");
                        tracePath != nullptr && tracePath[0] != '\0') {
                        std::ofstream trace(tracePath, std::ios::binary | std::ios::app);
                        if (!trace.is_open()) {
                            return fabgl::Result<std::string>::failure(
                                fabgl::Error(fabgl::ErrorCode::IoError,
                                             "fixture could not open its lifecycle trace"));
                        }
                        trace << capability << '|' << invocation.operation << '|'
                              << invocation.payload << '\n';
                        if (!trace.good()) {
                            return fabgl::Result<std::string>::failure(
                                fabgl::Error(fabgl::ErrorCode::IoError,
                                             "fixture could not write its lifecycle trace"));
                        }
                    }
                    return fabgl::Result<std::string>::success(std::string(capability));
                });
            if (!registered)
                return registered;
        }
        for (const auto kind : {fabgl::PackageEntryPointKind::EditorPlugin,
                                fabgl::PackageEntryPointKind::RuntimeModule,
                                fabgl::PackageEntryPointKind::AssetImporter,
                                fabgl::PackageEntryPointKind::CustomInspector,
                                fabgl::PackageEntryPointKind::CustomWindow,
                                fabgl::PackageEntryPointKind::BuildStep,
                                fabgl::PackageEntryPointKind::RendererExtension,
                                fabgl::PackageEntryPointKind::Framework}) {
            const auto registrationCapability =
                fabgl::project::projectExtensionRegistrationCapability(kind);
            registered = host.registerHook(
                std::string(registrationCapability),
                [kind](const fabgl::ExtensionInvocation& invocation) {
                    const auto* context =
                        static_cast<const fabgl::project::ProjectExtensionHostContext*>(
                            invocation.hostContext);
                    if (context == nullptr || context->services == nullptr ||
                        context->services->structureSize !=
                            sizeof(fabgl::project::ProjectExtensionHostServices) ||
                        context->services->apiVersion !=
                            fabgl::project::ProjectExtensionServiceApiVersion ||
                        context->services->hostState == nullptr ||
                        context->services->registerService == nullptr) {
                        return fabgl::Result<std::string>::failure(
                            fabgl::Error(fabgl::ErrorCode::InvalidArgument,
                                         "fixture received an invalid service table"));
                    }
                    const std::string serviceId(fabgl::packageEntryPointKindName(kind));
                    const std::string displayName = "Fixture " + serviceId;
                    const fabgl::project::ProjectExtensionServiceDescriptor descriptor{
                        sizeof(fabgl::project::ProjectExtensionServiceDescriptor),
                        fabgl::project::ProjectExtensionServiceApiVersion,
                        kind,
                        serviceId.c_str(),
                        displayName.c_str(),
                        "fixture.echo",
                        10};
                    auto service = context->services->registerService(
                        context->services->hostState, &descriptor);
                    if (!service)
                        return fabgl::Result<std::string>::failure(service.error());
                    return fabgl::Result<std::string>::success(serviceId);
                });
            if (!registered)
                return registered;
        }
        return fabgl::Result<void>::success();
    }

    void deactivate(fabgl::ExtensionHost&) noexcept override {}
};

fabgl::IExtension* createFixture() noexcept {
    try {
        return new FixtureExtension();
    } catch (...) {
        return nullptr;
    }
}

void destroyFixture(fabgl::IExtension* extension) noexcept {
    delete extension;
}

const fabgl::extensions::ExtensionModuleDescriptor Descriptor{
    sizeof(fabgl::extensions::ExtensionModuleDescriptor),
    "fixture",
    &createFixture,
    &destroyFixture};

} // namespace

FGL_EXTENSION_MODULE_EXPORT bool
fabglStudioGetExtensionModuleV1(fabgl::extensions::ExtensionModuleView* output) noexcept {
    if (output == nullptr)
        return false;
    output->structureSize = sizeof(*output);
    output->abiVersion = fabgl::extensions::ExtensionModuleAbiVersion;
    output->descriptors = &Descriptor;
    output->descriptorCount = 1U;
    return true;
}
