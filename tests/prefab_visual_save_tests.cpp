#include "test_harness.h"

#include "fabgl/prefab/prefab.h"
#include "fabgl/save/save_system.h"
#include "fabgl/visual/visual_graph.h"

#include <chrono>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>

using namespace fabgl;

namespace {

namespace test_filesystem = std::filesystem;

struct TemporaryDirectory final {
    test_filesystem::path path;

    ~TemporaryDirectory() {
        std::error_code ignored;
        test_filesystem::remove_all(path, ignored);
    }
};

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

    PrefabAsset base{baseId, "Base", std::nullopt, {}, {}};
    base.components.emplace(healthId, health);
    PrefabAsset derived{derivedId, "Derived", baseId, {}, {}};
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
    PrefabAsset asset{id, "Apply", std::nullopt, {}, {}};
    asset.components.emplace(healthId, health);
    PrefabInstance instance(id);
    FGL_CHECK(instance.setPropertyOverride(healthId, "current", PropertyValue(std::int64_t{25})));
    FGL_CHECK(instance.applyTo(asset));
    FGL_CHECK(std::get<std::int64_t>(asset.components[healthId].properties["current"]) == 25);
    FGL_CHECK(instance.propertyOverrideCount() == 0);

    const auto firstId = AssetGuid::fromStableName("prefab.cycle.first");
    const auto secondId = AssetGuid::fromStableName("prefab.cycle.second");
    PrefabLibrary cycles;
    FGL_CHECK(cycles.add({firstId, "First", secondId, {}, {}}));
    FGL_CHECK(cycles.add({secondId, "Second", firstId, {}, {}}));
    auto cycle = cycles.validateDependencies();
    FGL_CHECK(!cycle && cycle.error().code() == ErrorCode::CycleDetected);

    PrefabLibrary missing;
    FGL_CHECK(missing.add({firstId, "First", secondId, {}, {}}));
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

FGL_TEST(save_system_parses_portable_integer_headers_strictly) {
    auto storage = std::make_shared<MemorySaveStorage>();
    SaveSystem saves(storage, 1);
    FGL_CHECK(saves.save("portable", "payload"));
    const auto stored = storage->read("portable");
    FGL_CHECK(stored);

    const auto replaceField = [](std::string text, std::string_view from, std::string_view to) {
        const auto position = text.find(from);
        FGL_CHECK(position != std::string::npos);
        text.replace(position, from.size(), to);
        return text;
    };

    auto whitespace = replaceField(stored.value(), "schema 1\n", "schema \t1 \r\n");
    whitespace = replaceField(std::move(whitespace), "checksum ", "checksum 0x");
    FGL_CHECK(storage->writeAtomically("portable", std::move(whitespace)));
    FGL_CHECK(saves.load("portable"));

    auto negative = replaceField(stored.value(), "schema 1\n", "schema -1\n");
    FGL_CHECK(storage->writeAtomically("portable", std::move(negative)));
    FGL_CHECK(!saves.load("portable"));

    auto trailing = replaceField(stored.value(), "size 7\n", "size 7bytes\n");
    FGL_CHECK(storage->writeAtomically("portable", std::move(trailing)));
    FGL_CHECK(!saves.load("portable"));

    auto overflow = replaceField(stored.value(), "schema 1\n", "schema 18446744073709551616\n");
    FGL_CHECK(storage->writeAtomically("portable", std::move(overflow)));
    FGL_CHECK(!saves.load("portable"));
}

FGL_TEST(save_system_round_trips_canonical_typed_gameplay_state) {
    SaveDocument document;
    document.primitives.emplace("completed", true);
    document.primitives.emplace("lives", std::int64_t{3});
    document.primitives.emplace("difficulty", 1.25);
    document.primitives.emplace("chapter", std::string("citadel"));
    document.player.emplace("position2d", Vec2{12.5F, -3.0F});
    document.player.emplace("position3d", Vec3{1.0F, 2.0F, 3.0F});
    document.scene.emplace("door.open", true);
    document.entities["enemy.guard.01"].emplace("health", std::int64_t{75});
    document.entities["enemy.guard.01"].emplace("alert", 0.5);

    const auto first = SaveSystem::serializeDocument(document);
    const auto second = SaveSystem::serializeDocument(document);
    FGL_CHECK(first && second && first.value() == second.value());
    auto decoded = SaveSystem::deserializeDocument(first.value());
    FGL_CHECK(decoded);
    FGL_CHECK(std::get<bool>(decoded.value().primitives.at("completed")));
    FGL_CHECK(std::get<std::int64_t>(decoded.value().primitives.at("lives")) == 3);
    FGL_CHECK(std::get<std::string>(decoded.value().primitives.at("chapter")) == "citadel");
    FGL_CHECK_NEAR(std::get<Vec2>(decoded.value().player.at("position2d")).x, 12.5F,
                   0.0001F);
    FGL_CHECK(std::get<std::int64_t>(
                  decoded.value().entities.at("enemy.guard.01").at("health")) == 75);

    auto storage = std::make_shared<MemorySaveStorage>();
    SaveSystem versionOne(storage, 1U);
    FGL_CHECK(versionOne.saveDocument("campaign", document));
    SaveSystem versionTwo(storage, 2U);
    FGL_CHECK(versionTwo.registerMigration(1U, [](std::string_view payload) {
        return Result<std::string>::success(std::string(payload));
    }));
    auto loaded = versionTwo.loadDocument("campaign");
    FGL_CHECK(loaded && loaded.value().migrated);
    FGL_CHECK(loaded.value().storedSchemaVersion == 1U);
    FGL_CHECK(std::get<bool>(loaded.value().document.scene.at("door.open")));

    auto trailing = first.value() + "x";
    FGL_CHECK(!SaveSystem::deserializeDocument(trailing));
    document.primitives["invalid.number"] = std::numeric_limits<double>::infinity();
    FGL_CHECK(!SaveSystem::serializeDocument(document));
    document.primitives.erase("invalid.number");
    document.primitives[std::string("bad\nkey")] = true;
    FGL_CHECK(!SaveSystem::serializeDocument(document));
}

FGL_TEST(file_save_storage_persists_slots_atomically_and_rotates_backups) {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory temporary{
        test_filesystem::temp_directory_path() /
        (std::string("fabgl-studio-save-tests-") + std::to_string(suffix))};
    auto storage = std::make_shared<FileSaveStorage>(temporary.path.string(), 2U);
    SaveSystem saves(storage, 1U);

    FGL_CHECK(saves.save("campaign", "v1"));
    FGL_CHECK(saves.save("campaign", "v2"));
    FGL_CHECK(saves.save("campaign", "v3"));
    FGL_CHECK(saves.slots() == std::vector<std::string>{"campaign"});
    FGL_CHECK(saves.load("campaign").value().payload == "v3");

    const auto newestBackup = storage->readBackup("campaign", 1U);
    const auto oldestBackup = storage->readBackup("campaign", 2U);
    FGL_CHECK(newestBackup && newestBackup.value().find("\n\nv2") != std::string::npos);
    FGL_CHECK(oldestBackup && oldestBackup.value().find("\n\nv1") != std::string::npos);
    FGL_CHECK(!storage->readBackup("campaign", 0U));
    FGL_CHECK(!storage->writeAtomically("../unsafe", "bad"));

    SaveSystem reopened(std::make_shared<FileSaveStorage>(temporary.path.string(), 2U), 1U);
    FGL_CHECK(reopened.load("campaign").value().payload == "v3");
    FGL_CHECK(reopened.remove("campaign"));
    FGL_CHECK(reopened.slots().empty());
    FGL_CHECK(!storage->readBackup("campaign", 1U));
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
