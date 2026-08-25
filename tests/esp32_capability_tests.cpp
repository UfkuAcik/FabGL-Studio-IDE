#include "test_harness.h"

#include <ProjectRuntime.h>
#include <esp32_capabilities.h>
#include <fabgl/scene/builtin_components.h>
#include <fabgl/scene/scene.h>

#include <memory>
#include <string>
#include <string_view>

namespace {

fabgl::DataComponent* addBuiltin(fabgl::Entity& entity,
                                 const fabgl::ReflectionRegistry& registry,
                                 const std::string_view name) {
    auto created = fabgl::createBuiltinDataComponent(registry, name);
    FGL_CHECK(created);
    auto* component = created.value().get();
    FGL_CHECK(entity.addComponent(std::move(created.value())));
    return component;
}

std::string errorContext(const fabgl::Error& error, const std::string_view key) {
    for (const auto& item : error.context()) {
        if (item.key == key)
            return item.value;
    }
    return {};
}

class UnknownComponent final : public fabgl::Component {
  public:
    [[nodiscard]] fabgl::ComponentTypeGuid typeId() const noexcept override {
        return fabgl::ComponentTypeGuid::fromStableName("tests.esp32.unknown-component");
    }
    [[nodiscard]] std::string_view typeName() const noexcept override {
        return "vendor.UnknownComponent";
    }
};

fabgl::project::Manifest baseManifest() {
    fabgl::project::Manifest manifest;
    manifest.projectGuid =
        fabgl::AssetGuid::fromStableName("tests.esp32.capability.project").toString();
    manifest.name = "ESP32 Capability Test";
    return manifest;
}

} // namespace

FGL_TEST(esp32_capability_contract_matches_the_allocation_free_runtime_limits) {
    FGL_CHECK(fabgl::project::Esp32RuntimeMaximumEntities ==
              fabgl_project_runtime::kMaximumEntities);
    FGL_CHECK(fabgl::project::Esp32RuntimeMaximumAssets ==
              fabgl_project_runtime::kMaximumAssets);
    FGL_CHECK(fabgl::project::Esp32RuntimeMaximumInputValues ==
              fabgl_project_runtime::kMaximumInputValues);
    FGL_CHECK(fabgl::project::Esp32RuntimeMaximumInputBindings ==
              fabgl_project_runtime::kMaximumInputBindings);

    FGL_CHECK(fabgl::project::classifyEsp32RuntimeComponent("fabgl.SpriteRenderer") ==
              fabgl::project::Esp32ComponentCapability::Supported);
    FGL_CHECK(fabgl::project::classifyEsp32RuntimeComponent("fabgl.Collider2D") ==
              fabgl::project::Esp32ComponentCapability::KnownButNotPorted);
    FGL_CHECK(fabgl::project::classifyEsp32RuntimeComponent("vendor.Custom") ==
              fabgl::project::Esp32ComponentCapability::Unknown);
    FGL_CHECK(fabgl::project::classifyEsp32RuntimeAsset("visual.script", "Graph.fglvisual") ==
              fabgl::project::Esp32AssetCapability::VisualScriptVmUnavailable);
    FGL_CHECK(fabgl::project::classifyEsp32RuntimeAsset("audio", "Tone.fgla") ==
              fabgl::project::Esp32AssetCapability::Unsupported);
}

FGL_TEST(esp32_capability_contract_accepts_the_firmware_scene_and_asset_subset) {
    fabgl::ReflectionRegistry registry;
    FGL_CHECK(fabgl::registerBuiltinComponentTypes(registry));
    fabgl::Scene scene("Supported ESP32 Scene",
                       fabgl::SceneGuid::fromStableName("tests.esp32.capability.scene"));
    auto entity = scene.createEntity(
        "Player", fabgl::EntityGuid::fromStableName("tests.esp32.capability.player"));
    FGL_CHECK(entity);
    auto* sprite = addBuiltin(*entity.value(), registry, "SpriteRenderer");
    const auto spriteGuid = fabgl::AssetGuid::fromStableName("tests.esp32.capability.sprite");
    FGL_CHECK(sprite->set("sprite", spriteGuid));

    auto manifest = baseManifest();
    manifest.assets = {{spriteGuid, "Assets/Player.fgli", "image"}};
    fabgl::project::InputContextDefinition input;
    input.name = "gameplay";
    input.axes = {{"MoveX", {{"Key.A", -1.0F, 0.5F}, {"Key.D", 1.0F, 0.5F}}}};
    manifest.inputContexts.push_back(std::move(input));

    auto compatible = fabgl::project::validateEsp32TargetCapabilities(manifest, scene);
    FGL_CHECK(compatible);
    FGL_CHECK(compatible.value().entityCount == 1U);
    FGL_CHECK(compatible.value().componentCount == 2U);
    FGL_CHECK(compatible.value().assetCount == 1U);
    FGL_CHECK(compatible.value().inputValueCount == 1U);
    FGL_CHECK(compatible.value().inputBindingCount == 2U);
}

FGL_TEST(esp32_capability_contract_distinguishes_unported_and_unknown_components) {
    fabgl::ReflectionRegistry registry;
    FGL_CHECK(fabgl::registerBuiltinComponentTypes(registry));
    auto manifest = baseManifest();

    fabgl::Scene knownScene("Known",
                            fabgl::SceneGuid::fromStableName("tests.esp32.known.scene"));
    auto knownEntity = knownScene.createEntity("Physics");
    FGL_CHECK(knownEntity);
    static_cast<void>(addBuiltin(*knownEntity.value(), registry, "Collider2D"));
    auto known = fabgl::project::validateEsp32TargetCapabilities(manifest, knownScene);
    FGL_CHECK(!known);
    FGL_CHECK(known.error().code() == fabgl::ErrorCode::InvalidState);
    FGL_CHECK(errorContext(known.error(), "reason") == "known_but_not_ported");
    FGL_CHECK(errorContext(known.error(), "component") == "fabgl.Collider2D");
    FGL_CHECK(!errorContext(known.error(), "entity_guid").empty());

    fabgl::Scene unknownScene("Unknown",
                              fabgl::SceneGuid::fromStableName("tests.esp32.unknown.scene"));
    auto unknownEntity = unknownScene.createEntity("Plugin Entity");
    FGL_CHECK(unknownEntity);
    FGL_CHECK(unknownEntity.value()->addComponent(std::make_unique<UnknownComponent>()));
    auto unknown = fabgl::project::validateEsp32TargetCapabilities(manifest, unknownScene);
    FGL_CHECK(!unknown);
    FGL_CHECK(unknown.error().code() == fabgl::ErrorCode::InvalidState);
    FGL_CHECK(errorContext(unknown.error(), "reason") == "unknown");
    FGL_CHECK(errorContext(unknown.error(), "component") == "vendor.UnknownComponent");
}

FGL_TEST(esp32_capability_contract_explicitly_rejects_visual_scripts_and_bad_asset_references) {
    auto manifest = baseManifest();
    const auto graphGuid = fabgl::AssetGuid::fromStableName("tests.esp32.capability.visual");
    manifest.assets = {{graphGuid, "Visual/Player.fglvisual", "visual.script"}};
    fabgl::Scene empty("Visual",
                       fabgl::SceneGuid::fromStableName("tests.esp32.capability.visual.scene"));
    auto visual = fabgl::project::validateEsp32TargetCapabilities(manifest, empty);
    FGL_CHECK(!visual);
    FGL_CHECK(visual.error().code() == fabgl::ErrorCode::InvalidState);
    FGL_CHECK(errorContext(visual.error(), "feature") == "visual_script");
    FGL_CHECK(visual.error().message().find("no visual-script VM") != std::string::npos);

    fabgl::ReflectionRegistry registry;
    FGL_CHECK(fabgl::registerBuiltinComponentTypes(registry));
    fabgl::Scene spriteScene(
        "Reference", fabgl::SceneGuid::fromStableName("tests.esp32.capability.reference.scene"));
    auto entity = spriteScene.createEntity("Sprite");
    FGL_CHECK(entity);
    auto* sprite = addBuiltin(*entity.value(), registry, "SpriteRenderer");
    FGL_CHECK(sprite->set("sprite", graphGuid));
    manifest.assets = {{graphGuid, "Assets/Payload.bin", "binary"}};
    auto mismatch = fabgl::project::validateEsp32TargetCapabilities(manifest, spriteScene);
    FGL_CHECK(!mismatch);
    FGL_CHECK(mismatch.error().code() == fabgl::ErrorCode::TypeMismatch);
    FGL_CHECK(errorContext(mismatch.error(), "expected_asset_type") == "image");
    FGL_CHECK(errorContext(mismatch.error(), "actual_asset_type") == "binary");
}

FGL_TEST(esp32_capability_contract_rejects_host_only_scene_shapes_before_export) {
    auto manifest = baseManifest();
    fabgl::Scene scene("Too Many",
                       fabgl::SceneGuid::fromStableName("tests.esp32.capability.capacity.scene"));
    for (std::size_t index = 0U; index <= fabgl::project::Esp32RuntimeMaximumEntities; ++index)
        FGL_CHECK(scene.createEntity("Entity " + std::to_string(index)));
    auto capacity = fabgl::project::validateEsp32TargetCapabilities(manifest, scene);
    FGL_CHECK(!capacity);
    FGL_CHECK(capacity.error().code() == fabgl::ErrorCode::CapacityExceeded);
    FGL_CHECK(errorContext(capacity.error(), "feature") == "scene_entities");

    fabgl::Scene hierarchy(
        "Hierarchy", fabgl::SceneGuid::fromStableName("tests.esp32.capability.hierarchy.scene"));
    auto parent = hierarchy.createEntity("Parent");
    auto child = hierarchy.createEntity("Child");
    FGL_CHECK(parent && child && hierarchy.setParent(child.value()->id(), parent.value()->id()));
    auto unsupportedHierarchy =
        fabgl::project::validateEsp32TargetCapabilities(manifest, hierarchy);
    FGL_CHECK(!unsupportedHierarchy);
    FGL_CHECK(errorContext(unsupportedHierarchy.error(), "feature") == "scene_hierarchy");
}
