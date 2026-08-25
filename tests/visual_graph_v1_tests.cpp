#include "test_harness.h"

#include "fabgl/visual/visual_graph.h"

#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace fabgl;

namespace {

VisualNode typedNode(const VisualBuiltinNodeType type, const VisualNodeId id,
                     std::string name) {
    auto created = VisualNodeRegistry::builtins().create(type, id, std::move(name));
    FGL_CHECK(created);
    return std::move(created).value();
}

void addNode(VisualGraph& graph, VisualNode node) {
    FGL_CHECK(graph.addNode(std::move(node)));
}

void addEdge(VisualGraph& graph, const VisualNodeId sourceNode, const VisualPinId sourcePin,
             const VisualNodeId targetNode, const VisualPinId targetPin) {
    FGL_CHECK(graph.addEdge({sourceNode, sourcePin, targetNode, targetPin}));
}

VisualGraph executableTypedGraph() {
    VisualGraph graph;
    graph.setGuid(AssetGuid::fromStableName("test.visual.typed.v1"));
    graph.setName("Flow \"test\"\nGraph");

    auto entry = typedNode(VisualBuiltinNodeType::EventStart, 1U, "Start");
    entry.layout = {10.5F, 20.25F, 190.0F, 90.0F};
    addNode(graph, std::move(entry));
    addNode(graph, typedNode(VisualBuiltinNodeType::FlowSequence, 2U, "Sequence"));

    auto set = typedNode(VisualBuiltinNodeType::VariableSet, 3U, "Set score");
    set.variableName = "score";
    addNode(graph, std::move(set));
    addNode(graph, typedNode(VisualBuiltinNodeType::FlowBranch, 4U, "Score under limit?"));

    auto two = typedNode(VisualBuiltinNodeType::NumberConstant, 5U, "Two");
    two.numberValue = 2.0;
    addNode(graph, std::move(two));
    addNode(graph, typedNode(VisualBuiltinNodeType::MathAdd, 6U, "Add"));
    auto three = typedNode(VisualBuiltinNodeType::NumberConstant, 7U, "Three");
    three.numberValue = 3.0;
    addNode(graph, std::move(three));

    auto getScore = typedNode(VisualBuiltinNodeType::VariableGet, 8U, "Get score");
    getScore.variableName = "score";
    addNode(graph, std::move(getScore));
    auto ten = typedNode(VisualBuiltinNodeType::NumberConstant, 9U, "Ten");
    ten.numberValue = 10.0;
    addNode(graph, std::move(ten));
    addNode(graph, typedNode(VisualBuiltinNodeType::CompareLess, 10U, "Less"));
    addNode(graph, typedNode(VisualBuiltinNodeType::FlowReturn, 11U, "Return score"));
    addNode(graph, typedNode(VisualBuiltinNodeType::FlowReturn, 12U, "Return fallback"));
    auto zero = typedNode(VisualBuiltinNodeType::NumberConstant, 13U, "Zero");
    zero.numberValue = 0.0;
    addNode(graph, std::move(zero));

    graph.setEntryNode(1U);
    addEdge(graph, 1U, 1U, 2U, 1U);
    addEdge(graph, 2U, 2U, 3U, 1U);
    addEdge(graph, 2U, 3U, 4U, 1U);
    addEdge(graph, 5U, 1U, 6U, 1U);
    addEdge(graph, 7U, 1U, 6U, 2U);
    addEdge(graph, 6U, 3U, 3U, 2U);
    addEdge(graph, 8U, 1U, 10U, 1U);
    addEdge(graph, 9U, 1U, 10U, 2U);
    addEdge(graph, 10U, 3U, 4U, 2U);
    addEdge(graph, 4U, 3U, 11U, 1U);
    addEdge(graph, 8U, 1U, 11U, 2U);
    addEdge(graph, 4U, 4U, 12U, 1U);
    addEdge(graph, 13U, 1U, 12U, 2U);

    FGL_CHECK(graph.addCommentBox({2U, "Later", {400.0F, 10.0F, 220.0F, 140.0F}}));
    FGL_CHECK(graph.addCommentBox({1U, "Flow\ncontrol", {0.0F, 0.0F, 350.0F, 180.0F}}));
    return graph;
}

VisualGraph hostGraph() {
    VisualGraph graph;
    graph.setGuid(AssetGuid::fromStableName("test.visual.host.v1"));
    graph.setName("Host callback");
    addNode(graph, typedNode(VisualBuiltinNodeType::EventStart, 1U, "Start"));
    auto call = typedNode(VisualBuiltinNodeType::FunctionCall, 2U, "Capture");
    call.callbackName = "test.capture";
    call.callbackPayload = "safe payload";
    addNode(graph, std::move(call));
    addNode(graph, typedNode(VisualBuiltinNodeType::FlowReturn, 3U, "Return"));
    auto value = typedNode(VisualBuiltinNodeType::NumberConstant, 4U, "Seven");
    value.numberValue = 7.0;
    addNode(graph, std::move(value));
    graph.setEntryNode(1U);
    addEdge(graph, 1U, 1U, 2U, 1U);
    addEdge(graph, 4U, 1U, 2U, 2U);
    addEdge(graph, 2U, 3U, 3U, 1U);
    addEdge(graph, 4U, 1U, 3U, 2U);
    return graph;
}

VisualGraph delayedHostGraph() {
    VisualGraph graph;
    graph.setGuid(AssetGuid::fromStableName("test.visual.delay.v1"));
    graph.setName("Latent delay");
    addNode(graph, typedNode(VisualBuiltinNodeType::EventStart, 1U, "Start"));
    addNode(graph, typedNode(VisualBuiltinNodeType::FlowDelay, 2U, "Wait"));
    auto seconds = typedNode(VisualBuiltinNodeType::NumberConstant, 3U, "Seconds");
    seconds.numberValue = 1.5;
    addNode(graph, std::move(seconds));
    auto call = typedNode(VisualBuiltinNodeType::FunctionCall, 4U, "Capture");
    call.callbackName = "test.capture";
    addNode(graph, std::move(call));
    auto value = typedNode(VisualBuiltinNodeType::NumberConstant, 5U, "Seven");
    value.numberValue = 7.0;
    addNode(graph, std::move(value));
    addNode(graph, typedNode(VisualBuiltinNodeType::FlowReturn, 6U, "Return"));
    graph.setEntryNode(1U);
    addEdge(graph, 1U, 1U, 2U, 1U);
    addEdge(graph, 3U, 1U, 2U, 2U);
    addEdge(graph, 2U, 3U, 4U, 1U);
    addEdge(graph, 5U, 1U, 4U, 2U);
    addEdge(graph, 4U, 3U, 6U, 1U);
    addEdge(graph, 5U, 1U, 6U, 2U);
    return graph;
}

VisualGraph referenceGraph() {
    VisualGraph graph;
    graph.setGuid(AssetGuid::fromStableName("test.visual.references.v1"));
    graph.setName("Reference round trip");
    addNode(graph, typedNode(VisualBuiltinNodeType::EventStart, 1U, "Start"));

    auto entity = typedNode(VisualBuiltinNodeType::EntityAction, 2U, "Entity");
    entity.entityReference = EntityGuid::fromStableName("test.entity");
    addNode(graph, std::move(entity));
    auto component = typedNode(VisualBuiltinNodeType::ComponentAction, 3U, "Component");
    component.componentReference = ComponentTypeGuid::fromStableName("test.component");
    addNode(graph, std::move(component));
    auto audio = typedNode(VisualBuiltinNodeType::AudioPlay, 4U, "Audio");
    audio.assetReference = AssetGuid::fromStableName("test.audio");
    addNode(graph, std::move(audio));
    auto value = typedNode(VisualBuiltinNodeType::NumberConstant, 5U, "One");
    value.numberValue = 1.0;
    addNode(graph, std::move(value));

    graph.setEntryNode(1U);
    addEdge(graph, 1U, 1U, 2U, 1U);
    addEdge(graph, 5U, 1U, 2U, 2U);
    addEdge(graph, 2U, 3U, 3U, 1U);
    addEdge(graph, 5U, 1U, 3U, 2U);
    addEdge(graph, 3U, 3U, 4U, 1U);
    addEdge(graph, 5U, 1U, 4U, 2U);
    return graph;
}

std::string replaceFirst(std::string text, const std::string_view from,
                         const std::string_view to) {
    const auto position = text.find(from);
    FGL_CHECK(position != std::string::npos);
    text.replace(position, from.size(), to);
    return text;
}

bool hasIssue(const VisualValidationReport& report, const VisualIssueCode code) {
    for (const auto& issue : report.issues) {
        if (issue.code == code)
            return true;
    }
    return false;
}

} // namespace

FGL_TEST(visual_node_registry_exposes_all_v1_categories_and_required_pin_metadata) {
    const auto& definitions = VisualNodeRegistry::builtins().definitions();
    std::set<VisualNodeCategory> categories;
    for (const auto& definition : definitions)
        categories.insert(definition.category);

    const std::vector<VisualNodeCategory> required{
        VisualNodeCategory::Event,     VisualNodeCategory::Branch,
        VisualNodeCategory::Sequence,  VisualNodeCategory::Delay,
        VisualNodeCategory::Variable,  VisualNodeCategory::Function,
        VisualNodeCategory::Entity,    VisualNodeCategory::Component,
        VisualNodeCategory::Vector,    VisualNodeCategory::Input,
        VisualNodeCategory::Collision, VisualNodeCategory::Audio,
        VisualNodeCategory::Animation, VisualNodeCategory::Scene,
        VisualNodeCategory::UI,
    };
    for (const auto category : required)
        FGL_CHECK(categories.contains(category));

    const auto* branch = VisualNodeRegistry::builtins().find("flow.branch");
    FGL_CHECK(branch != nullptr);
    FGL_CHECK(branch->execution == VisualExecutionKind::Branch);
    FGL_CHECK(branch->pins.size() == 4U);
    for (const auto& pin : branch->pins)
        FGL_CHECK(pin.connectionRequired);

    auto missing = typedNode(VisualBuiltinNodeType::EventStart, 1U, "Start");
    VisualGraph graph;
    graph.setGuid(AssetGuid::fromStableName("test.visual.missing-pin"));
    graph.setName("Missing pin");
    addNode(graph, std::move(missing));
    graph.setEntryNode(1U);
    const auto report = VisualGraphValidator::validate(graph);
    FGL_CHECK(report.hasErrors());
    FGL_CHECK(hasIssue(report, VisualIssueCode::MissingConnection));
    FGL_CHECK(!serializeVisualGraph(graph));

    auto multipleFlow = executableTypedGraph();
    addNode(multipleFlow,
            typedNode(VisualBuiltinNodeType::FlowReturn, 14U, "Ambiguous return"));
    auto extraValue = typedNode(VisualBuiltinNodeType::NumberConstant, 15U, "Extra value");
    extraValue.numberValue = 1.0;
    addNode(multipleFlow, std::move(extraValue));
    addEdge(multipleFlow, 1U, 1U, 14U, 1U);
    addEdge(multipleFlow, 15U, 1U, 14U, 2U);
    const auto multipleReport = VisualGraphValidator::validate(multipleFlow);
    FGL_CHECK(multipleReport.hasErrors());
    FGL_CHECK(hasIssue(multipleReport, VisualIssueCode::MultipleFlowOutputConnections));
}

FGL_TEST(visual_graph_v1_round_trip_is_canonical_and_preserves_copyable_model_metadata) {
    const auto graph = executableTypedGraph();
    const auto copy = graph;
    auto first = serializeVisualGraph(graph);
    auto copied = serializeVisualGraph(copy);
    FGL_CHECK(first && copied);
    FGL_CHECK(first.value() == copied.value());
    FGL_CHECK(first.value().find("fglvisual 1\n") == 0U);
    FGL_CHECK(first.value().find("id 1\ntitle") < first.value().find("id 2\ntitle"));

    auto loaded = deserializeVisualGraph(first.value());
    FGL_CHECK(loaded);
    auto second = serializeVisualGraph(loaded.value());
    FGL_CHECK(second);
    FGL_CHECK(second.value() == first.value());
    FGL_CHECK(loaded.value().guid() == graph.guid());
    FGL_CHECK(loaded.value().name() == graph.name());
    FGL_CHECK(loaded.value().comments().size() == 2U);
    FGL_CHECK(loaded.value().findNode(1U) != nullptr);
    FGL_CHECK_NEAR(loaded.value().findNode(1U)->layout.x, 10.5F, 0.0001F);
    FGL_CHECK(loaded.value().edges().size() == graph.edges().size());
}

FGL_TEST(visual_typed_flow_branch_sequence_variables_and_math_execute_with_bounds) {
    const auto graph = executableTypedGraph();
    const auto report = VisualGraphValidator::validate(graph);
    FGL_CHECK(!report.hasErrors());
    auto bytecode = VisualGraphCompiler::compile(graph);
    FGL_CHECK(bytecode);
    FGL_CHECK(bytecode.value().code.size() <= VisualMaximumBytecodeBytes);
    FGL_CHECK(bytecode.value().instructionCount <= VisualMaximumCompiledInstructions);

    VisualBytecodeVm vm;
    auto result = vm.execute(bytecode.value());
    FGL_CHECK(result);
    FGL_CHECK_NEAR(result.value().returnValue, 5.0, 0.0001F);
    FGL_CHECK_NEAR(result.value().variables.at("score"), 5.0, 0.0001F);

    VisualCompileLimits tight;
    tight.maximumInstructions = 3U;
    auto compileLimited = VisualGraphCompiler::compile(graph, {}, tight);
    FGL_CHECK(!compileLimited);
    FGL_CHECK(compileLimited.error().code() == ErrorCode::CapacityExceeded);
    auto runtimeLimited = vm.execute(bytecode.value(), {}, 2U);
    FGL_CHECK(!runtimeLimited);
    FGL_CHECK(runtimeLimited.error().code() == ErrorCode::CapacityExceeded);

    VisualCompileLimits aboveHardLimit;
    aboveHardLimit.maximumNodes = VisualMaximumGraphNodes + 1U;
    auto unsafeLimits = VisualGraphCompiler::compile(graph, {}, aboveHardLimit);
    FGL_CHECK(!unsafeLimits);
    FGL_CHECK(unsafeLimits.error().code() == ErrorCode::CapacityExceeded);

    VisualBytecode unknown;
    unknown.code = {255U};
    unknown.instructionCount = 1U;
    auto unknownResult = vm.execute(unknown);
    FGL_CHECK(!unknownResult);
    FGL_CHECK(unknownResult.error().code() == ErrorCode::InvalidFormat);

    VisualBytecode oversized;
    oversized.code.resize(VisualMaximumBytecodeBytes + 1U);
    auto oversizedResult = vm.execute(oversized);
    FGL_CHECK(!oversizedResult);
    FGL_CHECK(oversizedResult.error().code() == ErrorCode::CapacityExceeded);

    VisualBytecode nonFinite;
    nonFinite.constants = {std::numeric_limits<double>::infinity()};
    nonFinite.code = {static_cast<std::uint8_t>(VisualOpcode::PushConstant), 0U, 0U,
                      static_cast<std::uint8_t>(VisualOpcode::Return)};
    nonFinite.instructionCount = 2U;
    FGL_CHECK(!vm.execute(nonFinite));
}

FGL_TEST(visual_host_callbacks_require_an_explicit_validated_dispatch_table) {
    const auto graph = hostGraph();
    auto missing = VisualGraphCompiler::compile(graph);
    FGL_CHECK(!missing);

    VisualHostCallbackTable wrongSignature;
    FGL_CHECK(wrongSignature.add(
        "test.capture", std::uint8_t{0},
        [](const VisualHostCallDescriptor&, const std::vector<double>&) {
            return Result<double>::success(0.0);
        }));
    auto wrong = VisualGraphCompiler::compile(graph, {}, {}, VisualNodeRegistry::builtins(),
                                              &wrongSignature);
    FGL_CHECK(!wrong);

    std::size_t calls = 0U;
    double argument = 0.0;
    std::string payload;
    VisualHostCallbackTable callbacks;
    FGL_CHECK(callbacks.add(
        "test.capture", std::uint8_t{1},
        [&calls, &argument, &payload](const VisualHostCallDescriptor& descriptor,
                                     const std::vector<double>& arguments) {
            ++calls;
            argument = arguments.at(0U);
            payload = descriptor.payload;
            return Result<double>::success(99.0);
        }));
    auto bytecode = VisualGraphCompiler::compile(graph, {}, {}, VisualNodeRegistry::builtins(),
                                                 &callbacks);
    FGL_CHECK(bytecode);
    FGL_CHECK(bytecode.value().hostCalls.size() == 1U);

    VisualBytecodeVm vm;
    auto unavailable = vm.execute(bytecode.value());
    FGL_CHECK(!unavailable);
    auto result = vm.execute(bytecode.value(), {}, VisualMaximumCompiledInstructions, &callbacks);
    FGL_CHECK(result);
    FGL_CHECK_NEAR(result.value().returnValue, 7.0, 0.0001F);
    FGL_CHECK(calls == 1U);
    FGL_CHECK_NEAR(argument, 7.0, 0.0001F);
    FGL_CHECK(payload == "safe payload");

    VisualHostCallbackTable nonFiniteCallback;
    FGL_CHECK(nonFiniteCallback.add(
        "test.capture", std::uint8_t{1},
        [](const VisualHostCallDescriptor&, const std::vector<double>&) {
            return Result<double>::success(std::numeric_limits<double>::infinity());
        }));
    FGL_CHECK(!vm.execute(bytecode.value(), {}, VisualMaximumCompiledInstructions,
                          &nonFiniteCallback));
}

FGL_TEST(visual_delay_suspends_and_resumes_without_blocking_the_host_thread) {
    std::size_t synchronousDelayCalls = 0U;
    std::size_t captureCalls = 0U;
    VisualHostCallbackTable callbacks;
    FGL_CHECK(callbacks.add(
        "time.delay", std::uint8_t{1},
        [&synchronousDelayCalls](const VisualHostCallDescriptor&,
                                 const std::vector<double>&) {
            ++synchronousDelayCalls;
            return Result<double>::success(0.0);
        }));
    FGL_CHECK(callbacks.add(
        "test.capture", std::uint8_t{1},
        [&captureCalls](const VisualHostCallDescriptor&,
                        const std::vector<double>& arguments) {
            ++captureCalls;
            return Result<double>::success(arguments.at(0U));
        }));
    auto bytecode = VisualGraphCompiler::compile(delayedHostGraph(), {}, {},
                                                 VisualNodeRegistry::builtins(), &callbacks);
    FGL_CHECK(bytecode);

    VisualBytecodeVm vm;
    auto suspended = vm.execute(bytecode.value(), {}, VisualMaximumCompiledInstructions,
                                &callbacks);
    FGL_CHECK(suspended && !suspended.value().completed);
    FGL_CHECK_NEAR(suspended.value().delaySeconds, 1.5, 0.0001F);
    FGL_CHECK(suspended.value().continuation.has_value());
    FGL_CHECK(synchronousDelayCalls == 0U);
    FGL_CHECK(captureCalls == 0U);

    auto resumed = vm.resume(bytecode.value(), std::move(*suspended.value().continuation),
                             std::move(suspended.value().variables),
                             VisualMaximumCompiledInstructions, &callbacks);
    FGL_CHECK(resumed && resumed.value().completed);
    FGL_CHECK_NEAR(resumed.value().returnValue, 7.0, 0.0001F);
    FGL_CHECK(captureCalls == 1U);
    FGL_CHECK(synchronousDelayCalls == 0U);

    VisualVmContinuation corrupt;
    corrupt.instructionPointer = bytecode.value().code.size() + 1U;
    FGL_CHECK(!vm.resume(bytecode.value(), std::move(corrupt), {},
                         VisualMaximumCompiledInstructions, &callbacks));
}

FGL_TEST(visual_graph_v1_rejects_unknown_version_type_schema_corruption_and_trailing_data) {
    auto serialized = serializeVisualGraph(executableTypedGraph());
    FGL_CHECK(serialized);

    const auto version = replaceFirst(serialized.value(), "fglvisual 1", "fglvisual 2");
    auto unsupported = deserializeVisualGraph(version);
    FGL_CHECK(!unsupported);
    FGL_CHECK(unsupported.error().code() == ErrorCode::UnsupportedVersion);

    const auto unknownType =
        replaceFirst(serialized.value(), "type \"math.add\"", "type \"math.unknown\"");
    auto unknown = deserializeVisualGraph(unknownType);
    FGL_CHECK(!unknown);
    FGL_CHECK(unknown.error().code() == ErrorCode::NotFound);

    const auto wrongPin = replaceFirst(serialized.value(), "pin 3 \"value\" number output",
                                       "pin 3 \"value\" boolean output");
    auto typeMismatch = deserializeVisualGraph(wrongPin);
    FGL_CHECK(!typeMismatch);
    FGL_CHECK(typeMismatch.error().code() == ErrorCode::InvalidFormat);

    const auto missingNode =
        replaceFirst(serialized.value(), "edge 1 1 2 1", "edge 1 1 99 1");
    FGL_CHECK(!deserializeVisualGraph(missingNode));
    FGL_CHECK(!deserializeVisualGraph(serialized.value() + "trailing\n"));
    FGL_CHECK(!deserializeVisualGraph("fglvisual 1\n"));

    VisualGraphFormatLimits sourceLimit;
    sourceLimit.maximumSourceBytes = 8U;
    auto bounded = deserializeVisualGraph(serialized.value(), VisualNodeRegistry::builtins(),
                                          sourceLimit);
    FGL_CHECK(!bounded);
    FGL_CHECK(bounded.error().code() == ErrorCode::CapacityExceeded);
}

FGL_TEST(visual_graph_v1_round_trips_references_and_reports_cycle_or_missing_targets) {
    const auto graph = referenceGraph();
    auto serialized = serializeVisualGraph(graph);
    FGL_CHECK(serialized);
    auto loaded = deserializeVisualGraph(serialized.value());
    FGL_CHECK(loaded);
    FGL_CHECK(loaded.value().findNode(2U)->entityReference ==
              graph.findNode(2U)->entityReference);
    FGL_CHECK(loaded.value().findNode(3U)->componentReference ==
              graph.findNode(3U)->componentReference);
    FGL_CHECK(loaded.value().findNode(4U)->assetReference == graph.findNode(4U)->assetReference);

    const VisualReferenceResolver missingReferences{
        [](AssetGuid) { return false; }, [](EntityGuid) { return false; },
        [](ComponentTypeGuid) { return false; }};
    const auto referenceReport = VisualGraphValidator::validate(loaded.value(), missingReferences);
    FGL_CHECK(referenceReport.hasErrors());
    FGL_CHECK(hasIssue(referenceReport, VisualIssueCode::MissingReference));

    auto cycle = executableTypedGraph();
    addEdge(cycle, 6U, 3U, 6U, 1U);
    const auto cycleReport = VisualGraphValidator::validate(cycle);
    FGL_CHECK(cycleReport.hasErrors());
    FGL_CHECK(hasIssue(cycleReport, VisualIssueCode::Cycle));
    FGL_CHECK(!VisualGraphCompiler::compile(cycle));
    FGL_CHECK(!serializeVisualGraph(cycle));
}
