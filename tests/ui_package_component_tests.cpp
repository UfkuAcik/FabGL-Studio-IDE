#include "test_harness.h"

#include "fabgl/packages/package_manifest.h"
#include "fabgl/scene/builtin_components.h"
#include "fabgl/scene/scene.h"
#include "fabgl/ui/ui_layout.h"

#include <cstdint>
#include <string>

using namespace fabgl;

FGL_TEST(ui_anchor_layout_computes_nested_rectangles) {
    UIModel ui;
    UILayoutProperties rootProperties;
    rootProperties.anchors = {{0.0F, 0.0F}, {1.0F, 1.0F}};
    rootProperties.minimumOffset = {};
    rootProperties.maximumOffset = {};
    auto root = ui.addElement(std::nullopt, rootProperties);
    FGL_CHECK(root);

    UILayoutProperties childProperties;
    childProperties.anchors = {{0.5F, 0.0F}, {1.0F, 0.5F}};
    childProperties.minimumOffset = {10.0F, 5.0F};
    childProperties.maximumOffset = {-10.0F, -5.0F};
    auto child = ui.addElement(root.value(), childProperties);
    FGL_CHECK(child);
    FGL_CHECK(ui.layout({0.0F, 0.0F, 200.0F, 100.0F}));
    const auto* rootElement = ui.find(root.value());
    const auto* childElement = ui.find(child.value());
    FGL_CHECK(rootElement != nullptr && childElement != nullptr);
    FGL_CHECK(rootElement->computedRect == Rect{0.0F, 0.0F, 200.0F, 100.0F});
    FGL_CHECK(childElement->computedRect == Rect{110.0F, 5.0F, 80.0F, 40.0F});
}

FGL_TEST(ui_focus_navigation_skips_disabled_elements_and_parenting_prevents_cycles) {
    UIModel ui;
    UILayoutProperties panelProperties;
    panelProperties.anchors = {{0.0F, 0.0F}, {1.0F, 1.0F}};
    panelProperties.maximumOffset = {};
    auto panel = ui.addElement(std::nullopt, panelProperties);
    FGL_CHECK(panel);
    UILayoutProperties buttonProperties;
    buttonProperties.focusable = true;
    auto first = ui.addElement(panel.value(), buttonProperties);
    auto second = ui.addElement(panel.value(), buttonProperties);
    auto third = ui.addElement(panel.value(), buttonProperties);
    FGL_CHECK(first && second && third);
    auto disabled = buttonProperties;
    disabled.enabled = false;
    FGL_CHECK(ui.setProperties(second.value(), disabled));

    FGL_CHECK(ui.focusNext() == first.value());
    FGL_CHECK(ui.focusNext() == third.value());
    FGL_CHECK(ui.focusNext() == first.value());
    FGL_CHECK(ui.focusNext(true) == third.value());
    FGL_CHECK(!ui.setFocus(second.value()));
    FGL_CHECK(!ui.reparent(panel.value(), first.value()));
    FGL_CHECK(ui.removeElement(third.value()));
    FGL_CHECK(!ui.focused());
}

FGL_TEST(semantic_versions_and_ranges_follow_compatible_release_rules) {
    auto release = SemVersion::parse("1.2.3");
    auto prerelease = SemVersion::parse("1.2.3-beta.2");
    auto earlierPrerelease = SemVersion::parse("1.2.3-beta.1");
    FGL_CHECK(release && prerelease && earlierPrerelease);
    FGL_CHECK(prerelease.value() < release.value());
    FGL_CHECK(earlierPrerelease.value() < prerelease.value());
    FGL_CHECK(release.value().toString() == "1.2.3");
    auto compatible = VersionRequirement::parse("^1.2.0");
    FGL_CHECK(compatible);
    FGL_CHECK(compatible.value().matches(release.value()));
    FGL_CHECK(!compatible.value().matches(SemVersion::parse("2.0.0").value()));
    auto zeroCompatible = VersionRequirement::parse("^0.2.1");
    FGL_CHECK(zeroCompatible.value().matches(SemVersion::parse("0.2.9").value()));
    FGL_CHECK(!zeroCompatible.value().matches(SemVersion::parse("0.3.0").value()));
    FGL_CHECK(!SemVersion::parse("1.2"));
    FGL_CHECK(!SemVersion::parse("01.2.3"));
    FGL_CHECK(!SemVersion::parse("1.2.3-beta.01"));
}

FGL_TEST(package_manifest_parsing_dependency_order_and_trust_policy_are_enforced) {
    auto core = PackageManifestParser::parse(
        "name=core.runtime\nversion=1.4.0\npath=packages/core\ntrust=trusted\nexecutable=true\n");
    auto game = PackageManifestParser::parse(
        "name=game.pack\nversion=2.0.0\npath=packages/game\ntrust=untrusted\nexecutable=true\n"
        "dependency=core.runtime@^1.2.0\n");
    FGL_CHECK(core && game);
    PackageRegistry registry;
    FGL_CHECK(registry.add(core.value()));
    FGL_CHECK(registry.add(game.value()));
    auto blocked = registry.validate(false);
    FGL_CHECK(!blocked && blocked.error().code() == ErrorCode::InvalidState);
    auto allowed = registry.validate(true);
    FGL_CHECK(allowed);
    FGL_CHECK(allowed.value().size() == 2);
    FGL_CHECK(allowed.value()[0] == "core.runtime");
    FGL_CHECK(allowed.value()[1] == "game.pack");
    FGL_CHECK(!PackageManifestParser::parse(
        "name=unsafe\nversion=1.0.0\npath=../outside\ntrust=trusted\n"));
}

FGL_TEST(package_registry_detects_missing_versions_and_dependency_cycles) {
    auto version100 = SemVersion::parse("1.0.0").value();
    auto version200 = SemVersion::parse("2.0.0").value();
    auto any = VersionRequirement::parse("*").value();
    auto exact200 = VersionRequirement::parse("2.0.0").value();

    PackageRegistry mismatch;
    FGL_CHECK(mismatch.add({"base", version100, "base", PackageTrust::Trusted, false, {}}));
    FGL_CHECK(mismatch.add(
        {"app", version100, "app", PackageTrust::Trusted, false, {{"base", exact200}}}));
    auto mismatchResult = mismatch.validate(false);
    FGL_CHECK(!mismatchResult && mismatchResult.error().code() == ErrorCode::UnsupportedVersion);

    PackageRegistry cycle;
    FGL_CHECK(
        cycle.add({"first", version100, "first", PackageTrust::Trusted, false, {{"second", any}}}));
    FGL_CHECK(cycle.add(
        {"second", version200, "second", PackageTrust::Trusted, false, {{"first", any}}}));
    auto cycleResult = cycle.validate(false);
    FGL_CHECK(!cycleResult && cycleResult.error().code() == ErrorCode::CycleDetected);
}

FGL_TEST(builtin_component_registry_contains_all_required_types_and_live_data_components) {
    ReflectionRegistry registry;
    FGL_CHECK(registerBuiltinComponentTypes(registry));
    FGL_CHECK(registry.size() == builtinComponentNames().size());
    FGL_CHECK(builtinComponentNames().size() == 28U);
    for (const auto& name : builtinComponentNames()) {
        FGL_CHECK(registry.find(std::string("fabgl.") + name) != nullptr);
    }
    const auto* transform = registry.find("fabgl.Transform");
    FGL_CHECK(transform != nullptr && transform->findProperty("localPosition") != nullptr);

    auto health = createBuiltinDataComponent(registry, "Health");
    FGL_CHECK(health);
    auto current = health.value()->get("current");
    FGL_CHECK(current && std::get<std::int64_t>(current.value()) == 100);
    FGL_CHECK(health.value()->set("current", PropertyValue(std::int64_t{25})));
    FGL_CHECK(!health.value()->set("current", PropertyValue(std::string("wrong"))));
    FGL_CHECK(!createBuiltinDataComponent(registry, "Transform"));
}

FGL_TEST(data_components_attach_to_entities_and_reflection_accessors_update_values) {
    ReflectionRegistry registry;
    FGL_CHECK(registerBuiltinComponentTypes(registry));
    const auto* healthMetadata = registry.find("fabgl.Health");
    FGL_CHECK(healthMetadata != nullptr);
    Scene scene;
    auto entity = scene.createEntity("Player");
    FGL_CHECK(entity);
    auto health = entity.value()->addComponent<DataComponent>(*healthMetadata);
    FGL_CHECK(health);
    const auto* currentProperty = health.value()->metadata()->findProperty("current");
    FGL_CHECK(currentProperty != nullptr);
    FGL_CHECK(currentProperty->write(health.value(), PropertyValue(std::int64_t{75})));
    auto value = currentProperty->read(health.value());
    FGL_CHECK(value && std::get<std::int64_t>(value.value()) == 75);
    FGL_CHECK(!entity.value()->addComponent<DataComponent>(*healthMetadata));
}
