#include "test_harness.h"

#include "local_package_manager.h"

#include <fabgl/project/project_extension_modules.h>
#include <fabgl/project/project_extension_service_host.h>
#include <fabgl/runtime/scene_runtime.h>
#include <fabgl/scene/scene.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace fabgl;

namespace {

namespace fs = std::filesystem;

fs::path FixtureModulePath;

struct TemporaryDirectory final {
    fs::path path;

    ~TemporaryDirectory() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

std::string pathText(const fs::path& path) {
    const auto value = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

void writeText(const fs::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    FGL_CHECK(output.is_open());
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    FGL_CHECK(output.good());
}

TemporaryDirectory makeProjectFixture(const fs::path& modulePath, fs::path& projectManifest,
                                      const std::string& binaryEntryKind = "editor-plugin") {
    const auto token = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory temporary{fs::temp_directory_path() /
                                 ("fabgl-extension-module-" + std::to_string(token))};
    const auto projectRoot = temporary.path / "Project";
    const auto binaryPackage = temporary.path / "BinaryPackage";
    const auto sourcePackage = temporary.path / "SourcePackage";
    FGL_CHECK(fs::create_directories(projectRoot));
    FGL_CHECK(fs::create_directories(binaryPackage / "bin"));
    FGL_CHECK(fs::create_directories(sourcePackage / "src"));
    projectManifest = projectRoot / "ExtensionTest.fglproject";
    writeText(projectManifest,
              "{\n"
              "  \"kind\": \"FabGLStudioProject\",\n"
              "  \"formatVersion\": 2,\n"
              "  \"projectGuid\": \"50000000-0000-4000-8000-000000000001\",\n"
              "  \"name\": \"Extension Test\",\n"
              "  \"projectRoot\": \".\",\n"
              "  \"startupScene\": \"Scenes/Main.fglscene\",\n"
              "  \"build\": {\"program\": \"cmake\", \"arguments\": []},\n"
              "  \"assets\": [],\n"
              "  \"input\": {\"contexts\": []},\n"
              "  \"packages\": [],\n"
              "  \"targetProfiles\": {\"pc\": \"pc.default\", "
              "\"esp32\": \"olimex-esp32-sbc-fabgl-revb\"}\n"
              "}\n");

    const auto installedModule = binaryPackage / "bin" / modulePath.filename();
    std::error_code copyError;
    fs::copy_file(modulePath, installedModule, fs::copy_options::overwrite_existing, copyError);
    FGL_CHECK(!copyError);
    writeText(binaryPackage / "fabgl.package",
              "schema=2\n"
              "id=test.binary-extension\n"
              "displayName=Binary Extension\n"
              "version=1.0.0\n"
              "engine=*\n"
              "author=FabGL Tests\n"
              "license=MIT\n"
              "executable=true\n"
              "entry=" + binaryEntryKind + ":bin/" + pathText(modulePath.filename()) + "\n");
    project::LocalPackageInstallOptions approved;
    approved.allowExecutable = true;
    FGL_CHECK(project::installLocalPackage(pathText(projectManifest), pathText(binaryPackage),
                                           approved));

    writeText(sourcePackage / "fabgl.package",
              "schema=2\n"
              "id=test.source-extension\n"
              "displayName=Source Extension\n"
              "version=1.0.0\n"
              "engine=*\n"
              "author=FabGL Tests\n"
              "license=MIT\n"
              "executable=true\n"
              "entry=custom-window:src/window.cpp\n");
    writeText(sourcePackage / "src" / "window.cpp", "void source_extension_fixture() {}\n");
    FGL_CHECK(project::installLocalPackage(pathText(projectManifest), pathText(sourcePackage),
                                           approved));
    return temporary;
}

} // namespace

FGL_TEST(project_extension_modules_discover_trusted_binary_activate_and_invoke) {
    fs::path projectManifest;
    auto temporary = makeProjectFixture(FixtureModulePath, projectManifest);

    auto modules = project::ProjectExtensionModules::load(pathText(projectManifest));
    FGL_CHECK(modules);
    FGL_CHECK(modules.value().stats().discoveredPackages == 2U);
    FGL_CHECK(modules.value().stats().discoveredEntries == 2U);
    FGL_CHECK(modules.value().stats().loadedModules == 1U);
    FGL_CHECK(modules.value().stats().registeredExtensions == 1U);
    FGL_CHECK(modules.value().stats().skippedSourceEntries == 1U);
    FGL_CHECK(modules.value().activate());
    FGL_CHECK(modules.value().active());
    FGL_CHECK(modules.value().activeExtensionIds().size() == 1U);
    FGL_CHECK(modules.value().activeExtensionIds().front() ==
              "test.binary-extension/fixture");
    auto response = modules.value().invoke("test.binary-extension/fixture", "fixture.echo",
                                           {"build", "pc", nullptr});
    FGL_CHECK(response && response.value() == "build|pc");

    Scene scene("Extension module test");
    SceneRuntime runtime(scene);
    const auto manifestText = pathText(projectManifest);
    const auto rootText = pathText(projectManifest.parent_path());
    project::ProjectExtensionHostContext context;
    context.hostKind = project::ProjectExtensionHostKind::Player;
    context.projectManifestPath = manifestText.data();
    context.projectManifestPathBytes = manifestText.size();
    context.projectRoot = rootText.data();
    context.projectRootBytes = rootText.size();
    context.scene = &scene;
    context.sceneRuntime = &runtime;
    auto serviceHost = project::ProjectExtensionServiceHost::create(modules.value(), context);
    FGL_CHECK(serviceHost);
    FGL_CHECK(serviceHost.value().services().size() == 1U);
    FGL_CHECK(serviceHost.value().services().front().service.kind ==
              PackageEntryPointKind::EditorPlugin);
    FGL_CHECK(serviceHost.value().services().front().service.qualifiedId() ==
              "test.binary-extension/fixture:editor-plugin");
    auto editorResponse = serviceHost.value().invoke(
        "test.binary-extension/fixture:editor-plugin", "execute", "editor-action", context);
    FGL_CHECK(editorResponse && editorResponse.value() == "execute|editor-action");
    auto unsupported = serviceHost.value().invoke(
        "test.binary-extension/fixture:editor-plugin", "startup", {}, context);
    FGL_CHECK(!unsupported && unsupported.error().code() == ErrorCode::InvalidArgument);
    FGL_CHECK(serviceHost.value().services().front().enabled());
    auto serviceFailure = serviceHost.value().invoke(
        "test.binary-extension/fixture:editor-plugin", "execute", "fail-service", context);
    FGL_CHECK(!serviceFailure && serviceFailure.error().code() == ErrorCode::InvalidState);
    FGL_CHECK(!serviceHost.value().services().front().enabled());
    FGL_CHECK(serviceHost.value().stats().disabledServices == 1U);
    auto disabled = serviceHost.value().invoke(
        "test.binary-extension/fixture:editor-plugin", "execute", "again", context);
    FGL_CHECK(!disabled && disabled.error().code() == ErrorCode::InvalidState);
    FGL_CHECK(modules.value().invokeAll("fixture.missing", {"ignored", {}, nullptr}));
    FGL_CHECK(modules.value().invokeAll(project::ProjectOpenExtensionCapability,
                                        {"open", "test", &context}));
    auto lifecycleFailure = modules.value().invokeAll(
        project::RuntimeStartExtensionCapability, {"start", "fail", &context});
    FGL_CHECK(!lifecycleFailure && lifecycleFailure.error().code() == ErrorCode::InvalidState);
    FGL_CHECK(std::any_of(lifecycleFailure.error().context().cbegin(),
                          lifecycleFailure.error().context().cend(), [](const auto& item) {
                              return item.key == "extension" &&
                                     item.value == "test.binary-extension/fixture";
                          }));
    FGL_CHECK(!modules.value().invokeAll(project::ProjectCloseExtensionCapability,
                                         {"close", "test", nullptr}));
    modules.value().deactivate();
    FGL_CHECK(!modules.value().active());
    FGL_CHECK(modules.value().activeExtensionIds().empty());
    FGL_CHECK(!modules.value().invoke("test.binary-extension/fixture", "fixture.echo",
                                      {"build", "pc", nullptr}));
}

FGL_TEST(project_extension_product_host_dispatches_runtime_and_build_contracts) {
    fs::path runtimeManifest;
    auto runtimeProject =
        makeProjectFixture(FixtureModulePath, runtimeManifest, "runtime-module");
    auto runtimeModules = project::ProjectExtensionModules::load(pathText(runtimeManifest));
    FGL_CHECK(runtimeModules && runtimeModules.value().activate());
    Scene runtimeScene("Runtime service test");
    SceneRuntime runtime(runtimeScene);
    const auto runtimeManifestText = pathText(runtimeManifest);
    const auto runtimeRootText = pathText(runtimeManifest.parent_path());
    project::ProjectExtensionHostContext runtimeContext;
    runtimeContext.hostKind = project::ProjectExtensionHostKind::Player;
    runtimeContext.projectManifestPath = runtimeManifestText.data();
    runtimeContext.projectManifestPathBytes = runtimeManifestText.size();
    runtimeContext.projectRoot = runtimeRootText.data();
    runtimeContext.projectRootBytes = runtimeRootText.size();
    runtimeContext.scene = &runtimeScene;
    runtimeContext.sceneRuntime = &runtime;
    auto runtimeHost =
        project::ProjectExtensionServiceHost::create(runtimeModules.value(), runtimeContext);
    FGL_CHECK(runtimeHost && runtimeHost.value().services().size() == 1U);
    const auto started = runtimeHost.value().runtimeStart(runtimeContext, "test-player");
    FGL_CHECK(started.ok() && started.attempted == 1U && started.succeeded == 1U);
    FGL_CHECK(runtimeHost.value().services().front().state ==
              project::ProjectExtensionServiceStateKind::RuntimeStarted);
    const auto updated = runtimeHost.value().runtimeUpdate(runtimeContext, 1.0 / 60.0);
    FGL_CHECK(updated.ok() && updated.succeeded == 1U);
    const auto stopped = runtimeHost.value().runtimeStop(runtimeContext, "test-complete");
    FGL_CHECK(stopped.ok() && stopped.succeeded == 1U);
    FGL_CHECK(runtimeHost.value().services().front().state ==
              project::ProjectExtensionServiceStateKind::RuntimeStopped);
    FGL_CHECK(runtimeHost.value().stats().successfulInvocations == 3U);
    runtimeModules.value().deactivate();

    fs::path buildManifest;
    auto buildProject = makeProjectFixture(FixtureModulePath, buildManifest, "build-step");
    auto buildModules = project::ProjectExtensionModules::load(pathText(buildManifest));
    FGL_CHECK(buildModules && buildModules.value().activate());
    Scene buildScene("Build service test");
    const auto buildManifestText = pathText(buildManifest);
    const auto buildRootText = pathText(buildManifest.parent_path());
    project::ProjectExtensionHostContext buildContext;
    buildContext.hostKind = project::ProjectExtensionHostKind::Studio;
    buildContext.projectManifestPath = buildManifestText.data();
    buildContext.projectManifestPathBytes = buildManifestText.size();
    buildContext.projectRoot = buildRootText.data();
    buildContext.projectRootBytes = buildRootText.size();
    buildContext.scene = &buildScene;
    auto buildHost = project::ProjectExtensionServiceHost::create(buildModules.value(), buildContext);
    FGL_CHECK(buildHost && buildHost.value().services().size() == 1U);
    const auto pre = buildHost.value().buildStep(buildContext, "pre-build", "pc", "Release",
                                                 false, 0);
    FGL_CHECK(pre.ok() && pre.succeeded == 1U);
    const auto post = buildHost.value().buildStep(buildContext, "post-build", "pc", "Release",
                                                  true, 0);
    FGL_CHECK(post.ok() && post.succeeded == 1U);
    const auto invalid = buildHost.value().buildStep(buildContext, "during-build", "pc",
                                                     "Release", false, 0);
    FGL_CHECK(!invalid.ok() && invalid.attempted == 0U);
    buildModules.value().deactivate();
}

FGL_TEST(project_extension_modules_safe_mode_never_loads_native_code_and_tamper_fails_closed) {
    fs::path projectManifest;
    auto temporary = makeProjectFixture(FixtureModulePath, projectManifest);

    project::ProjectExtensionLoadOptions safeOptions;
    safeOptions.safeMode = true;
    auto safe = project::ProjectExtensionModules::load(pathText(projectManifest), safeOptions);
    FGL_CHECK(safe);
    FGL_CHECK(safe.value().stats().loadedModules == 0U);
    FGL_CHECK(safe.value().stats().skippedDisabledEntries == 2U);
    FGL_CHECK(safe.value().activate());
    FGL_CHECK(!safe.value().invoke("test.binary-extension/fixture", "fixture.echo",
                                   {"ignored", "ignored", nullptr}));
    safe.value().deactivate();

    const auto installed = temporary.path / "Project" / "Packages" /
                           "test.binary-extension" / "bin" / FixtureModulePath.filename();
    std::ofstream tamper(installed, std::ios::binary | std::ios::app);
    FGL_CHECK(tamper.is_open());
    tamper.put('\0');
    tamper.close();
    auto rejected = project::ProjectExtensionModules::load(pathText(projectManifest));
    FGL_CHECK(!rejected && rejected.error().code() == ErrorCode::InvalidState);
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "expected one extension module path\n";
        return EXIT_FAILURE;
    }
    FixtureModulePath = fs::path(argv[1]);
    std::size_t passed = 0U;
    std::size_t failed = 0U;
    for (const auto& test : fabgl::tests::registry()) {
        try {
            test.function();
            ++passed;
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& exception) {
            ++failed;
            std::cerr << "[FAIL] " << test.name << ": " << exception.what() << '\n';
        } catch (...) {
            ++failed;
            std::cerr << "[FAIL] " << test.name << ": unknown exception\n";
        }
    }
    std::cout << "Executed " << (passed + failed) << " tests: " << passed << " passed, "
              << failed << " failed.\n";
    return failed == 0U ? EXIT_SUCCESS : EXIT_FAILURE;
}
