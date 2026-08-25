#include "fabgl/serialization/prefab_instance_serializer.h"

#include "fabgl/serialization/prefab_serializer.h"

#include <algorithm>
#include <charconv>
#include <set>
#include <sstream>
#include <utility>

namespace fabgl {
namespace {

constexpr std::string_view PropertyEnvelopeName = "FabGL Prefab Instance Property Overrides";
constexpr std::string_view AddedEnvelopeName = "FabGL Prefab Instance Added Components";
constexpr std::string_view OverrideComponentName = "fabgl.PrefabPropertyOverrides";

Error formatError(std::string message) {
    return Error(ErrorCode::InvalidFormat, std::move(message));
}

class Cursor final {
  public:
    explicit Cursor(std::string_view text) : text_(text) {}

    [[nodiscard]] Result<std::string_view> line(std::string_view expected) {
        const auto end = text_.find('\n', offset_);
        if (end == std::string_view::npos) {
            return Result<std::string_view>::failure(
                formatError("prefab instance link ended before " + std::string(expected)));
        }
        const auto value = text_.substr(offset_, end - offset_);
        offset_ = end + 1U;
        ++line_;
        if (value.find('\r') != std::string_view::npos) {
            return Result<std::string_view>::failure(
                formatError("prefab instance link must use canonical LF line endings")
                    .addContext("line", std::to_string(line_)));
        }
        return Result<std::string_view>::success(value);
    }

    [[nodiscard]] Result<std::string_view> bytes(std::size_t size, std::string_view expected) {
        if (size > text_.size() - offset_) {
            return Result<std::string_view>::failure(
                formatError("prefab instance link ended inside " + std::string(expected)));
        }
        const auto value = text_.substr(offset_, size);
        offset_ += size;
        return Result<std::string_view>::success(value);
    }

    [[nodiscard]] bool empty() const noexcept {
        return offset_ == text_.size();
    }

    [[nodiscard]] std::size_t lineNumber() const noexcept {
        return line_;
    }

  private:
    std::string_view text_;
    std::size_t offset_ = 0U;
    std::size_t line_ = 0U;
};

Result<std::vector<std::string>> tokens(Cursor& cursor, std::string_view expected) {
    auto source = cursor.line(expected);
    if (!source)
        return Result<std::vector<std::string>>::failure(source.error());
    std::istringstream stream(std::string(source.value()));
    std::vector<std::string> result;
    for (std::string token; stream >> token;)
        result.push_back(std::move(token));
    if (result.empty()) {
        return Result<std::vector<std::string>>::failure(
            formatError("prefab instance link contains an empty record")
                .addContext("line", std::to_string(cursor.lineNumber())));
    }
    return Result<std::vector<std::string>>::success(std::move(result));
}

Result<std::string> singleValue(Cursor& cursor, std::string_view key) {
    auto record = tokens(cursor, key);
    if (!record)
        return Result<std::string>::failure(record.error());
    if (record.value().size() != 2U || record.value()[0] != key) {
        return Result<std::string>::failure(
            formatError("invalid prefab instance link record")
                .addContext("expected", std::string(key))
                .addContext("line", std::to_string(cursor.lineNumber())));
    }
    return Result<std::string>::success(std::move(record.value()[1]));
}

Result<std::size_t> countValue(Cursor& cursor, std::string_view key, std::size_t maximum) {
    auto token = singleValue(cursor, key);
    if (!token)
        return Result<std::size_t>::failure(token.error());
    std::size_t value = 0U;
    const auto* begin = token.value().data();
    const auto* end = begin + token.value().size();
    const auto parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end || value > maximum) {
        return Result<std::size_t>::failure(
            formatError("prefab instance link count is invalid or exceeds its limit")
                .addContext("field", std::string(key))
                .addContext("maximum", std::to_string(maximum)));
    }
    return Result<std::size_t>::success(value);
}

template <typename GuidType>
Result<GuidType> requiredGuid(std::string_view token, std::string_view field) {
    auto guid = GuidType::parse(token);
    if (!guid || guid.value().isNil()) {
        return Result<GuidType>::failure(
            formatError("prefab instance link contains an invalid GUID")
                .addContext("field", std::string(field))
                .addContext("value", std::string(token)));
    }
    return Result<GuidType>::success(guid.value());
}

Result<void> validateEnvelope(const PrefabAsset& envelope, AssetGuid prefab,
                              std::string_view expectedName) {
    if (envelope.id != prefab || envelope.name != expectedName || envelope.nestedBase ||
        !envelope.entities.empty()) {
        return Result<void>::failure(
            formatError("prefab instance link contains an invalid override envelope"));
    }
    return Result<void>::success();
}

Result<std::pair<std::string, std::string>> overrideBlobs(const PrefabInstanceSnapshot& snapshot) {
    PrefabAsset propertyEnvelope{
        snapshot.prefab, std::string(PropertyEnvelopeName), std::nullopt, {}, {}};
    for (const auto& [key, value] : snapshot.propertyOverrides) {
        auto [component, inserted] = propertyEnvelope.components.emplace(
            key.component,
            PrefabComponentData{key.component, std::string(OverrideComponentName), {}});
        (void)inserted;
        component->second.properties.emplace(key.property, value);
    }
    PrefabAsset addedEnvelope{snapshot.prefab,
                              std::string(AddedEnvelopeName),
                              std::nullopt,
                              snapshot.addedComponents,
                              {}};
    auto properties = PrefabSerializer::serialize(propertyEnvelope);
    if (!properties)
        return Result<std::pair<std::string, std::string>>::failure(properties.error());
    auto added = PrefabSerializer::serialize(addedEnvelope);
    if (!added)
        return Result<std::pair<std::string, std::string>>::failure(added.error());
    return Result<std::pair<std::string, std::string>>::success(
        {std::move(properties.value()), std::move(added.value())});
}

Result<void> validateLinkage(const PrefabSceneInstance& instance) {
    if (instance.prefab.isNil() || instance.root.isNil() || instance.state.unpacked() ||
        instance.state.prefab() != instance.prefab) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "prefab scene instance linkage is invalid"));
    }
    if (instance.entities.empty() ||
        instance.entities.size() > PrefabInstanceSerializer::MaximumEntities ||
        instance.sourceToScene.size() > PrefabInstanceSerializer::MaximumMappings) {
        return Result<void>::failure(
            Error(ErrorCode::CapacityExceeded,
                  "prefab scene instance entity or mapping count exceeds its limit"));
    }

    std::set<EntityGuid> entities;
    for (const auto entity : instance.entities) {
        if (entity.isNil() || !entities.insert(entity).second) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument,
                      "prefab scene instance contains a nil or duplicate scene entity"));
        }
    }
    if (!entities.contains(instance.root)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument,
                  "prefab scene instance root is absent from its entity set"));
    }

    std::set<EntityGuid> mappedScenes;
    for (const auto& [source, scene] : instance.sourceToScene) {
        if (source.isNil() || scene.isNil() || !entities.contains(scene) ||
            !mappedScenes.insert(scene).second) {
            return Result<void>::failure(Error(ErrorCode::InvalidArgument,
                                               "prefab scene instance source mapping is invalid"));
        }
    }
    if (!instance.missing && (mappedScenes.size() != entities.size() ||
                              instance.sourceToScene.size() != entities.size())) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument,
                  "linked prefab scene instance must map every baked entity"));
    }
    if (instance.state.removedComponentCount() >
        PrefabInstanceSerializer::MaximumRemovedComponents) {
        return Result<void>::failure(
            Error(ErrorCode::CapacityExceeded,
                  "prefab scene instance removed-component count exceeds its limit"));
    }
    auto rebuilt = PrefabInstance::fromSnapshot(instance.state.snapshot());
    if (!rebuilt)
        return Result<void>::failure(rebuilt.error());
    return Result<void>::success();
}

} // namespace

Result<std::string> PrefabInstanceSerializer::serialize(const PrefabSceneInstance& instance) {
    auto valid = validateLinkage(instance);
    if (!valid)
        return Result<std::string>::failure(valid.error());
    const auto snapshot = instance.state.snapshot();
    auto blobs = overrideBlobs(snapshot);
    if (!blobs)
        return Result<std::string>::failure(blobs.error());

    std::set<EntityGuid> entities(instance.entities.cbegin(), instance.entities.cend());
    std::ostringstream output;
    output << "fglprefabinstance " << CurrentVersion << '\n';
    output << "prefab_guid " << instance.prefab.toString() << '\n';
    output << "root_guid " << instance.root.toString() << '\n';
    output << "missing " << (instance.missing ? 1 : 0) << '\n';
    output << "entity_count " << entities.size() << '\n';
    for (const auto entity : entities)
        output << "entity " << entity.toString() << '\n';
    output << "mapping_count " << instance.sourceToScene.size() << '\n';
    for (const auto& [source, scene] : instance.sourceToScene)
        output << "mapping " << source.toString() << ' ' << scene.toString() << '\n';
    output << "removed_count " << snapshot.removedComponents.size() << '\n';
    for (const auto component : snapshot.removedComponents)
        output << "removed " << component.toString() << '\n';
    output << "overrides_bytes " << blobs.value().first.size() << '\n';
    output << blobs.value().first;
    output << "added_bytes " << blobs.value().second.size() << '\n';
    output << blobs.value().second;
    output << "instance_end\n";

    auto result = output.str();
    if (result.size() > MaximumPropertyStringLength) {
        return Result<std::string>::failure(
            Error(ErrorCode::CapacityExceeded,
                  "prefab instance link exceeds the Scene v2 string-property limit")
                .addContext("maximum", std::to_string(MaximumPropertyStringLength)));
    }
    return Result<std::string>::success(std::move(result));
}

Result<PrefabSceneInstance> PrefabInstanceSerializer::deserialize(std::string_view text) {
    if (text.size() > MaximumPropertyStringLength) {
        return Result<PrefabSceneInstance>::failure(
            Error(ErrorCode::CapacityExceeded, "prefab instance link exceeds its input limit"));
    }
    Cursor cursor(text);
    auto header = tokens(cursor, "fglprefabinstance <version>");
    if (!header || header.value().size() != 2U || header.value()[0] != "fglprefabinstance" ||
        header.value()[1] != std::to_string(CurrentVersion)) {
        return Result<PrefabSceneInstance>::failure(
            header ? formatError("unsupported or invalid prefab instance link header")
                   : header.error());
    }
    auto prefabToken = singleValue(cursor, "prefab_guid");
    auto rootToken = singleValue(cursor, "root_guid");
    auto missingToken = singleValue(cursor, "missing");
    if (!prefabToken || !rootToken || !missingToken) {
        const auto error = !prefabToken ? prefabToken.error()
                           : !rootToken ? rootToken.error()
                                        : missingToken.error();
        return Result<PrefabSceneInstance>::failure(error);
    }
    auto prefab = requiredGuid<AssetGuid>(prefabToken.value(), "prefab_guid");
    auto root = requiredGuid<EntityGuid>(rootToken.value(), "root_guid");
    if (!prefab || !root || (missingToken.value() != "0" && missingToken.value() != "1")) {
        return Result<PrefabSceneInstance>::failure(
            !prefab ? prefab.error()
            : !root ? root.error()
                    : formatError("prefab instance link has an invalid missing flag"));
    }

    auto entityCount = countValue(cursor, "entity_count", MaximumEntities);
    if (!entityCount || entityCount.value() == 0U) {
        return Result<PrefabSceneInstance>::failure(
            entityCount ? formatError("prefab instance link must contain an entity")
                        : entityCount.error());
    }
    std::vector<EntityGuid> entities;
    entities.reserve(entityCount.value());
    for (std::size_t index = 0U; index < entityCount.value(); ++index) {
        auto token = singleValue(cursor, "entity");
        if (!token)
            return Result<PrefabSceneInstance>::failure(token.error());
        auto entity = requiredGuid<EntityGuid>(token.value(), "entity");
        if (!entity)
            return Result<PrefabSceneInstance>::failure(entity.error());
        entities.push_back(entity.value());
    }

    auto mappingCount = countValue(cursor, "mapping_count", MaximumMappings);
    if (!mappingCount)
        return Result<PrefabSceneInstance>::failure(mappingCount.error());
    std::map<EntityGuid, EntityGuid> mappings;
    for (std::size_t index = 0U; index < mappingCount.value(); ++index) {
        auto record = tokens(cursor, "mapping");
        if (!record || record.value().size() != 3U || record.value()[0] != "mapping") {
            return Result<PrefabSceneInstance>::failure(
                record ? formatError("invalid prefab instance source mapping") : record.error());
        }
        auto source = requiredGuid<EntityGuid>(record.value()[1], "mapping.source");
        auto scene = requiredGuid<EntityGuid>(record.value()[2], "mapping.scene");
        if (!source || !scene || !mappings.emplace(source.value(), scene.value()).second) {
            return Result<PrefabSceneInstance>::failure(
                !source  ? source.error()
                : !scene ? scene.error()
                         : formatError("duplicate prefab instance source mapping"));
        }
    }

    auto removedCount = countValue(cursor, "removed_count", MaximumRemovedComponents);
    if (!removedCount)
        return Result<PrefabSceneInstance>::failure(removedCount.error());
    std::set<ComponentTypeGuid> removed;
    for (std::size_t index = 0U; index < removedCount.value(); ++index) {
        auto token = singleValue(cursor, "removed");
        if (!token)
            return Result<PrefabSceneInstance>::failure(token.error());
        auto component = requiredGuid<ComponentTypeGuid>(token.value(), "removed");
        if (!component || !removed.insert(component.value()).second) {
            return Result<PrefabSceneInstance>::failure(
                component ? formatError("duplicate removed prefab component") : component.error());
        }
    }

    auto overrideSize = countValue(cursor, "overrides_bytes", MaximumPropertyStringLength);
    if (!overrideSize)
        return Result<PrefabSceneInstance>::failure(overrideSize.error());
    auto overrideBytes = cursor.bytes(overrideSize.value(), "property override envelope");
    if (!overrideBytes)
        return Result<PrefabSceneInstance>::failure(overrideBytes.error());
    auto addedSize = countValue(cursor, "added_bytes", MaximumPropertyStringLength);
    if (!addedSize)
        return Result<PrefabSceneInstance>::failure(addedSize.error());
    auto addedBytes = cursor.bytes(addedSize.value(), "added-component envelope");
    if (!addedBytes)
        return Result<PrefabSceneInstance>::failure(addedBytes.error());
    auto end = cursor.line("instance_end");
    if (!end || end.value() != "instance_end" || !cursor.empty()) {
        return Result<PrefabSceneInstance>::failure(
            end ? formatError("prefab instance link has trailing or invalid data") : end.error());
    }

    auto propertyEnvelope = PrefabSerializer::deserialize(overrideBytes.value());
    auto addedEnvelope = PrefabSerializer::deserialize(addedBytes.value());
    if (!propertyEnvelope || !addedEnvelope) {
        return Result<PrefabSceneInstance>::failure(!propertyEnvelope ? propertyEnvelope.error()
                                                                      : addedEnvelope.error());
    }
    auto propertyValid =
        validateEnvelope(propertyEnvelope.value(), prefab.value(), PropertyEnvelopeName);
    auto addedValid = validateEnvelope(addedEnvelope.value(), prefab.value(), AddedEnvelopeName);
    if (!propertyValid || !addedValid) {
        return Result<PrefabSceneInstance>::failure(!propertyValid ? propertyValid.error()
                                                                   : addedValid.error());
    }

    PrefabInstanceSnapshot snapshot;
    snapshot.prefab = prefab.value();
    snapshot.addedComponents = std::move(addedEnvelope.value().components);
    snapshot.removedComponents = std::move(removed);
    for (const auto& [type, component] : propertyEnvelope.value().components) {
        if (component.typeName != OverrideComponentName) {
            return Result<PrefabSceneInstance>::failure(
                formatError("prefab property override envelope has an invalid component tag"));
        }
        for (const auto& [name, value] : component.properties)
            snapshot.propertyOverrides.emplace(PrefabPropertyKey{type, name}, value);
    }
    auto state = PrefabInstance::fromSnapshot(std::move(snapshot));
    if (!state)
        return Result<PrefabSceneInstance>::failure(state.error());

    PrefabSceneInstance result(prefab.value());
    result.state = std::move(state.value());
    result.root = root.value();
    result.entities = std::move(entities);
    result.sourceToScene = std::move(mappings);
    result.missing = missingToken.value() == "1";
    auto valid = validateLinkage(result);
    if (!valid)
        return Result<PrefabSceneInstance>::failure(valid.error());
    auto canonical = serialize(result);
    if (!canonical)
        return Result<PrefabSceneInstance>::failure(canonical.error());
    if (canonical.value() != text) {
        return Result<PrefabSceneInstance>::failure(
            formatError("prefab instance link is valid but not canonical"));
    }
    return Result<PrefabSceneInstance>::success(std::move(result));
}

} // namespace fabgl
