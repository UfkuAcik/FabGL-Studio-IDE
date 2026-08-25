#include "test_harness.h"

#include "fabgl/save/save_system.h"
#include "fabgl/save/scene_save_state.h"
#include "fabgl/scene/builtin_components.h"
#include "fabgl/scene/scene.h"

#include <cstdint>
#include <memory>
#include <string>

using namespace fabgl;

namespace {

DataComponent* addBuiltin(Entity& entity, const ReflectionRegistry& registry,
                          std::string_view name) {
    auto component = createBuiltinDataComponent(registry, name);
    FGL_CHECK(component);
    auto attached = entity.addComponent(std::move(component.value()));
    FGL_CHECK(attached);
    return static_cast<DataComponent*>(attached.value());
}

std::string propertyKey(const Component& component, std::string_view property) {
    return "c/" + component.typeId().toString() + "/" + std::string(property);
}

} // namespace

FGL_TEST(scene_save_state_round_trips_hierarchy_player_and_reflected_runtime_values) {
    ReflectionRegistry registry;
    FGL_CHECK(registerBuiltinComponentTypes(registry));
    const auto sceneId = SceneGuid::fromStableName("save.scene.roundtrip");
    const auto parentId = EntityGuid::fromStableName("save.entity.parent");
    const auto playerId = EntityGuid::fromStableName("save.entity.player");
    Scene scene("Before Save", sceneId);
    auto parent = scene.createEntity("Parent", parentId);
    auto player = scene.createEntity("Player", playerId);
    FGL_CHECK(parent && player);
    FGL_CHECK(scene.setParent(playerId, parentId));
    player.value()->transform().setLocalPosition({4.0F, 5.0F, 6.0F});
    player.value()->transform().setLocalRotation({0.1F, 0.2F, 0.3F});
    player.value()->transform().setLocalScale({2.0F, 3.0F, 4.0F});
    auto* health = addBuiltin(*player.value(), registry, "Health");
    FGL_CHECK(health->set("current", PropertyValue(std::int64_t{73})));
    health->setEnabled(false);
    player.value()->setActive(false);

    SaveDocument document;
    document.primitives.emplace("chapter", std::int64_t{3});
    FGL_CHECK(SceneSaveState::captureInto(
        scene, document, SceneSaveCaptureOptions{playerId, false}));
    FGL_CHECK(std::get<std::int64_t>(document.primitives.at("chapter")) == 3);
    FGL_CHECK(document.entities.size() == 2U);
    FGL_CHECK(std::get<std::string>(document.player.at("$entity")) == playerId.toString());
    FGL_CHECK(std::get<std::int64_t>(document.player.at(propertyKey(*health, "current"))) == 73);

    auto storage = std::make_shared<MemorySaveStorage>();
    SaveSystem saves(storage, 2U);
    FGL_CHECK(saves.saveDocument("campaign", document));
    auto loaded = saves.loadDocument("campaign");
    FGL_CHECK(loaded);

    scene.setName("Mutated");
    player.value()->setName("Mutated Player");
    player.value()->setActive(true);
    health->setEnabled(true);
    FGL_CHECK(health->set("current", PropertyValue(std::int64_t{1})));
    player.value()->transform().setLocalPosition({});
    player.value()->transform().setLocalRotation({});
    player.value()->transform().setLocalScale({1.0F, 1.0F, 1.0F});
    FGL_CHECK(scene.clearParent(playerId));
    FGL_CHECK(scene.setParent(parentId, playerId));

    FGL_CHECK(SceneSaveState::restore(scene, loaded.value().document));
    FGL_CHECK(scene.name() == "Before Save");
    FGL_CHECK(player.value()->name() == "Player");
    FGL_CHECK(!player.value()->active());
    FGL_CHECK(!health->enabled());
    FGL_CHECK(std::get<std::int64_t>(health->get("current").value()) == 73);
    FGL_CHECK(player.value()->transform().parent() == parentId);
    FGL_CHECK(!parent.value()->transform().parent());
    FGL_CHECK(player.value()->transform().localPosition() == Vec3{4.0F, 5.0F, 6.0F});
    FGL_CHECK(player.value()->transform().localRotation() == Vec3{0.1F, 0.2F, 0.3F});
    FGL_CHECK(player.value()->transform().localScale() == Vec3{2.0F, 3.0F, 4.0F});
}

FGL_TEST(scene_save_state_validates_identity_missing_targets_and_player_override) {
    ReflectionRegistry registry;
    FGL_CHECK(registerBuiltinComponentTypes(registry));
    const auto sceneId = SceneGuid::fromStableName("save.scene.validation");
    const auto playerId = EntityGuid::fromStableName("save.player.validation");
    Scene scene("Validation", sceneId);
    auto player = scene.createEntity("Player", playerId);
    FGL_CHECK(player);
    auto* health = addBuiltin(*player.value(), registry, "Health");
    FGL_CHECK(health->set("current", PropertyValue(std::int64_t{50})));

    auto captured = SceneSaveState::capture(
        scene, SceneSaveCaptureOptions{playerId, false});
    FGL_CHECK(captured);
    captured.value().player[propertyKey(*health, "current")] = std::int64_t{77};
    FGL_CHECK(health->set("current", PropertyValue(std::int64_t{1})));
    FGL_CHECK(SceneSaveState::restore(scene, captured.value()));
    FGL_CHECK(std::get<std::int64_t>(health->get("current").value()) == 77);

    auto wrongScene = captured.value();
    wrongScene.scene["$scene_id"] =
        SceneGuid::fromStableName("save.scene.other").toString();
    auto mismatch = SceneSaveState::restore(scene, wrongScene);
    FGL_CHECK(!mismatch && mismatch.error().code() == ErrorCode::InvalidState);

    auto missing = captured.value();
    missing.entities.emplace(EntityGuid::fromStableName("save.missing").toString(),
                             SaveStateMap{{"$name", std::string("Missing")}});
    auto strict = SceneSaveState::restore(scene, missing);
    FGL_CHECK(!strict && strict.error().code() == ErrorCode::NotFound);
    SceneSaveRestoreOptions lenient;
    lenient.requireEntities = false;
    FGL_CHECK(SceneSaveState::restore(scene, missing, lenient));

    auto unknownField = captured.value();
    unknownField.entities[playerId.toString()]["c/unknown/property"] = true;
    auto unknown = SceneSaveState::restore(scene, unknownField);
    FGL_CHECK(!unknown && unknown.error().code() == ErrorCode::NotFound);
    SceneSaveRestoreOptions allowUnknown;
    allowUnknown.requireComponents = false;
    FGL_CHECK(SceneSaveState::restore(scene, unknownField, allowUnknown));
}

FGL_TEST(scene_save_state_can_reject_unrepresentable_runtime_properties_explicitly) {
    ReflectionRegistry registry;
    FGL_CHECK(registerBuiltinComponentTypes(registry));
    Scene scene("Unsupported", SceneGuid::fromStableName("save.scene.unsupported"));
    auto entity = scene.createEntity("Receiver");
    FGL_CHECK(entity);
    addBuiltin(*entity.value(), registry, "DamageReceiver");

    auto permissive = SceneSaveState::capture(scene);
    FGL_CHECK(permissive);
    auto strict = SceneSaveState::capture(
        scene, SceneSaveCaptureOptions{std::nullopt, true});
    FGL_CHECK(!strict && strict.error().code() == ErrorCode::TypeMismatch);
}
