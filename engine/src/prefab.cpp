#include "fabgl/prefab/prefab.h"

#include <utility>

namespace fabgl {

Result<void> PrefabLibrary::add(PrefabAsset asset) {
    if (asset.id.isNil() || asset.name.empty()) {
        return Result<void>::failure(Error(ErrorCode::InvalidArgument, "prefab asset is invalid"));
    }
    if (asset.nestedBase && *asset.nestedBase == asset.id) {
        return Result<void>::failure(Error(ErrorCode::CycleDetected, "prefab cannot nest itself"));
    }
    for (const auto& component : asset.components) {
        if (component.first.isNil() || component.second.typeId != component.first ||
            component.second.typeName.empty()) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "prefab component is invalid"));
        }
    }
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
        auto resolved = resolveRecursive(entry.first, visiting);
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
    for (const auto& entry : asset->components) {
        const auto existing = components.find(entry.first);
        if (existing == components.end()) {
            components.emplace(entry.first, entry.second);
        } else {
            existing->second.typeName = entry.second.typeName;
            for (const auto& property : entry.second.properties) {
                existing->second.properties[property.first] = property.second;
            }
        }
    }
    visiting.erase(id);
    return Result<std::map<ComponentTypeGuid, PrefabComponentData>>::success(std::move(components));
}

Result<void> PrefabInstance::setPropertyOverride(ComponentTypeGuid component, std::string property,
                                                 PropertyValue value) {
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

Result<std::map<ComponentTypeGuid, PrefabComponentData>>
PrefabInstance::resolve(const PrefabLibrary& library) const {
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

} // namespace fabgl
