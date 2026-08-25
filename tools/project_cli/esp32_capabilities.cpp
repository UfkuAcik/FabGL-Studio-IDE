#include "esp32_capabilities.h"

#include <fabgl/reflection/reflection.h>
#include <fabgl/scene/builtin_components.h>
#include <fabgl/scene/entity.h>
#include <fabgl/scene/scene.h>
#include <fabgl/scene/transform_component.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <string>
#include <utility>

namespace fabgl::project {
namespace {

constexpr std::array<std::string_view, 11U> SupportedComponents{
    "fabgl.Transform",          "fabgl.Camera",       "fabgl.SpriteRenderer",
    "fabgl.CharacterBody2D",   "fabgl.VehicleController",
    "fabgl.RaycastMap",         "fabgl.FirstPersonController",
    "fabgl.Collider2D",         "fabgl.Rigidbody2D",
    "fabgl.ParticleEmitter",    "fabgl.UITransform",
};

constexpr std::array<std::string_view, 4U> SupportedAssetTypes{
    "binary", "image", "racer.track", "raycast.map"};

[[nodiscard]] std::string_view normalizedComponentName(std::string_view name) noexcept {
    if (name == "Transform")
        return "fabgl.Transform";
    return name;
}

[[nodiscard]] std::string_view extensionOf(std::string_view path) noexcept {
    const auto separator = path.find_last_of("/\\");
    const auto dot = path.find_last_of('.');
    if (dot == std::string_view::npos || dot + 1U == path.size() ||
        (separator != std::string_view::npos && dot < separator)) {
        return {};
    }
    return path.substr(dot + 1U);
}

[[nodiscard]] bool extensionEquals(std::string_view extension,
                                   std::string_view expected) noexcept {
    return extension.size() == expected.size() &&
           std::equal(extension.begin(), extension.end(), expected.begin(),
                      [](const char left, const char right) {
                          const auto lower = [](const char value) {
                              return value >= 'A' && value <= 'Z'
                                         ? static_cast<char>(value - 'A' + 'a')
                                         : value;
                          };
                          return lower(left) == lower(right);
                      });
}

[[nodiscard]] bool oneOfExtensions(std::string_view extension,
                                   std::initializer_list<std::string_view> expected) noexcept {
    return std::any_of(expected.begin(), expected.end(), [extension](const auto candidate) {
        return extensionEquals(extension, candidate);
    });
}

[[nodiscard]] Error targetError(const ErrorCode code, std::string message,
                                std::string feature) {
    return Error(code, std::move(message))
        .addContext("target", std::string(Esp32RuntimeProfileId))
        .addContext("feature", std::move(feature));
}

[[nodiscard]] Error entityError(const ErrorCode code, std::string message,
                                std::string feature, const Entity& entity) {
    return targetError(code, std::move(message), std::move(feature))
        .addContext("entity", entity.name())
        .addContext("entity_guid", entity.id().toString());
}

[[nodiscard]] const ProjectAssetEntry* findAsset(const Manifest& manifest,
                                                 const AssetGuid guid) noexcept {
    const auto found = std::find_if(manifest.assets.begin(), manifest.assets.end(),
                                    [guid](const auto& entry) { return entry.guid == guid; });
    return found == manifest.assets.end() ? nullptr : &*found;
}

[[nodiscard]] Result<AssetGuid> readAssetReference(const Component& component,
                                                   std::string_view property) {
    const auto* metadata = component.metadata();
    const auto* reflected = metadata == nullptr ? nullptr : metadata->findProperty(property);
    if (reflected == nullptr) {
        return Result<AssetGuid>::failure(
            Error(ErrorCode::InternalError, "supported ESP32 component property is missing")
                .addContext("component", std::string(component.typeName()))
                .addContext("property", std::string(property)));
    }
    auto value = reflected->read(&component);
    if (!value)
        return Result<AssetGuid>::failure(value.error());
    const auto* reference = std::get_if<AssetGuid>(&value.value());
    if (reference == nullptr) {
        return Result<AssetGuid>::failure(
            Error(ErrorCode::InternalError, "supported ESP32 component property has wrong type")
                .addContext("component", std::string(component.typeName()))
                .addContext("property", std::string(property)));
    }
    return Result<AssetGuid>::success(*reference);
}

[[nodiscard]] Result<std::string> readStringProperty(const Component& component,
                                                     std::string_view property) {
    const auto* metadata = component.metadata();
    const auto* reflected = metadata == nullptr ? nullptr : metadata->findProperty(property);
    if (reflected == nullptr) {
        return Result<std::string>::failure(
            Error(ErrorCode::InternalError, "supported ESP32 component property is missing")
                .addContext("component", std::string(component.typeName()))
                .addContext("property", std::string(property)));
    }
    auto value = reflected->read(&component);
    if (!value)
        return Result<std::string>::failure(value.error());
    const auto* text = std::get_if<std::string>(&value.value());
    if (text == nullptr) {
        return Result<std::string>::failure(
            Error(ErrorCode::InternalError, "supported ESP32 component property has wrong type")
                .addContext("component", std::string(component.typeName()))
                .addContext("property", std::string(property)));
    }
    return Result<std::string>::success(*text);
}

template <typename T>
[[nodiscard]] Result<T> readTypedProperty(const Component& component,
                                          const std::string_view property) {
    const auto* metadata = component.metadata();
    const auto* reflected = metadata == nullptr ? nullptr : metadata->findProperty(property);
    if (reflected == nullptr) {
        return Result<T>::failure(
            Error(ErrorCode::InternalError, "supported ESP32 component property is missing")
                .addContext("component", std::string(component.typeName()))
                .addContext("property", std::string(property)));
    }
    auto value = reflected->read(&component);
    if (!value)
        return Result<T>::failure(value.error());
    const auto* typed = std::get_if<T>(&value.value());
    if (typed == nullptr) {
        return Result<T>::failure(
            Error(ErrorCode::InternalError, "supported ESP32 component property has wrong type")
                .addContext("component", std::string(component.typeName()))
                .addContext("property", std::string(property)));
    }
    return Result<T>::success(*typed);
}

[[nodiscard]] Result<void> unsupportedValue(const Component& component,
                                            const Entity& entity,
                                            const std::string_view property,
                                            const std::string_view supported) {
    return Result<void>::failure(
        entityError(ErrorCode::InvalidState,
                    "component property exceeds the bounded ESP32 runtime subset",
                    "component_property_subset", entity)
            .addContext("component", std::string(component.typeName()))
            .addContext("property", std::string(property))
            .addContext("supported", std::string(supported)));
}

[[nodiscard]] Result<void> validateComponentString(const Component& component,
                                                   std::string_view property,
                                                   const Entity& entity) {
    auto text = readStringProperty(component, property);
    if (!text)
        return Result<void>::failure(text.error());
    if (text.value().size() <= 31U)
        return Result<void>::success();
    return Result<void>::failure(
        entityError(ErrorCode::CapacityExceeded,
                    "component string exceeds the ESP32 runtime text capacity",
                    "component_property", entity)
            .addContext("component", std::string(component.metadata()->name))
            .addContext("property", std::string(property))
            .addContext("bytes", std::to_string(text.value().size()))
            .addContext("maximum_bytes", "31"));
}

[[nodiscard]] Result<void> validateAssetReference(const Manifest& manifest,
                                                  const Component& component,
                                                  std::string_view property,
                                                  std::string_view expectedType,
                                                  const Entity& entity) {
    auto reference = readAssetReference(component, property);
    if (!reference)
        return Result<void>::failure(reference.error());
    if (reference.value().isNil())
        return Result<void>::success();
    const auto* asset = findAsset(manifest, reference.value());
    if (asset == nullptr) {
        return Result<void>::failure(
            entityError(ErrorCode::NotFound,
                        "ESP32 scene component references an undeclared project asset",
                        "component_asset_reference", entity)
                .addContext("component", std::string(component.metadata()->name))
                .addContext("property", std::string(property))
                .addContext("asset_guid", reference.value().toString())
                .addContext("expected_asset_type", std::string(expectedType)));
    }
    if (asset->type != expectedType) {
        return Result<void>::failure(
            entityError(ErrorCode::TypeMismatch,
                        "ESP32 scene component references an incompatible project asset",
                        "component_asset_reference", entity)
                .addContext("component", std::string(component.metadata()->name))
                .addContext("property", std::string(property))
                .addContext("asset", asset->path)
                .addContext("asset_guid", asset->guid.toString())
                .addContext("expected_asset_type", std::string(expectedType))
                .addContext("actual_asset_type", asset->type));
    }
    return Result<void>::success();
}

[[nodiscard]] Result<void> validateSupportedComponent(const Manifest& manifest,
                                                      const Component& component,
                                                      std::string_view name,
                                                      const Entity& entity) {
    if (name == "fabgl.SpriteRenderer")
        return validateAssetReference(manifest, component, "sprite", "image", entity);
    if (name == "fabgl.RaycastMap")
        return validateAssetReference(manifest, component, "map", "raycast.map", entity);
    if (name == "fabgl.VehicleController") {
        auto track = validateAssetReference(manifest, component, "track", "racer.track", entity);
        if (!track)
            return track;
        for (const auto property : {"steerAxis", "throttleAxis", "brakeAction", "driftAction"}) {
            auto valid = validateComponentString(component, property, entity);
            if (!valid)
                return valid;
        }
    } else if (name == "fabgl.CharacterBody2D") {
        for (const auto property : {"moveXAxis", "moveYAxis", "primaryAction"}) {
            auto valid = validateComponentString(component, property, entity);
            if (!valid)
                return valid;
        }
    } else if (name == "fabgl.FirstPersonController") {
        for (const auto property : {"moveXAxis", "moveYAxis", "lookXAxis", "primaryAction"}) {
            auto valid = validateComponentString(component, property, entity);
            if (!valid)
                return valid;
        }
    } else if (name == "fabgl.Collider2D") {
        auto shape = readTypedProperty<std::int64_t>(component, "shape");
        if (!shape)
            return Result<void>::failure(shape.error());
        if (shape.value() != 0)
            return unsupportedValue(component, entity, "shape", "AABB (0)");
    } else if (name == "fabgl.Rigidbody2D") {
        auto mass = readTypedProperty<double>(component, "mass");
        auto friction = readTypedProperty<double>(component, "friction");
        if (!mass)
            return Result<void>::failure(mass.error());
        if (!friction)
            return Result<void>::failure(friction.error());
        if (std::fabs(mass.value() - 1.0) > 0.000001)
            return unsupportedValue(component, entity, "mass", "1.0");
        if (std::fabs(friction.value() - 0.5) > 0.000001)
            return unsupportedValue(component, entity, "friction", "0.5");
    } else if (name == "fabgl.ParticleEmitter") {
        auto maximum = readTypedProperty<std::uint64_t>(component, "maxParticles");
        auto startSize = readTypedProperty<double>(component, "startSize");
        auto endSize = readTypedProperty<double>(component, "endSize");
        auto startRotation = readTypedProperty<double>(component, "startRotation");
        auto endRotation = readTypedProperty<double>(component, "endRotation");
        auto cull = readTypedProperty<bool>(component, "cullOutsideBounds");
        if (!maximum)
            return Result<void>::failure(maximum.error());
        if (!startSize)
            return Result<void>::failure(startSize.error());
        if (!endSize)
            return Result<void>::failure(endSize.error());
        if (!startRotation)
            return Result<void>::failure(startRotation.error());
        if (!endRotation)
            return Result<void>::failure(endRotation.error());
        if (!cull)
            return Result<void>::failure(cull.error());
        if (maximum.value() == 0U || maximum.value() > 128U)
            return unsupportedValue(component, entity, "maxParticles", "1..128");
        if (std::fabs(startSize.value() - 1.0) > 0.000001 ||
            std::fabs(endSize.value()) > 0.000001)
            return unsupportedValue(component, entity, "startSize/endSize", "1.0/0.0");
        if (std::fabs(startRotation.value()) > 0.000001 ||
            std::fabs(endRotation.value()) > 0.000001)
            return unsupportedValue(component, entity, "rotation", "0.0");
        if (cull.value())
            return unsupportedValue(component, entity, "cullOutsideBounds", "false");
    } else if (name == "fabgl.UITransform") {
        auto widget = readTypedProperty<std::int64_t>(component, "widgetType");
        auto anchorMinimum = readTypedProperty<Vec2>(component, "anchorMinimum");
        auto anchorMaximum = readTypedProperty<Vec2>(component, "anchorMaximum");
        auto text = readStringProperty(component, "text");
        if (!widget)
            return Result<void>::failure(widget.error());
        if (!anchorMinimum)
            return Result<void>::failure(anchorMinimum.error());
        if (!anchorMaximum)
            return Result<void>::failure(anchorMaximum.error());
        if (!text)
            return Result<void>::failure(text.error());
        if (widget.value() != 0 && widget.value() != 2 && widget.value() != 6)
            return unsupportedValue(component, entity, "widgetType", "Panel, Text, Progress");
        if (std::fabs(anchorMinimum.value().x) > 0.000001F ||
            std::fabs(anchorMinimum.value().y) > 0.000001F ||
            std::fabs(anchorMaximum.value().x) > 0.000001F ||
            std::fabs(anchorMaximum.value().y) > 0.000001F)
            return unsupportedValue(component, entity, "anchors", "fixed top-left anchors");
        if (text.value().size() > 63U)
            return unsupportedValue(component, entity, "text", "at most 63 UTF-8 bytes");
    }
    return Result<void>::success();
}

} // namespace

Esp32ComponentCapability
classifyEsp32RuntimeComponent(const std::string_view componentName) {
    const auto normalized = normalizedComponentName(componentName);
    if (std::find(SupportedComponents.begin(), SupportedComponents.end(), normalized) !=
        SupportedComponents.end()) {
        return Esp32ComponentCapability::Supported;
    }
    constexpr std::string_view Prefix = "fabgl.";
    if (normalized.rfind(Prefix, 0U) != 0U)
        return Esp32ComponentCapability::Unknown;
    const auto shortName = normalized.substr(Prefix.size());
    const auto& known = builtinComponentNames();
    return std::find(known.begin(), known.end(), shortName) != known.end()
               ? Esp32ComponentCapability::KnownButNotPorted
               : Esp32ComponentCapability::Unknown;
}

Esp32AssetCapability classifyEsp32RuntimeAsset(const std::string_view assetType,
                                               const std::string_view assetPath) noexcept {
    const auto extension = extensionOf(assetPath);
    if (assetType == "visual.script" || extensionEquals(extension, "fglvisual"))
        return Esp32AssetCapability::VisualScriptVmUnavailable;
    if (std::find(SupportedAssetTypes.begin(), SupportedAssetTypes.end(), assetType) ==
        SupportedAssetTypes.end()) {
        return Esp32AssetCapability::Unsupported;
    }
    if (assetType == "image" &&
        oneOfExtensions(extension, {"fgli", "png", "jpg", "jpeg", "bmp"})) {
        return Esp32AssetCapability::Supported;
    }
    if (assetType == "raycast.map" && extensionEquals(extension, "fglray"))
        return Esp32AssetCapability::Supported;
    if (assetType == "racer.track" && extensionEquals(extension, "fgltrack"))
        return Esp32AssetCapability::Supported;
    if (assetType == "binary" && oneOfExtensions(extension, {"bin", "dat", "raw"}))
        return Esp32AssetCapability::Supported;
    return Esp32AssetCapability::Unsupported;
}

Result<Esp32CapabilitySummary> validateEsp32TargetCapabilities(const Manifest& manifest,
                                                              const Scene& startupScene) {
    if (manifest.targetProfiles.esp32 != Esp32RuntimeProfileId) {
        return Result<Esp32CapabilitySummary>::failure(
            targetError(ErrorCode::InvalidArgument,
                        "project selects an unsupported ESP32 runtime profile", "target_profile")
                .addContext("selected_profile", manifest.targetProfiles.esp32));
    }
    if (manifest.name.size() > 63U) {
        return Result<Esp32CapabilitySummary>::failure(
            targetError(ErrorCode::CapacityExceeded,
                        "project name exceeds the ESP32 runtime text capacity", "project_name")
                .addContext("bytes", std::to_string(manifest.name.size()))
                .addContext("maximum_bytes", "63"));
    }
    if (manifest.assets.size() > Esp32RuntimeMaximumAssets) {
        return Result<Esp32CapabilitySummary>::failure(
            targetError(ErrorCode::CapacityExceeded,
                        "project declares more assets than the ESP32 runtime can index",
                        "manifest_assets")
                .addContext("count", std::to_string(manifest.assets.size()))
                .addContext("maximum", std::to_string(Esp32RuntimeMaximumAssets)));
    }

    Esp32CapabilitySummary summary;
    summary.assetCount = manifest.assets.size();
    for (const auto& asset : manifest.assets) {
        if (asset.path.size() > 127U || asset.type.size() > 31U) {
            return Result<Esp32CapabilitySummary>::failure(
                targetError(ErrorCode::CapacityExceeded,
                            "asset metadata exceeds the ESP32 runtime text capacity",
                            "manifest_asset")
                    .addContext("asset", asset.path)
                    .addContext("asset_guid", asset.guid.toString())
                    .addContext("path_bytes", std::to_string(asset.path.size()))
                    .addContext("type_bytes", std::to_string(asset.type.size())));
        }
        const auto capability = classifyEsp32RuntimeAsset(asset.type, asset.path);
        if (capability == Esp32AssetCapability::VisualScriptVmUnavailable) {
            return Result<Esp32CapabilitySummary>::failure(
                targetError(ErrorCode::InvalidState,
                            "ESP32 firmware has no visual-script VM; remove the graph or provide "
                            "a portable Scripts/ESP32 native module",
                            "visual_script")
                    .addContext("asset", asset.path)
                    .addContext("asset_guid", asset.guid.toString())
                    .addContext("asset_type", asset.type));
        }
        if (capability == Esp32AssetCapability::Unsupported) {
            return Result<Esp32CapabilitySummary>::failure(
                targetError(ErrorCode::InvalidState,
                            "project asset type or representation is not supported by the ESP32 "
                            "runtime",
                            "asset_type")
                    .addContext("asset", asset.path)
                    .addContext("asset_guid", asset.guid.toString())
                    .addContext("asset_type", asset.type)
                    .addContext("supported_asset_types",
                                "binary(.bin/.dat/.raw), image, racer.track, raycast.map"));
        }
    }

    for (const auto& context : manifest.inputContexts) {
        if (context.name.size() > 31U) {
            return Result<Esp32CapabilitySummary>::failure(
                targetError(ErrorCode::CapacityExceeded,
                            "input context name exceeds the ESP32 runtime text capacity",
                            "input_context")
                    .addContext("context", context.name)
                    .addContext("maximum_bytes", "31"));
        }
        const auto validateValues = [&](const std::vector<InputValueDefinition>& values,
                                        std::string_view kind) -> Result<void> {
            for (const auto& value : values) {
                if (++summary.inputValueCount > Esp32RuntimeMaximumInputValues) {
                    return Result<void>::failure(
                        targetError(ErrorCode::CapacityExceeded,
                                    "project defines more input values than the ESP32 runtime can "
                                    "index",
                                    "input_values")
                            .addContext("maximum",
                                        std::to_string(Esp32RuntimeMaximumInputValues)));
                }
                if (value.name.size() > 31U) {
                    return Result<void>::failure(
                        targetError(ErrorCode::CapacityExceeded,
                                    "input value name exceeds the ESP32 runtime text capacity",
                                    "input_value")
                            .addContext("context", context.name)
                            .addContext("kind", std::string(kind))
                            .addContext("value", value.name)
                            .addContext("maximum_bytes", "31"));
                }
                for (const auto& binding : value.bindings) {
                    if (++summary.inputBindingCount > Esp32RuntimeMaximumInputBindings) {
                        return Result<void>::failure(
                            targetError(ErrorCode::CapacityExceeded,
                                        "project defines more input bindings than the ESP32 "
                                        "runtime can index",
                                        "input_bindings")
                                .addContext("maximum",
                                            std::to_string(Esp32RuntimeMaximumInputBindings)));
                    }
                    if (binding.control.size() > 31U) {
                        return Result<void>::failure(
                            targetError(ErrorCode::CapacityExceeded,
                                        "input control name exceeds the ESP32 runtime text capacity",
                                        "input_binding")
                                .addContext("context", context.name)
                                .addContext("value", value.name)
                                .addContext("control", binding.control)
                                .addContext("maximum_bytes", "31"));
                    }
                }
            }
            return Result<void>::success();
        };
        auto actions = validateValues(context.actions, "action");
        if (!actions)
            return Result<Esp32CapabilitySummary>::failure(actions.error());
        auto axes = validateValues(context.axes, "axis");
        if (!axes)
            return Result<Esp32CapabilitySummary>::failure(axes.error());
    }

    summary.entityCount = startupScene.entityCount();
    if (summary.entityCount > Esp32RuntimeMaximumEntities) {
        return Result<Esp32CapabilitySummary>::failure(
            targetError(ErrorCode::CapacityExceeded,
                        "startup scene has more entities than the ESP32 runtime can allocate",
                        "scene_entities")
                .addContext("scene", startupScene.name())
                .addContext("count", std::to_string(summary.entityCount))
                .addContext("maximum", std::to_string(Esp32RuntimeMaximumEntities)));
    }
    if (startupScene.name().size() > 63U) {
        return Result<Esp32CapabilitySummary>::failure(
            targetError(ErrorCode::CapacityExceeded,
                        "scene name exceeds the ESP32 runtime text capacity", "scene_name")
                .addContext("scene", startupScene.name())
                .addContext("maximum_bytes", "63"));
    }

    for (const auto* entity : startupScene.entities()) {
        if (entity->name().size() > 39U) {
            return Result<Esp32CapabilitySummary>::failure(
                entityError(ErrorCode::CapacityExceeded,
                            "entity name exceeds the ESP32 runtime text capacity", "entity_name",
                            *entity)
                    .addContext("maximum_bytes", "39"));
        }
        if (entity->transform().parent()) {
            return Result<Esp32CapabilitySummary>::failure(
                entityError(ErrorCode::InvalidState,
                            "ESP32 runtime does not apply scene parent transforms",
                            "scene_hierarchy", *entity)
                    .addContext("parent_guid", entity->transform().parent()->toString()));
        }
        const auto rotation = entity->transform().localRotation();
        const auto scale = entity->transform().localScale();
        if (std::fabs(rotation.x) > 0.000001F || std::fabs(rotation.y) > 0.000001F ||
            std::fabs(scale.z - 1.0F) > 0.000001F) {
            return Result<Esp32CapabilitySummary>::failure(
                entityError(ErrorCode::InvalidState,
                            "ESP32 runtime supports only Z rotation and XY scale",
                            "transform_dimensions", *entity));
        }
        const auto components = entity->components();
        if (components.size() > Esp32RuntimeMaximumComponentsPerEntity) {
            return Result<Esp32CapabilitySummary>::failure(
                entityError(ErrorCode::CapacityExceeded,
                            "entity has more components than the ESP32 scene reader accepts",
                            "entity_components", *entity)
                    .addContext("count", std::to_string(components.size()))
                    .addContext("maximum",
                                std::to_string(Esp32RuntimeMaximumComponentsPerEntity)));
        }
        for (const auto* component : components) {
            ++summary.componentCount;
            const auto name = component->metadata() == nullptr
                                  ? normalizedComponentName(component->typeName())
                                  : std::string_view(component->metadata()->name);
            const auto capability = classifyEsp32RuntimeComponent(name);
            if (capability != Esp32ComponentCapability::Supported) {
                const auto known = capability == Esp32ComponentCapability::KnownButNotPorted;
                return Result<Esp32CapabilitySummary>::failure(
                    entityError(ErrorCode::InvalidState,
                                known ? "scene component is known to Studio but is not ported to "
                                        "the ESP32 runtime"
                                      : "scene component is unknown to the ESP32 capability "
                                        "contract",
                                "scene_component", *entity)
                        .addContext("component", std::string(name))
                        .addContext("component_type_id", component->typeId().toString())
                        .addContext("reason", known ? "known_but_not_ported" : "unknown")
                        .addContext("supported_components",
                                    "Transform, Camera, SpriteRenderer, CharacterBody2D, "
                                    "VehicleController, RaycastMap, FirstPersonController"));
            }
            auto valid = validateSupportedComponent(manifest, *component, name, *entity);
            if (!valid)
                return Result<Esp32CapabilitySummary>::failure(valid.error());
        }
    }
    return Result<Esp32CapabilitySummary>::success(summary);
}

} // namespace fabgl::project
