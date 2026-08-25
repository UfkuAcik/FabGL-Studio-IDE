#include "fabgl/scene/builtin_components.h"

#include "fabgl/scene/transform_component.h"

#include <algorithm>
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
        metadata.properties.push_back(property(
            "viewport", PropertyType::Rect, PropertyValue(Rect{0.0F, 0.0F, 1.0F, 1.0F}), "Camera"));
        metadata.properties.push_back(property("projection", PropertyType::Enumeration,
                                               PropertyValue(std::int64_t{0}), "Camera"));
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
        metadata.properties.push_back(
            property("cellSize", PropertyType::Fixed, PropertyValue(Fixed(1)), "Rendering"));
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
        auto shape = property("shape", PropertyType::Enumeration,
                              PropertyValue(std::int64_t{0}), "Physics");
        shape.enumOptions = {{0, "AABB"}, {1, "Circle"}, {2, "Point"}};
        shape.tooltip = "Collision primitive used by the portable 2D physics runtime.";
        metadata.properties.push_back(std::move(shape));
        metadata.properties.push_back(
            property("size", PropertyType::Vec2, PropertyValue(Vec2{1.0F, 1.0F}), "Physics"));
        metadata.properties.push_back(
            property("radius", PropertyType::Float, PropertyValue(0.5), "Physics"));
        metadata.properties.push_back(
            property("trigger", PropertyType::Boolean, PropertyValue(false), "Physics"));
        metadata.properties.push_back(
            property("layer", PropertyType::BitFlags, PropertyValue(std::uint64_t{1}), "Physics"));
        metadata.properties.push_back(property("collisionMask", PropertyType::BitFlags,
                                               PropertyValue(std::uint64_t{0xFFFFFFFFU}),
                                               "Physics"));
    } else if (name == "Collider3D") {
        metadata.properties.push_back(
            property("size", PropertyType::Vec3, PropertyValue(Vec3{1.0F, 1.0F, 1.0F}), "Physics"));
        metadata.properties.push_back(
            property("trigger", PropertyType::Boolean, PropertyValue(false), "Physics"));
        metadata.properties.push_back(property("orientation", PropertyType::Quaternion,
                                               PropertyValue(Quaternion{}), "Physics"));
    } else if (name == "Rigidbody2D") {
        auto bodyType = property("bodyType", PropertyType::Enumeration,
                                 PropertyValue(std::int64_t{2}), "Physics");
        bodyType.enumOptions = {{0, "Static"}, {1, "Kinematic"}, {2, "Dynamic"}};
        metadata.properties.push_back(std::move(bodyType));
        metadata.properties.push_back(
            property("velocity", PropertyType::Vec2, PropertyValue(Vec2{}), "Physics"));
        metadata.properties.push_back(
            property("mass", PropertyType::Float, PropertyValue(1.0), "Physics"));
        metadata.properties.push_back(
            property("gravityScale", PropertyType::Float, PropertyValue(1.0), "Physics"));
        metadata.properties.push_back(
            property("restitution", PropertyType::Float, PropertyValue(0.0), "Physics"));
        metadata.properties.push_back(
            property("friction", PropertyType::Float, PropertyValue(0.5), "Physics"));
    } else if (name == "CharacterBody2D" || name == "CharacterBody3D") {
        metadata.properties.push_back(
            property("moveSpeed", PropertyType::Float, PropertyValue(4.0), "Movement"));
        if (name == "CharacterBody2D") {
            auto mode = property("movementMode", PropertyType::Enumeration,
                                 PropertyValue(std::int64_t{0}), "Movement");
            mode.enumOptions = {{0, "Platformer"}, {1, "TopDown"}};
            metadata.properties.push_back(std::move(mode));
        }
        metadata.properties.push_back(property("moveXAxis", PropertyType::String,
                                               PropertyValue(std::string("MoveX")), "Input"));
        metadata.properties.push_back(property("moveYAxis", PropertyType::String,
                                               PropertyValue(std::string("MoveY")), "Input"));
        metadata.properties.push_back(property("primaryAction", PropertyType::String,
                                               PropertyValue(std::string("Jump")), "Input"));
    } else if (name == "ScriptComponent") {
        metadata.properties.push_back(property("script", PropertyType::AssetReference,
                                               PropertyValue(AssetGuid{}), "Scripting"));
        metadata.properties.push_back(property("class", PropertyType::String,
                                               PropertyValue(std::string{}), "Scripting"));
        auto notes = property("notes", PropertyType::String, PropertyValue(std::string{}),
                              "Scripting");
        notes.editorHint = PropertyEditorHint::Multiline;
        metadata.properties.push_back(std::move(notes));
    } else if (name == "VisualScriptComponent") {
        metadata.properties.push_back(property("graph", PropertyType::AssetReference,
                                               PropertyValue(AssetGuid{}), "Scripting"));
        metadata.properties.push_back(property("triggerAction", PropertyType::ActionReference,
                                               PropertyValue(ActionReference{}), "Scripting"));
        metadata.properties.push_back(property("completionEvent", PropertyType::EventReference,
                                               PropertyValue(EventReference{}), "Scripting"));
    } else if (name == "Animator") {
        metadata.properties.push_back(property("controller", PropertyType::AssetReference,
                                               PropertyValue(AssetGuid{}), "Animation"));
        metadata.properties.push_back(property("initialState", PropertyType::String,
                                               PropertyValue(std::string{}), "Animation"));
        metadata.properties.push_back(
            property("speed", PropertyType::Float, PropertyValue(1.0), "Animation"));
        metadata.properties.push_back(
            property("playing", PropertyType::Boolean, PropertyValue(true), "Animation"));
    } else if (name == "PrefabInstanceLink") {
        auto state =
            property("state", PropertyType::String, PropertyValue(std::string{}), "Prefab Link");
        state.editorHint = PropertyEditorHint::Multiline;
        state.tooltip = "Canonical FabGL Studio prefab linkage. Managed by the Prefab Editor.";
        metadata.properties.push_back(std::move(state));
    } else if (name == "UITransform") {
        auto widgetType = property("widgetType", PropertyType::Enumeration,
                                   PropertyValue(std::int64_t{0}), "UI");
        widgetType.enumOptions = {{0, "Panel"},   {1, "Image"},   {2, "Text"},
                                  {3, "Button"},  {4, "Toggle"},  {5, "Slider"},
                                  {6, "Progress"}, {7, "List"},   {8, "Layout"}};
        metadata.properties.push_back(std::move(widgetType));
        metadata.properties.push_back(
            property("anchorMinimum", PropertyType::Vec2, PropertyValue(Vec2{}), "Layout"));
        metadata.properties.push_back(
            property("anchorMaximum", PropertyType::Vec2, PropertyValue(Vec2{}), "Layout"));
        metadata.properties.push_back(
            property("offsetMinimum", PropertyType::Vec2, PropertyValue(Vec2{}), "Layout"));
        metadata.properties.push_back(property("offsetMaximum", PropertyType::Vec2,
                                               PropertyValue(Vec2{100.0F, 30.0F}), "Layout"));
        metadata.properties.push_back(
            property("visible", PropertyType::Boolean, PropertyValue(true), "UI"));
        metadata.properties.push_back(
            property("text", PropertyType::String, PropertyValue(std::string{}), "UI"));
        metadata.properties.push_back(
            property("minimum", PropertyType::Float, PropertyValue(0.0), "UI"));
        metadata.properties.push_back(
            property("maximum", PropertyType::Float, PropertyValue(1.0), "UI"));
        metadata.properties.push_back(
            property("value", PropertyType::Float, PropertyValue(0.0), "UI"));
        metadata.properties.push_back(
            property("step", PropertyType::Float, PropertyValue(0.1), "UI"));
        metadata.properties.push_back(
            property("checked", PropertyType::Boolean, PropertyValue(false), "UI"));
        metadata.properties.push_back(
            property("items", PropertyType::String, PropertyValue(std::string{}), "UI"));
        metadata.properties.push_back(property("selectedIndex", PropertyType::SignedInteger,
                                               PropertyValue(std::int64_t{-1}), "UI"));
        auto direction = property("layoutDirection", PropertyType::Enumeration,
                                  PropertyValue(std::int64_t{1}), "Layout");
        direction.enumOptions = {{0, "Horizontal"}, {1, "Vertical"}};
        metadata.properties.push_back(std::move(direction));
        metadata.properties.push_back(
            property("layoutSpacing", PropertyType::Float, PropertyValue(-1.0), "Layout"));
        metadata.properties.push_back(
            property("layoutPadding", PropertyType::Float, PropertyValue(-1.0), "Layout"));
    } else if (name == "UIImage") {
        metadata.properties.push_back(
            property("image", PropertyType::AssetReference, PropertyValue(AssetGuid{}), "UI"));
        metadata.properties.push_back(
            property("color", PropertyType::Color, PropertyValue(Color{255, 255, 255, 255}), "UI"));
        auto frames = property("frames", PropertyType::List,
                               PropertyValue(PropertyList{PropertyType::AssetReference, {}}), "UI");
        frames.listElementType = PropertyType::AssetReference;
        frames.assetTypeFilter = "image";
        metadata.properties.push_back(std::move(frames));
    } else if (name == "UIText") {
        metadata.properties.push_back(
            property("text", PropertyType::String, PropertyValue(std::string{}), "UI"));
        metadata.properties.push_back(
            property("color", PropertyType::Color, PropertyValue(Color{255, 255, 255, 255}), "UI"));
    } else if (name == "UIButton") {
        metadata.properties.push_back(
            property("interactable", PropertyType::Boolean, PropertyValue(true), "UI"));
    } else if (name == "Light") {
        auto intensity =
            property("intensity", PropertyType::Float, PropertyValue(1.0), "Lighting");
        intensity.numeric = NumericConstraints{0.0, 8.0, 0.05};
        intensity.editorHint = PropertyEditorHint::Slider;
        metadata.properties.push_back(std::move(intensity));
        metadata.properties.push_back(property(
            "color", PropertyType::Color, PropertyValue(Color{255, 255, 255, 255}), "Lighting"));
        metadata.properties.push_back(property(
            "falloff", PropertyType::Curve,
            PropertyValue(Curve{{CurvePoint{0.0, 1.0}, CurvePoint{1.0, 0.0}}}), "Lighting"));
    } else if (name == "ParticleEmitter") {
        metadata.properties.push_back(
            property("rate", PropertyType::Float, PropertyValue(10.0), "Particles"));
        metadata.properties.push_back(property("maxParticles", PropertyType::UnsignedInteger,
                                               PropertyValue(std::uint64_t{1000}), "Particles"));
        metadata.properties.push_back(
            property("burstOnStart", PropertyType::UnsignedInteger,
                     PropertyValue(std::uint64_t{0}), "Particles"));
        metadata.properties.push_back(
            property("lifetime", PropertyType::Float, PropertyValue(1.0), "Particles"));
        metadata.properties.push_back(
            property("velocity", PropertyType::Vec2, PropertyValue(Vec2{}), "Particles"));
        metadata.properties.push_back(
            property("acceleration", PropertyType::Vec2, PropertyValue(Vec2{}), "Particles"));
        metadata.properties.push_back(property("startColor", PropertyType::Color,
                                               PropertyValue(Color{255, 255, 255, 255}),
                                               "Particles"));
        metadata.properties.push_back(property("endColor", PropertyType::Color,
                                               PropertyValue(Color{255, 255, 255, 0}),
                                               "Particles"));
        metadata.properties.push_back(
            property("startSize", PropertyType::Float, PropertyValue(1.0), "Particles"));
        metadata.properties.push_back(
            property("endSize", PropertyType::Float, PropertyValue(0.0), "Particles"));
        metadata.properties.push_back(
            property("startRotation", PropertyType::Float, PropertyValue(0.0), "Particles"));
        metadata.properties.push_back(
            property("endRotation", PropertyType::Float, PropertyValue(0.0), "Particles"));
        metadata.properties.push_back(
            property("cullOutsideBounds", PropertyType::Boolean, PropertyValue(false),
                     "Particles"));
        metadata.properties.push_back(property("cullingBounds", PropertyType::Rect,
                                               PropertyValue(Rect{}), "Particles"));
    } else if (name == "Health") {
        metadata.properties.push_back(property("current", PropertyType::SignedInteger,
                                               PropertyValue(std::int64_t{100}), "Gameplay"));
        metadata.properties.push_back(property("maximum", PropertyType::SignedInteger,
                                               PropertyValue(std::int64_t{100}), "Gameplay"));
    } else if (name == "DamageReceiver") {
        metadata.properties.push_back(
            property("multiplier", PropertyType::Float, PropertyValue(1.0), "Gameplay"));
        metadata.properties.push_back(property(
            "responseCurve", PropertyType::AnimationCurve,
            PropertyValue(PropertyAnimationCurve{{AnimationCurveKey{0.0, 0.0, 0.0, 0.0},
                                                   AnimationCurveKey{1.0, 1.0, 0.0, 0.0}}}),
            "Gameplay"));
    } else if (name == "NavigationAgent") {
        metadata.properties.push_back(
            property("speed", PropertyType::Float, PropertyValue(3.0), "Navigation"));
        metadata.properties.push_back(property("target", PropertyType::EntityReference,
                                               PropertyValue(EntityGuid{}), "Navigation"));
        metadata.properties.push_back(property("targetComponent",
                                               PropertyType::ComponentReference,
                                               PropertyValue(ComponentReference{}), "Navigation"));
        metadata.properties.push_back(
            property("arrivalRadius", PropertyType::Float, PropertyValue(0.1), "Navigation"));
        metadata.properties.push_back(property("requireLineOfSight", PropertyType::Boolean,
                                               PropertyValue(false), "Navigation"));
        metadata.properties.push_back(property("obstacleMask", PropertyType::BitFlags,
                                               PropertyValue(std::uint64_t{0xFFFFFFFFU}),
                                               "Navigation"));
    } else if (name == "VehicleController") {
        metadata.properties.push_back(
            property("acceleration", PropertyType::Float, PropertyValue(8.0), "Vehicle"));
        metadata.properties.push_back(property("track", PropertyType::AssetReference,
                                               PropertyValue(AssetGuid{}), "Vehicle"));
        metadata.properties.push_back(property("steerAxis", PropertyType::String,
                                               PropertyValue(std::string("Steer")), "Input"));
        metadata.properties.push_back(property("throttleAxis", PropertyType::String,
                                               PropertyValue(std::string("Throttle")), "Input"));
        metadata.properties.push_back(property("brakeAction", PropertyType::String,
                                               PropertyValue(std::string("Brake")), "Input"));
        metadata.properties.push_back(property("driftAction", PropertyType::String,
                                               PropertyValue(std::string("Handbrake")), "Input"));
    } else if (name == "FirstPersonController" || name == "ThirdPersonController") {
        metadata.properties.push_back(
            property("moveSpeed", PropertyType::Float, PropertyValue(4.0), "Controller"));
        metadata.properties.push_back(
            property("lookSensitivity", PropertyType::Float, PropertyValue(1.0), "Controller"));
        metadata.properties.push_back(property("moveXAxis", PropertyType::String,
                                               PropertyValue(std::string("MoveX")), "Input"));
        metadata.properties.push_back(property("moveYAxis", PropertyType::String,
                                               PropertyValue(std::string("MoveY")), "Input"));
        metadata.properties.push_back(property("lookXAxis", PropertyType::String,
                                               PropertyValue(std::string("LookX")), "Input"));
        metadata.properties.push_back(property("primaryAction", PropertyType::String,
                                               PropertyValue(std::string("Fire")), "Input"));
    }
    return metadata;
}

bool valueMatches(PropertyType type, const PropertyValue& value) noexcept {
    return propertyValueMatches(type, value);
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
    auto valid = validatePropertyValue(*propertyMetadata, value);
    if (!valid)
        return valid;
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
                                                "PrefabInstanceLink",
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

Result<std::unique_ptr<DataComponent>>
createBuiltinDataComponent(const ReflectionRegistry& registry, ComponentTypeGuid typeId) {
    const auto* metadata = registry.find(typeId);
    if (metadata == nullptr) {
        return Result<std::unique_ptr<DataComponent>>::failure(
            Error(ErrorCode::NotFound, "builtin component metadata was not found")
                .addContext("type_id", typeId.toString()));
    }
    if (typeId == TransformComponent::staticTypeId()) {
        return Result<std::unique_ptr<DataComponent>>::failure(Error(
            ErrorCode::InvalidArgument, "Transform uses its specialized component implementation"));
    }
    const auto prefix = std::string_view("fabgl.");
    if (metadata->name.size() <= prefix.size() ||
        metadata->name.compare(0, prefix.size(), prefix) != 0) {
        return Result<std::unique_ptr<DataComponent>>::failure(
            Error(ErrorCode::InvalidArgument, "component type is not a registered builtin")
                .addContext("type_id", typeId.toString())
                .addContext("component", metadata->name));
    }
    const auto shortName = std::string_view(metadata->name).substr(prefix.size());
    const auto& names = builtinComponentNames();
    if (std::find(names.begin(), names.end(), shortName) == names.end()) {
        return Result<std::unique_ptr<DataComponent>>::failure(
            Error(ErrorCode::InvalidArgument, "component type is not a registered builtin")
                .addContext("type_id", typeId.toString())
                .addContext("component", metadata->name));
    }
    return Result<std::unique_ptr<DataComponent>>::success(
        std::make_unique<DataComponent>(*metadata));
}

} // namespace fabgl
