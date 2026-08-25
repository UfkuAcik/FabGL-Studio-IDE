#include "test_harness.h"

#include "fabgl/scene/scene.h"
#include "fabgl/scripting/script_component.h"
#include "fabgl/scripting/script_module.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

using namespace fabgl;

namespace {

class PlayerMover final : public scripting::ScriptComponent {
  public:
    PlayerMover() : ScriptComponent(metadataDefinition()) {}

    float speed = 3.5F;
    std::int32_t hitPoints = 100;
    std::string label = "Player";
    int createCount = 0;
    int updateCount = 0;

    static TypeMetadata metadataDefinition() {
        auto metadata = scripting::makeScriptMetadata("sample.PlayerMover", "Player Mover");
        metadata.properties.push_back(
            scripting::scriptProperty("speed", &PlayerMover::speed, 3.5F, "Movement"));
        metadata.properties.push_back(
            scripting::scriptProperty("hitPoints", &PlayerMover::hitPoints, std::int32_t{100}));
        metadata.properties.push_back(
            scripting::scriptProperty("label", &PlayerMover::label, std::string("Player")));
        return metadata;
    }

  protected:
    void onCreate() override {
        ++createCount;
    }
    void onUpdate(float) override {
        ++updateCount;
    }
};

} // namespace

FABGL_REGISTER_SCRIPT(PlayerMover)

FGL_TEST(gameplay_script_properties_are_reflected_and_type_checked) {
    PlayerMover script;
    FGL_CHECK(script.apiCompatible());
    FGL_CHECK(script.requiredApiVersion() == scripting::CurrentApiVersion);
    FGL_CHECK(script.typeName() == "sample.PlayerMover");
    const auto* speed = script.metadata()->findProperty("speed");
    const auto* health = script.metadata()->findProperty("hitPoints");
    FGL_CHECK(speed != nullptr && health != nullptr);
    FGL_CHECK(speed->write(&script, PropertyValue(8.25)));
    FGL_CHECK_NEAR(script.speed, 8.25F, 0.0001F);
    FGL_CHECK(health->write(&script, PropertyValue(std::int64_t{72})));
    FGL_CHECK(script.hitPoints == 72);
    FGL_CHECK(!speed->write(&script, PropertyValue(std::string("fast"))));
    FGL_CHECK(!health->write(&script, PropertyValue(std::numeric_limits<std::int64_t>::max())));
}

FGL_TEST(gameplay_scripts_participate_in_scene_lifecycle) {
    Scene scene("Script lifecycle");
    auto entity = scene.createEntity("Player");
    FGL_CHECK(entity);
    auto script = entity.value()->addComponent<PlayerMover>();
    FGL_CHECK(script);
    FGL_CHECK(script.value()->createCount == 1);
    FGL_CHECK(scene.start());
    FGL_CHECK(scene.update(1.0F / 60.0F));
    FGL_CHECK(script.value()->updateCount == 1);
}

FGL_TEST(gameplay_api_version_rejects_incompatible_requests) {
    FGL_CHECK(scripting::isApiCompatible({0, 1, 9}));
    FGL_CHECK(!scripting::isApiCompatible({0, 2, 0}));
    FGL_CHECK(!scripting::isApiCompatible({1, 0, 0}));
}

FGL_TEST(gameplay_script_module_exports_registered_factories_with_v1_abi) {
    scripting::ScriptModuleView view;
    FGL_CHECK(scripting::detail::exportRegisteredScriptModule(&view));
    FGL_CHECK(view.abiVersion == scripting::ScriptModuleAbiVersion);
    FGL_CHECK(view.descriptorCount >= 1U);
    const auto* descriptor = std::find_if(
        view.descriptors, view.descriptors + view.descriptorCount,
        [](const auto& candidate) { return std::string_view(candidate.typeName) == "sample.PlayerMover"; });
    FGL_CHECK(descriptor != view.descriptors + view.descriptorCount);
    FGL_CHECK(descriptor->structureSize == sizeof(scripting::ScriptModuleDescriptor));
    std::unique_ptr<Component> instance(descriptor->create());
    FGL_CHECK(instance != nullptr);
    FGL_CHECK(instance->typeId() == descriptor->typeId);
    FGL_CHECK(instance->typeName() == "sample.PlayerMover");
}
