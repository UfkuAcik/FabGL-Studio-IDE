#pragma once

#include "fabgl/reflection/reflection.h"
#include "fabgl/scene/component.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace fabgl::scripting {

struct ApiVersion final {
    std::uint16_t major{};
    std::uint16_t minor{};
    std::uint16_t patch{};

    [[nodiscard]] constexpr bool operator==(const ApiVersion& other) const noexcept {
        return major == other.major && minor == other.minor && patch == other.patch;
    }
};

inline constexpr ApiVersion CurrentApiVersion{0, 1, 0};

[[nodiscard]] constexpr bool isApiCompatible(ApiVersion requested) noexcept {
    if (requested.major != CurrentApiVersion.major)
        return false;
    if (requested.major == 0U)
        return requested.minor == CurrentApiVersion.minor;
    return requested.minor <= CurrentApiVersion.minor;
}

[[nodiscard]] TypeMetadata makeScriptMetadata(std::string stableName, std::string displayName);

class ScriptComponent : public Component {
  public:
    [[nodiscard]] ComponentTypeGuid typeId() const noexcept override {
        return metadata_.typeId;
    }
    [[nodiscard]] std::string_view typeName() const noexcept override {
        return metadata_.name;
    }
    [[nodiscard]] const TypeMetadata* metadata() const noexcept override {
        return &metadata_;
    }
    [[nodiscard]] ApiVersion requiredApiVersion() const noexcept {
        return requiredApiVersion_;
    }
    [[nodiscard]] bool apiCompatible() const noexcept {
        return isApiCompatible(requiredApiVersion_);
    }

  protected:
    explicit ScriptComponent(TypeMetadata metadata,
                             ApiVersion requiredApiVersion = CurrentApiVersion);

  private:
    TypeMetadata metadata_;
    ApiVersion requiredApiVersion_;
};

namespace detail {

template <typename> inline constexpr bool AlwaysFalse = false;

template <typename Value> [[nodiscard]] constexpr PropertyType scriptPropertyType() {
    using Type = std::remove_cv_t<Value>;
    if constexpr (std::is_same_v<Type, bool>)
        return PropertyType::Boolean;
    else if constexpr (std::is_same_v<Type, std::int8_t> ||
                       std::is_same_v<Type, std::int16_t> ||
                       std::is_same_v<Type, std::int32_t> ||
                       std::is_same_v<Type, std::int64_t>)
        return PropertyType::SignedInteger;
    else if constexpr (std::is_same_v<Type, std::uint8_t> ||
                       std::is_same_v<Type, std::uint16_t> ||
                       std::is_same_v<Type, std::uint32_t> ||
                       std::is_same_v<Type, std::uint64_t>)
        return PropertyType::UnsignedInteger;
    else if constexpr (std::is_same_v<Type, float> || std::is_same_v<Type, double>) {
        return PropertyType::Float;
    } else if constexpr (std::is_same_v<Type, Fixed>)
        return PropertyType::Fixed;
    else if constexpr (std::is_same_v<Type, std::string>)
        return PropertyType::String;
    else if constexpr (std::is_same_v<Type, Vec2>)
        return PropertyType::Vec2;
    else if constexpr (std::is_same_v<Type, Vec3>)
        return PropertyType::Vec3;
    else if constexpr (std::is_same_v<Type, EulerAngles>)
        return PropertyType::EulerAngles;
    else if constexpr (std::is_same_v<Type, Quaternion>)
        return PropertyType::Quaternion;
    else if constexpr (std::is_same_v<Type, Rect>)
        return PropertyType::Rect;
    else if constexpr (std::is_same_v<Type, Color>)
        return PropertyType::Color;
    else if constexpr (std::is_same_v<Type, AssetGuid>)
        return PropertyType::AssetReference;
    else if constexpr (std::is_same_v<Type, EntityGuid>)
        return PropertyType::EntityReference;
    else if constexpr (std::is_same_v<Type, ComponentReference>)
        return PropertyType::ComponentReference;
    else if constexpr (std::is_same_v<Type, PropertyList>)
        return PropertyType::List;
    else if constexpr (std::is_same_v<Type, Curve>)
        return PropertyType::Curve;
    else if constexpr (std::is_same_v<Type, PropertyAnimationCurve>)
        return PropertyType::AnimationCurve;
    else if constexpr (std::is_same_v<Type, ActionReference>)
        return PropertyType::ActionReference;
    else if constexpr (std::is_same_v<Type, EventReference>)
        return PropertyType::EventReference;
    else
        static_assert(AlwaysFalse<Type>, "unsupported gameplay script property type");
}

template <typename Value> [[nodiscard]] PropertyValue toPropertyValue(const Value& value) {
    using Type = std::remove_cv_t<Value>;
    if constexpr (std::is_same_v<Type, std::int8_t> || std::is_same_v<Type, std::int16_t> ||
                  std::is_same_v<Type, std::int32_t>) {
        return PropertyValue(static_cast<std::int64_t>(value));
    } else if constexpr (std::is_same_v<Type, std::uint8_t> ||
                         std::is_same_v<Type, std::uint16_t> ||
                         std::is_same_v<Type, std::uint32_t>) {
        return PropertyValue(static_cast<std::uint64_t>(value));
    } else if constexpr (std::is_same_v<Type, float>) {
        return PropertyValue(static_cast<double>(value));
    } else {
        return PropertyValue(value);
    }
}

template <typename Value>
[[nodiscard]] Result<Value> fromPropertyValue(const PropertyValue& value) {
    using Type = std::remove_cv_t<Value>;
    if constexpr (std::is_same_v<Type, std::int8_t> || std::is_same_v<Type, std::int16_t> ||
                  std::is_same_v<Type, std::int32_t>) {
        if (const auto* stored = std::get_if<std::int64_t>(&value)) {
            if (*stored < static_cast<std::int64_t>(std::numeric_limits<Type>::min()) ||
                *stored > static_cast<std::int64_t>(std::numeric_limits<Type>::max())) {
                return Result<Value>::failure(
                    Error(ErrorCode::InvalidArgument, "gameplay script integer is out of range"));
            }
            return Result<Value>::success(static_cast<Value>(*stored));
        }
    } else if constexpr (std::is_same_v<Type, std::uint8_t> ||
                         std::is_same_v<Type, std::uint16_t> ||
                         std::is_same_v<Type, std::uint32_t>) {
        if (const auto* stored = std::get_if<std::uint64_t>(&value)) {
            if (*stored > static_cast<std::uint64_t>(std::numeric_limits<Type>::max())) {
                return Result<Value>::failure(
                    Error(ErrorCode::InvalidArgument, "gameplay script integer is out of range"));
            }
            return Result<Value>::success(static_cast<Value>(*stored));
        }
    } else if constexpr (std::is_same_v<Type, float>) {
        if (const auto* stored = std::get_if<double>(&value)) {
            if (!std::isfinite(*stored) ||
                *stored < -static_cast<double>(std::numeric_limits<Type>::max()) ||
                *stored > static_cast<double>(std::numeric_limits<Type>::max())) {
                return Result<Value>::failure(
                    Error(ErrorCode::InvalidArgument, "gameplay script float is out of range"));
            }
            return Result<Value>::success(static_cast<Value>(*stored));
        }
    } else if (const auto* stored = std::get_if<Type>(&value)) {
        return Result<Value>::success(*stored);
    }
    return Result<Value>::failure(
        Error(ErrorCode::TypeMismatch, "gameplay script property type mismatch"));
}

} // namespace detail

template <typename Script, typename Value>
[[nodiscard]] PropertyMetadata
scriptProperty(std::string name, Value Script::*member, Value defaultValue,
               std::string category = "Gameplay",
               PropertyFlags flags = PropertyFlags::Serialize | PropertyFlags::RuntimeEditable) {
    static_assert(std::is_base_of_v<ScriptComponent, Script>,
                  "script properties require a ScriptComponent-derived owner");
    PropertyMetadata property;
    property.name = name;
    property.displayName = std::move(name);
    property.type = detail::scriptPropertyType<Value>();
    property.flags = flags;
    property.defaultValue = detail::toPropertyValue(defaultValue);
    if constexpr (std::is_same_v<std::remove_cv_t<Value>, PropertyList>) {
        property.listElementType = defaultValue.elementType;
    }
    property.category = std::move(category);
    property.reader = [member](const void* instance) {
        const auto* script = static_cast<const Script*>(instance);
        return Result<PropertyValue>::success(detail::toPropertyValue(script->*member));
    };
    property.writer = [member](void* instance, const PropertyValue& value) {
        auto converted = detail::fromPropertyValue<Value>(value);
        if (!converted)
            return Result<void>::failure(converted.error());
        auto* script = static_cast<Script*>(instance);
        script->*member = std::move(converted.value());
        return Result<void>::success();
    };
    return property;
}

} // namespace fabgl::scripting
