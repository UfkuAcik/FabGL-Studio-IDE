#include "test_harness.h"

#include "fabgl/prefab/prefab.h"
#include "fabgl/save/save_system.h"
#include "fabgl/visual/visual_graph.h"

#include <memory>
#include <string>

using namespace fabgl;

namespace {

PrefabComponentData componentData(std::string_view name) {
    PrefabComponentData component;
    component.typeId = ComponentTypeGuid::fromStableName(std::string("test.") + std::string(name));
    component.typeName = std::string(name);
    return component;
}

VisualPin pin(VisualPinId id, const char* name, VisualValueType type,
              VisualPinDirection direction) {
    return {id, name, type, direction};
}

VisualGraph arithmeticGraph() {
    VisualGraph graph;
    VisualNode entry;
    entry.id = 1;
    entry.kind = VisualNodeKind::Entry;
    entry.name = "Entry";
    entry.pins = {pin(1, "flow", VisualValueType::Flow, VisualPinDirection::Output)};
    FGL_CHECK(graph.addNode(std::move(entry)));

    VisualNode two;
    two.id = 2;
    two.kind = VisualNodeKind::ConstantNumber;
    two.name = "Two";
    two.numberValue = 2.0;
    two.pins = {pin(1, "value", VisualValueType::Number, VisualPinDirection::Output)};
    FGL_CHECK(graph.addNode(std::move(two)));

    VisualNode three;
    three.id = 3;
    three.kind = VisualNodeKind::ConstantNumber;
    three.name = "Three";
    three.numberValue = 3.0;
    three.pins = {pin(1, "value", VisualValueType::Number, VisualPinDirection::Output)};
    FGL_CHECK(graph.addNode(std::move(three)));

    VisualNode add;
    add.id = 4;
    add.kind = VisualNodeKind::Add;
    add.name = "Add";
    add.pins = {pin(1, "a", VisualValueType::Number, VisualPinDirection::Input),
                pin(2, "b", VisualValueType::Number, VisualPinDirection::Input),
                pin(3, "value", VisualValueType::Number, VisualPinDirection::Output)};
    FGL_CHECK(graph.addNode(std::move(add)));

    VisualNode result;
    result.id = 5;
    result.kind = VisualNodeKind::Return;
    result.name = "Return";
    result.pins = {pin(1, "flow", VisualValueType::Flow, VisualPinDirection::Input),
                   pin(2, "value", VisualValueType::Number, VisualPinDirection::Input)};
    FGL_CHECK(graph.addNode(std::move(result)));
    graph.setEntryNode(1);
    FGL_CHECK(graph.addEdge({1, 1, 5, 1}));
    FGL_CHECK(graph.addEdge({2, 1, 4, 1}));
    FGL_CHECK(graph.addEdge({3, 1, 4, 2}));
    FGL_CHECK(graph.addEdge({4, 3, 5, 2}));
    return graph;
}

} // namespace

FGL_TEST(prefab_nested_resolution_and_instance_overrides_are_deterministic) {
    const auto baseId = AssetGuid::fromStableName("prefab.base");
    const auto derivedId = AssetGuid::fromStableName("prefab.derived");
    auto health = componentData("Health");
    const auto healthId = health.typeId;
    health.properties["current"] = std::int64_t{100};
    auto sprite = componentData("SpriteRenderer");
    const auto spriteId = sprite.typeId;
    auto light = componentData("Light");
    const auto lightId = light.typeId;

    PrefabAsset base{baseId, "Base", std::nullopt, {}};
    base.components.emplace(healthId, health);
    PrefabAsset derived{derivedId, "Derived", baseId, {}};
    auto healthOverride = health;
    healthOverride.properties["current"] = std::int64_t{80};
    derived.components.emplace(healthId, healthOverride);
    derived.components.emplace(spriteId, sprite);

    PrefabLibrary library;
    FGL_CHECK(library.add(base));
    FGL_CHECK(library.add(derived));
    FGL_CHECK(library.validateDependencies());
    auto resolvedAsset = library.resolve(derivedId);
    FGL_CHECK(resolvedAsset);
    FGL_CHECK(std::get<std::int64_t>(resolvedAsset.value()[healthId].properties["current"]) == 80);

    PrefabInstance instance(derivedId);
    FGL_CHECK(instance.setPropertyOverride(healthId, "current", PropertyValue(std::int64_t{50})));
    FGL_CHECK(instance.addComponentOverride(light));
    instance.removeComponentOverride(spriteId);
    auto resolved = instance.resolve(library);
    FGL_CHECK(resolved);
    FGL_CHECK(resolved.value().find(spriteId) == resolved.value().end());
    FGL_CHECK(resolved.value().find(lightId) != resolved.value().end());
    FGL_CHECK(std::get<std::int64_t>(resolved.value()[healthId].properties["current"]) == 50);
    FGL_CHECK(instance.revertProperty(healthId, "current"));
    resolved = instance.resolve(library);
    FGL_CHECK(resolved);
    FGL_CHECK(std::get<std::int64_t>(resolved.value()[healthId].properties["current"]) == 80);
}

FGL_TEST(prefab_apply_revert_and_dependency_cycle_validation) {
    const auto id = AssetGuid::fromStableName("prefab.apply");
    auto health = componentData("Health");
    const auto healthId = health.typeId;
    health.properties["current"] = std::int64_t{100};
    PrefabAsset asset{id, "Apply", std::nullopt, {}};
    asset.components.emplace(healthId, health);
    PrefabInstance instance(id);
    FGL_CHECK(instance.setPropertyOverride(healthId, "current", PropertyValue(std::int64_t{25})));
    FGL_CHECK(instance.applyTo(asset));
    FGL_CHECK(std::get<std::int64_t>(asset.components[healthId].properties["current"]) == 25);
    FGL_CHECK(instance.propertyOverrideCount() == 0);

    const auto firstId = AssetGuid::fromStableName("prefab.cycle.first");
    const auto secondId = AssetGuid::fromStableName("prefab.cycle.second");
    PrefabLibrary cycles;
    FGL_CHECK(cycles.add({firstId, "First", secondId, {}}));
    FGL_CHECK(cycles.add({secondId, "Second", firstId, {}}));
    auto cycle = cycles.validateDependencies();
    FGL_CHECK(!cycle && cycle.error().code() == ErrorCode::CycleDetected);

    PrefabLibrary missing;
    FGL_CHECK(missing.add({firstId, "First", secondId, {}}));
    auto missingResult = missing.validateDependencies();
    FGL_CHECK(!missingResult && missingResult.error().code() == ErrorCode::NotFound);
}

FGL_TEST(visual_graph_validates_compiles_and_executes_compact_bytecode) {
    const auto graph = arithmeticGraph();
    const auto report = VisualGraphValidator::validate(graph);
    FGL_CHECK(!report.hasErrors());
    FGL_CHECK(report.issues.empty());
    auto bytecode = VisualGraphCompiler::compile(graph);
    FGL_CHECK(bytecode);
    FGL_CHECK(bytecode.value().code.size() < 16U);
    VisualBytecodeVm vm;
    auto result = vm.execute(bytecode.value());
    FGL_CHECK(result);
    FGL_CHECK_NEAR(result.value().returnValue, 5.0, 0.0001F);
    FGL_CHECK(result.value().executedInstructions == 4U);
}

FGL_TEST(visual_graph_reports_type_cycle_unreachable_and_missing_reference_errors) {
    auto graph = arithmeticGraph();
    VisualNode dangling;
    dangling.id = 6;
    dangling.kind = VisualNodeKind::AssetReference;
    dangling.name = "Missing Asset";
    dangling.assetReference = AssetGuid::fromStableName("missing.asset");
    dangling.pins = {pin(1, "value", VisualValueType::Number, VisualPinDirection::Output)};
    FGL_CHECK(graph.addNode(std::move(dangling)));
    FGL_CHECK(graph.addEdge({4, 3, 4, 1}));
    const VisualReferenceResolver resolver{[](AssetGuid) { return false; },
                                           [](EntityGuid) { return true; }};
    const auto report = VisualGraphValidator::validate(graph, resolver);
    FGL_CHECK(report.hasErrors());
    bool cycle = false;
    bool unreachable = false;
    bool missing = false;
    bool multiple = false;
    for (const auto& issue : report.issues) {
        cycle = cycle || issue.code == VisualIssueCode::Cycle;
        unreachable = unreachable || issue.code == VisualIssueCode::UnreachableNode;
        missing = missing || issue.code == VisualIssueCode::MissingReference;
        multiple = multiple || issue.code == VisualIssueCode::MultipleInputConnections;
    }
    FGL_CHECK(cycle && unreachable && missing && multiple);
    FGL_CHECK(!VisualGraphCompiler::compile(graph, resolver));
}

FGL_TEST(save_system_checksums_multiple_slots_and_binary_payloads) {
    auto storage = std::make_shared<MemorySaveStorage>();
    SaveSystem saves(storage, 1);
    const std::string binary("abc\0def", 7);
    FGL_CHECK(saves.save("slot_1", binary));
    FGL_CHECK(saves.save("slot-2", "second"));
    FGL_CHECK(saves.slots().size() == 2);
    auto loaded = saves.load("slot_1");
    FGL_CHECK(loaded);
    FGL_CHECK(loaded.value().payload == binary);
    FGL_CHECK(!loaded.value().migrated);
    FGL_CHECK(!saves.save("../unsafe", "bad"));

    auto raw = storage->read("slot_1");
    FGL_CHECK(raw);
    raw.value().back() = raw.value().back() == 'x' ? 'y' : 'x';
    FGL_CHECK(storage->writeAtomically("slot_1", raw.value()));
    auto corrupt = saves.load("slot_1");
    FGL_CHECK(!corrupt && corrupt.error().code() == ErrorCode::InvalidFormat);
    FGL_CHECK(saves.remove("slot-2"));
    FGL_CHECK(!saves.load("slot-2"));
}

FGL_TEST(save_system_applies_sequential_schema_migrations_and_reports_gaps) {
    auto storage = std::make_shared<MemorySaveStorage>();
    SaveSystem oldRuntime(storage, 1);
    FGL_CHECK(oldRuntime.save("campaign", "v1"));

    SaveSystem current(storage, 3);
    FGL_CHECK(current.registerMigration(1, [](std::string_view payload) {
        return Result<std::string>::success(std::string(payload) + "->v2");
    }));
    FGL_CHECK(current.registerMigration(2, [](std::string_view payload) {
        return Result<std::string>::success(std::string(payload) + "->v3");
    }));
    auto loaded = current.load("campaign");
    FGL_CHECK(loaded);
    FGL_CHECK(loaded.value().storedSchemaVersion == 1);
    FGL_CHECK(loaded.value().currentSchemaVersion == 3);
    FGL_CHECK(loaded.value().migrated);
    FGL_CHECK(loaded.value().payload == "v1->v2->v3");

    SaveSystem missingMigration(storage, 3);
    auto missing = missingMigration.load("campaign");
    FGL_CHECK(!missing && missing.error().code() == ErrorCode::UnsupportedVersion);
}
