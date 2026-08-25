#include "fabgl/prefab/prefab.h"

#include <algorithm>
#include <functional>
#include <utility>

namespace fabgl {

namespace {

Result<void> validateComponent(const ComponentTypeGuid key, const PrefabComponentData& component) {
    if (key.isNil() || component.typeId != key || component.typeName.empty()) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "prefab component is invalid"));
    }
    for (const auto& [property, value] : component.properties) {
        (void)value;
        if (property.empty()) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "prefab property name is empty")
                    .addContext("component", component.typeName));
        }
    }
    return Result<void>::success();
}

Result<void>
validateComponents(const std::map<ComponentTypeGuid, PrefabComponentData>& components) {
    for (const auto& [key, component] : components) {
        auto valid = validateComponent(key, component);
        if (!valid)
            return valid;
    }
    return Result<void>::success();
}

Result<void> validateHierarchy(const std::vector<PrefabEntityData>& entities,
                               bool allowExternalParents = false) {
    std::map<EntityGuid, const PrefabEntityData*> index;
    for (const auto& entity : entities) {
        if (entity.id.isNil() || entity.name.empty()) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "prefab entity is invalid"));
        }
        if (!index.emplace(entity.id, &entity).second) {
            return Result<void>::failure(
                Error(ErrorCode::AlreadyExists, "duplicate prefab entity GUID")
                    .addContext("entity", entity.id.toString()));
        }
        auto componentsValid = validateComponents(entity.components);
        if (!componentsValid)
            return componentsValid;
    }

    for (const auto& entity : entities) {
        if (!entity.parent)
            continue;
        if (*entity.parent == entity.id) {
            return Result<void>::failure(
                Error(ErrorCode::CycleDetected, "prefab entity cannot parent itself")
                    .addContext("entity", entity.id.toString()));
        }
        if (index.find(*entity.parent) == index.end() && !allowExternalParents) {
            return Result<void>::failure(
                Error(ErrorCode::NotFound, "prefab entity parent is missing")
                    .addContext("entity", entity.id.toString())
                    .addContext("parent", entity.parent->toString()));
        }
    }

    enum class Visit : std::uint8_t { None, Active, Complete };
    std::map<EntityGuid, Visit> visits;
    std::function<Result<void>(EntityGuid)> visit = [&](const EntityGuid id) -> Result<void> {
        auto& state = visits[id];
        if (state == Visit::Active) {
            return Result<void>::failure(
                Error(ErrorCode::CycleDetected, "prefab entity hierarchy contains a cycle")
                    .addContext("entity", id.toString()));
        }
        if (state == Visit::Complete)
            return Result<void>::success();
        state = Visit::Active;
        const auto* entity = index.at(id);
        if (entity->parent && index.find(*entity->parent) != index.end()) {
            auto parentValid = visit(*entity->parent);
            if (!parentValid)
                return parentValid;
        }
        state = Visit::Complete;
        return Result<void>::success();
    };
    for (const auto& [id, entity] : index) {
        (void)entity;
        auto acyclic = visit(id);
        if (!acyclic)
            return acyclic;
    }
    return Result<void>::success();
}

void mergeComponents(std::map<ComponentTypeGuid, PrefabComponentData>& destination,
                     const std::map<ComponentTypeGuid, PrefabComponentData>& source) {
    for (const auto& [type, component] : source) {
        const auto existing = destination.find(type);
        if (existing == destination.end()) {
            destination.emplace(type, component);
            continue;
        }
        existing->second.typeName = component.typeName;
        for (const auto& [property, value] : component.properties)
            existing->second.properties[property] = value;
    }
}

} // namespace

Result<void> PrefabLibrary::add(PrefabAsset asset) {
    if (asset.id.isNil() || asset.name.empty()) {
        return Result<void>::failure(Error(ErrorCode::InvalidArgument, "prefab asset is invalid"));
    }
    if (asset.nestedBase && *asset.nestedBase == asset.id) {
        return Result<void>::failure(Error(ErrorCode::CycleDetected, "prefab cannot nest itself"));
    }
    auto componentsValid = validateComponents(asset.components);
    if (!componentsValid)
        return componentsValid;
    auto hierarchyValid = validateHierarchy(asset.entities, asset.nestedBase.has_value());
    if (!hierarchyValid)
        return hierarchyValid;
    if (assets_.find(asset.id) != assets_.end()) {
        return Result<void>::failure(Error(ErrorCode::AlreadyExists, "prefab asset already exists")
                                         .addContext("prefab", asset.id.toString()));
    }
    assets_.emplace(asset.id, std::move(asset));
    return Result<void>::success();
}

const PrefabAsset* PrefabLibrary::find(AssetGuid id) const noexcept {
    const auto iterator = assets_.find(id);
    return iterator == assets_.end() ? nullptr : &iterator->second;
}

Result<void> PrefabLibrary::validateDependencies() const {
    for (const auto& entry : assets_) {
        std::set<AssetGuid> visiting;
        auto resolved = resolveHierarchyRecursive(entry.first, visiting);
        if (!resolved)
            return Result<void>::failure(resolved.error());
    }
    return Result<void>::success();
}

Result<std::map<ComponentTypeGuid, PrefabComponentData>>
PrefabLibrary::resolve(AssetGuid id) const {
    std::set<AssetGuid> visiting;
    return resolveRecursive(id, visiting);
}

Result<std::map<ComponentTypeGuid, PrefabComponentData>>
PrefabLibrary::resolveRecursive(AssetGuid id, std::set<AssetGuid>& visiting) const {
    const auto* asset = find(id);
    if (asset == nullptr) {
        return Result<std::map<ComponentTypeGuid, PrefabComponentData>>::failure(
            Error(ErrorCode::NotFound, "nested prefab dependency is missing")
                .addContext("prefab", id.toString()));
    }
    if (!visiting.insert(id).second) {
        return Result<std::map<ComponentTypeGuid, PrefabComponentData>>::failure(
            Error(ErrorCode::CycleDetected, "nested prefab dependency cycle detected")
                .addContext("prefab", id.toString()));
    }

    std::map<ComponentTypeGuid, PrefabComponentData> components;
    if (asset->nestedBase) {
        auto base = resolveRecursive(*asset->nestedBase, visiting);
        if (!base)
            return base;
        components = std::move(base.value());
    }
    mergeComponents(components, asset->components);
    visiting.erase(id);
    return Result<std::map<ComponentTypeGuid, PrefabComponentData>>::success(std::move(components));
}

Result<ResolvedPrefab> PrefabLibrary::resolveHierarchy(AssetGuid id) const {
    std::set<AssetGuid> visiting;
    return resolveHierarchyRecursive(id, visiting);
}

Result<ResolvedPrefab>
PrefabLibrary::resolveHierarchyRecursive(AssetGuid id, std::set<AssetGuid>& visiting) const {
    const auto* asset = find(id);
    if (asset == nullptr) {
        return Result<ResolvedPrefab>::failure(
            Error(ErrorCode::NotFound, "nested prefab dependency is missing")
                .addContext("prefab", id.toString()));
    }
    if (!visiting.insert(id).second) {
        return Result<ResolvedPrefab>::failure(
            Error(ErrorCode::CycleDetected, "nested prefab dependency cycle detected")
                .addContext("prefab", id.toString()));
    }

    ResolvedPrefab resolved;
    if (asset->nestedBase) {
        auto base = resolveHierarchyRecursive(*asset->nestedBase, visiting);
        if (!base)
            return base;
        resolved = std::move(base.value());
    }
    mergeComponents(resolved.components, asset->components);
    for (const auto& entity : asset->entities) {
        const auto existing = resolved.entities.find(entity.id);
        if (existing == resolved.entities.end()) {
            resolved.entities.emplace(entity.id, entity);
            continue;
        }
        existing->second.name = entity.name;
        existing->second.active = entity.active;
        existing->second.parent = entity.parent;
        mergeComponents(existing->second.components, entity.components);
    }

    std::vector<PrefabEntityData> hierarchy;
    hierarchy.reserve(resolved.entities.size());
    for (const auto& [entityId, entity] : resolved.entities) {
        (void)entityId;
        hierarchy.push_back(entity);
    }
    auto hierarchyValid = validateHierarchy(hierarchy);
    if (!hierarchyValid)
        return Result<ResolvedPrefab>::failure(hierarchyValid.error());

    visiting.erase(id);
    return Result<ResolvedPrefab>::success(std::move(resolved));
}

Result<std::vector<AssetGuid>> PrefabLibrary::dependencies(AssetGuid id) const {
    const auto* asset = find(id);
    if (asset == nullptr) {
        return Result<std::vector<AssetGuid>>::failure(
            Error(ErrorCode::NotFound, "prefab was not found").addContext("prefab", id.toString()));
    }
    std::vector<AssetGuid> result;
    std::set<AssetGuid> seen{id};
    while (asset->nestedBase) {
        if (!seen.insert(*asset->nestedBase).second) {
            return Result<std::vector<AssetGuid>>::failure(
                Error(ErrorCode::CycleDetected, "nested prefab dependency cycle detected")
                    .addContext("prefab", asset->nestedBase->toString()));
        }
        result.push_back(*asset->nestedBase);
        asset = find(*asset->nestedBase);
        if (asset == nullptr) {
            return Result<std::vector<AssetGuid>>::failure(
                Error(ErrorCode::NotFound, "nested prefab dependency is missing")
                    .addContext("prefab", result.back().toString()));
        }
    }
    return Result<std::vector<AssetGuid>>::success(std::move(result));
}

Result<void> PrefabInstance::setPropertyOverride(ComponentTypeGuid component, std::string property,
                                                 PropertyValue value) {
    if (unpacked_) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "unpacked prefab instance is no longer linked"));
    }
    if (component.isNil() || property.empty()) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "prefab property override is invalid"));
    }
    removedComponents_.erase(component);
    propertyOverrides_[PrefabPropertyKey{component, std::move(property)}] = std::move(value);
    return Result<void>::success();
}

bool PrefabInstance::revertProperty(ComponentTypeGuid component, std::string_view property) {
    return propertyOverrides_.erase(PrefabPropertyKey{component, std::string(property)}) != 0U;
}

Result<void> PrefabInstance::addComponentOverride(PrefabComponentData component) {
    if (unpacked_) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "unpacked prefab instance is no longer linked"));
    }
    if (component.typeId.isNil() || component.typeName.empty()) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "added prefab component is invalid"));
    }
    if (addedComponents_.find(component.typeId) != addedComponents_.end()) {
        return Result<void>::failure(
            Error(ErrorCode::AlreadyExists, "prefab component override already exists"));
    }
    removedComponents_.erase(component.typeId);
    addedComponents_.emplace(component.typeId, std::move(component));
    return Result<void>::success();
}

void PrefabInstance::removeComponentOverride(ComponentTypeGuid component) {
    if (addedComponents_.erase(component) == 0U)
        removedComponents_.insert(component);
    for (auto iterator = propertyOverrides_.begin(); iterator != propertyOverrides_.end();) {
        if (iterator->first.component == component)
            iterator = propertyOverrides_.erase(iterator);
        else
            ++iterator;
    }
}

void PrefabInstance::revertAll() noexcept {
    propertyOverrides_.clear();
    addedComponents_.clear();
    removedComponents_.clear();
}

PrefabInstanceSnapshot PrefabInstance::snapshot() const {
    return PrefabInstanceSnapshot{prefab_, propertyOverrides_, addedComponents_,
                                  removedComponents_};
}

Result<PrefabInstance> PrefabInstance::fromSnapshot(PrefabInstanceSnapshot snapshot) {
    if (snapshot.prefab.isNil()) {
        return Result<PrefabInstance>::failure(
            Error(ErrorCode::InvalidArgument, "prefab instance snapshot has a nil prefab GUID"));
    }
    auto componentsValid = validateComponents(snapshot.addedComponents);
    if (!componentsValid)
        return Result<PrefabInstance>::failure(componentsValid.error());
    for (const auto component : snapshot.removedComponents) {
        if (component.isNil()) {
            return Result<PrefabInstance>::failure(
                Error(ErrorCode::InvalidArgument,
                      "prefab instance snapshot has a nil removed component GUID"));
        }
        if (snapshot.addedComponents.contains(component)) {
            return Result<PrefabInstance>::failure(
                Error(ErrorCode::InvalidState,
                      "prefab component cannot be both added and removed in an instance")
                    .addContext("component", component.toString()));
        }
    }
    for (const auto& [key, value] : snapshot.propertyOverrides) {
        (void)value;
        if (key.component.isNil() || key.property.empty()) {
            return Result<PrefabInstance>::failure(
                Error(ErrorCode::InvalidArgument,
                      "prefab instance snapshot has an invalid property override"));
        }
        if (snapshot.removedComponents.contains(key.component)) {
            return Result<PrefabInstance>::failure(
                Error(ErrorCode::InvalidState,
                      "removed prefab component cannot retain a property override")
                    .addContext("component", key.component.toString())
                    .addContext("property", key.property));
        }
    }

    PrefabInstance instance(snapshot.prefab);
    instance.propertyOverrides_ = std::move(snapshot.propertyOverrides);
    instance.addedComponents_ = std::move(snapshot.addedComponents);
    instance.removedComponents_ = std::move(snapshot.removedComponents);
    return Result<PrefabInstance>::success(std::move(instance));
}

Result<std::map<ComponentTypeGuid, PrefabComponentData>>
PrefabInstance::resolve(const PrefabLibrary& library) const {
    if (unpacked_) {
        return Result<std::map<ComponentTypeGuid, PrefabComponentData>>::failure(
            Error(ErrorCode::InvalidState, "unpacked prefab instance is no longer linked"));
    }
    auto resolved = library.resolve(prefab_);
    if (!resolved)
        return resolved;
    auto components = std::move(resolved.value());
    for (const auto component : removedComponents_)
        components.erase(component);
    for (const auto& component : addedComponents_)
        components[component.first] = component.second;
    for (const auto& overrideValue : propertyOverrides_) {
        const auto component = components.find(overrideValue.first.component);
        if (component == components.end()) {
            return Result<std::map<ComponentTypeGuid, PrefabComponentData>>::failure(
                Error(ErrorCode::NotFound, "prefab property override targets a missing component")
                    .addContext("component", overrideValue.first.component.toString()));
        }
        component->second.properties[overrideValue.first.property] = overrideValue.second;
    }
    return Result<std::map<ComponentTypeGuid, PrefabComponentData>>::success(std::move(components));
}

Result<void> PrefabInstance::applyTo(PrefabAsset& asset) {
    if (unpacked_) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "unpacked prefab instance is no longer linked"));
    }
    if (asset.id != prefab_) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "prefab instance does not belong to asset"));
    }
    auto updated = asset;
    for (const auto component : removedComponents_)
        updated.components.erase(component);
    for (const auto& component : addedComponents_)
        updated.components[component.first] = component.second;
    for (const auto& overrideValue : propertyOverrides_) {
        const auto component = updated.components.find(overrideValue.first.component);
        if (component == updated.components.end()) {
            return Result<void>::failure(Error(
                ErrorCode::NotFound, "prefab property override targets a missing asset component"));
        }
        component->second.properties[overrideValue.first.property] = overrideValue.second;
    }
    asset = std::move(updated);
    revertAll();
    return Result<void>::success();
}

Result<ResolvedPrefab> PrefabInstance::unpack(const PrefabLibrary& library) {
    if (unpacked_) {
        return Result<ResolvedPrefab>::failure(
            Error(ErrorCode::InvalidState, "prefab instance is already unpacked"));
    }
    auto resolved = library.resolveHierarchy(prefab_);
    if (!resolved)
        return resolved;
    for (const auto component : removedComponents_)
        resolved.value().components.erase(component);
    for (const auto& [type, component] : addedComponents_)
        resolved.value().components[type] = component;
    for (const auto& [key, value] : propertyOverrides_) {
        const auto component = resolved.value().components.find(key.component);
        if (component == resolved.value().components.end()) {
            return Result<ResolvedPrefab>::failure(
                Error(ErrorCode::NotFound, "prefab property override targets a missing component")
                    .addContext("component", key.component.toString()));
        }
        component->second.properties[key.property] = value;
    }
    revertAll();
    unpacked_ = true;
    prefab_ = AssetGuid{};
    return resolved;
}

} // namespace fabgl
