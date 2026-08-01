#include "fabgl/reflection/reflection.h"

#include <unordered_set>

namespace fabgl {

Result<PropertyValue> PropertyMetadata::read(const void* instance) const {
    if (instance == nullptr) {
        return Result<PropertyValue>::failure(
            Error(ErrorCode::InvalidArgument, "property instance is null"));
    }
    if (!reader) {
        return Result<PropertyValue>::failure(
            Error(ErrorCode::InvalidState, "property has no reader").addContext("property", name));
    }
    return reader(instance);
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
