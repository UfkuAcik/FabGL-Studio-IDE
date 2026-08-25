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

struct PrefabEntityData final {
    EntityGuid id;
    std::string name;
    bool active = true;
    std::optional<EntityGuid> parent;
    std::map<ComponentTypeGuid, PrefabComponentData> components;
};

struct PrefabAsset final {
    AssetGuid id;
    std::string name;
    std::optional<AssetGuid> nestedBase;
    // Components on the prefab instance root. This member deliberately remains in its original
    // position so existing aggregate initializers stay source compatible.
    std::map<ComponentTypeGuid, PrefabComponentData> components;
    std::vector<PrefabEntityData> entities;
};

struct ResolvedPrefab final {
    std::map<ComponentTypeGuid, PrefabComponentData> components;
    std::map<EntityGuid, PrefabEntityData> entities;
};

class PrefabLibrary final {
  public:
    [[nodiscard]] Result<void> add(PrefabAsset asset);
    [[nodiscard]] const PrefabAsset* find(AssetGuid id) const noexcept;
    [[nodiscard]] Result<void> validateDependencies() const;
    [[nodiscard]] Result<std::map<ComponentTypeGuid, PrefabComponentData>>
    resolve(AssetGuid id) const;
    [[nodiscard]] Result<ResolvedPrefab> resolveHierarchy(AssetGuid id) const;
    [[nodiscard]] Result<std::vector<AssetGuid>> dependencies(AssetGuid id) const;

  private:
    [[nodiscard]] Result<std::map<ComponentTypeGuid, PrefabComponentData>>
    resolveRecursive(AssetGuid id, std::set<AssetGuid>& visiting) const;
    [[nodiscard]] Result<ResolvedPrefab>
    resolveHierarchyRecursive(AssetGuid id, std::set<AssetGuid>& visiting) const;

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

    friend bool operator==(const PrefabPropertyKey&, const PrefabPropertyKey&) noexcept = default;
};

struct PrefabInstanceSnapshot final {
    AssetGuid prefab;
    std::map<PrefabPropertyKey, PropertyValue> propertyOverrides;
    std::map<ComponentTypeGuid, PrefabComponentData> addedComponents;
    std::set<ComponentTypeGuid> removedComponents;
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
    [[nodiscard]] Result<ResolvedPrefab> unpack(const PrefabLibrary& library);
    [[nodiscard]] PrefabInstanceSnapshot snapshot() const;
    [[nodiscard]] static Result<PrefabInstance> fromSnapshot(PrefabInstanceSnapshot snapshot);

    [[nodiscard]] std::size_t propertyOverrideCount() const noexcept {
        return propertyOverrides_.size();
    }
    [[nodiscard]] std::size_t addedComponentCount() const noexcept {
        return addedComponents_.size();
    }
    [[nodiscard]] std::size_t removedComponentCount() const noexcept {
        return removedComponents_.size();
    }
    [[nodiscard]] bool unpacked() const noexcept {
        return unpacked_;
    }

  private:
    AssetGuid prefab_;
    std::map<PrefabPropertyKey, PropertyValue> propertyOverrides_;
    std::map<ComponentTypeGuid, PrefabComponentData> addedComponents_;
    std::set<ComponentTypeGuid> removedComponents_;
    bool unpacked_ = false;
};

// Scene-side linkage for a baked prefab hierarchy. Entity GUIDs are the stable scene identities;
// sourceToScene preserves the prefab-source identity used to rebuild and inspect the instance.
// The linkage is serialized independently from PrefabAsset so unpacking can remove it without
// changing any baked scene entity or component.
struct PrefabSceneInstance final {
    explicit PrefabSceneInstance(AssetGuid prefabGuid) : prefab(prefabGuid), state(prefabGuid) {}

    AssetGuid prefab;
    PrefabInstance state;
    EntityGuid root;
    std::vector<EntityGuid> entities;
    std::map<EntityGuid, EntityGuid> sourceToScene;
    bool missing = false;
};

} // namespace fabgl
