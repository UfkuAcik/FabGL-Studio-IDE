#pragma once

#include "fabgl/reflection/reflection.h"
#include "fabgl/scene/component.h"

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fabgl {

class DataComponent final : public Component {
  public:
    explicit DataComponent(TypeMetadata metadata);

    [[nodiscard]] ComponentTypeGuid typeId() const noexcept override {
        return metadata_.typeId;
    }
    [[nodiscard]] std::string_view typeName() const noexcept override {
        return metadata_.name;
    }
    [[nodiscard]] const TypeMetadata* metadata() const noexcept override {
        return &metadata_;
    }

    [[nodiscard]] Result<PropertyValue> get(std::string_view property) const;
    [[nodiscard]] Result<void> set(std::string_view property, PropertyValue value);

  private:
    TypeMetadata metadata_;
    std::map<std::string, PropertyValue> values_;
};

[[nodiscard]] const std::vector<std::string>& builtinComponentNames();
[[nodiscard]] Result<void> registerBuiltinComponentTypes(ReflectionRegistry& registry);
[[nodiscard]] Result<std::unique_ptr<DataComponent>>
createBuiltinDataComponent(const ReflectionRegistry& registry, std::string_view shortName);

} // namespace fabgl
