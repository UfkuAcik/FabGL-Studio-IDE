#pragma once

#include "fabgl/core/guid.h"
#include "fabgl/core/result.h"
#include "fabgl/reflection/reflection.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fabgl {

struct PrefabComponentData final {
    ComponentTypeGuid typeId;
    std::string typeName;
    std::map<std::string, PropertyValue> properties;
};

struct PrefabAsset final {
    AssetGuid id;
    std::string name;
    std::optional<AssetGuid> nestedBase;
    std::map<ComponentTypeGuid, PrefabComponentData> components;
};

class PrefabLibrary final {
  public:
    [[nodiscard]] Result<void> add(PrefabAsset asset);
    [[nodiscard]] const PrefabAsset* find(AssetGuid id) const noexcept;
    [[nodiscard]] Result<void> validateDependencies() const;
    [[nodiscard]] Result<std::map<ComponentTypeGuid, PrefabComponentData>>
    resolve(AssetGuid id) const;

  private:
    [[nodiscard]] Result<std::map<ComponentTypeGuid, PrefabComponentData>>
    resolveRecursive(AssetGuid id, std::set<AssetGuid>& visiting) const;

    std::map<AssetGuid, PrefabAsset> assets_;
};

struct PrefabPropertyKey final {
    ComponentTypeGuid component;
    std::string property;

    friend bool operator<(const PrefabPropertyKey& lhs, const PrefabPropertyKey& rhs) noexcept {
        if (lhs.component != rhs.component)
            return lhs.component < rhs.component;
        return lhs.property < rhs.property;
    }
};

class PrefabInstance final {
  public:
    explicit PrefabInstance(AssetGuid prefab) : prefab_(prefab) {}

    [[nodiscard]] AssetGuid prefab() const noexcept {
        return prefab_;
    }
    [[nodiscard]] Result<void> setPropertyOverride(ComponentTypeGuid component,
                                                   std::string property, PropertyValue value);
    [[nodiscard]] bool revertProperty(ComponentTypeGuid component, std::string_view property);
    [[nodiscard]] Result<void> addComponentOverride(PrefabComponentData component);
    void removeComponentOverride(ComponentTypeGuid component);
    void revertAll() noexcept;

    [[nodiscard]] Result<std::map<ComponentTypeGuid, PrefabComponentData>>
    resolve(const PrefabLibrary& library) const;
    [[nodiscard]] Result<void> applyTo(PrefabAsset& asset);

    [[nodiscard]] std::size_t propertyOverrideCount() const noexcept {
        return propertyOverrides_.size();
    }
    [[nodiscard]] std::size_t addedComponentCount() const noexcept {
        return addedComponents_.size();
    }
    [[nodiscard]] std::size_t removedComponentCount() const noexcept {
        return removedComponents_.size();
    }

  private:
    AssetGuid prefab_;
    std::map<PrefabPropertyKey, PropertyValue> propertyOverrides_;
    std::map<ComponentTypeGuid, PrefabComponentData> addedComponents_;
    std::set<ComponentTypeGuid> removedComponents_;
};

} // namespace fabgl
