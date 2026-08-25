#include "StudioPlaySession.h"

#include <fabgl/assets/file_io.h>
#include <fabgl/frameworks/scene_gameplay.h>
#include <fabgl/scene/entity.h>
#include <fabgl/scene/scene.h>

#include <project_input_map.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace fgl::studio {
namespace {

constexpr std::uint32_t AudioSampleRate = 48'000U;
constexpr std::size_t AudioBlockFrames = 4096U;

[[nodiscard]] fabgl::AudioMixerConfig audioConfig() noexcept {
    fabgl::AudioMixerConfig result;
    result.outputSampleRate = AudioSampleRate;
    result.maximumVoices = 8U;
    result.maximumBuses = 8U;
    result.mixBlockFrames = AudioBlockFrames;
    return result;
}

[[nodiscard]] bool requiresNativeScripts(const fabgl::Scene& scene) {
    for (const auto* entity : scene.entities()) {
        for (const auto* component : entity->components()) {
            if (component->typeName() == "fabgl.ScriptComponent") {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] std::string joinPath(const std::string& left, const std::string& right) {
    return left + (left.empty() || left.back() == '/' || left.back() == '\\' ? "" : "/") + right;
}

[[nodiscard]] fabgl::Result<void> success() {
    return fabgl::Result<void>::success();
}

} // namespace

fabgl::Result<std::unique_ptr<StudioPlaySession>>
StudioPlaySession::create(std::unique_ptr<fabgl::Scene> scene, StudioPlaySessionConfig config) {
    if (scene == nullptr || config.projectRoot.empty()) {
        return fabgl::Result<std::unique_ptr<StudioPlaySession>>::failure(fabgl::Error(
            fabgl::ErrorCode::InvalidArgument, "Studio Play requires a scene and project root"));
    }

    const bool nativeScriptsRequired = requiresNativeScripts(*scene);
    if (nativeScriptsRequired && config.nativeScriptModules.empty()) {
        return fabgl::Result<std::unique_ptr<StudioPlaySession>>::failure(
            fabgl::Error(fabgl::ErrorCode::InvalidState,
                         "project scene requires a verified native gameplay module; build the "
                         "PC target before entering Studio Play"));
    }

    auto sceneAssetRoots =
        fabgl::project::ProjectAssetStreamingRuntime::collectSceneRoots(*scene, config.manifest);
    if (!sceneAssetRoots) {
        return fabgl::Result<std::unique_ptr<StudioPlaySession>>::failure(
            sceneAssetRoots.error().withContext("studioPlay", "scene_asset_roots"));
    }
    auto assets =
        fabgl::project::ProjectAssetStreamingRuntime::create(config.projectRoot, config.manifest);
    if (!assets) {
        return fabgl::Result<std::unique_ptr<StudioPlaySession>>::failure(
            assets.error().withContext("studioPlay", "project_assets"));
    }
    auto inputMap = fabgl::project::buildInputMap(config.manifest);
    if (!inputMap) {
        return fabgl::Result<std::unique_ptr<StudioPlaySession>>::failure(
            inputMap.error().withContext("studioPlay", "input_map"));
    }

    std::optional<fabgl::project::ProjectScriptModules> scriptModules;
    if (!config.nativeScriptModules.empty()) {
        auto loaded = fabgl::project::ProjectScriptModules::load(config.nativeScriptModules);
        if (!loaded) {
            return fabgl::Result<std::unique_ptr<StudioPlaySession>>::failure(
                loaded.error().withContext("studioPlay", "native_scripts"));
        }
        scriptModules.emplace(std::move(loaded.value()));
        auto attached = scriptModules->attach(*scene);
        if (!attached) {
            const auto error = attached.error().withContext("studioPlay", "native_script_attach");
            // attach() is transactional per component but can have replaced earlier placeholders
            // before a later class fails. Destroy those module-owned components while the DLLs
            // are still loaded.
            scene.reset();
            return fabgl::Result<std::unique_ptr<StudioPlaySession>>::failure(error);
        }
    }

    auto session = std::unique_ptr<StudioPlaySession>(
        new StudioPlaySession(std::move(scene), std::move(config), std::move(assets.value()),
                              std::move(sceneAssetRoots.value()), std::move(inputMap.value()),
                              std::move(scriptModules)));
    auto audio = session->configureAudioBuses();
    if (!audio) {
        return fabgl::Result<std::unique_ptr<StudioPlaySession>>::failure(audio.error());
    }
    auto visualRuntime = session->configureVisualRuntime();
    if (!visualRuntime) {
        return fabgl::Result<std::unique_ptr<StudioPlaySession>>::failure(
            visualRuntime.error().withContext("studioPlay", "visual_host"));
    }
    session->configureLoop();
    return fabgl::Result<std::unique_ptr<StudioPlaySession>>::success(std::move(session));
}

StudioPlaySession::StudioPlaySession(
    std::unique_ptr<fabgl::Scene> scene, StudioPlaySessionConfig config,
    fabgl::project::ProjectAssetStreamingRuntime assets,
    std::vector<fabgl::AssetGuid> sceneAssetRoots, fabgl::InputMap inputMap,
    std::optional<fabgl::project::ProjectScriptModules> scriptModules)
    : scriptModules_(std::move(scriptModules)), scene_(std::move(scene)),
      config_(std::move(config)), assets_(std::move(assets)),
      sceneAssetRoots_(std::move(sceneAssetRoots)), inputMap_(std::move(inputMap)),
      audioMixer_(audioConfig()),
      audioDiscard_(AudioBlockFrames * 2U) {
    gameplay_ = std::make_unique<fabgl::frameworks::SceneGameplayRuntime>(*scene_);
    projectAudio_ = std::make_unique<fabgl::project::ProjectSceneAudioRuntime>(
        *scene_, [this](const fabgl::AssetGuid clip) { return assets_.audioClip(clip); },
        audioMixer_);
}

fabgl::Result<void> StudioPlaySession::configureVisualRuntime() {
    fabgl::SceneRuntimeConfig runtimeConfig;
    const auto projectRoot = config_.projectRoot;
    const auto projectAssets = config_.manifest.assets;
    runtimeConfig.visualGraphSourceResolver = [projectRoot,
                                               projectAssets](const fabgl::AssetGuid requested) {
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
        if (asset->type != "visual.script" || !fabgl::assets::isSafeRelativePath(asset->path)) {
            return fabgl::Result<std::string>::failure(
                fabgl::Error(fabgl::ErrorCode::InvalidArgument,
                             "visual graph asset type or project-relative path is invalid")
                    .addContext("asset", requested.toString())
                    .addContext("assetPath", asset->path)
                    .addContext("assetType", asset->type));
        }
        return fabgl::assets::readTextFile(joinPath(projectRoot, asset->path));
    };
    runtimeConfig.visualReferences.assetExists = [projectAssets](const fabgl::AssetGuid requested) {
        return std::any_of(projectAssets.begin(), projectAssets.end(),
                           [requested](const fabgl::project::ProjectAssetEntry& candidate) {
                               return candidate.guid == requested;
                           });
    };
    runtimeConfig.animatorFactory = [this](const fabgl::AssetGuid controller) {
        return assets_.createAnimator(controller);
    };

    fabgl::project::ProjectVisualHostServices hostServices;
    hostServices.scene = scene_.get();
    hostServices.input = &inputMap_;
    hostServices.audio = &audioMixer_;
    hostServices.audioClipResolver =
        [this](const fabgl::AssetGuid clip) { return assets_.audioClip(clip); };
    auto host = fabgl::project::ProjectVisualHost::create(std::move(hostServices));
    if (!host)
        return fabgl::Result<void>::failure(host.error());
    visualHost_.emplace(std::move(host).value());
    runtimeConfig.visualHostCallbacks = visualHost_->callbacks();

    runtime_ = std::make_unique<fabgl::SceneRuntime>(*scene_, std::move(runtimeConfig));
    auto bound = visualHost_->bindRuntime(*runtime_);
    if (!bound) {
        runtime_.reset();
        visualHost_.reset();
        return bound;
    }
    return success();
}

StudioPlaySession::~StudioPlaySession() {
    shutdown();
}

fabgl::Result<void> StudioPlaySession::configureAudioBuses() {
    if (audioBusesConfigured_) {
        return success();
    }
    for (const auto bus : {fabgl::AudioBusId{1U}, fabgl::AudioBusId{2U}, fabgl::AudioBusId{3U}}) {
        auto created = audioMixer_.createBus(bus);
        if (!created) {
            return fabgl::Result<void>::failure(
                created.error().withContext("studioPlay", "audio_bus_setup"));
        }
    }
    audioBusesConfigured_ = true;
    return success();
}

void StudioPlaySession::configureLoop() {
    fabgl::EngineLoopCallbacks callbacks;
    callbacks.initialize = success;
    callbacks.loadResources = [this]() {
        return assets_.loadTransitionBlocking(sceneAssetRoots_);
    };
    callbacks.loadScene = [this]() {
        auto started = scene_->start();
        if (!started) {
            return started;
        }
        auto runtime = runtime_->initialize();
        if (!runtime) {
            return runtime;
        }
        auto gameplay = gameplay_->initialize();
        if (!gameplay) {
            return gameplay;
        }
        return projectAudio_->initialize();
    };
    callbacks.fixedUpdate = [this](const double delta) {
        return scene_->fixedUpdate(static_cast<float>(delta));
    };
    callbacks.physicsUpdate = [this](const double delta) {
        return runtime_->fixedUpdate(static_cast<float>(delta));
    };
    callbacks.variableUpdate = [this](const double delta) {
        // Host-value input nodes observe the controls submitted before this
        // tick, not the previous frame's resolved action state.
        inputMap_.update();
        auto updated = scene_->update(static_cast<float>(delta));
        if (!updated) {
            return updated;
        }
        updated = runtime_->update(static_cast<float>(delta));
        if (!updated) {
            return updated;
        }
        if (assets_.transitionState() == fabgl::AssetTransitionState::Idle) {
            if (inputMap_.action("EvictChunk").pressed) {
                auto begun = assets_.beginTransition({});
                if (!begun)
                    return begun;
                evictAfterTransition_ = true;
            } else if (inputMap_.action("LoadNextChunk").pressed) {
                auto begun = assets_.beginTransition(sceneAssetRoots_);
                if (!begun)
                    return begun;
            }
        }
        return success();
    };
    callbacks.aiUpdate = [this](const double delta) {
        return gameplay_->update(inputMap_, static_cast<float>(delta));
    };
    callbacks.animationUpdate = [this](const double delta) {
        auto updated = scene_->lateUpdate(static_cast<float>(delta));
        return updated ? runtime_->lateUpdate(static_cast<float>(delta)) : updated;
    };
    callbacks.audioUpdate = [this](const double delta) {
        const auto requested = std::llround(static_cast<double>(AudioSampleRate) * delta);
        const auto frames = static_cast<std::size_t>(std::clamp(requested, 1LL, 4096LL));
        if (config_.audioOutput != nullptr) {
            return audioMixer_.render(frames, *config_.audioOutput);
        }
        return audioMixer_.mixTo(audioDiscard_.data(), frames);
    };
    callbacks.assetStreamingUpdate = [this](double) {
        auto updated = assets_.update();
        if (!updated)
            return fabgl::Result<void>::failure(updated.error());
        if (evictAfterTransition_ &&
            assets_.transitionState() == fabgl::AssetTransitionState::Idle) {
            static_cast<void>(assets_.evictUnused());
            evictAfterTransition_ = false;
        }
        return success();
    };
    callbacks.renderSubmission = [](double) { return success(); };
    callbacks.render = success;
    callbacks.present = success;
    callbacks.shutdown = [this]() {
        projectAudio_->shutdown();
        gameplay_->shutdown();
        runtime_->shutdown();
        scene_->shutdown();
    };
    loop_.setCallbacks(std::move(callbacks));
}

fabgl::Result<void> StudioPlaySession::initialize() {
    return loop_.initialize();
}

fabgl::Result<fabgl::FrameMetrics> StudioPlaySession::tick(const double frameDeltaSeconds) {
    return loop_.tick(frameDeltaSeconds);
}

void StudioPlaySession::shutdown() noexcept {
    loop_.shutdown();
    if (visualHost_)
        visualHost_->unbindRuntime();
    audioMixer_.stopAll();
    inputMap_.clearControlValues();
}

fabgl::Result<void> StudioPlaySession::setControlValue(std::string control, const float value) {
    const auto alias = [&control]() -> const char* {
        if (control == "Key.A" || control == "Key.Left") {
            return "left";
        }
        if (control == "Key.D" || control == "Key.Right") {
            return "right";
        }
        if (control == "Key.W" || control == "Key.Up") {
            return "forward";
        }
        if (control == "Key.S" || control == "Key.Down") {
            return "backward";
        }
        if (control == "Key.Space" || control == "Mouse.Left") {
            return "action";
        }
        return nullptr;
    }();
    auto changed = inputMap_.setControlValue(std::move(control), value);
    if (!changed || alias == nullptr) {
        return changed;
    }
    return inputMap_.setControlValue(alias, value);
}

void StudioPlaySession::clearTransientControls() noexcept {
    for (const auto* control : {"Mouse.X", "Mouse.Y", "Mouse.WheelX", "Mouse.WheelY"}) {
        static_cast<void>(inputMap_.setControlValue(control, 0.0F));
    }
}

bool StudioPlaySession::initialized() const noexcept {
    return loop_.initialized();
}

fabgl::Scene& StudioPlaySession::scene() noexcept {
    return *scene_;
}

const fabgl::Scene& StudioPlaySession::scene() const noexcept {
    return *scene_;
}

fabgl::SceneRuntime& StudioPlaySession::runtime() noexcept {
    return *runtime_;
}

const fabgl::SceneRuntime& StudioPlaySession::runtime() const noexcept {
    return *runtime_;
}

fabgl::rendering::ScenePresentationResources StudioPlaySession::presentationResources() const {
    return assets_.resources();
}

StudioPlaySessionStats StudioPlaySession::stats() const noexcept {
    StudioPlaySessionStats result;
    result.animators = runtime_->animatorCount();
    result.visualScripts = runtime_->visualScriptCount();
    result.controlledGameplayEntities = gameplay_->controlledEntityCount();
    result.activeAudioVoices = audioMixer_.stats().activeVoices;
    result.nativeScriptComponents =
        scriptModules_ ? scriptModules_->stats().attachedComponents : 0U;
    result.gameplayUpdates = gameplay_->stats().updates;
    result.mixedAudioFrames = audioMixer_.stats().mixedFrames;
    const auto streaming = assets_.stats();
    result.residentAssets = streaming.residentAssets;
    result.residentAssetBytes = streaming.residentBytes;
    result.streamedAssetLoads = streaming.loads;
    result.assetEvictions = streaming.evictions;
    if (visualHost_) {
        const auto visual = visualHost_->stats();
        result.visualHostCalls = visual.calls;
        result.visualHostFailures = visual.failures;
    }
    return result;
}

} // namespace fgl::studio
