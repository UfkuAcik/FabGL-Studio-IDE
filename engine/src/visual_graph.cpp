#include "fabgl/visual/visual_graph.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string_view>
#include <utility>

namespace fabgl {
namespace {

VisualPinDefinition definitionPin(VisualPinId id, const char* name, VisualValueType type,
                                  VisualPinDirection direction, bool required) {
    return {id, name, type, direction, required};
}

std::vector<VisualNodeDefinition> builtinDefinitions() {
    using Category = VisualNodeCategory;
    using Direction = VisualPinDirection;
    using Execution = VisualExecutionKind;
    using Type = VisualBuiltinNodeType;
    using Value = VisualValueType;
    std::vector<VisualNodeDefinition> definitions;
    const auto add =
        [&definitions](Type type, const char* stableName, const char* displayName,
                       Category category, Execution execution,
                       std::optional<VisualNodeKind> legacyKind,
                       std::vector<VisualPinDefinition> pins) -> VisualNodeDefinition& {
        VisualNodeDefinition definition;
        definition.type = type;
        definition.stableName = stableName;
        definition.displayName = displayName;
        definition.category = category;
        definition.execution = execution;
        definition.legacyKind = legacyKind;
        definition.pins = std::move(pins);
        definitions.push_back(std::move(definition));
        return definitions.back();
    };
    add(Type::EventStart, "event.start", "Start", Category::Event, Execution::Entry,
        VisualNodeKind::Entry, {definitionPin(1, "flow", Value::Flow, Direction::Output, true)});
    add(Type::EventUpdate, "event.update", "Update", Category::Event, Execution::Entry,
        std::nullopt, {definitionPin(1, "flow", Value::Flow, Direction::Output, true)});
    add(Type::EventFixedUpdate, "event.fixed_update", "Fixed Update", Category::Event,
        Execution::Entry, std::nullopt,
        {definitionPin(1, "flow", Value::Flow, Direction::Output, true)});
    add(Type::EventLateUpdate, "event.late_update", "Late Update", Category::Event,
        Execution::Entry, std::nullopt,
        {definitionPin(1, "flow", Value::Flow, Direction::Output, true)});
    add(Type::FlowBranch, "flow.branch", "Branch", Category::Branch, Execution::Branch,
        std::nullopt,
        {definitionPin(1, "flow", Value::Flow, Direction::Input, true),
         definitionPin(2, "condition", Value::Boolean, Direction::Input, true),
         definitionPin(3, "true", Value::Flow, Direction::Output, true),
         definitionPin(4, "false", Value::Flow, Direction::Output, true)});
    add(Type::FlowSequence, "flow.sequence", "Sequence", Category::Sequence, Execution::Sequence,
        std::nullopt,
        {definitionPin(1, "flow", Value::Flow, Direction::Input, true),
         definitionPin(2, "then0", Value::Flow, Direction::Output, true),
         definitionPin(3, "then1", Value::Flow, Direction::Output, true)});
    auto& delay = add(Type::FlowDelay, "flow.delay", "Delay", Category::Delay, Execution::HostFlow,
                      std::nullopt,
                      {definitionPin(1, "flow", Value::Flow, Direction::Input, true),
                       definitionPin(2, "seconds", Value::Number, Direction::Input, true),
                       definitionPin(3, "then", Value::Flow, Direction::Output, true)});
    delay.hostCallback = "time.delay";
    delay.hostArgumentPins = {"seconds"};
    add(Type::VariableGet, "variable.get", "Get Variable", Category::Variable,
        Execution::GetVariable, VisualNodeKind::GetVariable,
        {definitionPin(1, "value", Value::Number, Direction::Output, false)});
    add(Type::VariableSet, "variable.set", "Set Variable", Category::Variable,
        Execution::SetVariable, std::nullopt,
        {definitionPin(1, "flow", Value::Flow, Direction::Input, true),
         definitionPin(2, "value", Value::Number, Direction::Input, true),
         definitionPin(3, "then", Value::Flow, Direction::Output, false)});
    auto& function = add(Type::FunctionCall, "function.call", "Call Function", Category::Function,
                         Execution::HostFlow, std::nullopt,
                         {definitionPin(1, "flow", Value::Flow, Direction::Input, true),
                          definitionPin(2, "argument", Value::Number, Direction::Input, true),
                          definitionPin(3, "then", Value::Flow, Direction::Output, false)});
    function.callbackNameRequired = true;
    function.hostArgumentPins = {"argument"};
    auto& entity = add(Type::EntityAction, "entity.action", "Entity Action", Category::Entity,
                       Execution::HostFlow, std::nullopt,
                       {definitionPin(1, "flow", Value::Flow, Direction::Input, true),
                        definitionPin(2, "argument", Value::Number, Direction::Input, true),
                        definitionPin(3, "then", Value::Flow, Direction::Output, false)});
    entity.hostCallback = "entity.action";
    entity.hostArgumentPins = {"argument"};
    entity.entityReferenceRequired = true;
    auto& component = add(Type::ComponentAction, "component.action", "Component Action",
                          Category::Component, Execution::HostFlow, std::nullopt,
                          {definitionPin(1, "flow", Value::Flow, Direction::Input, true),
                           definitionPin(2, "argument", Value::Number, Direction::Input, true),
                           definitionPin(3, "then", Value::Flow, Direction::Output, false)});
    component.hostCallback = "component.action";
    component.hostArgumentPins = {"argument"};
    component.componentReferenceRequired = true;
    auto& vector = add(Type::VectorLength3, "vector.length3", "Vector Length 3", Category::Vector,
                       Execution::HostValue, std::nullopt,
                       {definitionPin(1, "x", Value::Number, Direction::Input, true),
                        definitionPin(2, "y", Value::Number, Direction::Input, true),
                        definitionPin(3, "z", Value::Number, Direction::Input, true),
                        definitionPin(4, "value", Value::Number, Direction::Output, false)});
    vector.hostCallback = "vector.length3";
    vector.hostArgumentPins = {"x", "y", "z"};
    auto& input = add(Type::InputAction, "input.action", "Input Action", Category::Input,
                      Execution::HostValue, std::nullopt,
                      {definitionPin(1, "value", Value::Number, Direction::Output, false)});
    input.hostCallback = "input.action";
    add(Type::CollisionEvent, "event.collision", "Collision", Category::Collision, Execution::Entry,
        std::nullopt, {definitionPin(1, "flow", Value::Flow, Direction::Output, true)});
    add(Type::EventCollisionStay, "event.collision_stay", "Collision Stay", Category::Collision,
        Execution::Entry, std::nullopt,
        {definitionPin(1, "flow", Value::Flow, Direction::Output, true)});
    add(Type::EventCollisionExit, "event.collision_exit", "Collision Exit", Category::Collision,
        Execution::Entry, std::nullopt,
        {definitionPin(1, "flow", Value::Flow, Direction::Output, true)});
    add(Type::EventTriggerEnter, "event.trigger_enter", "Trigger Enter", Category::Collision,
        Execution::Entry, std::nullopt,
        {definitionPin(1, "flow", Value::Flow, Direction::Output, true)});
    add(Type::EventTriggerExit, "event.trigger_exit", "Trigger Exit", Category::Collision,
        Execution::Entry, std::nullopt,
        {definitionPin(1, "flow", Value::Flow, Direction::Output, true)});
    auto& audio = add(Type::AudioPlay, "audio.play", "Play Audio", Category::Audio,
                      Execution::HostFlow, std::nullopt,
                      {definitionPin(1, "flow", Value::Flow, Direction::Input, true),
                       definitionPin(2, "volume", Value::Number, Direction::Input, true),
                       definitionPin(3, "then", Value::Flow, Direction::Output, false)});
    audio.hostCallback = "audio.play";
    audio.hostArgumentPins = {"volume"};
    audio.assetReferenceRequired = true;
    auto& animation = add(Type::AnimationPlay, "animation.play", "Play Animation",
                          Category::Animation, Execution::HostFlow, std::nullopt,
                          {definitionPin(1, "flow", Value::Flow, Direction::Input, true),
                           definitionPin(2, "speed", Value::Number, Direction::Input, true),
                           definitionPin(3, "then", Value::Flow, Direction::Output, false)});
    animation.hostCallback = "animation.play";
    animation.hostArgumentPins = {"speed"};
    animation.assetReferenceRequired = true;
    auto& scene = add(Type::SceneLoad, "scene.load", "Load Scene", Category::Scene,
                      Execution::HostFlow, std::nullopt,
                      {definitionPin(1, "flow", Value::Flow, Direction::Input, true),
                       definitionPin(2, "then", Value::Flow, Direction::Output, false)});
    scene.hostCallback = "scene.load";
    scene.assetReferenceRequired = true;
    auto& ui = add(Type::UiAction, "ui.action", "UI Action", Category::UI, Execution::HostFlow,
                   std::nullopt,
                   {definitionPin(1, "flow", Value::Flow, Direction::Input, true),
                    definitionPin(2, "value", Value::Number, Direction::Input, true),
                    definitionPin(3, "then", Value::Flow, Direction::Output, false)});
    ui.hostCallback = "ui.action";
    ui.hostArgumentPins = {"value"};
    add(Type::NumberConstant, "value.number", "Number", Category::Math, Execution::ConstantNumber,
        VisualNodeKind::ConstantNumber,
        {definitionPin(1, "value", Value::Number, Direction::Output, false)});
    add(Type::BooleanConstant, "value.boolean", "Boolean", Category::Math,
        Execution::ConstantBoolean, VisualNodeKind::ConstantBoolean,
        {definitionPin(1, "value", Value::Boolean, Direction::Output, false)});
    add(Type::MathAdd, "math.add", "Add", Category::Math, Execution::Add, VisualNodeKind::Add,
        {definitionPin(1, "a", Value::Number, Direction::Input, true),
         definitionPin(2, "b", Value::Number, Direction::Input, true),
         definitionPin(3, "value", Value::Number, Direction::Output, false)});
    add(Type::MathMultiply, "math.multiply", "Multiply", Category::Math, Execution::Multiply,
        VisualNodeKind::Multiply,
        {definitionPin(1, "a", Value::Number, Direction::Input, true),
         definitionPin(2, "b", Value::Number, Direction::Input, true),
         definitionPin(3, "value", Value::Number, Direction::Output, false)});
    add(Type::CompareLess, "math.less", "Less", Category::Branch, Execution::Less,
        VisualNodeKind::Less,
        {definitionPin(1, "a", Value::Number, Direction::Input, true),
         definitionPin(2, "b", Value::Number, Direction::Input, true),
         definitionPin(3, "value", Value::Boolean, Direction::Output, false)});
    add(Type::FlowReturn, "flow.return", "Return", Category::Function, Execution::Return,
        VisualNodeKind::Return,
        {definitionPin(1, "flow", Value::Flow, Direction::Input, true),
         definitionPin(2, "value", Value::Number, Direction::Input, true)});
    add(Type::AssetReference, "reference.asset", "Asset Reference", Category::Reference,
        Execution::Unsupported, VisualNodeKind::AssetReference,
        {definitionPin(1, "value", Value::Number, Direction::Output, false)});
    definitions.back().assetReferenceRequired = true;
    add(Type::EntityReference, "reference.entity", "Entity Reference", Category::Reference,
        Execution::Unsupported, VisualNodeKind::EntityReference,
        {definitionPin(1, "value", Value::Number, Direction::Output, false)});
    definitions.back().entityReferenceRequired = true;
    return definitions;
}

const VisualNodeDefinition* definitionForNode(const VisualNode& node,
                                              const VisualNodeRegistry& registry) noexcept {
    return node.builtinType == VisualBuiltinNodeType::Legacy ? registry.findLegacy(node.kind)
                                                             : registry.find(node.builtinType);
}

bool safeCallbackName(std::string_view name) noexcept {
    if (name.empty() || name.size() > 128U)
        return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char character) {
        return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
               (character >= '0' && character <= '9') || character == '_' || character == '.' ||
               character == ':' || character == '-';
    });
}

const VisualPin* findNamedPin(const VisualNode& node, const char* name,
                              VisualPinDirection direction) noexcept {
    for (const auto& pin : node.pins) {
        if (pin.name == name && pin.direction == direction)
            return &pin;
    }
    return nullptr;
}

const VisualEdge* incomingEdge(const VisualGraph& graph, VisualNodeId node,
                               VisualPinId pin) noexcept {
    for (const auto& edge : graph.edges()) {
        if (edge.targetNode == node && edge.targetPin == pin)
            return &edge;
    }
    return nullptr;
}

void addIssue(VisualValidationReport& report, VisualIssueSeverity severity, VisualIssueCode code,
              VisualNodeId node, std::string message) {
    report.issues.push_back({severity, code, node, std::move(message)});
}

bool detectCycle(VisualNodeId node,
                 const std::map<VisualNodeId, std::vector<VisualNodeId>>& adjacency,
                 std::map<VisualNodeId, int>& marks) {
    if (marks[node] == 1)
        return true;
    if (marks[node] == 2)
        return false;
    marks[node] = 1;
    const auto outgoing = adjacency.find(node);
    if (outgoing != adjacency.end()) {
        for (const auto target : outgoing->second) {
            if (detectCycle(target, adjacency, marks))
                return true;
        }
    }
    marks[node] = 2;
    return false;
}

void markDataDependencies(const VisualGraph& graph, VisualNodeId node,
                          std::set<VisualNodeId>& reachable) {
    if (!reachable.insert(node).second)
        return;
    for (const auto& edge : graph.edges()) {
        if (edge.targetNode != node)
            continue;
        const auto* targetPin = graph.findPin(edge.targetNode, edge.targetPin);
        if (targetPin != nullptr && targetPin->type != VisualValueType::Flow) {
            markDataDependencies(graph, edge.sourceNode, reachable);
        }
    }
}

void emitOperand(std::vector<std::uint8_t>& code, std::uint16_t operand) {
    code.push_back(static_cast<std::uint8_t>(operand & 0xFFU));
    code.push_back(static_cast<std::uint8_t>((static_cast<std::uint32_t>(operand) >> 8U) & 0xFFU));
}

Result<std::uint16_t> addConstant(VisualBytecode& bytecode, double value) {
    if (bytecode.constants.size() >= 65535U) {
        return Result<std::uint16_t>::failure(
            Error(ErrorCode::CapacityExceeded, "too many bytecode constants"));
    }
    bytecode.constants.push_back(value);
    return Result<std::uint16_t>::success(
        static_cast<std::uint16_t>(bytecode.constants.size() - 1U));
}

Result<std::uint16_t> addVariable(VisualBytecode& bytecode, const std::string& name) {
    const auto iterator = std::find(bytecode.variables.begin(), bytecode.variables.end(), name);
    if (iterator != bytecode.variables.end()) {
        return Result<std::uint16_t>::success(
            static_cast<std::uint16_t>(std::distance(bytecode.variables.begin(), iterator)));
    }
    if (bytecode.variables.size() >= 65535U) {
        return Result<std::uint16_t>::failure(
            Error(ErrorCode::CapacityExceeded, "too many bytecode variables"));
    }
    bytecode.variables.push_back(name);
    return Result<std::uint16_t>::success(
        static_cast<std::uint16_t>(bytecode.variables.size() - 1U));
}

Result<void> compileValue(const VisualGraph& graph, const VisualNode& node,
                          VisualBytecode& bytecode, std::set<VisualNodeId>& compiling);

Result<void> compileInput(const VisualGraph& graph, const VisualNode& node, const char* pinName,
                          VisualBytecode& bytecode, std::set<VisualNodeId>& compiling) {
    const auto* pin = findNamedPin(node, pinName, VisualPinDirection::Input);
    if (pin == nullptr) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidFormat, "visual node is missing a required input pin")
                .addContext("pin", pinName));
    }
    const auto* edge = incomingEdge(graph, node.id, pin->id);
    if (edge == nullptr) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidFormat, "visual input pin is not connected")
                .addContext("pin", pinName));
    }
    const auto* source = graph.findNode(edge->sourceNode);
    if (source == nullptr) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidFormat, "visual input source is missing"));
    }
    return compileValue(graph, *source, bytecode, compiling);
}

Result<void> compileValue(const VisualGraph& graph, const VisualNode& node,
                          VisualBytecode& bytecode, std::set<VisualNodeId>& compiling) {
    if (!compiling.insert(node.id).second) {
        return Result<void>::failure(
            Error(ErrorCode::CycleDetected, "visual expression contains a cycle"));
    }
    Result<void> result = Result<void>::success();
    switch (node.kind) {
    case VisualNodeKind::ConstantNumber:
    case VisualNodeKind::ConstantBoolean: {
        const auto value = node.kind == VisualNodeKind::ConstantBoolean
                               ? (node.booleanValue ? 1.0 : 0.0)
                               : node.numberValue;
        auto constant = addConstant(bytecode, value);
        if (!constant)
            result = Result<void>::failure(constant.error());
        else {
            bytecode.code.push_back(static_cast<std::uint8_t>(VisualOpcode::PushConstant));
            emitOperand(bytecode.code, constant.value());
        }
        break;
    }
    case VisualNodeKind::GetVariable: {
        if (node.variableName.empty()) {
            result = Result<void>::failure(
                Error(ErrorCode::InvalidFormat, "variable node has an empty name"));
            break;
        }
        auto variable = addVariable(bytecode, node.variableName);
        if (!variable)
            result = Result<void>::failure(variable.error());
        else {
            bytecode.code.push_back(static_cast<std::uint8_t>(VisualOpcode::LoadVariable));
            emitOperand(bytecode.code, variable.value());
        }
        break;
    }
    case VisualNodeKind::Add:
    case VisualNodeKind::Multiply:
    case VisualNodeKind::Less: {
        result = compileInput(graph, node, "a", bytecode, compiling);
        if (result)
            result = compileInput(graph, node, "b", bytecode, compiling);
        if (result) {
            const auto opcode =
                node.kind == VisualNodeKind::Add
                    ? VisualOpcode::Add
                    : (node.kind == VisualNodeKind::Multiply ? VisualOpcode::Multiply
                                                             : VisualOpcode::Less);
            bytecode.code.push_back(static_cast<std::uint8_t>(opcode));
        }
        break;
    }
    default:
        result = Result<void>::failure(
            Error(ErrorCode::InvalidFormat, "visual node cannot produce a VM value"));
        break;
    }
    compiling.erase(node.id);
    return result;
}

struct TypedCompilerState final {
    const VisualGraph& graph;
    const VisualNodeRegistry& registry;
    const VisualHostCallbackTable* hostCallbacks = nullptr;
    const VisualCompileLimits& limits;
    VisualBytecode bytecode;
    std::set<VisualNodeId> compilingValues;
    std::set<VisualNodeId> compilingFlow;
    std::size_t returnCount = 0U;
};

Result<void> emitInstruction(TypedCompilerState& state, const VisualOpcode opcode) {
    if (state.bytecode.code.size() + 1U > state.limits.maximumBytecodeBytes ||
        state.bytecode.instructionCount >= state.limits.maximumInstructions) {
        return Result<void>::failure(
            Error(ErrorCode::CapacityExceeded, "visual bytecode instruction limit exceeded"));
    }
    state.bytecode.code.push_back(static_cast<std::uint8_t>(opcode));
    ++state.bytecode.instructionCount;
    return Result<void>::success();
}

Result<void> emitInstruction(TypedCompilerState& state, const VisualOpcode opcode,
                             const std::uint16_t operand) {
    if (state.bytecode.code.size() + 3U > state.limits.maximumBytecodeBytes) {
        return Result<void>::failure(
            Error(ErrorCode::CapacityExceeded, "visual bytecode byte limit exceeded"));
    }
    auto emitted = emitInstruction(state, opcode);
    if (!emitted)
        return emitted;
    emitOperand(state.bytecode.code, operand);
    return Result<void>::success();
}

Result<void> patchJump(VisualBytecode& bytecode, const std::size_t operandOffset,
                       const std::size_t target) {
    if (operandOffset + 2U > bytecode.code.size() || target > bytecode.code.size()) {
        return Result<void>::failure(
            Error(ErrorCode::InternalError, "visual jump patch is invalid"));
    }
    const auto base = operandOffset + 2U;
    const auto delta = static_cast<std::int64_t>(target) - static_cast<std::int64_t>(base);
    if (delta < std::numeric_limits<std::int16_t>::min() ||
        delta > std::numeric_limits<std::int16_t>::max()) {
        return Result<void>::failure(
            Error(ErrorCode::CapacityExceeded, "visual jump exceeds the signed 16-bit range"));
    }
    const auto encoded = static_cast<std::uint16_t>(static_cast<std::int16_t>(delta));
    bytecode.code[operandOffset] = static_cast<std::uint8_t>(encoded & 0xFFU);
    bytecode.code[operandOffset + 1U] =
        static_cast<std::uint8_t>((static_cast<std::uint32_t>(encoded) >> 8U) & 0xFFU);
    return Result<void>::success();
}

const VisualEdge* outgoingEdge(const VisualGraph& graph, const VisualNodeId node,
                               const VisualPinId pin) noexcept {
    for (const auto& edge : graph.edges()) {
        if (edge.sourceNode == node && edge.sourcePin == pin)
            return &edge;
    }
    return nullptr;
}

Result<void> compileTypedValue(TypedCompilerState& state, const VisualNode& node);
Result<void> compileTypedFlow(TypedCompilerState& state, const VisualNode& node);

Result<void> compileTypedInput(TypedCompilerState& state, const VisualNode& node,
                               const std::string_view pinName) {
    const auto* pin = findNamedPin(node, std::string(pinName).c_str(), VisualPinDirection::Input);
    if (pin == nullptr) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidFormat, "typed visual node input pin is missing")
                .addContext("pin", std::string(pinName)));
    }
    const auto* edge = incomingEdge(state.graph, node.id, pin->id);
    const auto* source = edge == nullptr ? nullptr : state.graph.findNode(edge->sourceNode);
    if (source == nullptr) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidFormat, "typed visual node input is not connected")
                .addContext("pin", std::string(pinName)));
    }
    return compileTypedValue(state, *source);
}

bool sameHostCall(const VisualHostCallDescriptor& left,
                  const VisualHostCallDescriptor& right) noexcept {
    return left.name == right.name && left.payload == right.payload &&
           left.argumentCount == right.argumentCount &&
           left.assetReference == right.assetReference &&
           left.entityReference == right.entityReference &&
           left.componentReference == right.componentReference;
}

Result<std::uint16_t> addHostCall(TypedCompilerState& state, const VisualNode& node,
                                  const VisualNodeDefinition& definition) {
    const auto callbackName =
        node.callbackName.empty() ? definition.hostCallback : node.callbackName;
    if (definition.hostArgumentPins.size() > VisualMaximumHostArguments ||
        node.callbackPayload.size() > VisualMaximumHostPayloadBytes ||
        !safeCallbackName(callbackName) || state.hostCallbacks == nullptr ||
        !state.hostCallbacks->supports(
            callbackName, static_cast<std::uint8_t>(definition.hostArgumentPins.size()))) {
        return Result<std::uint16_t>::failure(
            Error(ErrorCode::NotFound, "typed visual host callback is not registered")
                .addContext("callback", callbackName));
    }
    VisualHostCallDescriptor descriptor;
    descriptor.name = callbackName;
    descriptor.payload = node.callbackPayload;
    descriptor.argumentCount = static_cast<std::uint8_t>(definition.hostArgumentPins.size());
    descriptor.assetReference = node.assetReference;
    descriptor.entityReference = node.entityReference;
    descriptor.componentReference = node.componentReference;
    const auto found =
        std::find_if(state.bytecode.hostCalls.begin(), state.bytecode.hostCalls.end(),
                     [&descriptor](const VisualHostCallDescriptor& existing) {
                         return sameHostCall(existing, descriptor);
                     });
    if (found != state.bytecode.hostCalls.end()) {
        return Result<std::uint16_t>::success(
            static_cast<std::uint16_t>(std::distance(state.bytecode.hostCalls.begin(), found)));
    }
    if (state.bytecode.hostCalls.size() >= state.limits.maximumHostCalls ||
        state.bytecode.hostCalls.size() >= std::numeric_limits<std::uint16_t>::max()) {
        return Result<std::uint16_t>::failure(
            Error(ErrorCode::CapacityExceeded, "too many typed visual host calls"));
    }
    state.bytecode.hostCalls.push_back(std::move(descriptor));
    return Result<std::uint16_t>::success(
        static_cast<std::uint16_t>(state.bytecode.hostCalls.size() - 1U));
}

Result<void> compileHostArguments(TypedCompilerState& state, const VisualNode& node,
                                  const VisualNodeDefinition& definition) {
    for (const auto& pin : definition.hostArgumentPins) {
        auto compiled = compileTypedInput(state, node, pin);
        if (!compiled)
            return compiled;
    }
    return Result<void>::success();
}

Result<void> compileTypedValue(TypedCompilerState& state, const VisualNode& node) {
    if (!state.compilingValues.insert(node.id).second) {
        return Result<void>::failure(
            Error(ErrorCode::CycleDetected, "typed visual expression contains a cycle"));
    }
    const auto* definition = definitionForNode(node, state.registry);
    if (definition == nullptr) {
        state.compilingValues.erase(node.id);
        return Result<void>::failure(
            Error(ErrorCode::InvalidFormat, "typed visual node is not registered"));
    }
    Result<void> result = Result<void>::success();
    switch (definition->execution) {
    case VisualExecutionKind::ConstantNumber:
    case VisualExecutionKind::ConstantBoolean: {
        const auto value = definition->execution == VisualExecutionKind::ConstantBoolean
                               ? (node.booleanValue ? 1.0 : 0.0)
                               : node.numberValue;
        if (state.bytecode.constants.size() >= state.limits.maximumConstants) {
            result = Result<void>::failure(
                Error(ErrorCode::CapacityExceeded, "too many typed visual constants"));
            break;
        }
        auto constant = addConstant(state.bytecode, value);
        result = constant ? emitInstruction(state, VisualOpcode::PushConstant, constant.value())
                          : Result<void>::failure(constant.error());
        break;
    }
    case VisualExecutionKind::GetVariable: {
        if (state.bytecode.variables.size() >= state.limits.maximumVariables &&
            std::find(state.bytecode.variables.begin(), state.bytecode.variables.end(),
                      node.variableName) == state.bytecode.variables.end()) {
            result = Result<void>::failure(
                Error(ErrorCode::CapacityExceeded, "too many typed visual variables"));
            break;
        }
        auto variable = addVariable(state.bytecode, node.variableName);
        result = variable ? emitInstruction(state, VisualOpcode::LoadVariable, variable.value())
                          : Result<void>::failure(variable.error());
        break;
    }
    case VisualExecutionKind::Add:
    case VisualExecutionKind::Multiply:
    case VisualExecutionKind::Less:
        result = compileTypedInput(state, node, "a");
        if (result)
            result = compileTypedInput(state, node, "b");
        if (result) {
            const auto opcode = definition->execution == VisualExecutionKind::Add
                                    ? VisualOpcode::Add
                                    : (definition->execution == VisualExecutionKind::Multiply
                                           ? VisualOpcode::Multiply
                                           : VisualOpcode::Less);
            result = emitInstruction(state, opcode);
        }
        break;
    case VisualExecutionKind::HostValue: {
        result = compileHostArguments(state, node, *definition);
        if (result) {
            auto callback = addHostCall(state, node, *definition);
            result = callback ? emitInstruction(state, VisualOpcode::CallHost, callback.value())
                              : Result<void>::failure(callback.error());
        }
        break;
    }
    default:
        result = Result<void>::failure(
            Error(ErrorCode::InvalidFormat, "typed visual node cannot produce a value"));
        break;
    }
    state.compilingValues.erase(node.id);
    return result;
}

Result<void> compileFlowOutput(TypedCompilerState& state, const VisualNode& node,
                               const std::string_view pinName) {
    const auto* pin = findNamedPin(node, std::string(pinName).c_str(), VisualPinDirection::Output);
    if (pin == nullptr)
        return Result<void>::failure(
            Error(ErrorCode::InvalidFormat, "typed visual flow output pin is missing"));
    const auto* edge = outgoingEdge(state.graph, node.id, pin->id);
    if (edge == nullptr)
        return Result<void>::success();
    const auto* target = state.graph.findNode(edge->targetNode);
    if (target == nullptr)
        return Result<void>::failure(
            Error(ErrorCode::InvalidFormat, "typed visual flow target is missing"));
    return compileTypedFlow(state, *target);
}

Result<void> compileTypedFlow(TypedCompilerState& state, const VisualNode& node) {
    if (!state.compilingFlow.insert(node.id).second) {
        return Result<void>::failure(
            Error(ErrorCode::CycleDetected, "typed visual flow contains a cycle"));
    }
    const auto* definition = definitionForNode(node, state.registry);
    if (definition == nullptr) {
        state.compilingFlow.erase(node.id);
        return Result<void>::failure(
            Error(ErrorCode::InvalidFormat, "typed visual flow node is not registered"));
    }
    Result<void> result = Result<void>::success();
    switch (definition->execution) {
    case VisualExecutionKind::Entry:
        result = compileFlowOutput(state, node, "flow");
        break;
    case VisualExecutionKind::SetVariable: {
        result = compileTypedInput(state, node, "value");
        if (result) {
            auto variable = addVariable(state.bytecode, node.variableName);
            result = variable
                         ? emitInstruction(state, VisualOpcode::StoreVariable, variable.value())
                         : Result<void>::failure(variable.error());
        }
        if (result)
            result = compileFlowOutput(state, node, "then");
        break;
    }
    case VisualExecutionKind::Return:
        result = compileTypedInput(state, node, "value");
        if (result) {
            result = emitInstruction(state, VisualOpcode::Return);
            ++state.returnCount;
        }
        break;
    case VisualExecutionKind::Branch: {
        result = compileTypedInput(state, node, "condition");
        if (!result)
            break;
        result = emitInstruction(state, VisualOpcode::JumpIfFalse, 0U);
        if (!result)
            break;
        const auto falseOperand = state.bytecode.code.size() - 2U;
        result = compileFlowOutput(state, node, "true");
        if (!result)
            break;
        result = emitInstruction(state, VisualOpcode::Jump, 0U);
        if (!result)
            break;
        const auto endOperand = state.bytecode.code.size() - 2U;
        result = patchJump(state.bytecode, falseOperand, state.bytecode.code.size());
        if (result)
            result = compileFlowOutput(state, node, "false");
        if (result)
            result = patchJump(state.bytecode, endOperand, state.bytecode.code.size());
        break;
    }
    case VisualExecutionKind::Sequence:
        for (const auto& pin : definition->pins) {
            if (result && pin.direction == VisualPinDirection::Output &&
                pin.type == VisualValueType::Flow) {
                result = compileFlowOutput(state, node, pin.name);
            }
        }
        break;
    case VisualExecutionKind::HostFlow: {
        result = compileHostArguments(state, node, *definition);
        if (result) {
            auto callback = addHostCall(state, node, *definition);
            result = callback ? emitInstruction(state, VisualOpcode::CallHost, callback.value())
                              : Result<void>::failure(callback.error());
        }
        if (result)
            result = emitInstruction(state, VisualOpcode::Pop);
        if (result)
            result = compileFlowOutput(state, node, "then");
        break;
    }
    default:
        result = Result<void>::failure(
            Error(ErrorCode::InvalidFormat, "typed visual node cannot execute in flow"));
        break;
    }
    state.compilingFlow.erase(node.id);
    return result;
}

Result<VisualBytecode> compileTypedGraph(const VisualGraph& graph,
                                         const VisualNodeRegistry& registry,
                                         const VisualHostCallbackTable* hostCallbacks,
                                         const VisualCompileLimits& limits) {
    const auto* entry = graph.findNode(graph.entryNode());
    if (entry == nullptr)
        return Result<VisualBytecode>::failure(
            Error(ErrorCode::InvalidFormat, "typed visual graph entry is missing"));
    TypedCompilerState state{graph, registry, hostCallbacks, limits, {}, {}, {}, 0U};
    auto compiled = compileTypedFlow(state, *entry);
    if (!compiled)
        return Result<VisualBytecode>::failure(compiled.error());
    if (state.returnCount == 0U)
        return Result<VisualBytecode>::failure(
            Error(ErrorCode::InvalidFormat, "typed visual graph has no executable return"));
    return Result<VisualBytecode>::success(std::move(state.bytecode));
}

Result<std::uint16_t> readOperand(const VisualBytecode& bytecode, std::size_t& instructionPointer) {
    if (instructionPointer + 2U > bytecode.code.size()) {
        return Result<std::uint16_t>::failure(
            Error(ErrorCode::InvalidFormat, "truncated bytecode operand"));
    }
    const auto low = static_cast<std::uint16_t>(bytecode.code[instructionPointer]);
    const auto high = static_cast<std::uint16_t>(
        static_cast<std::uint32_t>(bytecode.code[instructionPointer + 1U]) << 8U);
    const auto value = static_cast<std::uint16_t>(low | high);
    instructionPointer += 2U;
    return Result<std::uint16_t>::success(value);
}

} // namespace

VisualNodeRegistry::VisualNodeRegistry(std::vector<VisualNodeDefinition> definitions)
    : definitions_(std::move(definitions)) {}

const VisualNodeRegistry& VisualNodeRegistry::builtins() {
    static const VisualNodeRegistry registry(builtinDefinitions());
    return registry;
}

const VisualNodeDefinition*
VisualNodeRegistry::find(const VisualBuiltinNodeType type) const noexcept {
    const auto found = std::find_if(
        definitions_.begin(), definitions_.end(),
        [type](const VisualNodeDefinition& definition) { return definition.type == type; });
    return found == definitions_.end() ? nullptr : &*found;
}

const VisualNodeDefinition*
VisualNodeRegistry::find(const std::string_view stableName) const noexcept {
    const auto found = std::find_if(definitions_.begin(), definitions_.end(),
                                    [stableName](const VisualNodeDefinition& definition) {
                                        return definition.stableName == stableName;
                                    });
    return found == definitions_.end() ? nullptr : &*found;
}

const VisualNodeDefinition*
VisualNodeRegistry::findLegacy(const VisualNodeKind kind) const noexcept {
    const auto found = std::find_if(
        definitions_.begin(), definitions_.end(), [kind](const VisualNodeDefinition& definition) {
            return definition.legacyKind && *definition.legacyKind == kind;
        });
    return found == definitions_.end() ? nullptr : &*found;
}

Result<VisualNode> VisualNodeRegistry::create(const VisualBuiltinNodeType type,
                                              const VisualNodeId id, std::string name) const {
    const auto* definition = find(type);
    if (definition == nullptr || id == 0U || name.empty()) {
        return Result<VisualNode>::failure(
            Error(ErrorCode::InvalidArgument, "visual node type, ID, or name is invalid"));
    }
    VisualNode node;
    node.id = id;
    node.kind = definition->legacyKind.value_or(VisualNodeKind::ConstantNumber);
    node.builtinType = definition->type;
    node.name = std::move(name);
    node.pins.reserve(definition->pins.size());
    for (const auto& pin : definition->pins) {
        node.pins.push_back({pin.id, pin.name, pin.type, pin.direction});
    }
    return Result<VisualNode>::success(std::move(node));
}

Result<void> VisualHostCallbackTable::add(std::string name, const std::uint8_t argumentCount,
                                          VisualHostCallback callback) {
    if (!safeCallbackName(name) || argumentCount > VisualMaximumHostArguments || !callback) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "visual host callback registration is invalid"));
    }
    if (callbacks_.find(name) != callbacks_.end()) {
        return Result<void>::failure(
            Error(ErrorCode::AlreadyExists, "visual host callback already exists")
                .addContext("callback", name));
    }
    callbacks_.emplace(std::move(name), Entry{argumentCount, std::move(callback)});
    return Result<void>::success();
}

bool VisualHostCallbackTable::supports(const std::string_view name,
                                       const std::uint8_t argumentCount) const noexcept {
    const auto found = callbacks_.find(name);
    return found != callbacks_.end() && found->second.argumentCount == argumentCount;
}

Result<double> VisualHostCallbackTable::dispatch(const VisualHostCallDescriptor& descriptor,
                                                 const std::vector<double>& arguments) const {
    if (!safeCallbackName(descriptor.name) ||
        descriptor.argumentCount > VisualMaximumHostArguments ||
        descriptor.payload.size() > VisualMaximumHostPayloadBytes ||
        (descriptor.assetReference && descriptor.assetReference->isNil()) ||
        (descriptor.entityReference && descriptor.entityReference->isNil()) ||
        (descriptor.componentReference && descriptor.componentReference->isNil()) ||
        (descriptor.execution.ownerEntity && descriptor.execution.ownerEntity->isNil()) ||
        (descriptor.execution.otherEntity && descriptor.execution.otherEntity->isNil()) ||
        !std::isfinite(descriptor.execution.deltaSeconds) ||
        descriptor.execution.deltaSeconds < 0.0 ||
        std::any_of(arguments.begin(), arguments.end(),
                    [](const double value) { return !std::isfinite(value); })) {
        return Result<double>::failure(Error(
            ErrorCode::InvalidFormat, "visual host call descriptor or arguments are invalid"));
    }
    const auto found = callbacks_.find(descriptor.name);
    if (found == callbacks_.end() || found->second.argumentCount != descriptor.argumentCount ||
        arguments.size() != descriptor.argumentCount || !found->second.callback) {
        return Result<double>::failure(
            Error(ErrorCode::NotFound, "visual host callback is missing or has the wrong signature")
                .addContext("callback", descriptor.name));
    }
    auto result = found->second.callback(descriptor, arguments);
    if (!result || !std::isfinite(result.value())) {
        if (!result)
            return result;
        return Result<double>::failure(
            Error(ErrorCode::InvalidFormat, "visual host callback returned a non-finite value")
                .addContext("callback", descriptor.name));
    }
    return result;
}

Result<void> VisualGraph::addNode(VisualNode node) {
    if (node.id == 0U || node.name.empty()) {
        return Result<void>::failure(Error(ErrorCode::InvalidArgument, "visual node is invalid"));
    }
    std::set<VisualPinId> pins;
    for (const auto& pin : node.pins) {
        if (pin.id == 0U || pin.name.empty() || !pins.insert(pin.id).second) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "visual pin is invalid or duplicated"));
        }
    }
    if (nodes_.find(node.id) != nodes_.end()) {
        return Result<void>::failure(
            Error(ErrorCode::AlreadyExists, "visual node ID already exists"));
    }
    nodes_.emplace(node.id, std::move(node));
    return Result<void>::success();
}

Result<void> VisualGraph::addEdge(VisualEdge edge) {
    if (edge.sourceNode == 0U || edge.targetNode == 0U || edge.sourcePin == 0U ||
        edge.targetPin == 0U) {
        return Result<void>::failure(Error(ErrorCode::InvalidArgument, "visual edge is invalid"));
    }
    edges_.push_back(edge);
    return Result<void>::success();
}

Result<void> VisualGraph::addCommentBox(VisualCommentBox comment) {
    if (comment.id == 0U || comment.title.empty() || !std::isfinite(comment.layout.x) ||
        !std::isfinite(comment.layout.y) || !std::isfinite(comment.layout.width) ||
        !std::isfinite(comment.layout.height) || comment.layout.width <= 0.0F ||
        comment.layout.height <= 0.0F) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "visual comment box is invalid"));
    }
    const auto duplicate = std::find_if(
        comments_.begin(), comments_.end(),
        [&comment](const VisualCommentBox& existing) { return existing.id == comment.id; });
    if (duplicate != comments_.end()) {
        return Result<void>::failure(
            Error(ErrorCode::AlreadyExists, "visual comment box ID already exists"));
    }
    comments_.push_back(std::move(comment));
    return Result<void>::success();
}

const VisualNode* VisualGraph::findNode(VisualNodeId id) const noexcept {
    const auto iterator = nodes_.find(id);
    return iterator == nodes_.end() ? nullptr : &iterator->second;
}

const VisualPin* VisualGraph::findPin(VisualNodeId node, VisualPinId pin) const noexcept {
    const auto* foundNode = findNode(node);
    if (foundNode == nullptr)
        return nullptr;
    for (const auto& foundPin : foundNode->pins)
        if (foundPin.id == pin)
            return &foundPin;
    return nullptr;
}

bool VisualValidationReport::hasErrors() const noexcept {
    for (const auto& issue : issues)
        if (issue.severity == VisualIssueSeverity::Error)
            return true;
    return false;
}

VisualValidationReport
VisualGraphValidator::validate(const VisualGraph& graph, const VisualReferenceResolver& references,
                               const VisualNodeRegistry& registry,
                               const VisualHostCallbackTable* hostCallbacks) {
    VisualValidationReport report;
    const auto* entry = graph.findNode(graph.entryNode());
    const auto* entryDefinition = entry == nullptr ? nullptr : definitionForNode(*entry, registry);
    if (entry == nullptr || entryDefinition == nullptr ||
        entryDefinition->execution != VisualExecutionKind::Entry) {
        addIssue(report, VisualIssueSeverity::Error, VisualIssueCode::MissingEntry,
                 graph.entryNode(), "graph entry node is missing or has the wrong kind");
    }

    std::map<std::pair<VisualNodeId, VisualPinId>, std::size_t> inputConnections;
    std::map<std::pair<VisualNodeId, VisualPinId>, std::size_t> flowOutputConnections;
    std::map<VisualNodeId, std::vector<VisualNodeId>> adjacency;
    for (const auto& edge : graph.edges()) {
        const auto* sourceNode = graph.findNode(edge.sourceNode);
        const auto* targetNode = graph.findNode(edge.targetNode);
        if (sourceNode == nullptr || targetNode == nullptr) {
            addIssue(report, VisualIssueSeverity::Error, VisualIssueCode::MissingNode,
                     edge.targetNode, "edge references a missing node");
            continue;
        }
        const auto* sourcePin = graph.findPin(edge.sourceNode, edge.sourcePin);
        const auto* targetPin = graph.findPin(edge.targetNode, edge.targetPin);
        if (sourcePin == nullptr || targetPin == nullptr) {
            addIssue(report, VisualIssueSeverity::Error, VisualIssueCode::MissingPin,
                     edge.targetNode, "edge references a missing pin");
            continue;
        }
        if (sourcePin->direction != VisualPinDirection::Output ||
            targetPin->direction != VisualPinDirection::Input) {
            addIssue(report, VisualIssueSeverity::Error, VisualIssueCode::InvalidPinDirection,
                     edge.targetNode, "edge direction must be output to input");
        }
        if (sourcePin->type != targetPin->type) {
            addIssue(report, VisualIssueSeverity::Error, VisualIssueCode::TypeMismatch,
                     edge.targetNode, "connected pin types do not match");
        }
        const auto input = std::make_pair(edge.targetNode, edge.targetPin);
        if (++inputConnections[input] > 1U) {
            addIssue(report, VisualIssueSeverity::Error, VisualIssueCode::MultipleInputConnections,
                     edge.targetNode, "input pin has multiple connections");
        }
        if (sourcePin->type == VisualValueType::Flow) {
            const auto output = std::make_pair(edge.sourceNode, edge.sourcePin);
            if (++flowOutputConnections[output] > 1U) {
                addIssue(report, VisualIssueSeverity::Error,
                         VisualIssueCode::MultipleFlowOutputConnections, edge.sourceNode,
                         "flow output pin has multiple connections");
            }
        }
        adjacency[edge.sourceNode].push_back(edge.targetNode);
    }

    for (const auto& pair : graph.nodes()) {
        const auto& node = pair.second;
        const auto* definition = definitionForNode(node, registry);
        if (definition == nullptr) {
            addIssue(report, VisualIssueSeverity::Error, VisualIssueCode::UnknownNodeType, node.id,
                     "visual node type is not registered");
            continue;
        }
        auto schemaMatches = node.pins.size() == definition->pins.size();
        if (schemaMatches) {
            for (auto index = std::size_t{0}; index < node.pins.size(); ++index) {
                const auto& actual = node.pins[index];
                const auto& expected = definition->pins[index];
                if (actual.id != expected.id || actual.name != expected.name ||
                    actual.type != expected.type || actual.direction != expected.direction) {
                    schemaMatches = false;
                    break;
                }
            }
        }
        if (!schemaMatches) {
            addIssue(report, VisualIssueSeverity::Error, VisualIssueCode::InvalidNodeSchema,
                     node.id, "visual node pins do not match registry metadata");
            continue;
        }
        for (const auto& pin : definition->pins) {
            if (!pin.connectionRequired)
                continue;
            auto connected = false;
            for (const auto& edge : graph.edges()) {
                connected = connected ||
                            (pin.direction == VisualPinDirection::Input &&
                             edge.targetNode == node.id && edge.targetPin == pin.id) ||
                            (pin.direction == VisualPinDirection::Output &&
                             edge.sourceNode == node.id && edge.sourcePin == pin.id);
            }
            if (!connected) {
                addIssue(report, VisualIssueSeverity::Error, VisualIssueCode::MissingConnection,
                         node.id, "required visual pin is not connected");
            }
        }
        if ((definition->execution == VisualExecutionKind::GetVariable ||
             definition->execution == VisualExecutionKind::SetVariable) &&
            !safeCallbackName(node.variableName)) {
            addIssue(report, VisualIssueSeverity::Error, VisualIssueCode::InvalidNode, node.id,
                     "visual variable name is invalid");
        }
        if (definition->execution == VisualExecutionKind::HostFlow ||
            definition->execution == VisualExecutionKind::HostValue) {
            const auto callbackName =
                node.callbackName.empty() ? definition->hostCallback : node.callbackName;
            const auto argumentCount =
                static_cast<std::uint8_t>(definition->hostArgumentPins.size());
            if (!safeCallbackName(callbackName) ||
                (definition->callbackNameRequired && node.callbackName.empty()) ||
                (hostCallbacks != nullptr &&
                 !hostCallbacks->supports(callbackName, argumentCount))) {
                addIssue(report, VisualIssueSeverity::Error, VisualIssueCode::MissingHostCallback,
                         node.id, "visual host callback is missing or has the wrong signature");
            }
        }
        if (!std::isfinite(node.numberValue) || !std::isfinite(node.layout.x) ||
            !std::isfinite(node.layout.y) || !std::isfinite(node.layout.width) ||
            !std::isfinite(node.layout.height) || node.layout.width <= 0.0F ||
            node.layout.height <= 0.0F) {
            addIssue(report, VisualIssueSeverity::Error, VisualIssueCode::InvalidNode, node.id,
                     "visual node contains non-finite values or invalid layout");
        }
    }

    std::map<VisualNodeId, int> marks;
    for (const auto& node : graph.nodes()) {
        if (detectCycle(node.first, adjacency, marks)) {
            addIssue(report, VisualIssueSeverity::Error, VisualIssueCode::Cycle, node.first,
                     "graph contains a directed cycle");
            break;
        }
    }

    std::set<VisualNodeId> controlReachable;
    std::vector<VisualNodeId> pending;
    pending.reserve(graph.nodes().size());
    for (const auto& pair : graph.nodes()) {
        const auto* definition = definitionForNode(pair.second, registry);
        if (definition != nullptr && definition->execution == VisualExecutionKind::Entry)
            pending.push_back(pair.first);
    }
    if (entry != nullptr) {
        while (!pending.empty()) {
            const auto node = pending.back();
            pending.pop_back();
            if (!controlReachable.insert(node).second)
                continue;
            for (const auto& edge : graph.edges()) {
                if (edge.sourceNode != node)
                    continue;
                const auto* pin = graph.findPin(edge.sourceNode, edge.sourcePin);
                if (pin != nullptr && pin->type == VisualValueType::Flow)
                    pending.push_back(edge.targetNode);
            }
        }
    }
    std::set<VisualNodeId> reachable;
    for (const auto node : controlReachable)
        markDataDependencies(graph, node, reachable);
    for (const auto& node : graph.nodes()) {
        if (reachable.find(node.first) == reachable.end()) {
            addIssue(report, VisualIssueSeverity::Warning, VisualIssueCode::UnreachableNode,
                     node.first, "node is unreachable from the entry flow");
        }
        const auto* definition = definitionForNode(node.second, registry);
        if ((node.second.kind == VisualNodeKind::AssetReference ||
             (definition != nullptr && definition->assetReferenceRequired))) {
            if (!node.second.assetReference ||
                (references.assetExists && !references.assetExists(*node.second.assetReference))) {
                addIssue(report, VisualIssueSeverity::Error, VisualIssueCode::MissingReference,
                         node.first, "asset reference is missing");
            }
        }
        if ((node.second.kind == VisualNodeKind::EntityReference ||
             (definition != nullptr && definition->entityReferenceRequired))) {
            if (!node.second.entityReference ||
                (references.entityExists &&
                 !references.entityExists(*node.second.entityReference))) {
                addIssue(report, VisualIssueSeverity::Error, VisualIssueCode::MissingReference,
                         node.first, "entity reference is missing");
            }
        }
        if (definition != nullptr && definition->componentReferenceRequired &&
            (!node.second.componentReference ||
             (references.componentExists &&
              !references.componentExists(*node.second.componentReference)))) {
            addIssue(report, VisualIssueSeverity::Error, VisualIssueCode::MissingReference,
                     node.first, "component reference is missing");
        }
    }
    return report;
}

Result<VisualBytecode> VisualGraphCompiler::compile(const VisualGraph& graph,
                                                    const VisualReferenceResolver& references,
                                                    const VisualCompileLimits& limits,
                                                    const VisualNodeRegistry& registry,
                                                    const VisualHostCallbackTable* hostCallbacks) {
    if (limits.maximumNodes > VisualMaximumGraphNodes ||
        limits.maximumEdges > VisualMaximumGraphEdges ||
        limits.maximumConstants > VisualMaximumConstants ||
        limits.maximumVariables > VisualMaximumVariables ||
        limits.maximumHostCalls > VisualMaximumHostCalls ||
        graph.nodes().size() > limits.maximumNodes || graph.edges().size() > limits.maximumEdges ||
        limits.maximumBytecodeBytes == 0U || limits.maximumInstructions == 0U ||
        limits.maximumBytecodeBytes > VisualMaximumBytecodeBytes ||
        limits.maximumInstructions > VisualMaximumCompiledInstructions) {
        return Result<VisualBytecode>::failure(Error(
            ErrorCode::CapacityExceeded, "visual graph compile limits are invalid or exceeded"));
    }
    const auto report = VisualGraphValidator::validate(graph, references, registry, hostCallbacks);
    if (report.hasErrors()) {
        return Result<VisualBytecode>::failure(
            Error(ErrorCode::InvalidFormat, "visual graph validation failed"));
    }
    const auto hasTypedNodes =
        std::any_of(graph.nodes().begin(), graph.nodes().end(), [](const auto& node) {
            return node.second.builtinType != VisualBuiltinNodeType::Legacy;
        });
    if (hasTypedNodes) {
        return compileTypedGraph(graph, registry, hostCallbacks, limits);
    }
    const VisualNode* returnNode = nullptr;
    for (const auto& node : graph.nodes()) {
        if (node.second.kind == VisualNodeKind::Return) {
            if (returnNode != nullptr) {
                return Result<VisualBytecode>::failure(
                    Error(ErrorCode::InvalidFormat, "graph has multiple return nodes"));
            }
            returnNode = &node.second;
        }
    }
    if (returnNode == nullptr) {
        return Result<VisualBytecode>::failure(
            Error(ErrorCode::InvalidFormat, "graph has no return node"));
    }
    const auto* valuePin = findNamedPin(*returnNode, "value", VisualPinDirection::Input);
    if (valuePin == nullptr) {
        return Result<VisualBytecode>::failure(
            Error(ErrorCode::InvalidFormat, "return node has no value pin"));
    }
    const auto* edge = incomingEdge(graph, returnNode->id, valuePin->id);
    if (edge == nullptr) {
        return Result<VisualBytecode>::failure(
            Error(ErrorCode::InvalidFormat, "return value is not connected"));
    }
    const auto* source = graph.findNode(edge->sourceNode);
    if (source == nullptr) {
        return Result<VisualBytecode>::failure(
            Error(ErrorCode::InvalidFormat, "return value source is missing"));
    }
    VisualBytecode bytecode;
    std::set<VisualNodeId> compiling;
    auto compiled = compileValue(graph, *source, bytecode, compiling);
    if (!compiled)
        return Result<VisualBytecode>::failure(compiled.error());
    bytecode.code.push_back(static_cast<std::uint8_t>(VisualOpcode::Return));
    bytecode.instructionCount = 1U;
    for (auto offset = std::size_t{0}; offset < bytecode.code.size();) {
        const auto opcode = static_cast<VisualOpcode>(bytecode.code[offset++]);
        ++bytecode.instructionCount;
        if (opcode == VisualOpcode::PushConstant || opcode == VisualOpcode::LoadVariable ||
            opcode == VisualOpcode::StoreVariable || opcode == VisualOpcode::JumpIfFalse ||
            opcode == VisualOpcode::Jump || opcode == VisualOpcode::CallHost) {
            offset += 2U;
        }
    }
    --bytecode.instructionCount;
    if (bytecode.code.size() > limits.maximumBytecodeBytes ||
        bytecode.instructionCount > limits.maximumInstructions ||
        bytecode.constants.size() > limits.maximumConstants ||
        bytecode.variables.size() > limits.maximumVariables ||
        bytecode.hostCalls.size() > limits.maximumHostCalls) {
        return Result<VisualBytecode>::failure(
            Error(ErrorCode::CapacityExceeded, "visual bytecode exceeds ESP32 compile limits"));
    }
    return Result<VisualBytecode>::success(std::move(bytecode));
}

Result<VisualVmResult> VisualBytecodeVm::execute(const VisualBytecode& bytecode,
                                                 std::map<std::string, double> variables,
                                                 std::size_t maximumInstructions,
                                                 const VisualHostCallbackTable* hostCallbacks,
                                                 const VisualHostExecutionContext& context) const {
    return resume(bytecode, VisualVmContinuation{}, std::move(variables), maximumInstructions,
                  hostCallbacks, context);
}

Result<VisualVmResult>
VisualBytecodeVm::resume(const VisualBytecode& bytecode, VisualVmContinuation continuation,
                         std::map<std::string, double> variables,
                         std::size_t maximumInstructions,
                         const VisualHostCallbackTable* hostCallbacks,
                         const VisualHostExecutionContext& context) const {
    if (maximumInstructions == 0U) {
        return Result<VisualVmResult>::failure(
            Error(ErrorCode::InvalidArgument, "VM instruction budget is zero"));
    }
    if (bytecode.code.size() > VisualMaximumBytecodeBytes ||
        bytecode.instructionCount > VisualMaximumCompiledInstructions ||
        bytecode.constants.size() > VisualMaximumConstants ||
        bytecode.variables.size() > VisualMaximumVariables ||
        bytecode.hostCalls.size() > VisualMaximumHostCalls) {
        return Result<VisualVmResult>::failure(
            Error(ErrorCode::CapacityExceeded, "visual bytecode exceeds VM limits"));
    }
    if ((context.ownerEntity && context.ownerEntity->isNil()) ||
        (context.otherEntity && context.otherEntity->isNil()) ||
        !std::isfinite(context.deltaSeconds) || context.deltaSeconds < 0.0 ||
        std::any_of(bytecode.constants.begin(), bytecode.constants.end(),
                    [](const double value) { return !std::isfinite(value); }) ||
        std::any_of(bytecode.variables.begin(), bytecode.variables.end(),
                    [](const std::string& name) { return !safeCallbackName(name); }) ||
        std::any_of(variables.begin(), variables.end(),
                    [](const auto& pair) { return !std::isfinite(pair.second); }) ||
        continuation.instructionPointer > bytecode.code.size() ||
        continuation.stack.size() > VisualMaximumVmStack ||
        std::any_of(continuation.stack.begin(), continuation.stack.end(),
                    [](const double value) { return !std::isfinite(value); })) {
        return Result<VisualVmResult>::failure(
            Error(ErrorCode::InvalidFormat, "visual VM constants or variables are invalid"));
    }
    maximumInstructions = std::min(maximumInstructions, VisualMaximumCompiledInstructions);
    auto stack = std::move(continuation.stack);
    stack.reserve(std::max<std::size_t>(32U, stack.size()));
    std::size_t instructionPointer = continuation.instructionPointer;
    std::size_t executed = 0;
    auto pop = [&stack]() -> Result<double> {
        if (stack.empty())
            return Result<double>::failure(Error(ErrorCode::InvalidState, "VM stack underflow"));
        const auto value = stack.back();
        stack.pop_back();
        return Result<double>::success(value);
    };

    while (instructionPointer < bytecode.code.size()) {
        if (++executed > maximumInstructions) {
            return Result<VisualVmResult>::failure(
                Error(ErrorCode::CapacityExceeded, "VM instruction budget exceeded"));
        }
        const auto opcode = static_cast<VisualOpcode>(bytecode.code[instructionPointer++]);
        switch (opcode) {
        case VisualOpcode::PushConstant: {
            auto operand = readOperand(bytecode, instructionPointer);
            if (!operand)
                return Result<VisualVmResult>::failure(operand.error());
            if (operand.value() >= bytecode.constants.size()) {
                return Result<VisualVmResult>::failure(
                    Error(ErrorCode::InvalidFormat, "constant index is invalid"));
            }
            stack.push_back(bytecode.constants[operand.value()]);
            break;
        }
        case VisualOpcode::LoadVariable:
        case VisualOpcode::StoreVariable: {
            auto operand = readOperand(bytecode, instructionPointer);
            if (!operand)
                return Result<VisualVmResult>::failure(operand.error());
            if (operand.value() >= bytecode.variables.size()) {
                return Result<VisualVmResult>::failure(
                    Error(ErrorCode::InvalidFormat, "variable index is invalid"));
            }
            const auto& name = bytecode.variables[operand.value()];
            if (opcode == VisualOpcode::LoadVariable) {
                const auto value = variables.find(name);
                stack.push_back(value == variables.end() ? 0.0 : value->second);
            } else {
                auto value = pop();
                if (!value)
                    return Result<VisualVmResult>::failure(value.error());
                variables[name] = value.value();
            }
            break;
        }
        case VisualOpcode::Add:
        case VisualOpcode::Multiply:
        case VisualOpcode::Less: {
            auto rhs = pop();
            auto lhs = pop();
            if (!rhs)
                return Result<VisualVmResult>::failure(rhs.error());
            if (!lhs)
                return Result<VisualVmResult>::failure(lhs.error());
            const auto value =
                opcode == VisualOpcode::Add
                    ? lhs.value() + rhs.value()
                    : (opcode == VisualOpcode::Multiply ? lhs.value() * rhs.value()
                                                        : (lhs.value() < rhs.value() ? 1.0 : 0.0));
            if (!std::isfinite(value)) {
                return Result<VisualVmResult>::failure(Error(
                    ErrorCode::InvalidFormat, "visual VM arithmetic produced a non-finite value"));
            }
            stack.push_back(value);
            break;
        }
        case VisualOpcode::JumpIfFalse:
        case VisualOpcode::Jump: {
            auto operand = readOperand(bytecode, instructionPointer);
            if (!operand)
                return Result<VisualVmResult>::failure(operand.error());
            bool jump = true;
            if (opcode == VisualOpcode::JumpIfFalse) {
                auto condition = pop();
                if (!condition)
                    return Result<VisualVmResult>::failure(condition.error());
                jump = condition.value() == 0.0;
            }
            if (jump) {
                const auto offset = static_cast<std::int16_t>(operand.value());
                const auto target =
                    static_cast<long long>(instructionPointer) + static_cast<long long>(offset);
                if (target < 0 || static_cast<std::size_t>(target) > bytecode.code.size()) {
                    return Result<VisualVmResult>::failure(
                        Error(ErrorCode::InvalidFormat, "jump target is invalid"));
                }
                instructionPointer = static_cast<std::size_t>(target);
            }
            break;
        }
        case VisualOpcode::Return: {
            auto value = pop();
            if (!value)
                return Result<VisualVmResult>::failure(value.error());
            VisualVmResult result;
            result.returnValue = value.value();
            result.variables = std::move(variables);
            result.executedInstructions = executed;
            result.completed = true;
            return Result<VisualVmResult>::success(std::move(result));
        }
        case VisualOpcode::Pop: {
            auto value = pop();
            if (!value)
                return Result<VisualVmResult>::failure(value.error());
            break;
        }
        case VisualOpcode::CallHost: {
            auto operand = readOperand(bytecode, instructionPointer);
            if (!operand)
                return Result<VisualVmResult>::failure(operand.error());
            if (operand.value() >= bytecode.hostCalls.size() || hostCallbacks == nullptr) {
                return Result<VisualVmResult>::failure(
                    Error(ErrorCode::InvalidFormat, "visual host call is unavailable"));
            }
            auto descriptor = bytecode.hostCalls[operand.value()];
            descriptor.execution = context;
            std::vector<double> arguments(descriptor.argumentCount);
            for (auto index = arguments.size(); index > 0U; --index) {
                auto value = pop();
                if (!value)
                    return Result<VisualVmResult>::failure(value.error());
                arguments[index - 1U] = value.value();
            }
            if (descriptor.name == "time.delay") {
                if (arguments.size() != 1U || !std::isfinite(arguments[0]) ||
                    arguments[0] < 0.0 || arguments[0] > 86400.0) {
                    return Result<VisualVmResult>::failure(
                        Error(ErrorCode::InvalidArgument,
                              "visual delay must be between zero and 86400 seconds"));
                }
                stack.push_back(0.0);
                if (arguments[0] > 0.0) {
                    VisualVmResult result;
                    result.variables = std::move(variables);
                    result.executedInstructions = executed;
                    result.completed = false;
                    result.delaySeconds = arguments[0];
                    result.continuation =
                        VisualVmContinuation{instructionPointer, std::move(stack)};
                    return Result<VisualVmResult>::success(std::move(result));
                }
                break;
            }
            auto result = hostCallbacks->dispatch(descriptor, arguments);
            if (!result)
                return Result<VisualVmResult>::failure(result.error());
            stack.push_back(result.value());
            break;
        }
        default:
            return Result<VisualVmResult>::failure(
                Error(ErrorCode::InvalidFormat, "unknown VM opcode"));
        }
        if (stack.size() > VisualMaximumVmStack) {
            return Result<VisualVmResult>::failure(
                Error(ErrorCode::CapacityExceeded, "VM stack limit exceeded"));
        }
    }
    return Result<VisualVmResult>::failure(
        Error(ErrorCode::InvalidFormat, "VM program ended without return"));
}

} // namespace fabgl
