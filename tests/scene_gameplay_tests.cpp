#include "test_harness.h"

#include <fabgl/frameworks/scene_gameplay.h>
#include <fabgl/input/input_map.h>
#include <fabgl/reflection/reflection.h>
#include <fabgl/scene/builtin_components.h>
#include <fabgl/scene/entity.h>
#include <fabgl/scene/scene.h>
#include <fabgl/serialization/scene_serializer.h>

#include <memory>

namespace {

fabgl::DataComponent* addGameplayComponent(fabgl::Entity& entity,
                                           const fabgl::ReflectionRegistry& registry,
                                           const char* name) {
    auto created = fabgl::createBuiltinDataComponent(registry, name);
    FGL_CHECK(created);
    auto* raw = created.value().get();
    FGL_CHECK(entity.addComponent(std::move(created.value())));
    return raw;
}

} // namespace

FGL_TEST(scene_gameplay_uses_project_style_input_bindings_on_a_serialized_component) {
    fabgl::ReflectionRegistry registry;
    FGL_CHECK(fabgl::registerBuiltinComponentTypes(registry));
    fabgl::Scene source("Gameplay", fabgl::SceneGuid::fromStableName("tests.scene-gameplay.scene"));
    const auto playerId = fabgl::EntityGuid::fromStableName("tests.scene-gameplay.player");
    auto player = source.createEntity("Player", playerId);
    FGL_CHECK(player);
    auto* controller = addGameplayComponent(*player.value(), registry, "CharacterBody2D");
    FGL_CHECK(controller->set("movementMode", std::int64_t{1}));
    FGL_CHECK(controller->set("moveSpeed", 20.0));
    FGL_CHECK(controller->set("moveXAxis", std::string("MoveX")));
    FGL_CHECK(controller->set("moveYAxis", std::string("MoveY")));
    player.value()->transform().setLocalPosition({10.0F, 12.0F, 0.0F});

    auto serialized = fabgl::SceneSerializer::serialize(source);
    FGL_CHECK(serialized);
    auto loaded = fabgl::SceneSerializer::deserialize(serialized.value());
    FGL_CHECK(loaded);
    FGL_CHECK(loaded.value()->start());

    fabgl::InputMap input;
    FGL_CHECK(input.defineContext("gameplay", 100));
    FGL_CHECK(input.bindAxis("gameplay", "MoveX", {"Key.D", 1.0F, 0.5F}));
    FGL_CHECK(input.bindAxis("gameplay", "MoveY", {"Key.W", -1.0F, 0.5F}));
    FGL_CHECK(input.setControlValue("Key.D", 1.0F));
    FGL_CHECK(input.setControlValue("Key.W", 1.0F));
    input.update();

    fabgl::frameworks::SceneGameplayRuntime gameplay(*loaded.value());
    FGL_CHECK(gameplay.initialize());
    FGL_CHECK(gameplay.update(input, 0.05F));
    const auto position = loaded.value()->findEntity(playerId)->transform().localPosition();
    FGL_CHECK(position.x > 10.0F);
    FGL_CHECK(position.y < 12.0F);
    FGL_CHECK(gameplay.controlledEntityCount() == 1U);
    gameplay.shutdown();
    loaded.value()->shutdown();
}

FGL_TEST(scene_gameplay_rejects_invalid_delta_and_excess_controller_count) {
    fabgl::ReflectionRegistry registry;
    FGL_CHECK(fabgl::registerBuiltinComponentTypes(registry));
    fabgl::Scene scene("Limits");
    for (int index = 0; index < 2; ++index) {
        auto entity = scene.createEntity("Controller");
        FGL_CHECK(entity);
        auto* controller = addGameplayComponent(*entity.value(), registry, "CharacterBody2D");
        FGL_CHECK(controller->set("movementMode", std::int64_t{1}));
    }
    FGL_CHECK(scene.start());
    fabgl::InputMap input;
    FGL_CHECK(input.defineContext("gameplay", 1));
    input.update();
    fabgl::frameworks::SceneGameplayRuntime gameplay(
        scene, fabgl::frameworks::SceneGameplayLimits{1U, 0.1F});
    FGL_CHECK(gameplay.initialize());
    FGL_CHECK(!gameplay.update(input, -0.1F));
    auto capacity = gameplay.update(input, 0.016F);
    FGL_CHECK(!capacity);
    FGL_CHECK(capacity.error().code() == fabgl::ErrorCode::CapacityExceeded);
    gameplay.shutdown();
    scene.shutdown();
}
