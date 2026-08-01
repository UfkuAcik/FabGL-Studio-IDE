#pragma once

#include "fabgl/core/guid.h"
#include "fabgl/core/result.h"
#include "fabgl/math/types.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace fabgl {

enum class PropertyType {
    Boolean,
    SignedInteger,
    UnsignedInteger,
    Float,
    Fixed,
    String,
    Enumeration,
    BitFlags,
    Vec2,
    Vec3,
    Rect,
    Color,
    AssetReference,
    EntityReference,
};

enum class PropertyFlags : std::uint32_t {
    None = 0,
    ReadOnly = 1U << 0U,
    Hidden = 1U << 1U,
    Serialize = 1U << 2U,
    RuntimeEditable = 1U << 3U,
    EditorOnly = 1U << 4U,
};

constexpr PropertyFlags operator|(PropertyFlags lhs, PropertyFlags rhs) noexcept {
    return static_cast<PropertyFlags>(static_cast<std::uint32_t>(lhs) |
                                      static_cast<std::uint32_t>(rhs));
}

constexpr bool hasFlag(PropertyFlags value, PropertyFlags flag) noexcept {
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0U;
}

using PropertyValue = std::variant<bool, std::int64_t, std::uint64_t, double, Fixed, std::string,
                                   Vec2, Vec3, Rect, Color, AssetGuid, EntityGuid>;

struct NumericConstraints final {
    std::optional<double> minimum;
    std::optional<double> maximum;
    std::optional<double> step;
};

struct EnumOption final {
    std::int64_t value = 0;
    std::string name;
};

struct PropertyMetadata final {
    std::string name;
    std::string displayName;
    PropertyType type = PropertyType::String;
    PropertyFlags flags = PropertyFlags::Serialize;
    std::optional<PropertyValue> defaultValue;
    NumericConstraints numeric;
    std::string tooltip;
    std::string category;
    std::string assetTypeFilter;
    std::vector<EnumOption> enumOptions;
    std::function<Result<PropertyValue>(const void*)> reader;
    std::function<Result<void>(void*, const PropertyValue&)> writer;

    [[nodiscard]] Result<PropertyValue> read(const void* instance) const;
    [[nodiscard]] Result<void> write(void* instance, const PropertyValue& value) const;
};

struct TypeMetadata final {
    ComponentTypeGuid typeId;
    std::string name;
    std::string displayName;
    std::vector<PropertyMetadata> properties;

    [[nodiscard]] const PropertyMetadata*
    findProperty(std::string_view propertyName) const noexcept;
};

class ReflectionRegistry final {
  public:
    [[nodiscard]] Result<void> registerType(TypeMetadata metadata);
    [[nodiscard]] const TypeMetadata* find(ComponentTypeGuid id) const noexcept;
    [[nodiscard]] const TypeMetadata* find(std::string_view name) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept {
        return types_.size();
    }

  private:
    std::unordered_map<ComponentTypeGuid, std::unique_ptr<TypeMetadata>,
                       StrongGuidHash<ComponentTypeGuidTag>>
        types_;
    std::unordered_map<std::string, ComponentTypeGuid> names_;
};

} // namespace fabgl
