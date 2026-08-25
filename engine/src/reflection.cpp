#include "fabgl/reflection/reflection.h"

#include <cmath>
#include <limits>
#include <unordered_set>

namespace fabgl {
namespace {

Error valueError(std::string message, const PropertyMetadata& metadata) {
    return Error(ErrorCode::InvalidArgument, std::move(message)).addContext("property", metadata.name);
}

bool finite(EulerAngles value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool finite(Quaternion value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
           std::isfinite(value.w);
}

bool listElementMatches(PropertyType type, const PropertyListElement& value) noexcept {
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
    case PropertyType::EulerAngles:
        return std::holds_alternative<EulerAngles>(value);
    case PropertyType::Quaternion:
        return std::holds_alternative<Quaternion>(value);
    case PropertyType::Rect:
        return std::holds_alternative<Rect>(value);
    case PropertyType::Color:
        return std::holds_alternative<Color>(value);
    case PropertyType::AssetReference:
        return std::holds_alternative<AssetGuid>(value);
    case PropertyType::EntityReference:
        return std::holds_alternative<EntityGuid>(value);
    case PropertyType::ComponentReference:
        return std::holds_alternative<ComponentReference>(value);
    case PropertyType::ActionReference:
        return std::holds_alternative<ActionReference>(value);
    case PropertyType::EventReference:
        return std::holds_alternative<EventReference>(value);
    case PropertyType::List:
    case PropertyType::Curve:
    case PropertyType::AnimationCurve:
        return false;
    }
    return false;
}

Result<void> validateListElement(PropertyType type, const PropertyListElement& value,
                                 const PropertyMetadata& metadata) {
    if (!listElementMatches(type, value)) {
        return Result<void>::failure(valueError("property list element type mismatch", metadata));
    }
    if (const auto* number = std::get_if<double>(&value); number != nullptr && !std::isfinite(*number))
        return Result<void>::failure(valueError("property list contains a non-finite number", metadata));
    if (const auto* vector = std::get_if<Vec2>(&value); vector != nullptr &&
        (!std::isfinite(vector->x) || !std::isfinite(vector->y)))
        return Result<void>::failure(valueError("property list contains a non-finite Vec2", metadata));
    if (const auto* vector = std::get_if<Vec3>(&value); vector != nullptr &&
        (!std::isfinite(vector->x) || !std::isfinite(vector->y) || !std::isfinite(vector->z)))
        return Result<void>::failure(valueError("property list contains a non-finite Vec3", metadata));
    if (const auto* euler = std::get_if<EulerAngles>(&value); euler != nullptr && !finite(*euler))
        return Result<void>::failure(valueError("property list contains non-finite Euler angles", metadata));
    if (const auto* quaternion = std::get_if<Quaternion>(&value); quaternion != nullptr &&
        !finite(*quaternion))
        return Result<void>::failure(valueError("property list contains a non-finite quaternion", metadata));
    if (const auto* rectangle = std::get_if<Rect>(&value); rectangle != nullptr &&
        (!std::isfinite(rectangle->x) || !std::isfinite(rectangle->y) ||
         !std::isfinite(rectangle->width) || !std::isfinite(rectangle->height)))
        return Result<void>::failure(valueError("property list contains a non-finite Rect", metadata));
    if (const auto* text = std::get_if<std::string>(&value); text != nullptr &&
        text->size() > MaximumPropertyStringLength)
        return Result<void>::failure(valueError("property list string is too long", metadata));
    if (const auto* action = std::get_if<ActionReference>(&value); action != nullptr &&
        action->name.size() > MaximumActionOrEventNameLength)
        return Result<void>::failure(valueError("property list action name is too long", metadata));
    if (const auto* event = std::get_if<EventReference>(&value); event != nullptr &&
        event->name.size() > MaximumActionOrEventNameLength)
        return Result<void>::failure(valueError("property list event name is too long", metadata));
    if (const auto* reference = std::get_if<ComponentReference>(&value); reference != nullptr &&
        reference->entity.isNil() != reference->component.isNil())
        return Result<void>::failure(
            valueError("component reference must be either fully set or fully empty", metadata));
    return Result<void>::success();
}

std::optional<double> numericValue(PropertyType type, const PropertyValue& value) noexcept {
    switch (type) {
    case PropertyType::SignedInteger:
    case PropertyType::Enumeration:
        return static_cast<double>(std::get<std::int64_t>(value));
    case PropertyType::UnsignedInteger:
    case PropertyType::BitFlags:
        return static_cast<double>(std::get<std::uint64_t>(value));
    case PropertyType::Float:
        return std::get<double>(value);
    case PropertyType::Fixed:
        return static_cast<double>(std::get<Fixed>(value).toFloat());
    default:
        return std::nullopt;
    }
}

bool numericPropertyType(PropertyType type) noexcept {
    switch (type) {
    case PropertyType::SignedInteger:
    case PropertyType::UnsignedInteger:
    case PropertyType::Float:
    case PropertyType::Fixed:
    case PropertyType::Enumeration:
    case PropertyType::BitFlags:
        return true;
    default:
        return false;
    }
}

} // namespace

bool propertyValueMatches(PropertyType type, const PropertyValue& value) noexcept {
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
    case PropertyType::EulerAngles:
        return std::holds_alternative<EulerAngles>(value);
    case PropertyType::Quaternion:
        return std::holds_alternative<Quaternion>(value);
    case PropertyType::Rect:
        return std::holds_alternative<Rect>(value);
    case PropertyType::Color:
        return std::holds_alternative<Color>(value);
    case PropertyType::AssetReference:
        return std::holds_alternative<AssetGuid>(value);
    case PropertyType::EntityReference:
        return std::holds_alternative<EntityGuid>(value);
    case PropertyType::ComponentReference:
        return std::holds_alternative<ComponentReference>(value);
    case PropertyType::List:
        return std::holds_alternative<PropertyList>(value);
    case PropertyType::Curve:
        return std::holds_alternative<Curve>(value);
    case PropertyType::AnimationCurve:
        return std::holds_alternative<PropertyAnimationCurve>(value);
    case PropertyType::ActionReference:
        return std::holds_alternative<ActionReference>(value);
    case PropertyType::EventReference:
        return std::holds_alternative<EventReference>(value);
    }
    return false;
}

bool propertyListElementTypeSupported(PropertyType type) noexcept {
    switch (type) {
    case PropertyType::List:
    case PropertyType::Curve:
    case PropertyType::AnimationCurve:
        return false;
    default:
        return true;
    }
}

Result<void> validatePropertyValue(const PropertyMetadata& metadata, const PropertyValue& value) {
    if (!propertyValueMatches(metadata.type, value)) {
        return Result<void>::failure(valueError("property value type mismatch", metadata));
    }
    if (const auto* number = std::get_if<double>(&value); number != nullptr && !std::isfinite(*number))
        return Result<void>::failure(valueError("property value must be finite", metadata));
    if (const auto* vector = std::get_if<Vec2>(&value); vector != nullptr &&
        (!std::isfinite(vector->x) || !std::isfinite(vector->y)))
        return Result<void>::failure(valueError("Vec2 property value must be finite", metadata));
    if (const auto* vector = std::get_if<Vec3>(&value); vector != nullptr &&
        (!std::isfinite(vector->x) || !std::isfinite(vector->y) || !std::isfinite(vector->z)))
        return Result<void>::failure(valueError("Vec3 property value must be finite", metadata));
    if (const auto* euler = std::get_if<EulerAngles>(&value); euler != nullptr && !finite(*euler))
        return Result<void>::failure(valueError("Euler angle property value must be finite", metadata));
    if (const auto* quaternion = std::get_if<Quaternion>(&value); quaternion != nullptr &&
        !finite(*quaternion))
        return Result<void>::failure(valueError("quaternion property value must be finite", metadata));
    if (const auto* rectangle = std::get_if<Rect>(&value); rectangle != nullptr &&
        (!std::isfinite(rectangle->x) || !std::isfinite(rectangle->y) ||
         !std::isfinite(rectangle->width) || !std::isfinite(rectangle->height)))
        return Result<void>::failure(valueError("Rect property value must be finite", metadata));
    if (const auto* text = std::get_if<std::string>(&value); text != nullptr &&
        text->size() > MaximumPropertyStringLength)
        return Result<void>::failure(valueError("string property value is too long", metadata));
    if (const auto* reference = std::get_if<ComponentReference>(&value); reference != nullptr &&
        reference->entity.isNil() != reference->component.isNil())
        return Result<void>::failure(
            valueError("component reference must be either fully set or fully empty", metadata));
    if (const auto* list = std::get_if<PropertyList>(&value); list != nullptr) {
        if (list->elementType != metadata.listElementType ||
            !propertyListElementTypeSupported(list->elementType))
            return Result<void>::failure(valueError("property list element type mismatch", metadata));
        if (list->values.size() > MaximumPropertyListItems)
            return Result<void>::failure(valueError("property list has too many items", metadata));
        for (const auto& element : list->values) {
            auto valid = validateListElement(list->elementType, element, metadata);
            if (!valid)
                return valid;
        }
    }
    if (const auto* curve = std::get_if<Curve>(&value); curve != nullptr) {
        if (curve->points.size() > MaximumCurvePoints)
            return Result<void>::failure(valueError("curve has too many points", metadata));
        double previous = -std::numeric_limits<double>::infinity();
        for (const auto& point : curve->points) {
            if (!std::isfinite(point.position) || !std::isfinite(point.value) ||
                point.position <= previous)
                return Result<void>::failure(
                    valueError("curve positions must be finite and strictly increasing", metadata));
            previous = point.position;
        }
    }
    if (const auto* curve = std::get_if<PropertyAnimationCurve>(&value); curve != nullptr) {
        if (curve->keys.size() > MaximumCurvePoints)
            return Result<void>::failure(valueError("animation curve has too many keys", metadata));
        double previous = -std::numeric_limits<double>::infinity();
        for (const auto& key : curve->keys) {
            if (!std::isfinite(key.time) || !std::isfinite(key.value) ||
                !std::isfinite(key.inTangent) || !std::isfinite(key.outTangent) ||
                key.time <= previous)
                return Result<void>::failure(valueError(
                    "animation curve key times must be finite and strictly increasing", metadata));
            previous = key.time;
        }
    }
    if (const auto* action = std::get_if<ActionReference>(&value); action != nullptr &&
        action->name.size() > MaximumActionOrEventNameLength)
        return Result<void>::failure(valueError("action name is too long", metadata));
    if (const auto* event = std::get_if<EventReference>(&value); event != nullptr &&
        event->name.size() > MaximumActionOrEventNameLength)
        return Result<void>::failure(valueError("event name is too long", metadata));

    if (const auto numeric = numericValue(metadata.type, value); numeric) {
        if (metadata.numeric.minimum && *numeric < *metadata.numeric.minimum)
            return Result<void>::failure(valueError("property value is below its minimum", metadata));
        if (metadata.numeric.maximum && *numeric > *metadata.numeric.maximum)
            return Result<void>::failure(valueError("property value is above its maximum", metadata));
    }
    return Result<void>::success();
}

Result<PropertyValue> PropertyMetadata::read(const void* instance) const {
    if (instance == nullptr) {
        return Result<PropertyValue>::failure(
            Error(ErrorCode::InvalidArgument, "property instance is null"));
    }
    if (!reader) {
        return Result<PropertyValue>::failure(
            Error(ErrorCode::InvalidState, "property has no reader").addContext("property", name));
    }
    auto value = reader(instance);
    if (!value)
        return value;
    auto valid = validatePropertyValue(*this, value.value());
    if (!valid)
        return Result<PropertyValue>::failure(valid.error());
    return value;
}

Result<void> PropertyMetadata::write(void* instance, const PropertyValue& value) const {
    if (instance == nullptr) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "property instance is null"));
    }
    if (hasFlag(flags, PropertyFlags::ReadOnly)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "property is read-only").addContext("property", name));
    }
    if (!writer) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "property has no writer").addContext("property", name));
    }
    auto valid = validatePropertyValue(*this, value);
    if (!valid)
        return valid;
    return writer(instance, value);
}

const PropertyMetadata* TypeMetadata::findProperty(std::string_view propertyName) const noexcept {
    for (const auto& property : properties) {
        if (property.name == propertyName) {
            return &property;
        }
    }
    return nullptr;
}

Result<void> ReflectionRegistry::registerType(TypeMetadata metadata) {
    if (metadata.typeId.isNil()) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "reflected type ID cannot be nil"));
    }
    if (metadata.name.empty()) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "reflected type name cannot be empty"));
    }
    if (types_.find(metadata.typeId) != types_.end()) {
        return Result<void>::failure(
            Error(ErrorCode::AlreadyExists, "reflected type ID is already registered")
                .addContext("type", metadata.name));
    }
    if (names_.find(metadata.name) != names_.end()) {
        return Result<void>::failure(
            Error(ErrorCode::AlreadyExists, "reflected type name is already registered")
                .addContext("type", metadata.name));
    }

    std::unordered_set<std::string> propertyNames;
    for (const auto& property : metadata.properties) {
        if (property.name.empty()) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "reflected property name cannot be empty")
                    .addContext("type", metadata.name));
        }
        if (!propertyNames.insert(property.name).second) {
            return Result<void>::failure(
                Error(ErrorCode::AlreadyExists, "duplicate reflected property")
                    .addContext("type", metadata.name)
                    .addContext("property", property.name));
        }
        if ((property.numeric.minimum && !std::isfinite(*property.numeric.minimum)) ||
            (property.numeric.maximum && !std::isfinite(*property.numeric.maximum)) ||
            (property.numeric.step &&
             (!std::isfinite(*property.numeric.step) || *property.numeric.step <= 0.0)) ||
            (property.numeric.minimum && property.numeric.maximum &&
             *property.numeric.minimum > *property.numeric.maximum)) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "invalid reflected numeric constraints")
                    .addContext("type", metadata.name)
                    .addContext("property", property.name));
        }
        if (property.type == PropertyType::List &&
            !propertyListElementTypeSupported(property.listElementType)) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "unsupported reflected list element type")
                    .addContext("type", metadata.name)
                    .addContext("property", property.name));
        }
        if (property.editorHint == PropertyEditorHint::Slider &&
            (!numericPropertyType(property.type) ||
             !property.numeric.minimum || !property.numeric.maximum)) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument,
                      "slider properties require a numeric type and finite bounds")
                    .addContext("type", metadata.name)
                    .addContext("property", property.name));
        }
        if (property.editorHint == PropertyEditorHint::Multiline &&
            property.type != PropertyType::String) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "multiline editor requires a string property")
                    .addContext("type", metadata.name)
                    .addContext("property", property.name));
        }
        if (property.defaultValue) {
            auto valid = validatePropertyValue(property, *property.defaultValue);
            if (!valid)
                return Result<void>::failure(valid.error().withContext("type", metadata.name));
        }
    }

    const auto id = metadata.typeId;
    const auto name = metadata.name;
    types_.emplace(id, std::make_unique<TypeMetadata>(std::move(metadata)));
    names_.emplace(name, id);
    return Result<void>::success();
}

const TypeMetadata* ReflectionRegistry::find(ComponentTypeGuid id) const noexcept {
    const auto iterator = types_.find(id);
    return iterator == types_.end() ? nullptr : iterator->second.get();
}

const TypeMetadata* ReflectionRegistry::find(std::string_view name) const noexcept {
    const auto nameIterator = names_.find(std::string(name));
    if (nameIterator == names_.end()) {
        return nullptr;
    }
    return find(nameIterator->second);
}

} // namespace fabgl
