#include "test_harness.h"

#include "fabgl/prefab/prefab.h"
#include "fabgl/scene/builtin_components.h"
#include "fabgl/scene/entity.h"
#include "fabgl/scene/scene.h"
#include "fabgl/serialization/prefab_instance_serializer.h"
#include "fabgl/serialization/prefab_serializer.h"
#include "fabgl/serialization/scene_serializer.h"

#include <cstdint>
#include <limits>
#include <string>

using namespace fabgl;

namespace {

PrefabComponentData makeComponent(std::string name) {
    PrefabComponentData component;
    component.typeId = ComponentTypeGuid::fromStableName("prefab.test." + name);
    component.typeName = std::move(name);
    return component;
}

} // namespace

FGL_TEST(prefab_v2_serialization_round_trips_hierarchy_and_all_property_values) {
    auto rootComponent = makeComponent("RootState");
    rootComponent.properties["active"] = true;
    rootComponent.properties["signed"] = std::int64_t{-42};
    rootComponent.properties["unsigned"] = std::uint64_t{42};
    rootComponent.properties["float"] = 3.25;
    rootComponent.properties["fixed"] = Fixed::fromRaw(-1234);
    rootComponent.properties["text"] = std::string("line one\n\"line two\"\\tail");
    rootComponent.properties["vec2"] = Vec2{1.0F, -2.0F};
    rootComponent.properties["vec3"] = Vec3{1.0F, 2.0F, 3.0F};
    rootComponent.properties["euler"] = EulerAngles{0.1F, 0.2F, 0.3F};
    rootComponent.properties["quaternion"] = Quaternion{0.0F, 0.0F, 0.707F, 0.707F};
    rootComponent.properties["rect"] = Rect{1.0F, 2.0F, 3.0F, 4.0F};
    rootComponent.properties["color"] = Color{1U, 2U, 3U, 4U};
    rootComponent.properties["asset"] = AssetGuid::fromStableName("prefab.asset.reference");
    rootComponent.properties["entity"] = EntityGuid::fromStableName("prefab.entity.reference");
    rootComponent.properties["component"] =
        ComponentReference{EntityGuid::fromStableName("prefab.component.entity"),
                           ComponentTypeGuid::fromStableName("prefab.component.type")};
    rootComponent.properties["list"] = PropertyList{
        PropertyType::String, {std::string("first"), std::string("second with spaces")}};
    rootComponent.properties["curve"] = Curve{{CurvePoint{0.0, 1.0}, CurvePoint{2.0, -1.0}}};
    rootComponent.properties["animation_curve"] = PropertyAnimationCurve{
        {AnimationCurveKey{0.0, 0.0, 0.0, 0.5}, AnimationCurveKey{1.0, 1.0, -0.5, 0.0}}};
    rootComponent.properties["action"] = ActionReference{"Jump"};
    rootComponent.properties["event"] = EventReference{"Finished"};

    const auto rootEntityId = EntityGuid::fromStableName("prefab.hierarchy.root");
    const auto childEntityId = EntityGuid::fromStableName("prefab.hierarchy.child");
    auto childComponent = makeComponent("ChildState");
    childComponent.properties["label"] = std::string("Çocuk");

    PrefabAsset prefab{
        AssetGuid::fromStableName("prefab.serialization"),
        "Serialized Prefab",
        std::nullopt,
        {{rootComponent.typeId, rootComponent}},
        {{childEntityId, "Child", false, rootEntityId, {{childComponent.typeId, childComponent}}},
         {rootEntityId, "Root", true, std::nullopt, {}}}};

    auto first = PrefabSerializer::serialize(prefab);
    FGL_CHECK(first);
    auto loaded = PrefabSerializer::deserialize(first.value());
    FGL_CHECK(loaded);
    FGL_CHECK(loaded.value().id == prefab.id);
    FGL_CHECK(loaded.value().entities.size() == 2U);
    FGL_CHECK(loaded.value().components.size() == 1U);
    FGL_CHECK(std::get<std::string>(
                  loaded.value().components.at(rootComponent.typeId).properties.at("text")) ==
              "line one\n\"line two\"\\tail");
    FGL_CHECK(std::get<PropertyList>(
                  loaded.value().components.at(rootComponent.typeId).properties.at("list")) ==
              std::get<PropertyList>(rootComponent.properties.at("list")));
    FGL_CHECK(
        std::get<PropertyAnimationCurve>(
            loaded.value().components.at(rootComponent.typeId).properties.at("animation_curve")) ==
        std::get<PropertyAnimationCurve>(rootComponent.properties.at("animation_curve")));
    auto second = PrefabSerializer::serialize(loaded.value());
    FGL_CHECK(second);
    FGL_CHECK(first.value() == second.value());
}

FGL_TEST(prefab_v1_is_migrated_to_canonical_v2) {
    const auto asset = AssetGuid::fromStableName("prefab.legacy");
    const auto component = ComponentTypeGuid::fromStableName("prefab.legacy.component");
    const std::string legacy = "fglprefab 1\n"
                               "asset_guid " +
                               asset.toString() +
                               "\nname \"Legacy\"\n"
                               "nested_base nil\n"
                               "component_count 1\n"
                               "component_begin\n"
                               "type_id " +
                               component.toString() +
                               "\ntype_name \"LegacyComponent\"\n"
                               "property_count 1\n"
                               "property \"health\" sint 100\n"
                               "component_end\n"
                               "prefab_end\n";
    auto migrated = PrefabSerializer::deserialize(legacy);
    FGL_CHECK(migrated);
    FGL_CHECK(migrated.value().entities.empty());
    auto canonical = PrefabSerializer::serialize(migrated.value());
    FGL_CHECK(canonical);
    FGL_CHECK(canonical.value().find("fglprefab 2\n") == 0U);
    FGL_CHECK(canonical.value().find("entity_count 0\n") != std::string::npos);
}

FGL_TEST(prefab_hierarchy_nested_dependencies_and_unpack_are_validated) {
    const auto baseId = AssetGuid::fromStableName("prefab.hierarchy.base");
    const auto derivedId = AssetGuid::fromStableName("prefab.hierarchy.derived");
    const auto rootId = EntityGuid::fromStableName("prefab.hierarchy.base.root");
    const auto childId = EntityGuid::fromStableName("prefab.hierarchy.base.child");
    auto health = makeComponent("Health");
    health.properties["value"] = std::int64_t{100};

    PrefabAsset base{baseId,
                     "Base",
                     std::nullopt,
                     {{health.typeId, health}},
                     {{rootId, "Root", true, std::nullopt, {}}}};
    PrefabAsset derived{derivedId, "Derived", baseId, {}, {{childId, "Child", true, rootId, {}}}};
    PrefabLibrary library;
    FGL_CHECK(library.add(base));
    FGL_CHECK(library.add(derived));
    FGL_CHECK(library.validateDependencies());
    auto dependencies = library.dependencies(derivedId);
    FGL_CHECK(dependencies && dependencies.value().size() == 1U);
    FGL_CHECK(dependencies.value().front() == baseId);

    PrefabInstance instance(derivedId);
    FGL_CHECK(instance.setPropertyOverride(health.typeId, "value", std::int64_t{25}));
    auto unpacked = instance.unpack(library);
    FGL_CHECK(unpacked);
    FGL_CHECK(unpacked.value().entities.size() == 2U);
    FGL_CHECK(std::get<std::int64_t>(
                  unpacked.value().components.at(health.typeId).properties.at("value")) == 25);
    FGL_CHECK(instance.unpacked());
    FGL_CHECK(instance.prefab().isNil());
    FGL_CHECK(!instance.resolve(library));
}

FGL_TEST(prefab_parser_rejects_cycles_duplicates_non_finite_and_trailing_data) {
    const auto asset = AssetGuid::fromStableName("prefab.invalid");
    const auto first = EntityGuid::fromStableName("prefab.invalid.first");
    const auto second = EntityGuid::fromStableName("prefab.invalid.second");
    PrefabAsset cyclic{asset,
                       "Cyclic",
                       std::nullopt,
                       {},
                       {{first, "First", true, second, {}}, {second, "Second", true, first, {}}}};
    auto cycle = PrefabSerializer::serialize(cyclic);
    FGL_CHECK(!cycle && cycle.error().code() == ErrorCode::CycleDetected);

    auto invalidValue = makeComponent("Invalid");
    invalidValue.properties["value"] = std::numeric_limits<double>::infinity();
    PrefabAsset nonFinite{
        asset, "NonFinite", std::nullopt, {{invalidValue.typeId, invalidValue}}, {}};
    auto nonFiniteResult = PrefabSerializer::serialize(nonFinite);
    FGL_CHECK(!nonFiniteResult && nonFiniteResult.error().code() == ErrorCode::SerializationFailed);

    const std::string duplicate =
        "fglprefab 2\nasset_guid " + asset.toString() +
        "\nname \"Duplicate\"\nnested_base nil\nroot_component_count 0\nentity_count 2\n"
        "entity_begin\nentity_guid " +
        first.toString() +
        "\nname \"One\"\nactive 1\nparent nil\ncomponent_count 0\nentity_end\n"
        "entity_begin\nentity_guid " +
        first.toString() +
        "\nname \"Two\"\nactive 1\nparent nil\ncomponent_count 0\nentity_end\n"
        "prefab_end\n";
    auto duplicateResult = PrefabSerializer::deserialize(duplicate);
    FGL_CHECK(!duplicateResult && duplicateResult.error().code() == ErrorCode::AlreadyExists);

    auto valid = PrefabSerializer::serialize(
        {AssetGuid::fromStableName("prefab.trailing"), "Trailing", std::nullopt, {}, {}});
    FGL_CHECK(valid);
    auto trailing = PrefabSerializer::deserialize(valid.value() + "unexpected\n");
    FGL_CHECK(!trailing && trailing.error().code() == ErrorCode::InvalidFormat);
}

FGL_TEST(prefab_instance_link_codec_is_canonical_bounded_and_preserves_all_override_state) {
    const auto prefab = AssetGuid::fromStableName("prefab.instance.persisted");
    const auto propertyType = ComponentTypeGuid::fromStableName("prefab.instance.property");
    const auto addedType = ComponentTypeGuid::fromStableName("prefab.instance.added");
    const auto removedType = ComponentTypeGuid::fromStableName("prefab.instance.removed");
    const auto sourceRoot = EntityGuid::fromStableName("prefab.instance.source.root");
    const auto sourceChild = EntityGuid::fromStableName("prefab.instance.source.child");
    const auto sceneRoot = EntityGuid::fromStableName("prefab.instance.scene.root");
    const auto sceneChild = EntityGuid::fromStableName("prefab.instance.scene.child");

    PrefabSceneInstance instance(prefab);
    instance.root = sceneRoot;
    instance.entities = {sceneChild, sceneRoot};
    instance.sourceToScene = {{sourceChild, sceneChild}, {sourceRoot, sceneRoot}};
    FGL_CHECK(instance.state.setPropertyOverride(propertyType, "current", std::int64_t{25}));
    auto added = makeComponent("PersistedAdded");
    added.typeId = addedType;
    added.properties["label"] = std::string("persisted\nvalue");
    FGL_CHECK(instance.state.addComponentOverride(added));
    instance.state.removeComponentOverride(removedType);

    auto first = PrefabInstanceSerializer::serialize(instance);
    FGL_CHECK(first);
    FGL_CHECK(first.value().find("fglprefabinstance 1\n") == 0U);
    auto loaded = PrefabInstanceSerializer::deserialize(first.value());
    FGL_CHECK(loaded);
    FGL_CHECK(loaded.value().prefab == prefab);
    FGL_CHECK(loaded.value().root == sceneRoot);
    FGL_CHECK(loaded.value().entities.size() == 2U);
    FGL_CHECK(loaded.value().sourceToScene == instance.sourceToScene);
    const auto snapshot = loaded.value().state.snapshot();
    FGL_CHECK(snapshot.propertyOverrides == instance.state.snapshot().propertyOverrides);
    FGL_CHECK(snapshot.addedComponents.at(addedType).properties == added.properties);
    FGL_CHECK(snapshot.removedComponents.contains(removedType));
    auto second = PrefabInstanceSerializer::serialize(loaded.value());
    FGL_CHECK(second && second.value() == first.value());

    auto nonCanonical = first.value();
    const auto firstEntity = nonCanonical.find("entity ");
    const auto firstEnd = nonCanonical.find('\n', firstEntity);
    const auto secondEntity = nonCanonical.find("entity ", firstEnd);
    const auto secondEnd = nonCanonical.find('\n', secondEntity);
    FGL_CHECK(firstEntity != std::string::npos && firstEnd != std::string::npos &&
              secondEntity != std::string::npos && secondEnd != std::string::npos);
    const auto firstLine = nonCanonical.substr(firstEntity, firstEnd - firstEntity + 1U);
    const auto secondLine = nonCanonical.substr(secondEntity, secondEnd - secondEntity + 1U);
    nonCanonical.replace(secondEntity, secondLine.size(), firstLine);
    nonCanonical.replace(firstEntity, firstLine.size(), secondLine);
    auto rejectedOrder = PrefabInstanceSerializer::deserialize(nonCanonical);
    FGL_CHECK(!rejectedOrder && rejectedOrder.error().code() == ErrorCode::InvalidFormat);

    auto truncated = first.value().substr(0U, first.value().size() - 1U);
    FGL_CHECK(!PrefabInstanceSerializer::deserialize(truncated));
    FGL_CHECK(
        !PrefabInstanceSerializer::deserialize(std::string(MaximumPropertyStringLength + 1U, 'x')));
}

FGL_TEST(scene_v2_round_trips_the_internal_prefab_instance_link_component) {
    const auto prefab = AssetGuid::fromStableName("prefab.scene.roundtrip");
    const auto sourceRoot = EntityGuid::fromStableName("prefab.scene.source");
    const auto sceneRoot = EntityGuid::fromStableName("prefab.scene.root");
    Scene scene("Prefab Scene Roundtrip",
                SceneGuid::fromStableName("prefab.scene.roundtrip.scene"));
    auto entity = scene.createEntity("Baked Linked Instance", sceneRoot);
    FGL_CHECK(entity);

    PrefabSceneInstance link(prefab);
    link.root = sceneRoot;
    link.entities = {sceneRoot};
    link.sourceToScene = {{sourceRoot, sceneRoot}};
    FGL_CHECK(link.state.setPropertyOverride(
        ComponentTypeGuid::fromStableName("prefab.scene.component"), "speed", 1.25));
    auto encoded = PrefabInstanceSerializer::serialize(link);
    FGL_CHECK(encoded);

    ReflectionRegistry registry;
    FGL_CHECK(registerBuiltinComponentTypes(registry));
    auto component = createBuiltinDataComponent(registry, "PrefabInstanceLink");
    FGL_CHECK(component);
    FGL_CHECK(component.value()->set("state", encoded.value()));
    FGL_CHECK(entity.value()->addComponent(std::move(component.value())));

    auto sceneText = SceneSerializer::serialize(scene);
    FGL_CHECK(sceneText);
    FGL_CHECK(sceneText.value().find("type_name \"fabgl.PrefabInstanceLink\"") !=
              std::string::npos);
    auto reopened = SceneSerializer::deserialize(sceneText.value());
    FGL_CHECK(reopened);
    const auto linkType =
        ComponentTypeGuid::fromStableName("fabgl.component.PrefabInstanceLink.v1");
    const auto* persisted = reopened.value()->findEntity(sceneRoot)->getComponent(linkType);
    FGL_CHECK(persisted != nullptr && persisted->metadata() != nullptr);
    const auto state = persisted->metadata()->findProperty("state")->read(persisted);
    FGL_CHECK(state && std::holds_alternative<std::string>(state.value()));
    auto decoded = PrefabInstanceSerializer::deserialize(std::get<std::string>(state.value()));
    FGL_CHECK(decoded && decoded.value().root == sceneRoot &&
              decoded.value().state.propertyOverrideCount() == 1U);
}
