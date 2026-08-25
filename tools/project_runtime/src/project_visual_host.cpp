#include <fabgl/project/project_visual_host.h>

#include <fabgl/animation/animation.h>
#include <fabgl/audio/audio_mixer.h>
#include <fabgl/input/input_map.h>
#include <fabgl/project/project_asset_library.h>
#include <fabgl/reflection/reflection.h>
#include <fabgl/runtime/scene_runtime.h>
#include <fabgl/scene/component.h>
#include <fabgl/scene/entity.h>
#include <fabgl/scene/scene.h>
#include <fabgl/ui/runtime_widgets.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace fabgl::project {
namespace {

using HostResult = Result<double>;

struct RetainedVoice final {
    AudioVoiceId voice;
    std::shared_ptr<const ProjectAudioClip> clip;
};

[[nodiscard]] Error hostError(const ErrorCode code, std::string message,
                              const VisualHostCallDescriptor& descriptor) {
    return Error(code, std::move(message))
        .addContext("callback", descriptor.name)
        .addContext("payload", descriptor.payload);
}

[[nodiscard]] bool startsWith(const std::string_view value,
                              const std::string_view prefix) noexcept {
    return value.size() >= prefix.size() && value.substr(0U, prefix.size()) == prefix;
}

[[nodiscard]] Result<double> propertyNumber(const PropertyMetadata& metadata,
                                            const PropertyValue& value) {
    switch (metadata.type) {
    case PropertyType::Boolean:
        return Result<double>::success(std::get<bool>(value) ? 1.0 : 0.0);
    case PropertyType::SignedInteger:
    case PropertyType::Enumeration:
        return Result<double>::success(static_cast<double>(std::get<std::int64_t>(value)));
    case PropertyType::UnsignedInteger:
    case PropertyType::BitFlags:
        return Result<double>::success(static_cast<double>(std::get<std::uint64_t>(value)));
    case PropertyType::Float:
        return Result<double>::success(std::get<double>(value));
    case PropertyType::Fixed:
        return Result<double>::success(static_cast<double>(std::get<Fixed>(value).toFloat()));
    default:
        return Result<double>::failure(
            Error(ErrorCode::TypeMismatch, "visual component action requires a numeric property")
                .addContext("property", metadata.name));
    }
}

[[nodiscard]] Result<PropertyValue> propertyValue(const PropertyMetadata& metadata,
                                                  const double value) {
    if (!std::isfinite(value)) {
        return Result<PropertyValue>::failure(
            Error(ErrorCode::InvalidArgument, "visual component value is not finite"));
    }
    switch (metadata.type) {
    case PropertyType::Boolean:
        return Result<PropertyValue>::success(PropertyValue(value != 0.0));
    case PropertyType::SignedInteger:
    case PropertyType::Enumeration: {
        constexpr double maximum = 9'007'199'254'740'991.0;
        constexpr double minimum = -maximum;
        if (value < minimum || value > maximum) {
            return Result<PropertyValue>::failure(
                Error(ErrorCode::InvalidArgument, "visual signed component value is out of range"));
        }
        return Result<PropertyValue>::success(
            PropertyValue(static_cast<std::int64_t>(std::llround(value))));
    }
    case PropertyType::UnsignedInteger:
    case PropertyType::BitFlags: {
        constexpr double maximum = 9'007'199'254'740'991.0;
        if (value < 0.0 || value > maximum) {
            return Result<PropertyValue>::failure(Error(
                ErrorCode::InvalidArgument, "visual unsigned component value is out of range"));
        }
        return Result<PropertyValue>::success(
            PropertyValue(static_cast<std::uint64_t>(std::round(value))));
    }
    case PropertyType::Float:
        return Result<PropertyValue>::success(PropertyValue(value));
    case PropertyType::Fixed:
        return Result<PropertyValue>::success(
            PropertyValue(Fixed::fromFloat(static_cast<float>(value))));
    default:
        return Result<PropertyValue>::failure(
            Error(ErrorCode::TypeMismatch, "visual component action requires a numeric property")
                .addContext("property", metadata.name));
    }
}

[[nodiscard]] HostResult validationDispatch(const VisualHostCallDescriptor& descriptor,
                                            const std::vector<double>& arguments) {
    if (descriptor.name == "vector.length3") {
        const auto value = std::sqrt(arguments[0U] * arguments[0U] +
                                     arguments[1U] * arguments[1U] +
                                     arguments[2U] * arguments[2U]);
        return std::isfinite(value)
                   ? HostResult::success(value)
                   : HostResult::failure(Error(ErrorCode::InvalidArgument,
                                               "vector length is not finite"));
    }
    if (descriptor.name == "math.abs")
        return HostResult::success(std::fabs(arguments[0U]));
    if (descriptor.name == "math.negate")
        return HostResult::success(-arguments[0U]);
    if (descriptor.name == "math.clamp01")
        return HostResult::success(std::clamp(arguments[0U], 0.0, 1.0));
    if (descriptor.name == "math.sign")
        return HostResult::success(arguments[0U] < 0.0 ? -1.0 : (arguments[0U] > 0.0 ? 1.0 : 0.0));
    if (descriptor.name == "function.identity")
        return HostResult::success(arguments[0U]);
    return HostResult::success(arguments.empty() ? 0.0 : arguments[0U]);
}

template <typename Callback>
[[nodiscard]] Result<VisualHostCallbackTable> buildCallbackTable(Callback callback) {
    VisualHostCallbackTable table;
    const std::pair<const char*, std::uint8_t> signatures[] = {
        {"time.delay", 1U},       {"vector.length3", 3U},
        {"input.action", 0U},     {"entity.action", 1U},
        {"component.action", 1U}, {"audio.play", 1U},
        {"animation.play", 1U},   {"scene.load", 0U},
        {"ui.action", 1U},        {"function.identity", 1U},
        {"math.abs", 1U},         {"math.negate", 1U},
        {"math.clamp01", 1U},     {"math.sign", 1U},
    };
    for (const auto& [name, argumentCount] : signatures) {
        auto added = table.add(name, argumentCount, callback);
        if (!added)
            return Result<VisualHostCallbackTable>::failure(added.error());
    }
    return Result<VisualHostCallbackTable>::success(std::move(table));
}

} // namespace

struct ProjectVisualHost::State final {
    ProjectVisualHostServices services;
    ProjectVisualHostLimits limits;
    SceneRuntime* runtime = nullptr;
    ProjectVisualHostStats stats;
    std::vector<ProjectVisualHostDiagnostic> diagnostics;
    std::vector<RetainedVoice> retainedVoices;
    std::vector<AssetGuid> sceneLoadRequests;
    std::uint64_t diagnosticSequence = 0U;

    void record(const VisualHostCallDescriptor& descriptor, const Error* error) {
        ++diagnosticSequence;
        if (limits.maximumDiagnostics == 0U)
            return;
        if (diagnostics.size() == limits.maximumDiagnostics)
            diagnostics.erase(diagnostics.begin());
        ProjectVisualHostDiagnostic diagnostic;
        diagnostic.sequence = diagnosticSequence;
        diagnostic.callback = descriptor.name;
        diagnostic.payload = descriptor.payload;
        diagnostic.failure = error != nullptr;
        if (error != nullptr) {
            diagnostic.code = error->code();
            diagnostic.message = error->message();
        } else {
            diagnostic.message = "completed";
        }
        diagnostics.push_back(std::move(diagnostic));
    }

    void pruneAudio() {
        std::erase_if(retainedVoices, [this](const RetainedVoice& retained) {
            return !services.audio->isPlaying(retained.voice);
        });
    }
};

namespace {

[[nodiscard]] Entity* requireOwner(ProjectVisualHost::State& state,
                                   const VisualHostCallDescriptor& descriptor) {
    return descriptor.execution.ownerEntity
               ? state.services.scene->findEntity(*descriptor.execution.ownerEntity)
               : nullptr;
}

[[nodiscard]] HostResult inputAction(ProjectVisualHost::State& state,
                                     const VisualHostCallDescriptor& descriptor) {
    auto mode = std::string_view("held");
    auto name = std::string_view(descriptor.payload);
    const auto separator = name.find(':');
    if (separator != std::string_view::npos) {
        mode = name.substr(0U, separator);
        name.remove_prefix(separator + 1U);
    }
    if (name.empty()) {
        return HostResult::failure(
            hostError(ErrorCode::InvalidArgument, "input action payload has no action name",
                      descriptor));
    }
    if (mode == "axis")
        return HostResult::success(static_cast<double>(state.services.input->axis(name)));
    const auto action = state.services.input->action(name);
    if (mode == "held")
        return HostResult::success(action.held ? 1.0 : 0.0);
    if (mode == "pressed")
        return HostResult::success(action.pressed ? 1.0 : 0.0);
    if (mode == "released")
        return HostResult::success(action.released ? 1.0 : 0.0);
    return HostResult::failure(
        hostError(ErrorCode::InvalidArgument, "unsupported input action payload mode", descriptor));
}

[[nodiscard]] HostResult entityAction(ProjectVisualHost::State& state,
                                      const VisualHostCallDescriptor& descriptor,
                                      const double argument) {
    if (!descriptor.entityReference) {
        return HostResult::failure(hostError(ErrorCode::InvalidArgument,
                                             "entity action has no entity reference", descriptor));
    }
    auto* entity = state.services.scene->findEntity(*descriptor.entityReference);
    if (entity == nullptr) {
        return HostResult::failure(
            hostError(ErrorCode::NotFound, "entity action target was not found", descriptor));
    }
    const auto payload = std::string_view(descriptor.payload);
    if (payload.empty() || payload == "set_active") {
        entity->setActive(argument != 0.0);
        return HostResult::success(entity->active() ? 1.0 : 0.0);
    }
    if (payload == "toggle_active") {
        entity->setActive(!entity->active());
        return HostResult::success(entity->active() ? 1.0 : 0.0);
    }
    auto position = entity->transform().localPosition();
    if (payload == "translate_x")
        position.x += static_cast<float>(argument);
    else if (payload == "translate_y")
        position.y += static_cast<float>(argument);
    else if (payload == "translate_z")
        position.z += static_cast<float>(argument);
    else if (payload == "set_position_x")
        position.x = static_cast<float>(argument);
    else if (payload == "set_position_y")
        position.y = static_cast<float>(argument);
    else if (payload == "set_position_z")
        position.z = static_cast<float>(argument);
    else
        return HostResult::failure(hostError(ErrorCode::InvalidArgument,
                                             "unsupported entity action payload", descriptor));
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
        return HostResult::failure(hostError(ErrorCode::InvalidArgument,
                                             "entity transform result is not finite", descriptor));
    }
    entity->transform().setLocalPosition(position);
    return HostResult::success(argument);
}

[[nodiscard]] HostResult componentAction(ProjectVisualHost::State& state,
                                         const VisualHostCallDescriptor& descriptor,
                                         const double argument) {
    if (!descriptor.componentReference) {
        return HostResult::failure(hostError(ErrorCode::InvalidArgument,
                                             "component action has no component reference",
                                             descriptor));
    }
    auto* entity = descriptor.entityReference
                       ? state.services.scene->findEntity(*descriptor.entityReference)
                       : requireOwner(state, descriptor);
    if (entity == nullptr) {
        return HostResult::failure(hostError(ErrorCode::NotFound,
                                             "component action entity was not found", descriptor));
    }
    auto* component = entity->getComponent(*descriptor.componentReference);
    if (component == nullptr) {
        return HostResult::failure(hostError(ErrorCode::NotFound,
                                             "component action target was not found", descriptor));
    }
    const auto payload = std::string_view(descriptor.payload);
    if (payload.empty() || payload == "set_enabled") {
        component->setEnabled(argument != 0.0);
        return HostResult::success(component->enabled() ? 1.0 : 0.0);
    }
    if (payload == "toggle_enabled") {
        component->setEnabled(!component->enabled());
        return HostResult::success(component->enabled() ? 1.0 : 0.0);
    }
    const bool add = startsWith(payload, "add:");
    const bool set = startsWith(payload, "set:");
    if (!add && !set) {
        return HostResult::failure(hostError(ErrorCode::InvalidArgument,
                                             "unsupported component action payload", descriptor));
    }
    const auto propertyName = payload.substr(4U);
    const auto* metadata = component->metadata();
    const auto* property = metadata != nullptr ? metadata->findProperty(propertyName) : nullptr;
    if (property == nullptr) {
        return HostResult::failure(hostError(ErrorCode::NotFound,
                                             "reflected component property was not found",
                                             descriptor));
    }
    if (!hasFlag(property->flags, PropertyFlags::RuntimeEditable) ||
        hasFlag(property->flags, PropertyFlags::ReadOnly)) {
        return HostResult::failure(hostError(ErrorCode::InvalidState,
                                             "reflected component property is not runtime editable",
                                             descriptor));
    }
    auto nextNumber = argument;
    if (add) {
        auto current = property->read(component);
        if (!current)
            return HostResult::failure(current.error().withContext("callback", descriptor.name));
        auto number = propertyNumber(*property, current.value());
        if (!number)
            return HostResult::failure(number.error().withContext("callback", descriptor.name));
        nextNumber += number.value();
    }
    auto next = propertyValue(*property, nextNumber);
    if (!next)
        return HostResult::failure(next.error().withContext("callback", descriptor.name));
    auto written = property->write(component, next.value());
    if (!written)
        return HostResult::failure(written.error().withContext("callback", descriptor.name));
    return HostResult::success(nextNumber);
}

[[nodiscard]] HostResult audioPlay(ProjectVisualHost::State& state,
                                   const VisualHostCallDescriptor& descriptor,
                                   const double volume) {
    if (!descriptor.assetReference) {
        return HostResult::failure(
            hostError(ErrorCode::InvalidArgument, "audio play has no asset reference", descriptor));
    }
    auto clip = state.services.audioClipResolver(*descriptor.assetReference);
    if (clip == nullptr) {
        return HostResult::failure(
            hostError(ErrorCode::NotFound, "audio asset is not resident", descriptor));
    }
    AudioVoiceSettings settings;
    auto payload = std::string_view(descriptor.payload);
    if (payload.ends_with(".loop")) {
        settings.loop = true;
        payload.remove_suffix(5U);
    }
    if (payload.empty() || payload == "sfx")
        settings.bus = AudioBusId{2U};
    else if (payload == "music")
        settings.bus = AudioBusId{1U};
    else if (payload == "ui")
        settings.bus = AudioBusId{3U};
    else
        return HostResult::failure(
            hostError(ErrorCode::InvalidArgument, "unsupported audio play payload", descriptor));
    settings.volume = static_cast<float>(std::clamp(volume, 0.0, 1.0));
    state.pruneAudio();
    if (state.retainedVoices.size() >= state.limits.maximumRetainedAudioClips) {
        return HostResult::failure(hostError(ErrorCode::CapacityExceeded,
                                             "visual audio retention limit is exhausted",
                                             descriptor));
    }
    auto voice = state.services.audio->play(clip->view(), settings);
    if (!voice)
        return HostResult::failure(voice.error().withContext("callback", descriptor.name));
    state.retainedVoices.push_back({voice.value(), std::move(clip)});
    state.pruneAudio();
    ++state.stats.audioVoicesStarted;
    return HostResult::success(static_cast<double>(voice.value().value));
}

[[nodiscard]] HostResult animationPlay(ProjectVisualHost::State& state,
                                       const VisualHostCallDescriptor& descriptor,
                                       const double speed) {
    if (!descriptor.assetReference || descriptor.assetReference->isNil()) {
        return HostResult::failure(hostError(ErrorCode::InvalidArgument,
                                             "animation play has no asset reference", descriptor));
    }
    auto* owner = requireOwner(state, descriptor);
    if (owner == nullptr || state.runtime == nullptr) {
        return HostResult::failure(hostError(ErrorCode::InvalidState,
                                             "animation host is not bound to an owner runtime",
                                             descriptor));
    }
    if (descriptor.payload.empty()) {
        return HostResult::failure(hostError(ErrorCode::InvalidArgument,
                                             "animation play payload has no state name",
                                             descriptor));
    }
    if (speed < 0.0 || speed > 100.0) {
        return HostResult::failure(hostError(ErrorCode::InvalidArgument,
                                             "animation speed must be between zero and 100",
                                             descriptor));
    }
    auto* animator = state.runtime->animatorFor(owner->id());
    if (animator == nullptr) {
        return HostResult::failure(
            hostError(ErrorCode::NotFound, "owner has no runtime animator", descriptor));
    }
    const auto animatorType =
        ComponentTypeGuid::fromStableName("fabgl.component.Animator.v1");
    if (auto* component = owner->getComponent(animatorType); component != nullptr) {
        const auto* metadata = component->metadata();
        const auto* property = metadata != nullptr ? metadata->findProperty("speed") : nullptr;
        if (property != nullptr) {
            auto written = property->write(component, PropertyValue(speed));
            if (!written)
                return HostResult::failure(written.error().withContext("callback", descriptor.name));
        }
    }
    auto played = animator->play(descriptor.payload);
    if (!played)
        return HostResult::failure(played.error().withContext("callback", descriptor.name));
    return HostResult::success(speed);
}

[[nodiscard]] HostResult sceneLoad(ProjectVisualHost::State& state,
                                   const VisualHostCallDescriptor& descriptor) {
    if (!descriptor.assetReference || descriptor.assetReference->isNil()) {
        return HostResult::failure(
            hostError(ErrorCode::InvalidArgument, "scene load has no asset reference", descriptor));
    }
    if (state.services.sceneLoadHandler) {
        auto loaded = state.services.sceneLoadHandler(*descriptor.assetReference);
        if (!loaded)
            return HostResult::failure(loaded.error().withContext("callback", descriptor.name));
    } else {
        if (state.sceneLoadRequests.size() >= state.limits.maximumSceneLoadRequests) {
            return HostResult::failure(hostError(ErrorCode::CapacityExceeded,
                                                 "visual scene-load request queue is full",
                                                 descriptor));
        }
        state.sceneLoadRequests.push_back(*descriptor.assetReference);
    }
    ++state.stats.sceneLoadRequests;
    return HostResult::success(1.0);
}

[[nodiscard]] HostResult uiAction(ProjectVisualHost::State& state,
                                  const VisualHostCallDescriptor& descriptor,
                                  const double argument) {
    auto* owner = requireOwner(state, descriptor);
    if (owner == nullptr || state.runtime == nullptr) {
        return HostResult::failure(hostError(ErrorCode::InvalidState,
                                             "UI host is not bound to an owner runtime",
                                             descriptor));
    }
    const auto element = state.runtime->uiElementFor(owner->id());
    if (!element) {
        return HostResult::failure(
            hostError(ErrorCode::NotFound, "owner has no runtime UI element", descriptor));
    }
    auto& ui = state.runtime->ui();
    const auto payload = std::string_view(descriptor.payload);
    if (payload.empty() || payload == "set_value") {
        auto updated = ui.setValue(*element, static_cast<float>(argument), true);
        return updated ? HostResult::success(argument)
                       : HostResult::failure(updated.error().withContext("callback", descriptor.name));
    }
    if (payload == "set_checked") {
        auto* widget = ui.widget(*element);
        if (widget == nullptr)
            return HostResult::failure(hostError(ErrorCode::NotFound, "UI widget was not found",
                                                 descriptor));
        widget->checked = argument != 0.0;
        return HostResult::success(widget->checked ? 1.0 : 0.0);
    }
    if (payload == "select_index") {
        auto* widget = ui.widget(*element);
        if (widget == nullptr || argument < 0.0 || std::floor(argument) != argument ||
            argument >= static_cast<double>(widget->items.size())) {
            return HostResult::failure(hostError(ErrorCode::InvalidArgument,
                                                 "UI selection index is invalid", descriptor));
        }
        widget->selectedItem = static_cast<std::size_t>(argument);
        return HostResult::success(argument);
    }
    if (payload == "set_scale") {
        auto updated = ui.setScale(static_cast<float>(argument));
        return updated ? HostResult::success(argument)
                       : HostResult::failure(updated.error().withContext("callback", descriptor.name));
    }
    return HostResult::failure(
        hostError(ErrorCode::InvalidArgument, "unsupported UI action payload", descriptor));
}

[[nodiscard]] HostResult runtimeDispatch(const std::weak_ptr<ProjectVisualHost::State>& weak,
                                         const VisualHostCallDescriptor& descriptor,
                                         const std::vector<double>& arguments) {
    auto state = weak.lock();
    if (state == nullptr) {
        return HostResult::failure(
            Error(ErrorCode::InvalidState, "visual host lifetime has ended")
                .addContext("callback", descriptor.name));
    }
    ++state->stats.calls;
    HostResult result = HostResult::failure(
        hostError(ErrorCode::NotFound, "visual callback is not implemented", descriptor));
    if (descriptor.payload.size() > state->limits.maximumPayloadBytes) {
        result = HostResult::failure(
            hostError(ErrorCode::CapacityExceeded, "visual callback payload exceeds host limit",
                      descriptor));
    } else if (std::any_of(arguments.begin(), arguments.end(),
                           [](const double value) { return !std::isfinite(value); })) {
        result = HostResult::failure(hostError(ErrorCode::InvalidArgument,
                                               "visual callback argument is not finite",
                                               descriptor));
    } else if (descriptor.name == "time.delay") {
        result = HostResult::success(0.0);
    } else if (descriptor.name == "vector.length3" ||
               descriptor.name == "function.identity" || descriptor.name == "math.abs" ||
               descriptor.name == "math.negate" || descriptor.name == "math.clamp01" ||
               descriptor.name == "math.sign") {
        result = validationDispatch(descriptor, arguments);
    } else if (descriptor.name == "input.action") {
        result = inputAction(*state, descriptor);
    } else if (descriptor.name == "entity.action") {
        result = entityAction(*state, descriptor, arguments[0U]);
    } else if (descriptor.name == "component.action") {
        result = componentAction(*state, descriptor, arguments[0U]);
    } else if (descriptor.name == "audio.play") {
        result = audioPlay(*state, descriptor, arguments[0U]);
    } else if (descriptor.name == "animation.play") {
        result = animationPlay(*state, descriptor, arguments[0U]);
    } else if (descriptor.name == "scene.load") {
        result = sceneLoad(*state, descriptor);
    } else if (descriptor.name == "ui.action") {
        result = uiAction(*state, descriptor, arguments[0U]);
    }
    if (!result) {
        ++state->stats.failures;
        const auto error = result.error();
        state->record(descriptor, &error);
    } else {
        state->record(descriptor, nullptr);
    }
    return result;
}

} // namespace

Result<ProjectVisualHost> ProjectVisualHost::create(ProjectVisualHostServices services,
                                                    ProjectVisualHostLimits limits) {
    if (services.scene == nullptr || services.input == nullptr || services.audio == nullptr ||
        !services.audioClipResolver) {
        return Result<ProjectVisualHost>::failure(
            Error(ErrorCode::InvalidArgument,
                  "project visual host requires scene, input, audio, and asset resolver services"));
    }
    if (limits.maximumRetainedAudioClips == 0U || limits.maximumDiagnostics == 0U ||
        limits.maximumSceneLoadRequests == 0U || limits.maximumPayloadBytes == 0U ||
        limits.maximumRetainedAudioClips > 4096U || limits.maximumDiagnostics > 4096U ||
        limits.maximumSceneLoadRequests > 1024U ||
        limits.maximumPayloadBytes > VisualMaximumHostPayloadBytes) {
        return Result<ProjectVisualHost>::failure(
            Error(ErrorCode::InvalidArgument, "project visual host limits are unsafe"));
    }
    auto state = std::make_shared<State>();
    state->services = std::move(services);
    state->limits = limits;
    const std::weak_ptr<State> weak = state;
    auto callbacks = buildCallbackTable(
        [weak](const VisualHostCallDescriptor& descriptor,
               const std::vector<double>& arguments) {
            return runtimeDispatch(weak, descriptor, arguments);
        });
    if (!callbacks)
        return Result<ProjectVisualHost>::failure(callbacks.error());
    return Result<ProjectVisualHost>::success(
        ProjectVisualHost(std::move(state), std::move(callbacks).value()));
}

ProjectVisualHost::~ProjectVisualHost() {
    if (state_ == nullptr)
        return;
    for (const auto& retained : state_->retainedVoices)
        static_cast<void>(state_->services.audio->stop(retained.voice));
    state_->retainedVoices.clear();
    state_->runtime = nullptr;
}

ProjectVisualHost& ProjectVisualHost::operator=(ProjectVisualHost&& other) noexcept {
    if (this == &other)
        return *this;
    if (state_ != nullptr) {
        for (const auto& retained : state_->retainedVoices)
            static_cast<void>(state_->services.audio->stop(retained.voice));
        state_->retainedVoices.clear();
        state_->runtime = nullptr;
    }
    state_ = std::move(other.state_);
    callbacks_ = std::move(other.callbacks_);
    return *this;
}

Result<void> ProjectVisualHost::bindRuntime(SceneRuntime& runtime) {
    if (state_ == nullptr)
        return Result<void>::failure(Error(ErrorCode::InvalidState, "visual host has no state"));
    if (state_->runtime != nullptr && state_->runtime != &runtime) {
        return Result<void>::failure(
            Error(ErrorCode::AlreadyExists, "visual host is already bound to another runtime"));
    }
    state_->runtime = &runtime;
    return Result<void>::success();
}

void ProjectVisualHost::unbindRuntime() noexcept {
    if (state_ != nullptr)
        state_->runtime = nullptr;
}

ProjectVisualHostStats ProjectVisualHost::stats() const noexcept {
    return state_ != nullptr ? state_->stats : ProjectVisualHostStats{};
}

std::vector<ProjectVisualHostDiagnostic> ProjectVisualHost::diagnostics() const {
    return state_ != nullptr ? state_->diagnostics : std::vector<ProjectVisualHostDiagnostic>{};
}

std::vector<AssetGuid> ProjectVisualHost::takeSceneLoadRequests() {
    if (state_ == nullptr)
        return {};
    auto requests = std::move(state_->sceneLoadRequests);
    state_->sceneLoadRequests.clear();
    return requests;
}

std::size_t ProjectVisualHost::retainedAudioClipCount() const noexcept {
    return state_ != nullptr ? state_->retainedVoices.size() : 0U;
}

const VisualHostCallbackTable& ProjectVisualHost::validationCallbacks() {
    static const VisualHostCallbackTable callbacks = [] {
        auto built = buildCallbackTable(validationDispatch);
        if (!built)
            throw std::logic_error("cannot construct visual host validation callbacks");
        return std::move(built).value();
    }();
    return callbacks;
}

const std::vector<std::string>& ProjectVisualHost::safeFunctionCallbacks() {
    static const std::vector<std::string> callbacks{
        "function.identity", "math.abs", "math.negate", "math.clamp01", "math.sign"};
    return callbacks;
}

} // namespace fabgl::project
