#include "demo.h"

#if FGL_HAS_NATIVE_WINDOW
#include "win32_audio.h"
#include "win32_window.h"
#endif

#include "project_format.h"
#include "project_input_map.h"
#include "utf8_arguments.h"

#include <fabgl/assets/file_io.h>
#include <fabgl/audio/audio_mixer.h>
#include <fabgl/frameworks/scene_gameplay.h>
#include <fabgl/input/input_map.h>
#include <fabgl/project/project_asset_streaming.h>
#include <fabgl/project/project_extension_modules.h>
#include <fabgl/project/project_extension_service_host.h>
#include <fabgl/project/project_scene_audio.h>
#include <fabgl/project/project_script_modules.h>
#include <fabgl/project/project_visual_host.h>
#include <fabgl/rendering/framebuffer.h>
#include <fabgl/rendering/scene_presenter.h>
#include <fabgl/runtime/engine_loop.h>
#include <fabgl/runtime/scene_runtime.h>
#include <fabgl/scene/scene.h>
#include <fabgl/serialization/scene_serializer.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

struct Options final {
    std::optional<fabgl::player::DemoKind> demo;
    std::string projectPath;
    std::size_t frames = 180U;
    bool headless = false;
    std::string output;
    std::string replayInputPath;
    std::string recordInputPath;
    std::vector<std::string> scriptModulePaths;
    bool safeMode = false;
    bool pluginsEnabled = true;
};

struct LoadedProject final {
    fabgl::project::Manifest manifest;
    std::unique_ptr<fabgl::Scene> scene;
    std::string root;
};

[[nodiscard]] std::string describeError(const fabgl::Error& error) {
    std::string description = error.message();
    for (const auto& item : error.context()) {
        description += " [" + item.key + '=' + item.value + ']';
    }
    return description;
}

[[nodiscard]] std::string joinPath(const std::string& left, const std::string& right) {
    if (left.empty()) {
        return right;
    }
    const auto separator = left.back() == '/' || left.back() == '\\' ? "" : "/";
    return left + separator + right;
}

[[nodiscard]] std::string parentPath(const std::string& path) {
    const auto separator = path.find_last_of("/\\");
    return separator == std::string::npos ? "." : path.substr(0U, separator);
}

[[nodiscard]] LoadedProject loadProject(const std::string& path) {
    auto source = fabgl::assets::readTextFile(path);
    if (!source) {
        throw std::runtime_error("cannot read project: " + describeError(source.error()));
    }
    auto manifest = fabgl::project::parseManifest(source.value());
    if (!manifest) {
        throw std::runtime_error("cannot parse project: " + describeError(manifest.error()));
    }

    const auto projectRoot = joinPath(parentPath(path), manifest.value().projectRoot);
    const auto scenePath = joinPath(projectRoot, manifest.value().startupScene);
    auto sceneSource = fabgl::assets::readTextFile(scenePath);
    if (!sceneSource) {
        throw std::runtime_error("cannot read startup scene: " +
                                 describeError(sceneSource.error()));
    }
    auto scene = fabgl::SceneSerializer::deserialize(sceneSource.value());
    if (!scene) {
        throw std::runtime_error("cannot parse startup scene: " + describeError(scene.error()));
    }
    return {std::move(manifest.value()), std::move(scene.value()), projectRoot};
}

[[nodiscard]] Options parseOptions(const std::vector<std::string>& arguments) {
    Options options;
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        const auto& argument = arguments[index];
        if (argument == "--help") {
            std::cout << "Usage: fabgl_player_pc [--project Game.fglproject] [--demo "
                         "empty|2d|topdown|raycast|racer|lowpoly|ui|audio|animation|streaming] "
                         "[--headless] [--frames N] [--replay-input run.fglreplay] "
                         "[--record-input run.fglreplay] [--script-module gameplay.dll] "
                         "[--safe-mode|--disable-plugins] [--output frame.ppm]\n";
            std::exit(EXIT_SUCCESS);
        }
        if (argument == "--safe-mode") {
            options.safeMode = true;
            options.pluginsEnabled = false;
            continue;
        }
        if (argument == "--disable-plugins") {
            options.pluginsEnabled = false;
            continue;
        }
        if (argument == "--headless") {
            options.headless = true;
            continue;
        }
        if (argument == "--project" && index + 1U < arguments.size()) {
            options.projectPath = arguments[++index];
            continue;
        }
        if (argument == "--demo" && index + 1U < arguments.size()) {
            options.demo = fabgl::player::parseDemoKind(arguments[++index]);
            continue;
        }
        if (argument == "--frames" && index + 1U < arguments.size()) {
            options.frames = static_cast<std::size_t>(std::stoull(arguments[++index]));
            if (options.frames < 1 || options.frames > 36000) {
                throw std::invalid_argument("--frames must be between 1 and 36000");
            }
            continue;
        }
        if (argument == "--output" && index + 1U < arguments.size()) {
            options.output = arguments[++index];
            continue;
        }
        if (argument == "--replay-input" && index + 1U < arguments.size()) {
            options.replayInputPath = arguments[++index];
            continue;
        }
        if (argument == "--record-input" && index + 1U < arguments.size()) {
            options.recordInputPath = arguments[++index];
            continue;
        }
        if (argument == "--script-module" && index + 1U < arguments.size()) {
            options.scriptModulePaths.push_back(arguments[++index]);
            continue;
        }
        throw std::invalid_argument("unknown or incomplete argument: " + argument);
    }
    if (!options.replayInputPath.empty() && !options.recordInputPath.empty()) {
        throw std::invalid_argument("--replay-input and --record-input are mutually exclusive");
    }
    return options;
}

[[nodiscard]] bool validBit(const int value) noexcept {
    return value == 0 || value == 1;
}

[[nodiscard]] bool validControlName(const std::string_view name) noexcept {
    if (name.empty() || name.size() > 64U)
        return false;
    return std::all_of(name.begin(), name.end(), [](const unsigned char value) {
        return std::isalnum(value) != 0 || value == '.' || value == '_' || value == '-';
    });
}

[[nodiscard]] std::vector<fabgl::player::InputState> parseReplay(std::string_view source) {
    std::istringstream stream{std::string(source)};
    std::string magic;
    int version = 0;
    if (!(stream >> magic >> version) || magic != "fglreplay" || (version != 1 && version != 2)) {
        throw std::runtime_error("input replay has an invalid header");
    }
    std::vector<fabgl::player::InputState> frames;
    std::string marker;
    while (stream >> marker) {
        if (marker == "end") {
            stream >> std::ws;
            if (!stream.eof()) {
                throw std::runtime_error("input replay contains data after end");
            }
            if (frames.empty()) {
                throw std::runtime_error("input replay contains no frames");
            }
            return frames;
        }
        std::size_t frameIndex = 0U;
        int left = 0;
        int right = 0;
        int forward = 0;
        int backward = 0;
        int action = 0;
        if (marker != "frame" ||
            !(stream >> frameIndex >> left >> right >> forward >> backward >> action) ||
            frameIndex != frames.size() || !validBit(left) || !validBit(right) ||
            !validBit(forward) || !validBit(backward) || !validBit(action)) {
            throw std::runtime_error("input replay contains an invalid frame record");
        }
        fabgl::player::InputState frame;
        frame.left = left != 0;
        frame.right = right != 0;
        frame.forward = forward != 0;
        frame.backward = backward != 0;
        frame.action = action != 0;
        if (version == 2) {
            std::size_t controlCount = 0U;
            if (!(stream >> controlCount) || controlCount > 128U)
                throw std::runtime_error("input replay control count exceeds limits");
            for (std::size_t index = 0U; index < controlCount; ++index) {
                std::string control;
                float value = 0.0F;
                if (!(stream >> std::quoted(control) >> value) || !validControlName(control) ||
                    !std::isfinite(value) || value < -1.0F || value > 1.0F ||
                    !frame.controls.emplace(std::move(control), value).second) {
                    throw std::runtime_error("input replay contains an invalid control");
                }
            }
        }
        frames.push_back(std::move(frame));
    }
    throw std::runtime_error("input replay is missing end marker");
}

[[nodiscard]] std::vector<fabgl::player::InputState> loadReplay(const std::string& path) {
    auto source = fabgl::assets::readTextFile(path);
    if (!source) {
        throw std::runtime_error("cannot read input replay: " + describeError(source.error()));
    }
    return parseReplay(source.value());
}

void saveReplay(const std::string& path, const std::vector<fabgl::player::InputState>& frames) {
    std::ostringstream stream;
    stream << "fglreplay 2\n" << std::setprecision(9);
    for (std::size_t index = 0; index < frames.size(); ++index) {
        const auto& frame = frames[index];
        stream << "frame " << index << ' ' << (frame.left ? 1 : 0) << ' ' << (frame.right ? 1 : 0)
               << ' ' << (frame.forward ? 1 : 0) << ' ' << (frame.backward ? 1 : 0) << ' '
               << (frame.action ? 1 : 0) << ' ' << frame.controls.size();
        for (const auto& [control, value] : frame.controls)
            stream << ' ' << std::quoted(control) << ' ' << value;
        stream << '\n';
    }
    stream << "end\n";
    const auto text = stream.str();
    const std::vector<std::uint8_t> bytes(text.begin(), text.end());
    auto saved = fabgl::assets::writeBinaryFileAtomic(path, bytes);
    if (!saved) {
        throw std::runtime_error("cannot write input replay: " + describeError(saved.error()));
    }
}

[[nodiscard]] fabgl::Result<void> success() {
    return fabgl::Result<void>::success();
}

struct AudioMixEvidence final {
    std::uint64_t checksum = 1469598103934665603ULL;
    std::uint64_t samples = 0U;
    std::uint64_t nonZeroSamples = 0U;

    void record(const float* interleavedStereo, const std::size_t sampleCount) noexcept {
        for (std::size_t index = 0U; index < sampleCount; ++index) {
            const auto normalized = std::clamp(interleavedStereo[index], -1.0F, 1.0F);
            const auto quantized =
                static_cast<std::int16_t>(std::lround(static_cast<double>(normalized) * 32767.0));
            nonZeroSamples += quantized != 0 ? 1U : 0U;
            const auto bits = static_cast<std::uint16_t>(quantized);
            checksum ^= static_cast<std::uint8_t>(bits & 0xFFU);
            checksum *= 1099511628211ULL;
            checksum ^= static_cast<std::uint8_t>(bits >> 8U);
            checksum *= 1099511628211ULL;
        }
        samples += sampleCount;
    }
};

void requireFrame(const fabgl::Result<fabgl::FrameMetrics>& frame) {
    if (!frame) {
        throw std::runtime_error("engine loop failed: " + describeError(frame.error()));
    }
}

void reportExtensionServiceFailures(
    const std::string_view phase,
    const fabgl::project::ProjectExtensionDispatchReport& report) {
    for (const auto& failure : report.failures) {
        std::cerr << "fabgl_player_pc: extension service disabled during " << phase << ": "
                  << failure.qualifiedServiceId << ": " << describeError(failure.error) << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto arguments = fabgl::tools::utf8Arguments(argc, argv);
        const auto options = parseOptions(arguments);
        const auto replayFrames = options.replayInputPath.empty()
                                      ? std::vector<fabgl::player::InputState>{}
                                      : loadReplay(options.replayInputPath);
        if (!replayFrames.empty() && replayFrames.size() < options.frames) {
            throw std::invalid_argument("input replay has fewer frames than --frames");
        }
        std::vector<fabgl::player::InputState> recordedFrames;
        if (!options.recordInputPath.empty()) {
            recordedFrames.reserve(options.frames);
        }

        std::optional<LoadedProject> loadedProject;
        if (!options.projectPath.empty()) {
            loadedProject = loadProject(options.projectPath);
        }

        // Keep trusted package modules resident until every scene/runtime object has shut down
        // and been destroyed. Safe mode and --disable-plugins still validate canonical package
        // state, but ProjectExtensionModules guarantees that no native library is opened.
        std::optional<fabgl::project::ProjectExtensionModules> extensionModules;
        if (loadedProject) {
            fabgl::project::ProjectExtensionLoadOptions extensionOptions;
            extensionOptions.safeMode = options.safeMode;
            extensionOptions.extensionsEnabled = options.pluginsEnabled;
            extensionOptions.acceptedKinds = {
                fabgl::PackageEntryPointKind::RuntimeModule,
                fabgl::PackageEntryPointKind::RendererExtension,
                fabgl::PackageEntryPointKind::Framework,
            };
            auto loadedExtensions = fabgl::project::ProjectExtensionModules::load(
                options.projectPath, extensionOptions);
            if (!loadedExtensions) {
                throw std::runtime_error("cannot load trusted project extensions: " +
                                         describeError(loadedExtensions.error()));
            }
            extensionModules.emplace(std::move(loadedExtensions.value()));
            auto activated = extensionModules->activate();
            if (!activated) {
                throw std::runtime_error("cannot activate trusted project extensions: " +
                                         describeError(activated.error()));
            }
        }

        std::optional<fabgl::project::ProjectScriptModules> scriptModules;
        auto scene = loadedProject ? std::move(loadedProject->scene)
                                   : std::make_unique<fabgl::Scene>("Built-in Demo");
        const auto sceneEntities = scene->entities();
        const auto requiresNativeScripts = std::any_of(
            sceneEntities.begin(), sceneEntities.end(), [](const fabgl::Entity* entity) {
                const auto components = entity->components();
                return std::any_of(components.begin(), components.end(), [](const auto* component) {
                    return component->typeName() == "fabgl.ScriptComponent";
                });
            });
        if (requiresNativeScripts && options.scriptModulePaths.empty()) {
            throw std::runtime_error(
                "project scene requires native gameplay code; pass --script-module with the "
                "trusted output from build_project_scripts.ps1");
        }
        if (!options.scriptModulePaths.empty()) {
            if (!loadedProject) {
                throw std::invalid_argument("--script-module requires --project");
            }
            auto loadedModules =
                fabgl::project::ProjectScriptModules::load(options.scriptModulePaths);
            if (!loadedModules) {
                throw std::runtime_error("cannot load native gameplay module: " +
                                         describeError(loadedModules.error()));
            }
            scriptModules.emplace(std::move(loadedModules.value()));
            auto attached = scriptModules->attach(*scene);
            if (!attached) {
                throw std::runtime_error("cannot attach native gameplay scripts: " +
                                         describeError(attached.error()));
            }
        }
        // Project playback is selected from serialized scene components. The
        // legacy previewDemo hint is retained only for migration/UI display and
        // never decides which renderer or gameplay implementation executes.
        auto demoKind = loadedProject
                            ? fabgl::player::DemoKind::Empty
                            : options.demo.value_or(fabgl::player::DemoKind::Platformer2D);

        fabgl::rendering::Framebuffer framebuffer(320, 180);
        std::optional<fabgl::player::Demo> demo;
        std::optional<fabgl::project::ProjectAssetStreamingRuntime> projectAssetLibrary;
        std::vector<fabgl::AssetGuid> sceneAssetRoots;
        bool evictAssetsAfterTransition = false;
        std::optional<fabgl::rendering::ScenePresenter> scenePresenter;
        if (loadedProject) {
            auto roots = fabgl::project::ProjectAssetStreamingRuntime::collectSceneRoots(
                *scene, loadedProject->manifest);
            if (!roots) {
                throw std::runtime_error("cannot collect project runtime asset roots: " +
                                         describeError(roots.error()));
            }
            sceneAssetRoots = std::move(roots.value());
            auto assets = fabgl::project::ProjectAssetStreamingRuntime::create(
                loadedProject->root, loadedProject->manifest);
            if (!assets) {
                throw std::runtime_error("cannot load project runtime assets: " +
                                         describeError(assets.error()));
            }
            projectAssetLibrary.emplace(std::move(assets.value()));
            scenePresenter.emplace(framebuffer, projectAssetLibrary->resources());
        } else {
            demo.emplace(framebuffer, demoKind);
            demo->bindScene(*scene);
        }
        fabgl::player::InputState input;
        fabgl::InputMap inputMap;
        if (loadedProject) {
            auto projectInput = fabgl::project::buildInputMap(loadedProject->manifest);
            if (!projectInput) {
                throw std::runtime_error("cannot build project input map: " +
                                         describeError(projectInput.error()));
            }
            inputMap = std::move(projectInput.value());
        } else {
            const auto inputContext = inputMap.defineContext("Gameplay", 100);
            if (!inputContext) {
                throw std::runtime_error("cannot initialize input map: " +
                                         describeError(inputContext.error()));
            }
            for (const auto& binding : std::vector<std::pair<std::string, fabgl::InputBinding>>{
                     {"MoveX", {"left", -1.0F, 0.5F}},
                     {"MoveX", {"right", 1.0F, 0.5F}},
                     {"MoveY", {"forward", 1.0F, 0.5F}},
                     {"MoveY", {"backward", -1.0F, 0.5F}},
                 }) {
                const auto bound = inputMap.bindAxis("Gameplay", binding.first, binding.second);
                if (!bound) {
                    throw std::runtime_error("cannot bind input axis: " +
                                             describeError(bound.error()));
                }
            }
            const auto actionBound =
                inputMap.bindAction("Gameplay", "Action", {"action", 1.0F, 0.5F});
            if (!actionBound) {
                throw std::runtime_error("cannot bind input action: " +
                                         describeError(actionBound.error()));
            }
        }

        constexpr std::uint32_t AudioSampleRate = 48'000U;
        fabgl::AudioMixerConfig audioConfig;
        audioConfig.outputSampleRate = AudioSampleRate;
        audioConfig.maximumVoices = 8U;
        audioConfig.maximumBuses = 8U;
        audioConfig.mixBlockFrames = 4096U;
        fabgl::AudioMixer audioMixer(audioConfig);
        for (const auto bus :
             {fabgl::AudioBusId{1U}, fabgl::AudioBusId{2U}, fabgl::AudioBusId{3U}}) {
            const auto created = audioMixer.createBus(bus);
            if (!created) {
                throw std::runtime_error("cannot initialize audio buses: " +
                                         describeError(created.error()));
            }
        }
        std::vector<float> audioDiscard(audioConfig.mixBlockFrames * 2U);
        AudioMixEvidence audioEvidence;
        std::vector<float> showcaseTone;
        if (!loadedProject && demoKind == fabgl::player::DemoKind::AudioShowcase) {
            showcaseTone.resize(AudioSampleRate / 5U);
            constexpr double TwoPi = 6.28318530717958647692;
            for (std::size_t index = 0; index < showcaseTone.size(); ++index) {
                const auto phase = TwoPi * 440.0 * static_cast<double>(index) /
                                   static_cast<double>(AudioSampleRate);
                showcaseTone[index] = static_cast<float>(std::sin(phase) * 0.18);
            }
            fabgl::AudioVoiceSettings toneSettings;
            toneSettings.bus = {2U};
            toneSettings.loop = true;
            const auto voice = audioMixer.play(
                {showcaseTone.data(), showcaseTone.size(), 1U, AudioSampleRate}, toneSettings);
            if (!voice) {
                throw std::runtime_error("cannot start audio showcase: " +
                                         describeError(voice.error()));
            }
        }

        fabgl::IAudioOutputBackend* audioOutput = nullptr;

#if FGL_HAS_NATIVE_WINDOW
        fabgl::player::Win32Window window;
        fabgl::player::Win32AudioOutput nativeAudio;
        fabgl::player::Win32Window* nativeWindow = nullptr;
        if (!options.headless) {
            std::string error;
            const auto windowTitle = loadedProject ? loadedProject->manifest.name : demo->title();
            if (!window.open(framebuffer.width(), framebuffer.height(), 3, windowTitle, error)) {
                std::cerr << error << '\n';
                return EXIT_FAILURE;
            }
            nativeWindow = &window;
            if (!nativeAudio.open(AudioSampleRate, error)) {
                std::cerr << "warning: audio output disabled: " << error << '\n';
            } else {
                audioOutput = &nativeAudio;
            }
        }
#endif

        fabgl::SceneRuntimeConfig sceneRuntimeConfig;
        if (loadedProject) {
            const auto projectDirectory = loadedProject->root;
            const auto projectAssets = loadedProject->manifest.assets;
            sceneRuntimeConfig.visualGraphSourceResolver = [projectDirectory, projectAssets](
                                                               const fabgl::AssetGuid requested) {
                const auto asset =
                    std::find_if(projectAssets.begin(), projectAssets.end(),
                                 [requested](const fabgl::project::ProjectAssetEntry& candidate) {
                                     return candidate.guid == requested;
                                 });
                if (asset == projectAssets.end()) {
                    return fabgl::Result<std::string>::failure(
                        fabgl::Error(fabgl::ErrorCode::NotFound,
                                     "visual graph GUID is not in the project asset table")
                            .addContext("asset", requested.toString()));
                }
                if (asset->type != "visual.script" ||
                    !fabgl::assets::isSafeRelativePath(asset->path)) {
                    return fabgl::Result<std::string>::failure(
                        fabgl::Error(fabgl::ErrorCode::InvalidFormat,
                                     "visual graph asset mapping is unsafe or has the wrong type")
                            .addContext("asset", requested.toString())
                            .addContext("type", asset->type)
                            .addContext("path", asset->path));
                }
                return fabgl::assets::readTextFile(joinPath(projectDirectory, asset->path));
            };
            sceneRuntimeConfig.visualReferences.assetExists =
                [projectAssets](const fabgl::AssetGuid requested) {
                    return std::any_of(
                        projectAssets.begin(), projectAssets.end(),
                        [requested](const fabgl::project::ProjectAssetEntry& candidate) {
                            return candidate.guid == requested;
                        });
                };
            sceneRuntimeConfig.animatorFactory =
                [&projectAssetLibrary](const fabgl::AssetGuid controller) {
                    return projectAssetLibrary->createAnimator(controller);
                };
        }
        std::optional<fabgl::project::ProjectVisualHost> visualHost;
        if (loadedProject) {
            fabgl::project::ProjectVisualHostServices services;
            services.scene = scene.get();
            services.input = &inputMap;
            services.audio = &audioMixer;
            services.audioClipResolver = [&projectAssetLibrary](const fabgl::AssetGuid clip) {
                return projectAssetLibrary->audioClip(clip);
            };
            auto created = fabgl::project::ProjectVisualHost::create(std::move(services));
            if (!created) {
                throw std::runtime_error("cannot create project visual host: " +
                                         describeError(created.error()));
            }
            visualHost.emplace(std::move(created).value());
            sceneRuntimeConfig.visualHostCallbacks = visualHost->callbacks();
        }
        fabgl::SceneRuntime sceneRuntime(*scene, std::move(sceneRuntimeConfig));
        if (visualHost) {
            auto bound = visualHost->bindRuntime(sceneRuntime);
            if (!bound) {
                throw std::runtime_error("cannot bind project visual host: " +
                                         describeError(bound.error()));
            }
        }
        fabgl::frameworks::SceneGameplayRuntime gameplayRuntime(*scene);
        std::optional<fabgl::project::ProjectSceneAudioRuntime> projectAudioRuntime;
        if (loadedProject) {
            projectAudioRuntime.emplace(
                *scene,
                [&projectAssetLibrary](const fabgl::AssetGuid clip) {
                    return projectAssetLibrary->audioClip(clip);
                },
                audioMixer);
        }
        std::optional<fabgl::project::ProjectExtensionHostContext> extensionHostContext;
        std::optional<fabgl::project::ProjectExtensionServiceHost> extensionServices;
        if (extensionModules) {
            auto& context = extensionHostContext.emplace();
            context.hostKind = fabgl::project::ProjectExtensionHostKind::Player;
            context.projectManifestPath = options.projectPath.data();
            context.projectManifestPathBytes = options.projectPath.size();
            context.projectRoot = loadedProject->root.data();
            context.projectRootBytes = loadedProject->root.size();
            context.scene = scene.get();
            context.sceneRuntime = &sceneRuntime;
            auto services = fabgl::project::ProjectExtensionServiceHost::create(
                *extensionModules, context);
            if (!services) {
                extensionModules->deactivate();
                throw std::runtime_error("project extension service registration failed: " +
                                         describeError(services.error()));
            }
            extensionServices.emplace(std::move(services.value()));
            auto opened = extensionModules->invokeAll(
                fabgl::project::ProjectOpenExtensionCapability,
                {"open", "player", &context});
            if (!opened) {
                extensionModules->deactivate();
                throw std::runtime_error("project extension open hook failed: " +
                                         describeError(opened.error()));
            }
        }
        fabgl::EngineLoop engineLoop;
        fabgl::EngineLoopCallbacks callbacks;
        float elapsedSeconds = 0.0F;
        std::uint64_t animationSamples = 0U;
        std::uint64_t animationChangedSamples = 0U;
        std::size_t finalAnimatorCount = 0U;
        fabgl::rendering::ScenePresentationStats presentationStats;
        callbacks.initialize = success;
        callbacks.loadResources = [&projectAssetLibrary, &sceneAssetRoots]() {
            return projectAssetLibrary
                       ? projectAssetLibrary->loadTransitionBlocking(sceneAssetRoots)
                       : success();
        };
        callbacks.loadScene = [&scene, &sceneRuntime, &gameplayRuntime, &projectAudioRuntime]() {
            auto started = scene->start();
            if (!started)
                return started;
            auto runtime = sceneRuntime.initialize();
            if (!runtime)
                return runtime;
            auto gameplay = gameplayRuntime.initialize();
            if (!gameplay)
                return gameplay;
            return projectAudioRuntime ? projectAudioRuntime->initialize() : success();
        };
        callbacks.fixedUpdate = [&scene](const double delta) {
            return scene->fixedUpdate(static_cast<float>(delta));
        };
        callbacks.physicsUpdate = [&sceneRuntime](const double delta) {
            return sceneRuntime.fixedUpdate(static_cast<float>(delta));
        };
        callbacks.variableUpdate = [&scene, &sceneRuntime, &demo, &input, &inputMap, &loadedProject,
                                    &projectAssetLibrary, &sceneAssetRoots,
                                    &evictAssetsAfterTransition,
                                    &elapsedSeconds](const double delta) {
            elapsedSeconds += static_cast<float>(delta);
            const std::array<std::pair<const char*, float>, 5U> legacyControls{{
                {"left", input.left ? 1.0F : 0.0F},
                {"right", input.right ? 1.0F : 0.0F},
                {"forward", input.forward ? 1.0F : 0.0F},
                {"backward", input.backward ? 1.0F : 0.0F},
                {"action", input.action ? 1.0F : 0.0F},
            }};
            for (const auto& control : legacyControls) {
                auto set = inputMap.setControlValue(control.first, control.second);
                if (!set) {
                    return set;
                }
            }
            if (input.controls.empty()) {
                const std::array<std::pair<const char*, float>, 10U> simulatedControls{{
                    {"Key.A", input.left ? 1.0F : 0.0F},
                    {"Key.Left", input.left ? 1.0F : 0.0F},
                    {"Key.D", input.right ? 1.0F : 0.0F},
                    {"Key.Right", input.right ? 1.0F : 0.0F},
                    {"Key.W", input.forward ? 1.0F : 0.0F},
                    {"Key.Up", input.forward ? 1.0F : 0.0F},
                    {"Key.S", input.backward ? 1.0F : 0.0F},
                    {"Key.Down", input.backward ? 1.0F : 0.0F},
                    {"Key.Space", input.action ? 1.0F : 0.0F},
                    {"Mouse.Left", input.action ? 1.0F : 0.0F},
                }};
                for (const auto& [control, value] : simulatedControls) {
                    auto set = inputMap.setControlValue(control, value);
                    if (!set)
                        return set;
                }
            } else {
                for (const auto& [control, value] : input.controls) {
                    auto set = inputMap.setControlValue(control, value);
                    if (!set)
                        return set;
                }
            }
            inputMap.update();
            // InputAction host nodes execute inside SceneRuntime::update and
            // must observe this frame's controls.
            auto updated = scene->update(static_cast<float>(delta));
            if (!updated) {
                return updated;
            }
            auto runtimeUpdated = sceneRuntime.update(static_cast<float>(delta));
            if (!runtimeUpdated) {
                return runtimeUpdated;
            }
            if (loadedProject) {
                if (projectAssetLibrary->transitionState() ==
                    fabgl::AssetTransitionState::Idle) {
                    if (inputMap.action("EvictChunk").pressed) {
                        auto begun = projectAssetLibrary->beginTransition({});
                        if (!begun)
                            return begun;
                        evictAssetsAfterTransition = true;
                    } else if (inputMap.action("LoadNextChunk").pressed) {
                        auto begun = projectAssetLibrary->beginTransition(sceneAssetRoots);
                        if (!begun)
                            return begun;
                    }
                }
                return success();
            }
            const auto horizontal = inputMap.axis("MoveX");
            const auto vertical = inputMap.axis("MoveY");
            const auto action = inputMap.action("Action");
            fabgl::player::InputState mappedInput;
            mappedInput.left = horizontal < -0.5F;
            mappedInput.right = horizontal > 0.5F;
            mappedInput.forward = vertical > 0.5F;
            mappedInput.backward = vertical < -0.5F;
            mappedInput.action = action.held;
            demo->update(static_cast<float>(delta), mappedInput);
            return success();
        };
        callbacks.aiUpdate = [&gameplayRuntime, &inputMap, &loadedProject](const double delta) {
            return loadedProject ? gameplayRuntime.update(inputMap, static_cast<float>(delta))
                                 : success();
        };
        callbacks.animationUpdate = [&scene, &sceneRuntime, &animationSamples,
                                     &animationChangedSamples](const double delta) {
            auto updated = scene->lateUpdate(static_cast<float>(delta));
            if (!updated)
                return updated;
            auto runtime = sceneRuntime.lateUpdate(static_cast<float>(delta));
            if (!runtime)
                return runtime;
            for (const auto* entity : scene->entities()) {
                const auto* frame = sceneRuntime.animationFrameFor(entity->id());
                if (frame == nullptr)
                    continue;
                ++animationSamples;
                animationChangedSamples +=
                    std::any_of(frame->values.begin(), frame->values.end(),
                                [](const auto& value) { return std::fabs(value.second) > 0.0001F; })
                        ? 1U
                        : 0U;
            }
            return success();
        };
        callbacks.audioUpdate = [&audioMixer, &audioDiscard, &audioEvidence,
                                 audioOutput](const double delta) {
            const auto requested = std::llround(static_cast<double>(AudioSampleRate) * delta);
            const auto bounded = std::clamp(requested, 1LL, 4096LL);
            const auto frames = static_cast<std::size_t>(bounded);
            if (audioOutput != nullptr)
                return audioMixer.render(frames, *audioOutput);
            auto mixed = audioMixer.mixTo(audioDiscard.data(), frames);
            if (mixed)
                audioEvidence.record(audioDiscard.data(), frames * 2U);
            return mixed;
        };
        callbacks.assetStreamingUpdate = [&projectAssetLibrary,
                                          &evictAssetsAfterTransition](double) {
            if (!projectAssetLibrary)
                return success();
            auto updated = projectAssetLibrary->update();
            if (!updated)
                return fabgl::Result<void>::failure(updated.error());
            if (evictAssetsAfterTransition &&
                projectAssetLibrary->transitionState() ==
                    fabgl::AssetTransitionState::Idle) {
                static_cast<void>(projectAssetLibrary->evictUnused());
                evictAssetsAfterTransition = false;
            }
            return success();
        };
        callbacks.renderSubmission = [&extensionServices,
                                      &extensionHostContext](const double delta) {
            if (extensionServices) {
                const auto report = extensionServices->runtimeUpdate(
                    *extensionHostContext, delta);
                reportExtensionServiceFailures("runtime update", report);
            }
            return success();
        };
        callbacks.render = [&demo, &scenePresenter, &scene, &sceneRuntime, &elapsedSeconds,
                            &presentationStats]() {
            if (scenePresenter) {
                presentationStats = scenePresenter->render(*scene, &sceneRuntime, elapsedSeconds);
            } else {
                demo->render();
            }
            return success();
        };
        callbacks.present = [&framebuffer
#if FGL_HAS_NATIVE_WINDOW
                             ,
                             nativeWindow
#endif
        ]() {
#if FGL_HAS_NATIVE_WINDOW
            if (nativeWindow != nullptr) {
                nativeWindow->present(framebuffer);
            }
#else
            static_cast<void>(framebuffer);
#endif
            return success();
        };
        callbacks.shutdown = [&scene, &sceneRuntime, &gameplayRuntime, &projectAudioRuntime,
                              &visualHost, &finalAnimatorCount]() {
            finalAnimatorCount = sceneRuntime.animatorCount();
            if (projectAudioRuntime)
                projectAudioRuntime->shutdown();
            gameplayRuntime.shutdown();
            sceneRuntime.shutdown();
            if (visualHost)
                visualHost->unbindRuntime();
            scene->shutdown();
        };
        engineLoop.setCallbacks(std::move(callbacks));
        const auto initialized = engineLoop.initialize();
        if (!initialized) {
            if (extensionModules) {
                static_cast<void>(extensionModules->invokeAll(
                    fabgl::project::ProjectCloseExtensionCapability,
                    {"close", "initialization-failed", &*extensionHostContext}));
                extensionModules->deactivate();
            }
            throw std::runtime_error("engine initialization failed: " +
                                     describeError(initialized.error()));
        }
        if (extensionModules) {
            auto started = extensionModules->invokeAll(
                fabgl::project::RuntimeStartExtensionCapability,
                {"start", "player", &*extensionHostContext});
            if (!started) {
                engineLoop.shutdown();
                static_cast<void>(extensionModules->invokeAll(
                    fabgl::project::ProjectCloseExtensionCapability,
                    {"close", "runtime-start-failed", &*extensionHostContext}));
                extensionModules->deactivate();
                throw std::runtime_error("project extension runtime-start hook failed: " +
                                         describeError(started.error()));
            }
            const auto serviceStart =
                extensionServices->runtimeStart(*extensionHostContext, "player");
            reportExtensionServiceFailures("runtime startup", serviceStart);
        }

#if FGL_HAS_NATIVE_WINDOW
        if (nativeWindow != nullptr) {
            auto previous = std::chrono::steady_clock::now();
            while (nativeWindow->poll(input)) {
                const auto now = std::chrono::steady_clock::now();
                const auto delta = std::chrono::duration<double>(now - previous).count();
                previous = now;
                requireFrame(engineLoop.tick(delta));
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } else
#endif
        {
            input.forward = true;
            input.right = demoKind == fabgl::player::DemoKind::Racer;
            for (std::size_t frame = 0U; frame < options.frames; ++frame) {
                if (!replayFrames.empty()) {
                    input = replayFrames[frame];
                } else {
                    input.action = (frame % 90) == 4;
                }
                if (!options.recordInputPath.empty()) {
                    recordedFrames.push_back(input);
                }
                requireFrame(engineLoop.tick(1.0 / 60.0));
            }
        }
        std::optional<fabgl::Error> extensionShutdownError;
        if (extensionModules) {
            const auto serviceStop =
                extensionServices->runtimeStop(*extensionHostContext, "player");
            reportExtensionServiceFailures("runtime shutdown", serviceStop);
            auto stopped = extensionModules->invokeAll(
                fabgl::project::RuntimeStopExtensionCapability,
                {"stop", "player", &*extensionHostContext});
            if (!stopped) {
                extensionShutdownError = stopped.error();
            }
        }
        engineLoop.shutdown();
        if (extensionModules) {
            auto closed = extensionModules->invokeAll(
                fabgl::project::ProjectCloseExtensionCapability,
                {"close", "player", &*extensionHostContext});
            if (!closed && !extensionShutdownError) {
                extensionShutdownError = closed.error();
            }
            extensionModules->deactivate();
        }
        if (extensionShutdownError) {
            throw std::runtime_error("project extension shutdown hook failed: " +
                                     describeError(*extensionShutdownError));
        }

        if (!options.recordInputPath.empty()) {
            saveReplay(options.recordInputPath, recordedFrames);
        }

        if (!options.output.empty()) {
            std::string error;
            if (!framebuffer.savePpm(options.output, error)) {
                std::cerr << error << '\n';
                return EXIT_FAILURE;
            }
        }
        std::cout << (loadedProject ? loadedProject->manifest.name : demo->title());
        if (loadedProject) {
            const auto& gameplay = gameplayRuntime.stats();
            const auto streaming = projectAssetLibrary->stats();
            const auto visual = visualHost ? visualHost->stats()
                                           : fabgl::project::ProjectVisualHostStats{};
            std::cout << " project=\"" << loadedProject->manifest.name
                      << "\" entities=" << scene->entityCount()
                      << " visual_assets=" << projectAssetLibrary->residentAssetCount() << " mode="
                      << fabgl::rendering::scenePresentationModeName(presentationStats.mode)
                      << " draws=" << presentationStats.drawCalls
                      << " missing_assets=" << presentationStats.missingAssets << " native_scripts="
                      << (scriptModules ? scriptModules->stats().attachedComponents : 0U)
                      << " extension_modules="
                      << (extensionModules ? extensionModules->stats().loadedModules : 0U)
                      << " extensions="
                      << (extensionModules ? extensionModules->stats().registeredExtensions : 0U)
                      << " extension_services="
                      << (extensionServices ? extensionServices->stats().registeredServices : 0U)
                      << " extension_service_failures="
                      << (extensionServices ? extensionServices->stats().failedInvocations : 0U)
                      << " animation_clips="
                      << projectAssetLibrary->residentAssetCount("animation.clip")
                      << " animator_controllers="
                      << projectAssetLibrary->residentAssetCount("animation.controller")
                      << " animators=" << finalAnimatorCount
                      << " animation_samples=" << animationSamples
                      << " animation_changed_samples=" << animationChangedSamples
                      << " audio_clips=" << projectAssetLibrary->residentAssetCount("audio")
                      << " asset_resident_bytes=" << streaming.residentBytes
                      << " asset_stream_loads=" << streaming.loads
                      << " asset_stream_evictions=" << streaming.evictions
                      << " asset_stream_transitions=" << streaming.transitionsCommitted
                      << " visual_host_calls=" << visual.calls
                      << " visual_host_failures=" << visual.failures
                      << " audio_listeners="
                      << (projectAudioRuntime ? projectAudioRuntime->stats().listeners : 0U)
                      << " audio_sources="
                      << (projectAudioRuntime ? projectAudioRuntime->stats().sources : 0U)
                      << " audio_voices_started=" << audioMixer.stats().voicesStarted
                      << " audio_mixed_frames=" << audioMixer.stats().mixedFrames
                      << " audio_nonzero_samples=" << audioEvidence.nonZeroSamples
                      << " audio_checksum=" << audioEvidence.checksum
                      << " ui_widgets=" << presentationStats.uiWidgets
                      << " ui_glyphs=" << presentationStats.uiGlyphs
                      << " gameplay_updates=" << gameplay.updates
                      << " platformer_collectibles=" << gameplay.platformerCollectibles
                      << " platformer_damage=" << gameplay.platformerDamageEvents
                      << " platformer_checkpoints=" << gameplay.platformerCheckpoints
                      << " platformer_transitions=" << gameplay.platformerTransitions
                      << " platformer_health=" << gameplay.platformerHealth
                      << " topdown_shots=" << gameplay.topDownShots
                      << " topdown_hits=" << gameplay.topDownHits
                      << " topdown_pickups=" << gameplay.topDownPickups
                      << " topdown_enemies=" << gameplay.topDownEnemies
                      << " topdown_rooms=" << gameplay.topDownRoomTransitions
                      << " fps_shots=" << gameplay.fpsShots << " fps_hits=" << gameplay.fpsHits
                      << " fps_doors=" << gameplay.fpsDoorActivations
                      << " fps_pickups=" << gameplay.fpsPickups << " fps_keys=" << gameplay.fpsKeys
                      << " fps_secrets=" << gameplay.fpsSecrets
                      << " tps_shots=" << gameplay.tpsShots << " tps_hits=" << gameplay.tpsHits
                      << " tps_pickups=" << gameplay.tpsPickups
                      << " tps_targets=" << gameplay.tpsTargets
                      << " racer_opponents=" << gameplay.racerOpponents
                      << " racer_checkpoints=" << gameplay.racerCheckpointCrossings
                      << " racer_position=" << gameplay.racerPosition
                      << " racer_lap=" << gameplay.racerLap << " racer_gear=" << gameplay.racerGear
                      << " racer_speed_kph=" << gameplay.racerSpeedKph
                      << " racer_countdown=" << gameplay.racerCountdown
                      << " racer_finished=" << (gameplay.racerFinished ? 1 : 0);
        }
        std::cout << " checksum=" << framebuffer.checksum() << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "fabgl_player_pc: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
