#include "fabgl/scene/builtin_components.h"

#include "fabgl/scene/transform_component.h"

#include <utility>

namespace fabgl {
namespace {

PropertyMetadata property(std::string name, PropertyType type, PropertyValue defaultValue,
                          std::string category = "General") {
    PropertyMetadata metadata;
    metadata.name = name;
    metadata.displayName = std::move(name);
    metadata.type = type;
    metadata.flags = PropertyFlags::Serialize | PropertyFlags::RuntimeEditable;
    metadata.defaultValue = std::move(defaultValue);
    metadata.category = std::move(category);
    return metadata;
}

TypeMetadata dataType(std::string_view shortName) {
    TypeMetadata metadata;
    metadata.typeId = ComponentTypeGuid::fromStableName(std::string("fabgl.component.") +
                                                        std::string(shortName) + ".v1");
    metadata.name = std::string("fabgl.") + std::string(shortName);
    metadata.displayName = std::string(shortName);
    return metadata;
}

void addCommonEnabled(TypeMetadata& metadata) {
    metadata.properties.push_back(property("enabled", PropertyType::Boolean, PropertyValue(true)));
}

TypeMetadata metadataFor(std::string_view name) {
    auto metadata = dataType(name);
    addCommonEnabled(metadata);
    if (name == "Camera") {
        metadata.properties.push_back(
            property("orthographic", PropertyType::Boolean, PropertyValue(true), "Camera"));
        metadata.properties.push_back(
            property("size", PropertyType::Float, PropertyValue(5.0), "Camera"));
        metadata.properties.push_back(
            property("clearColor", PropertyType::Color, PropertyValue(Color{}), "Camera"));
    } else if (name == "SpriteRenderer") {
        metadata.properties.push_back(property("sprite", PropertyType::AssetReference,
                                               PropertyValue(AssetGuid{}), "Rendering"));
        metadata.properties.push_back(property(
            "tint", PropertyType::Color, PropertyValue(Color{255, 255, 255, 255}), "Rendering"));
    } else if (name == "TilemapRenderer") {
        metadata.properties.push_back(property("tilemap", PropertyType::AssetReference,
                                               PropertyValue(AssetGuid{}), "Rendering"));
    } else if (name == "MeshRenderer") {
        metadata.properties.push_back(property("mesh", PropertyType::AssetReference,
                                               PropertyValue(AssetGuid{}), "Rendering"));
        metadata.properties.push_back(property("material", PropertyType::AssetReference,
                                               PropertyValue(AssetGuid{}), "Rendering"));
    } else if (name == "RaycastMap") {
        metadata.properties.push_back(
            property("map", PropertyType::AssetReference, PropertyValue(AssetGuid{}), "Rendering"));
    } else if (name == "AudioSource") {
        metadata.properties.push_back(
            property("clip", PropertyType::AssetReference, PropertyValue(AssetGuid{}), "Audio"));
        metadata.properties.push_back(
            property("volume", PropertyType::Float, PropertyValue(1.0), "Audio"));
        metadata.properties.push_back(
            property("pitch", PropertyType::Float, PropertyValue(1.0), "Audio"));
        metadata.properties.push_back(
            property("loop", PropertyType::Boolean, PropertyValue(false), "Audio"));
    } else if (name == "Collider2D") {
        metadata.properties.push_back(
            property("size", PropertyType::Vec2, PropertyValue(Vec2{1.0F, 1.0F}), "Physics"));
        metadata.properties.push_back(
            property("trigger", PropertyType::Boolean, PropertyValue(false), "Physics"));
        metadata.properties.push_back(
            property("layer", PropertyType::BitFlags, PropertyValue(std::uint64_t{1}), "Physics"));
    } else if (name == "Collider3D") {
        metadata.properties.push_back(
            property("size", PropertyType::Vec3, PropertyValue(Vec3{1.0F, 1.0F, 1.0F}), "Physics"));
        metadata.properties.push_back(
            property("trigger", PropertyType::Boolean, PropertyValue(false), "Physics"));
    } else if (name == "Rigidbody2D") {
        metadata.properties.push_back(
            property("mass", PropertyType::Float, PropertyValue(1.0), "Physics"));
        metadata.properties.push_back(
            property("gravityScale", PropertyType::Float, PropertyValue(1.0), "Physics"));
    } else if (name == "CharacterBody2D" || name == "CharacterBody3D") {
        metadata.properties.push_back(
            property("moveSpeed", PropertyType::Float, PropertyValue(4.0), "Movement"));
    } else if (name == "ScriptComponent") {
        metadata.properties.push_back(property("script", PropertyType::AssetReference,
                                               PropertyValue(AssetGuid{}), "Scripting"));
    } else if (name == "VisualScriptComponent") {
        metadata.properties.push_back(property("graph", PropertyType::AssetReference,
                                               PropertyValue(AssetGuid{}), "Scripting"));
    } else if (name == "Animator") {
        metadata.properties.push_back(property("controller", PropertyType::AssetReference,
                                               PropertyValue(AssetGuid{}), "Animation"));
    } else if (name == "UITransform") {
        metadata.properties.push_back(
            property("anchorMinimum", PropertyType::Vec2, PropertyValue(Vec2{}), "Layout"));
        metadata.properties.push_back(
            property("anchorMaximum", PropertyType::Vec2, PropertyValue(Vec2{}), "Layout"));
    } else if (name == "UIImage") {
        metadata.properties.push_back(
            property("image", PropertyType::AssetReference, PropertyValue(AssetGuid{}), "UI"));
        metadata.properties.push_back(
            property("color", PropertyType::Color, PropertyValue(Color{255, 255, 255, 255}), "UI"));
    } else if (name == "UIText") {
        metadata.properties.push_back(
            property("text", PropertyType::String, PropertyValue(std::string{}), "UI"));
        metadata.properties.push_back(
            property("color", PropertyType::Color, PropertyValue(Color{255, 255, 255, 255}), "UI"));
    } else if (name == "UIButton") {
        metadata.properties.push_back(
            property("interactable", PropertyType::Boolean, PropertyValue(true), "UI"));
    } else if (name == "Light") {
        metadata.properties.push_back(
            property("intensity", PropertyType::Float, PropertyValue(1.0), "Lighting"));
        metadata.properties.push_back(property(
            "color", PropertyType::Color, PropertyValue(Color{255, 255, 255, 255}), "Lighting"));
    } else if (name == "ParticleEmitter") {
        metadata.properties.push_back(
            property("rate", PropertyType::Float, PropertyValue(10.0), "Particles"));
    } else if (name == "Health") {
        metadata.properties.push_back(property("current", PropertyType::SignedInteger,
                                               PropertyValue(std::int64_t{100}), "Gameplay"));
        metadata.properties.push_back(property("maximum", PropertyType::SignedInteger,
                                               PropertyValue(std::int64_t{100}), "Gameplay"));
    } else if (name == "DamageReceiver") {
        metadata.properties.push_back(
            property("multiplier", PropertyType::Float, PropertyValue(1.0), "Gameplay"));
    } else if (name == "NavigationAgent") {
        metadata.properties.push_back(
            property("speed", PropertyType::Float, PropertyValue(3.0), "Navigation"));
        metadata.properties.push_back(property("target", PropertyType::EntityReference,
                                               PropertyValue(EntityGuid{}), "Navigation"));
    } else if (name == "VehicleController") {
        metadata.properties.push_back(
            property("acceleration", PropertyType::Float, PropertyValue(8.0), "Vehicle"));
    } else if (name == "FirstPersonController" || name == "ThirdPersonController") {
        metadata.properties.push_back(
            property("moveSpeed", PropertyType::Float, PropertyValue(4.0), "Controller"));
        metadata.properties.push_back(
            property("lookSensitivity", PropertyType::Float, PropertyValue(1.0), "Controller"));
    }
    return metadata;
}

bool valueMatches(PropertyType type, const PropertyValue& value) noexcept {
    switch (type) {
    case PropertyType::Boolean:
        return std::holds_alternative<bool>(value);
    case PropertyType::SignedInteger:
    case PropertyType::Enumeration:
        return std::holds_alternative<std::int64_t>(value);
    case PropertyType::UnsignedInteger:
    case PropertyType::BitFlags:
        return std::holds_alternative<std::uint64_t>(value);
    case PropertyType::Float:
        return std::holds_alternative<double>(value);
    case PropertyType::Fixed:
        return std::holds_alternative<Fixed>(value);
    case PropertyType::String:
        return std::holds_alternative<std::string>(value);
    case PropertyType::Vec2:
        return std::holds_alternative<Vec2>(value);
    case PropertyType::Vec3:
        return std::holds_alternative<Vec3>(value);
    case PropertyType::Rect:
        return std::holds_alternative<Rect>(value);
    case PropertyType::Color:
        return std::holds_alternative<Color>(value);
    case PropertyType::AssetReference:
        return std::holds_alternative<AssetGuid>(value);
    case PropertyType::EntityReference:
        return std::holds_alternative<EntityGuid>(value);
    }
    return false;
}

} // namespace

DataComponent::DataComponent(TypeMetadata metadata) : metadata_(std::move(metadata)) {
    for (auto& propertyMetadata : metadata_.properties) {
        if (propertyMetadata.defaultValue)
            values_[propertyMetadata.name] = *propertyMetadata.defaultValue;
        const auto propertyName = propertyMetadata.name;
        propertyMetadata.reader = [propertyName](const void* instance) {
            return static_cast<const DataComponent*>(instance)->get(propertyName);
        };
        propertyMetadata.writer = [propertyName](void* instance, const PropertyValue& value) {
            return static_cast<DataComponent*>(instance)->set(propertyName, value);
        };
    }
}

Result<PropertyValue> DataComponent::get(std::string_view propertyName) const {
    const auto value = values_.find(std::string(propertyName));
    if (value == values_.end()) {
        return Result<PropertyValue>::failure(
            Error(ErrorCode::NotFound, "data component property has no value")
                .addContext("property", std::string(propertyName)));
    }
    return Result<PropertyValue>::success(value->second);
}

Result<void> DataComponent::set(std::string_view propertyName, PropertyValue value) {
    const auto* propertyMetadata = metadata_.findProperty(propertyName);
    if (propertyMetadata == nullptr) {
        return Result<void>::failure(
            Error(ErrorCode::NotFound, "data component property was not found")
                .addContext("property", std::string(propertyName)));
    }
    if (hasFlag(propertyMetadata->flags, PropertyFlags::ReadOnly)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "data component property is read-only"));
    }
    if (!valueMatches(propertyMetadata->type, value)) {
        return Result<void>::failure(
            Error(ErrorCode::TypeMismatch, "data component property type mismatch"));
    }
    values_[std::string(propertyName)] = std::move(value);
    return Result<void>::success();
}

const std::vector<std::string>& builtinComponentNames() {
    static const std::vector<std::string> names{"Transform",
                                                "Camera",
                                                "SpriteRenderer",
                                                "TilemapRenderer",
                                                "MeshRenderer",
                                                "RaycastMap",
                                                "AudioSource",
                                                "AudioListener",
                                                "Collider2D",
                                                "Collider3D",
                                                "Rigidbody2D",
                                                "CharacterBody2D",
                                                "CharacterBody3D",
                                                "ScriptComponent",
                                                "VisualScriptComponent",
                                                "Animator",
                                                "UITransform",
                                                "UIImage",
                                                "UIText",
                                                "UIButton",
                                                "Light",
                                                "ParticleEmitter",
                                                "Health",
                                                "DamageReceiver",
                                                "NavigationAgent",
                                                "VehicleController",
                                                "FirstPersonController",
                                                "ThirdPersonController"};
    return names;
}

Result<void> registerBuiltinComponentTypes(ReflectionRegistry& registry) {
    TransformComponent transform;
    auto transformResult = registry.registerType(*transform.metadata());
    if (!transformResult)
        return transformResult;
    for (const auto& name : builtinComponentNames()) {
        if (name == "Transform")
            continue;
        auto registered = registry.registerType(metadataFor(name));
        if (!registered)
            return registered;
    }
    return Result<void>::success();
}

Result<std::unique_ptr<DataComponent>>
createBuiltinDataComponent(const ReflectionRegistry& registry, std::string_view shortName) {
    if (shortName == "Transform") {
        return Result<std::unique_ptr<DataComponent>>::failure(Error(
            ErrorCode::InvalidArgument, "Transform uses its specialized component implementation"));
    }
    const auto fullName = std::string("fabgl.") + std::string(shortName);
    const auto* metadata = registry.find(fullName);
    if (metadata == nullptr) {
        return Result<std::unique_ptr<DataComponent>>::failure(
            Error(ErrorCode::NotFound, "builtin component metadata was not found")
                .addContext("component", std::string(shortName)));
    }
    return Result<std::unique_ptr<DataComponent>>::success(
        std::make_unique<DataComponent>(*metadata));
}

} // namespace fabgl
