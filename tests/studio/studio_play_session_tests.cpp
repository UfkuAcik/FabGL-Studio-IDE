#include "StudioPlaySession.h"

#include <fabgl/assets/file_io.h>
#include <fabgl/reflection/reflection.h>
#include <fabgl/scene/builtin_components.h>
#include <fabgl/scene/entity.h>
#include <fabgl/scene/scene.h>
#include <fabgl/serialization/scene_serializer.h>
#include <fabgl/visual/visual_graph.h>

#include <project_format.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string describe(const fabgl::Error& error) {
    std::string result = error.message();
    for (const auto& context : error.context()) {
        result += " [" + context.key + '=' + context.value + ']';
    }
    return result;
}

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Type> Type take(fabgl::Result<Type> result, const std::string_view operation) {
    if (!result) {
        throw std::runtime_error(std::string(operation) + ": " + describe(result.error()));
    }
    return std::move(result.value());
}

void take(fabgl::Result<void> result, const std::string_view operation) {
    if (!result) {
        throw std::runtime_error(std::string(operation) + ": " + describe(result.error()));
    }
}

[[nodiscard]] std::string pathText(const fs::path& path) {
    return path.generic_string();
}

[[nodiscard]] std::unique_ptr<fgl::studio::StudioPlaySession>
loadProjectSession(const fs::path& projectPath) {
    const auto source = take(fabgl::assets::readTextFile(pathText(projectPath)), "read manifest");
    auto manifest = take(fabgl::project::parseManifest(source), "parse manifest");
    const auto root = projectPath.parent_path() / fs::path(manifest.projectRoot);
    const auto sceneSource =
        take(fabgl::assets::readTextFile(pathText(root / fs::path(manifest.startupScene))),
             "read startup scene");
    auto scene = take(fabgl::SceneSerializer::deserialize(sceneSource), "deserialize scene");
    fgl::studio::StudioPlaySessionConfig config;
    config.projectRoot = pathText(root);
    config.manifest = std::move(manifest);
    return take(fgl::studio::StudioPlaySession::create(std::move(scene), std::move(config)),
                "create Studio Play session");
}

void animatorProjectInitializesAndAdvances(const fs::path& repositoryRoot) {
    auto session = loadProjectSession(repositoryRoot / "examples" / "animation_showcase" /
                                      "AnimationShowcase.fglproject");
    take(session->initialize(), "initialize animator session");
    require(session->stats().animators == 1U, "Studio Play did not create the project animator");
    take(session->tick(1.0 / 60.0), "advance animator session");
    bool sampled = false;
    for (const auto* entity : session->scene().entities()) {
        sampled = sampled || session->runtime().animationFrameFor(entity->id()) != nullptr;
    }
    require(sampled, "Studio Play animator produced no animation frame");
    session->shutdown();
}

void gameplayInputAndAudioMatchProjectRuntime(const fs::path& repositoryRoot) {
    auto gameplay =
        loadProjectSession(repositoryRoot / "examples" / "platformer" / "Platformer.fglproject");
    take(gameplay->initialize(), "initialize gameplay session");
    fabgl::Entity* player = nullptr;
    for (auto* entity : gameplay->scene().entities()) {
        if (entity->name() == "Player") {
            player = entity;
            break;
        }
    }
    require(player != nullptr, "platformer fixture has no Player entity");
    const float initialX = player->transform().localPosition().x;
    take(gameplay->setControlValue("Key.D", 1.0F), "set gameplay input");
    const auto gameplayFrame = take(gameplay->tick(1.0 / 60.0), "advance gameplay session");
    require(gameplay->stats().gameplayUpdates == 1U,
            "Studio Play did not update framework gameplay");
    require(gameplayFrame.aiCpuSeconds > 0.0,
            "framework gameplay was not measured in the EngineLoop AI phase");
    require(player->transform().localPosition().x > initialX,
            "manifest InputMap was not delivered to project gameplay");
    gameplay->shutdown();

    auto audio = loadProjectSession(repositoryRoot / "examples" / "audio_showcase" /
                                    "AudioShowcase.fglproject");
    take(audio->initialize(), "initialize audio session");
    require(audio->stats().activeAudioVoices > 0U,
            "Studio Play did not bind serialized AudioSource components");
    take(audio->tick(1.0 / 60.0), "advance audio session");
    require(audio->stats().mixedAudioFrames > 0U,
            "Studio Play did not advance the project audio mixer");
    audio->shutdown();
}

[[nodiscard]] fabgl::VisualNode visualNode(const fabgl::VisualBuiltinNodeType type,
                                           const fabgl::VisualNodeId id, std::string name) {
    return take(fabgl::VisualNodeRegistry::builtins().create(type, id, std::move(name)),
                "create visual node");
}

void writeText(const fs::path& path, const std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    require(static_cast<bool>(output), "could not write visual Studio Play fixture");
}

void visualScriptResolvesFromManifest() {
    const auto unique = fabgl::AssetGuid::generate().toString();
    const auto fixtureRoot = fs::temp_directory_path() / ("fabgl-studio-play-" + unique);
    struct Cleanup final {
        fs::path path;
        ~Cleanup() {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
    } cleanup{fixtureRoot};

    const auto graphGuid = fabgl::AssetGuid::fromStableName("tests.studio.play.visual");
    fabgl::VisualGraph graph;
    graph.setGuid(graphGuid);
    graph.setName("Studio Play visual graph");
    take(graph.addNode(visualNode(fabgl::VisualBuiltinNodeType::EventStart, 1U, "Start")),
         "add visual start");
    auto value = visualNode(fabgl::VisualBuiltinNodeType::NumberConstant, 2U, "Health value");
    value.numberValue = 42.0;
    take(graph.addNode(std::move(value)), "add visual constant");
    auto action = visualNode(fabgl::VisualBuiltinNodeType::ComponentAction, 3U, "Set health");
    action.callbackPayload = "set:current";
    action.componentReference =
        fabgl::ComponentTypeGuid::fromStableName("fabgl.component.Health.v1");
    take(graph.addNode(std::move(action)), "add visual component action");
    auto returnedValue = visualNode(fabgl::VisualBuiltinNodeType::NumberConstant, 4U, "Return value");
    returnedValue.numberValue = 1.0;
    take(graph.addNode(std::move(returnedValue)), "add visual return constant");
    take(graph.addNode(visualNode(fabgl::VisualBuiltinNodeType::FlowReturn, 5U, "Return")),
         "add visual return");
    take(graph.addEdge({1U, 1U, 3U, 1U}), "connect visual action flow");
    take(graph.addEdge({2U, 1U, 3U, 2U}), "connect visual action value");
    take(graph.addEdge({3U, 3U, 5U, 1U}), "connect visual return flow");
    take(graph.addEdge({4U, 1U, 5U, 2U}), "connect visual return value");
    graph.setEntryNode(1U);
    const auto graphSource = take(fabgl::serializeVisualGraph(graph), "serialize visual graph");
    writeText(fixtureRoot / "Assets" / "Main.fglvisual", graphSource);

    fabgl::ReflectionRegistry registry;
    take(fabgl::registerBuiltinComponentTypes(registry), "register builtins");
    auto scene = std::make_unique<fabgl::Scene>("Studio Play visual scene");
    auto* entity = take(scene->createEntity("Visual owner"), "create visual owner");
    const auto owner = entity->id();
    auto visual = take(fabgl::createBuiltinDataComponent(registry, "VisualScriptComponent"),
                       "create visual component");
    take(visual->set("graph", fabgl::PropertyValue(graphGuid)), "assign visual graph");
    take(entity->addComponent(std::move(visual)), "attach visual component");
    auto health = take(fabgl::createBuiltinDataComponent(registry, "Health"),
                       "create health component");
    auto* healthData = health.get();
    take(entity->addComponent(std::move(health)), "attach health component");

    fgl::studio::StudioPlaySessionConfig config;
    config.projectRoot = pathText(fixtureRoot);
    config.manifest.projectGuid =
        fabgl::AssetGuid::fromStableName("tests.studio.play.project").toString();
    config.manifest.name = "Studio Play Visual Fixture";
    config.manifest.assets.push_back({graphGuid, "Assets/Main.fglvisual", "visual.script"});
    auto session = take(fgl::studio::StudioPlaySession::create(std::move(scene), std::move(config)),
                        "create visual Studio Play session");
    take(session->initialize(), "initialize visual Studio Play session");
    require(session->stats().visualScripts == 1U,
            "Studio Play did not initialize VisualScriptComponent");
    require(std::get<std::int64_t>(healthData->get("current").value()) == 42,
            "Studio Play visual host did not mutate a reflected component");
    require(session->stats().visualHostCalls == 1U &&
                session->stats().visualHostFailures == 0U,
            "Studio Play did not route typed bytecode through the production visual host");
    take(session->tick(1.0 / 60.0), "advance visual Studio Play session");
    require(session->runtime().visualVariable(owner, "runtime.delta_seconds").has_value(),
            "Studio Play did not execute the visual update lifecycle");
    session->shutdown();
}

[[nodiscard]] std::unique_ptr<fabgl::Scene> nativeScriptScene() {
    fabgl::ReflectionRegistry registry;
    take(fabgl::registerBuiltinComponentTypes(registry), "register native fixture builtins");
    auto scene = std::make_unique<fabgl::Scene>("Studio Play native scene");
    auto* entity = take(scene->createEntity("Scripted"), "create native fixture entity");
    auto script = take(fabgl::createBuiltinDataComponent(registry, "ScriptComponent"),
                       "create native script placeholder");
    take(script->set("class", std::string("game.NativeTestScript")), "assign native script class");
    take(entity->addComponent(std::move(script)), "attach native script placeholder");
    return scene;
}

void nativeScriptModuleIsRequiredAndKeptAlive(const fs::path& repositoryRoot,
                                              const fs::path& modulePath) {
    fgl::studio::StudioPlaySessionConfig missingConfig;
    missingConfig.projectRoot = pathText(repositoryRoot);
    missingConfig.manifest.projectGuid =
        fabgl::AssetGuid::fromStableName("tests.studio.play.native-project").toString();
    missingConfig.manifest.name = "Studio Play Native Fixture";
    auto missing = fgl::studio::StudioPlaySession::create(nativeScriptScene(), missingConfig);
    require(!missing && missing.error().code() == fabgl::ErrorCode::InvalidState,
            "Studio Play accepted a native script placeholder without a verified module");

    auto config = std::move(missingConfig);
    config.nativeScriptModules.push_back(pathText(modulePath));
    auto session =
        take(fgl::studio::StudioPlaySession::create(nativeScriptScene(), std::move(config)),
             "create native Studio Play session");
    take(session->initialize(), "initialize native Studio Play session");
    take(session->tick(0.25), "advance native Studio Play session first frame");
    take(session->tick(0.25), "advance native Studio Play session second frame");
    require(session->stats().nativeScriptComponents == 1U,
            "Studio Play did not attach the verified native gameplay component");
    const auto entities = session->scene().entities();
    require(!entities.empty() &&
                std::fabs(entities.front()->transform().localPosition().x - 30.0F) < 0.0001F,
            "native gameplay module did not execute inside Studio Play");
    session->shutdown();
}

void run(const char* name, const std::function<void()>& test, std::size_t& passed) {
    test();
    ++passed;
    std::cout << "[PASS] " << name << '\n';
}

} // namespace

int main(const int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: studio_play_session_tests <repository-root> <native-module>\n";
        return EXIT_FAILURE;
    }
    std::size_t passed = 0U;
    try {
        const fs::path repositoryRoot(argv[1]);
        run(
            "animator project initializes and advances",
            [&]() { animatorProjectInitializesAndAdvances(repositoryRoot); }, passed);
        run(
            "gameplay input and audio match project runtime",
            [&]() { gameplayInputAndAudioMatchProjectRuntime(repositoryRoot); }, passed);
        run("visual script resolves from manifest", visualScriptResolvesFromManifest, passed);
        run(
            "native script module is required and kept alive",
            [&]() { nativeScriptModuleIsRequiredAndKeptAlive(repositoryRoot, fs::path(argv[2])); },
            passed);
    } catch (const std::exception& exception) {
        std::cerr << "[FAIL] after " << passed << " passing tests: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Executed " << passed << " Studio Play runtime parity tests.\n";
    return EXIT_SUCCESS;
}
