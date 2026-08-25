#include "fabgl/save/scene_save_state.h"

#include "fabgl/reflection/reflection.h"
#include "fabgl/scene/component.h"
#include "fabgl/scene/entity.h"
#include "fabgl/scene/scene.h"

#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fabgl {
namespace {

constexpr std::string_view SceneIdKey = "$scene_id";
constexpr std::string_view SceneNameKey = "$scene_name";
constexpr std::string_view EntityCountKey = "$entity_count";
constexpr std::string_view EntityNameKey = "$name";
constexpr std::string_view EntityActiveKey = "$active";
constexpr std::string_view EntityParentKey = "$parent";
constexpr std::string_view PlayerEntityKey = "$entity";
constexpr std::string_view ComponentPrefix = "c/";
constexpr std::string_view ComponentEnabledSuffix = "$enabled";
constexpr std::size_t MaximumEntityFields = 256U;
constexpr std::size_t MaximumStateKeyBytes = 128U;

std::string componentKey(ComponentTypeGuid typeId, std::string_view property) {
    return std::string(ComponentPrefix) + typeId.toString() + "/" + std::string(property);
}

Result<void> insertState(SaveStateMap& state, std::string key, SaveValue value,
                         EntityGuid entity) {
    if (key.empty() || key.size() > MaximumStateKeyBytes) {
        return Result<void>::failure(
            Error(ErrorCode::CapacityExceeded, "scene save state key exceeds its limit")
                .addContext("entity", entity.toString())
                .addContext("key", std::move(key)));
    }
    if (state.size() >= MaximumEntityFields) {
        return Result<void>::failure(
            Error(ErrorCode::CapacityExceeded, "scene save entity has too many state fields")
                .addContext("entity", entity.toString()));
    }
    if (!state.emplace(std::move(key), std::move(value)).second) {
        return Result<void>::failure(
            Error(ErrorCode::AlreadyExists, "scene save state key is duplicated")
                .addContext("entity", entity.toString()));
    }
    return Result<void>::success();
}

Result<std::optional<SaveValue>> toSaveValue(const PropertyMetadata& metadata,
                                              const PropertyValue& value,
                                              bool failOnUnsupported) {
    switch (metadata.type) {
    case PropertyType::Boolean:
        return Result<std::optional<SaveValue>>::success(SaveValue(std::get<bool>(value)));
    case PropertyType::SignedInteger:
    case PropertyType::Enumeration:
        return Result<std::optional<SaveValue>>::success(
            SaveValue(std::get<std::int64_t>(value)));
    case PropertyType::UnsignedInteger:
    case PropertyType::BitFlags: {
        const auto number = std::get<std::uint64_t>(value);
        if (number <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return Result<std::optional<SaveValue>>::success(
                SaveValue(static_cast<std::int64_t>(number)));
        }
        return Result<std::optional<SaveValue>>::success(SaveValue(std::to_string(number)));
    }
    case PropertyType::Float:
        return Result<std::optional<SaveValue>>::success(SaveValue(std::get<double>(value)));
    case PropertyType::Fixed:
        return Result<std::optional<SaveValue>>::success(
            SaveValue(static_cast<std::int64_t>(std::get<Fixed>(value).raw())));
    case PropertyType::String:
        return Result<std::optional<SaveValue>>::success(
            SaveValue(std::get<std::string>(value)));
    case PropertyType::Vec2:
        return Result<std::optional<SaveValue>>::success(SaveValue(std::get<Vec2>(value)));
    case PropertyType::Vec3:
        return Result<std::optional<SaveValue>>::success(SaveValue(std::get<Vec3>(value)));
    case PropertyType::EulerAngles: {
        const auto rotation = std::get<EulerAngles>(value);
        return Result<std::optional<SaveValue>>::success(
            SaveValue(Vec3{rotation.x, rotation.y, rotation.z}));
    }
    case PropertyType::Color:
        return Result<std::optional<SaveValue>>::success(
            SaveValue(static_cast<std::int64_t>(std::get<Color>(value).rgba32())));
    case PropertyType::AssetReference:
        return Result<std::optional<SaveValue>>::success(
            SaveValue(std::get<AssetGuid>(value).toString()));
    case PropertyType::EntityReference:
        return Result<std::optional<SaveValue>>::success(
            SaveValue(std::get<EntityGuid>(value).toString()));
    case PropertyType::ActionReference:
        return Result<std::optional<SaveValue>>::success(
            SaveValue(std::get<ActionReference>(value).name));
    case PropertyType::EventReference:
        return Result<std::optional<SaveValue>>::success(
            SaveValue(std::get<EventReference>(value).name));
    case PropertyType::Quaternion:
    case PropertyType::Rect:
    case PropertyType::ComponentReference:
    case PropertyType::List:
    case PropertyType::Curve:
    case PropertyType::AnimationCurve:
        break;
    }
    if (failOnUnsupported) {
        return Result<std::optional<SaveValue>>::failure(
            Error(ErrorCode::TypeMismatch,
                  "reflected property type is not representable in gameplay save state")
                .addContext("property", metadata.name));
    }
    return Result<std::optional<SaveValue>>::success(std::nullopt);
}

Result<std::uint64_t> readUnsigned(const SaveValue& value) {
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        if (*integer < 0) {
            return Result<std::uint64_t>::failure(
                Error(ErrorCode::TypeMismatch, "saved unsigned value is negative"));
        }
        return Result<std::uint64_t>::success(static_cast<std::uint64_t>(*integer));
    }
    const auto* text = std::get_if<std::string>(&value);
    if (text == nullptr || text->empty()) {
        return Result<std::uint64_t>::failure(
            Error(ErrorCode::TypeMismatch, "saved unsigned value has the wrong type"));
    }
    std::uint64_t number = 0U;
    const auto parsed = std::from_chars(text->data(), text->data() + text->size(), number);
    if (parsed.ec != std::errc{} || parsed.ptr != text->data() + text->size()) {
        return Result<std::uint64_t>::failure(
            Error(ErrorCode::InvalidFormat, "saved unsigned value is invalid"));
    }
    return Result<std::uint64_t>::success(number);
}

template <typename Guid>
Result<PropertyValue> readGuidProperty(const SaveValue& saved, std::string_view property) {
    const auto* text = std::get_if<std::string>(&saved);
    if (text == nullptr) {
        return Result<PropertyValue>::failure(
            Error(ErrorCode::TypeMismatch, "saved GUID property has the wrong type")
                .addContext("property", std::string(property)));
    }
    auto guid = Guid::parse(*text);
    if (!guid) {
        return Result<PropertyValue>::failure(
            guid.error().withContext("property", std::string(property)));
    }
    return Result<PropertyValue>::success(PropertyValue(guid.value()));
}

Result<PropertyValue> fromSaveValue(const PropertyMetadata& metadata, const SaveValue& saved) {
    switch (metadata.type) {
    case PropertyType::Boolean:
        if (const auto* value = std::get_if<bool>(&saved))
            return Result<PropertyValue>::success(PropertyValue(*value));
        break;
    case PropertyType::SignedInteger:
    case PropertyType::Enumeration:
        if (const auto* value = std::get_if<std::int64_t>(&saved))
            return Result<PropertyValue>::success(PropertyValue(*value));
        break;
    case PropertyType::UnsignedInteger:
    case PropertyType::BitFlags: {
        auto value = readUnsigned(saved);
        if (!value)
            return Result<PropertyValue>::failure(value.error());
        return Result<PropertyValue>::success(PropertyValue(value.value()));
    }
    case PropertyType::Float:
        if (const auto* value = std::get_if<double>(&saved))
            return Result<PropertyValue>::success(PropertyValue(*value));
        break;
    case PropertyType::Fixed:
        if (const auto* value = std::get_if<std::int64_t>(&saved);
            value != nullptr && *value >= std::numeric_limits<std::int32_t>::min() &&
            *value <= std::numeric_limits<std::int32_t>::max()) {
            return Result<PropertyValue>::success(
                PropertyValue(Fixed::fromRaw(static_cast<std::int32_t>(*value))));
        }
        break;
    case PropertyType::String:
        if (const auto* value = std::get_if<std::string>(&saved))
            return Result<PropertyValue>::success(PropertyValue(*value));
        break;
    case PropertyType::Vec2:
        if (const auto* value = std::get_if<Vec2>(&saved))
            return Result<PropertyValue>::success(PropertyValue(*value));
        break;
    case PropertyType::Vec3:
        if (const auto* value = std::get_if<Vec3>(&saved))
            return Result<PropertyValue>::success(PropertyValue(*value));
        break;
    case PropertyType::EulerAngles:
        if (const auto* value = std::get_if<Vec3>(&saved)) {
            return Result<PropertyValue>::success(
                PropertyValue(EulerAngles{value->x, value->y, value->z}));
        }
        break;
    case PropertyType::Color:
        if (const auto* value = std::get_if<std::int64_t>(&saved);
            value != nullptr && *value >= 0 &&
            *value <= static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
            const auto rgba = static_cast<std::uint32_t>(*value);
            return Result<PropertyValue>::success(PropertyValue(Color{
                static_cast<std::uint8_t>((rgba >> 24U) & 0xFFU),
                static_cast<std::uint8_t>((rgba >> 16U) & 0xFFU),
                static_cast<std::uint8_t>((rgba >> 8U) & 0xFFU),
                static_cast<std::uint8_t>(rgba & 0xFFU)}));
        }
        break;
    case PropertyType::AssetReference:
        return readGuidProperty<AssetGuid>(saved, metadata.name);
    case PropertyType::EntityReference:
        return readGuidProperty<EntityGuid>(saved, metadata.name);
    case PropertyType::ActionReference:
        if (const auto* value = std::get_if<std::string>(&saved))
            return Result<PropertyValue>::success(PropertyValue(ActionReference{*value}));
        break;
    case PropertyType::EventReference:
        if (const auto* value = std::get_if<std::string>(&saved))
            return Result<PropertyValue>::success(PropertyValue(EventReference{*value}));
        break;
    case PropertyType::Quaternion:
    case PropertyType::Rect:
    case PropertyType::ComponentReference:
    case PropertyType::List:
    case PropertyType::Curve:
    case PropertyType::AnimationCurve:
        break;
    }
    return Result<PropertyValue>::failure(
        Error(ErrorCode::TypeMismatch, "saved property value has the wrong type")
            .addContext("property", metadata.name));
}

bool isSaveProperty(const PropertyMetadata& metadata) noexcept {
    return hasFlag(metadata.flags, PropertyFlags::RuntimeEditable) &&
           !hasFlag(metadata.flags, PropertyFlags::EditorOnly);
}

Result<SaveStateMap> captureEntity(const Entity& entity, bool failOnUnsupported) {
    SaveStateMap state;
    auto inserted = insertState(state, std::string(EntityNameKey), SaveValue(entity.name()),
                                entity.id());
    if (!inserted)
        return Result<SaveStateMap>::failure(inserted.error());
    inserted = insertState(state, std::string(EntityActiveKey), SaveValue(entity.active()),
                           entity.id());
    if (!inserted)
        return Result<SaveStateMap>::failure(inserted.error());
    inserted = insertState(
        state, std::string(EntityParentKey),
        SaveValue(entity.transform().parent() ? entity.transform().parent()->toString() : ""),
        entity.id());
    if (!inserted)
        return Result<SaveStateMap>::failure(inserted.error());

    for (const auto* component : entity.components()) {
        inserted = insertState(state, componentKey(component->typeId(), ComponentEnabledSuffix),
                               SaveValue(component->enabled()), entity.id());
        if (!inserted)
            return Result<SaveStateMap>::failure(inserted.error());
        const auto* metadata = component->metadata();
        if (metadata == nullptr) {
            if (failOnUnsupported) {
                return Result<SaveStateMap>::failure(
                    Error(ErrorCode::InvalidState,
                          "component has no reflection metadata for gameplay save")
                        .addContext("entity", entity.id().toString())
                        .addContext("component", std::string(component->typeName())));
            }
            continue;
        }
        for (const auto& property : metadata->properties) {
            if (!isSaveProperty(property))
                continue;
            auto value = property.read(component);
            if (!value) {
                return Result<SaveStateMap>::failure(
                    value.error()
                        .withContext("entity", entity.id().toString())
                        .withContext("component", metadata->name));
            }
            auto saved = toSaveValue(property, value.value(), failOnUnsupported);
            if (!saved) {
                return Result<SaveStateMap>::failure(
                    saved.error()
                        .withContext("entity", entity.id().toString())
                        .withContext("component", metadata->name));
            }
            if (!saved.value())
                continue;
            inserted = insertState(state, componentKey(component->typeId(), property.name),
                                   std::move(*saved.value()), entity.id());
            if (!inserted)
                return Result<SaveStateMap>::failure(inserted.error());
        }
    }
    return Result<SaveStateMap>::success(std::move(state));
}

struct PropertyWrite final {
    Component* component = nullptr;
    const PropertyMetadata* metadata = nullptr;
    PropertyValue value = false;
};

struct ComponentEnableWrite final {
    Component* component = nullptr;
    bool enabled = true;
};

struct EntityWrite final {
    Entity* entity = nullptr;
    std::optional<std::string> name;
    std::optional<bool> active;
    std::optional<std::optional<EntityGuid>> parent;
    std::vector<ComponentEnableWrite> componentEnables;
    std::vector<PropertyWrite> properties;
};

Result<EntityWrite> prepareEntityWrite(Scene& scene, Entity& entity, const SaveStateMap& state,
                                       const SceneSaveRestoreOptions& options,
                                       bool playerSection) {
    EntityWrite write;
    write.entity = &entity;
    std::set<std::string> consumed;
    if (playerSection)
        consumed.emplace(PlayerEntityKey);

    if (const auto found = state.find(std::string(EntityNameKey)); found != state.end()) {
        const auto* name = std::get_if<std::string>(&found->second);
        if (name == nullptr) {
            return Result<EntityWrite>::failure(
                Error(ErrorCode::TypeMismatch, "saved entity name has the wrong type")
                    .addContext("entity", entity.id().toString()));
        }
        write.name = *name;
        consumed.emplace(found->first);
    }
    if (const auto found = state.find(std::string(EntityActiveKey)); found != state.end()) {
        const auto* active = std::get_if<bool>(&found->second);
        if (active == nullptr) {
            return Result<EntityWrite>::failure(
                Error(ErrorCode::TypeMismatch, "saved entity active state has the wrong type")
                    .addContext("entity", entity.id().toString()));
        }
        write.active = *active;
        consumed.emplace(found->first);
    }
    if (const auto found = state.find(std::string(EntityParentKey)); found != state.end()) {
        const auto* parentText = std::get_if<std::string>(&found->second);
        if (parentText == nullptr) {
            return Result<EntityWrite>::failure(
                Error(ErrorCode::TypeMismatch, "saved entity parent has the wrong type")
                    .addContext("entity", entity.id().toString()));
        }
        if (parentText->empty()) {
            write.parent.emplace();
        } else {
            auto parent = EntityGuid::parse(*parentText);
            if (!parent) {
                return Result<EntityWrite>::failure(
                    parent.error().withContext("entity", entity.id().toString()));
            }
            if (scene.findEntity(parent.value()) == nullptr && options.requireEntities) {
                return Result<EntityWrite>::failure(
                    Error(ErrorCode::NotFound, "saved parent entity is missing")
                        .addContext("entity", entity.id().toString())
                        .addContext("parent", parentText->c_str()));
            }
            write.parent.emplace(parent.value());
        }
        consumed.emplace(found->first);
    }

    for (auto* component : entity.components()) {
        const auto enabledKey = componentKey(component->typeId(), ComponentEnabledSuffix);
        if (const auto found = state.find(enabledKey); found != state.end()) {
            const auto* enabled = std::get_if<bool>(&found->second);
            if (enabled == nullptr) {
                return Result<EntityWrite>::failure(
                    Error(ErrorCode::TypeMismatch,
                          "saved component enabled state has the wrong type")
                        .addContext("entity", entity.id().toString())
                        .addContext("component", std::string(component->typeName())));
            }
            write.componentEnables.push_back({component, *enabled});
            consumed.emplace(found->first);
        }
        const auto* metadata = component->metadata();
        if (metadata == nullptr)
            continue;
        for (const auto& property : metadata->properties) {
            if (!isSaveProperty(property))
                continue;
            const auto key = componentKey(component->typeId(), property.name);
            const auto found = state.find(key);
            if (found == state.end())
                continue;
            auto value = fromSaveValue(property, found->second);
            if (!value) {
                return Result<EntityWrite>::failure(
                    value.error()
                        .withContext("entity", entity.id().toString())
                        .withContext("component", metadata->name));
            }
            auto valid = validatePropertyValue(property, value.value());
            if (!valid) {
                return Result<EntityWrite>::failure(
                    valid.error()
                        .withContext("entity", entity.id().toString())
                        .withContext("component", metadata->name));
            }
            write.properties.push_back({component, &property, std::move(value.value())});
            consumed.emplace(found->first);
        }
    }

    if (options.requireComponents || options.requireProperties) {
        for (const auto& [key, unused] : state) {
            static_cast<void>(unused);
            if (consumed.find(key) != consumed.end())
                continue;
            if (key.starts_with(ComponentPrefix) || options.requireProperties) {
                return Result<EntityWrite>::failure(
                    Error(ErrorCode::NotFound, "saved state field has no runtime target")
                        .addContext("entity", entity.id().toString())
                        .addContext("field", key));
            }
        }
    }
    return Result<EntityWrite>::success(std::move(write));
}

Result<void> applyWrites(Scene& scene, std::vector<EntityWrite>& writes,
                         const SceneSaveRestoreOptions& options) {
    for (auto& write : writes) {
        if (write.name)
            write.entity->setName(*write.name);
        if (write.active)
            write.entity->setActive(*write.active);
        for (const auto& enabled : write.componentEnables)
            enabled.component->setEnabled(enabled.enabled);
        for (auto& property : write.properties) {
            auto applied = property.metadata->write(property.component, property.value);
            if (!applied) {
                return Result<void>::failure(
                    applied.error()
                        .withContext("entity", write.entity->id().toString())
                        .withContext("component", std::string(property.component->typeName())));
            }
        }
    }
    if (!options.restoreHierarchy)
        return Result<void>::success();
    for (const auto& write : writes) {
        if (!write.parent)
            continue;
        if (*write.parent && scene.findEntity(**write.parent) == nullptr)
            continue;
        auto parented = scene.setParent(write.entity->id(), *write.parent);
        if (!parented)
            return parented;
    }
    return Result<void>::success();
}

} // namespace

Result<SaveDocument> SceneSaveState::capture(const Scene& scene,
                                             const SceneSaveCaptureOptions& options) {
    SaveDocument document;
    auto captured = captureInto(scene, document, options);
    if (!captured)
        return Result<SaveDocument>::failure(captured.error());
    return Result<SaveDocument>::success(std::move(document));
}

Result<void> SceneSaveState::captureInto(const Scene& scene, SaveDocument& document,
                                         const SceneSaveCaptureOptions& options) {
    SaveStateMap sceneState;
    sceneState.emplace(SceneIdKey, scene.id().toString());
    sceneState.emplace(SceneNameKey, scene.name());
    sceneState.emplace(EntityCountKey, static_cast<std::int64_t>(scene.entityCount()));

    std::map<std::string, SaveStateMap> entityStates;
    SaveStateMap playerState;
    bool playerFound = !options.playerEntity.has_value();
    for (const auto* entity : scene.entities()) {
        auto state = captureEntity(*entity, options.failOnUnsupportedProperties);
        if (!state)
            return Result<void>::failure(state.error());
        if (options.playerEntity && entity->id() == *options.playerEntity) {
            if (state.value().size() >= MaximumEntityFields) {
                return Result<void>::failure(
                    Error(ErrorCode::CapacityExceeded,
                          "player save state has too many fields for its identity"));
            }
            playerState = state.value();
            playerState.emplace(PlayerEntityKey, entity->id().toString());
            playerFound = true;
        }
        entityStates.emplace(entity->id().toString(), std::move(state.value()));
    }
    if (!playerFound) {
        return Result<void>::failure(
            Error(ErrorCode::NotFound, "player entity was not found while capturing save state")
                .addContext("player", options.playerEntity->toString()));
    }
    document.scene = std::move(sceneState);
    document.entities = std::move(entityStates);
    document.player = std::move(playerState);
    return Result<void>::success();
}

Result<void> SceneSaveState::restore(Scene& scene, const SaveDocument& document,
                                     const SceneSaveRestoreOptions& options) {
    if (const auto found = document.scene.find(std::string(SceneIdKey));
        found != document.scene.end()) {
        const auto* id = std::get_if<std::string>(&found->second);
        if (id == nullptr) {
            return Result<void>::failure(
                Error(ErrorCode::TypeMismatch, "saved scene identity has the wrong type"));
        }
        auto parsed = SceneGuid::parse(*id);
        if (!parsed) {
            return Result<void>::failure(parsed.error().withContext("section", "scene"));
        }
        if (options.requireSceneIdentity && parsed.value() != scene.id()) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidState, "save belongs to a different scene")
                    .addContext("expected", scene.id().toString())
                    .addContext("actual", *id));
        }
    } else if (options.requireSceneIdentity) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidFormat, "saved scene identity is missing"));
    }

    std::vector<EntityWrite> writes;
    writes.reserve(document.entities.size() + (document.player.empty() ? 0U : 1U));
    for (const auto& [entityText, state] : document.entities) {
        auto id = EntityGuid::parse(entityText);
        if (!id) {
            return Result<void>::failure(id.error().withContext("section", "entities"));
        }
        auto* entity = scene.findEntity(id.value());
        if (entity == nullptr) {
            if (options.requireEntities) {
                return Result<void>::failure(
                    Error(ErrorCode::NotFound, "saved entity is missing from the scene")
                        .addContext("entity", entityText));
            }
            continue;
        }
        auto prepared = prepareEntityWrite(scene, *entity, state, options, false);
        if (!prepared)
            return Result<void>::failure(prepared.error());
        writes.push_back(std::move(prepared.value()));
    }

    if (!document.player.empty()) {
        const auto player = document.player.find(std::string(PlayerEntityKey));
        if (player == document.player.end() ||
            !std::holds_alternative<std::string>(player->second)) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidFormat, "player save state has no valid entity identity"));
        }
        auto playerId = EntityGuid::parse(std::get<std::string>(player->second));
        if (!playerId)
            return Result<void>::failure(playerId.error().withContext("section", "player"));
        auto* playerEntity = scene.findEntity(playerId.value());
        if (playerEntity == nullptr) {
            if (options.requireEntities) {
                return Result<void>::failure(
                    Error(ErrorCode::NotFound, "saved player entity is missing from the scene")
                        .addContext("player", playerId.value().toString()));
            }
        } else {
            auto prepared =
                prepareEntityWrite(scene, *playerEntity, document.player, options, true);
            if (!prepared)
                return Result<void>::failure(prepared.error());
            writes.push_back(std::move(prepared.value()));
        }
    }

    if (const auto name = document.scene.find(std::string(SceneNameKey));
        name != document.scene.end()) {
        const auto* text = std::get_if<std::string>(&name->second);
        if (text == nullptr) {
            return Result<void>::failure(
                Error(ErrorCode::TypeMismatch, "saved scene name has the wrong type"));
        }
        scene.setName(*text);
    }
    return applyWrites(scene, writes, options);
}

} // namespace fabgl
