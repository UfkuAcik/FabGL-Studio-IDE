#include <fabgl/project/project_scene_audio.h>

#include <fabgl/scene/builtin_components.h>
#include <fabgl/scene/entity.h>
#include <fabgl/scene/scene.h>

#include <cmath>
#include <string>
#include <utility>
#include <variant>

namespace fabgl::project {
namespace {

[[nodiscard]] ComponentTypeGuid builtinTypeId(const char* shortName) {
    return ComponentTypeGuid::fromStableName(std::string("fabgl.component.") + shortName + ".v1");
}

[[nodiscard]] const DataComponent* component(const Entity& entity, const char* shortName) noexcept {
    return dynamic_cast<const DataComponent*>(entity.getComponent(builtinTypeId(shortName)));
}

template <typename Type>
[[nodiscard]] Result<Type> property(const DataComponent& componentValue, const char* name) {
    auto reflected = componentValue.get(name);
    if (!reflected)
        return Result<Type>::failure(reflected.error());
    const auto* typed = std::get_if<Type>(&reflected.value());
    if (typed == nullptr) {
        return Result<Type>::failure(
            Error(ErrorCode::TypeMismatch, "audio component property has an unexpected type")
                .addContext("property", name));
    }
    return Result<Type>::success(*typed);
}

[[nodiscard]] Result<bool> runtimeEnabled(const DataComponent& componentValue) {
    auto reflected = property<bool>(componentValue, "enabled");
    if (!reflected)
        return reflected;
    return Result<bool>::success(componentValue.activeAndEnabled() && reflected.value());
}

} // namespace

ProjectSceneAudioRuntime::ProjectSceneAudioRuntime(Scene& scene, const ProjectAssetLibrary& assets,
                                                   AudioMixer& mixer)
    : ProjectSceneAudioRuntime(
          scene, [&assets](const AssetGuid guid) { return assets.audioClip(guid); }, mixer) {}

ProjectSceneAudioRuntime::ProjectSceneAudioRuntime(Scene& scene,
                                                   ProjectAudioClipResolver clipResolver,
                                                   AudioMixer& mixer)
    : scene_(&scene), clipResolver_(std::move(clipResolver)), mixer_(&mixer) {}

ProjectSceneAudioRuntime::~ProjectSceneAudioRuntime() {
    shutdown();
}

Result<void> ProjectSceneAudioRuntime::initialize() {
    if (initialized_) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "project scene audio runtime is already initialized"));
    }
    initialized_ = true;
    stats_ = {};
    bindings_.clear();
    if (!clipResolver_) {
        shutdown();
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "project audio clip resolver is missing"));
    }

    for (const auto* entity : scene_->entities()) {
        if (!entity->active())
            continue;
        if (const auto* listener = component(*entity, "AudioListener")) {
            auto enabled = runtimeEnabled(*listener);
            if (!enabled) {
                shutdown();
                return Result<void>::failure(
                    enabled.error().withContext("entity", entity->id().toString()));
            }
            if (enabled.value())
                ++stats_.listeners;
        }

        const auto* source = component(*entity, "AudioSource");
        if (source == nullptr)
            continue;
        auto enabled = runtimeEnabled(*source);
        if (!enabled) {
            shutdown();
            return Result<void>::failure(
                enabled.error().withContext("entity", entity->id().toString()));
        }
        if (!enabled.value())
            continue;

        auto clipGuid = property<AssetGuid>(*source, "clip");
        auto volume = property<double>(*source, "volume");
        auto pitch = property<double>(*source, "pitch");
        auto loop = property<bool>(*source, "loop");
        if (!clipGuid || !volume || !pitch || !loop) {
            const auto error = !clipGuid ? clipGuid.error()
                               : !volume ? volume.error()
                               : !pitch  ? pitch.error()
                                         : loop.error();
            shutdown();
            return Result<void>::failure(error.withContext("entity", entity->id().toString()));
        }
        if (clipGuid.value().isNil() || !std::isfinite(volume.value()) || volume.value() < 0.0 ||
            !std::isfinite(pitch.value()) || pitch.value() <= 0.0) {
            shutdown();
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "AudioSource settings are invalid")
                    .addContext("entity", entity->id().toString()));
        }
        auto clip = clipResolver_(clipGuid.value());
        if (!clip || !clip->valid()) {
            shutdown();
            return Result<void>::failure(
                Error(ErrorCode::NotFound, "AudioSource clip is not loaded")
                    .addContext("entity", entity->id().toString())
                    .addContext("clip", clipGuid.value().toString()));
        }

        AudioVoiceSettings settings;
        settings.volume = static_cast<float>(volume.value());
        settings.pitch = static_cast<float>(pitch.value());
        settings.loop = loop.value();
        auto voice = mixer_->play(clip->view(), settings);
        if (!voice) {
            shutdown();
            return Result<void>::failure(
                voice.error().withContext("entity", entity->id().toString()));
        }
        bindings_.push_back({entity->id(), std::move(clip), voice.value()});
        ++stats_.sources;
        ++stats_.voicesStarted;
    }
    return Result<void>::success();
}

void ProjectSceneAudioRuntime::shutdown() noexcept {
    if (mixer_ != nullptr) {
        for (const auto& binding : bindings_)
            static_cast<void>(mixer_->stop(binding.voice));
    }
    bindings_.clear();
    initialized_ = false;
}

std::size_t ProjectSceneAudioRuntime::activeVoiceCount() const noexcept {
    std::size_t result = 0U;
    if (mixer_ == nullptr)
        return result;
    for (const auto& binding : bindings_)
        result += mixer_->isPlaying(binding.voice) ? 1U : 0U;
    return result;
}

} // namespace fabgl::project
