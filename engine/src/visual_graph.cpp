#include "fabgl/visual/visual_graph.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace fabgl {
namespace {

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

VisualValidationReport VisualGraphValidator::validate(const VisualGraph& graph,
                                                      const VisualReferenceResolver& references) {
    VisualValidationReport report;
    const auto* entry = graph.findNode(graph.entryNode());
    if (entry == nullptr || entry->kind != VisualNodeKind::Entry) {
        addIssue(report, VisualIssueSeverity::Error, VisualIssueCode::MissingEntry,
                 graph.entryNode(), "graph entry node is missing or has the wrong kind");
    }

    std::map<std::pair<VisualNodeId, VisualPinId>, std::size_t> inputConnections;
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
        adjacency[edge.sourceNode].push_back(edge.targetNode);
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
    if (entry != nullptr) {
        std::vector<VisualNodeId> pending{entry->id};
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
        if (node.second.kind == VisualNodeKind::AssetReference) {
            if (!node.second.assetReference || !references.assetExists ||
                !references.assetExists(*node.second.assetReference)) {
                addIssue(report, VisualIssueSeverity::Error, VisualIssueCode::MissingReference,
                         node.first, "asset reference is missing");
            }
        }
        if (node.second.kind == VisualNodeKind::EntityReference) {
            if (!node.second.entityReference || !references.entityExists ||
                !references.entityExists(*node.second.entityReference)) {
                addIssue(report, VisualIssueSeverity::Error, VisualIssueCode::MissingReference,
                         node.first, "entity reference is missing");
            }
        }
    }
    return report;
}

Result<VisualBytecode> VisualGraphCompiler::compile(const VisualGraph& graph,
                                                    const VisualReferenceResolver& references) {
    const auto report = VisualGraphValidator::validate(graph, references);
    if (report.hasErrors()) {
        return Result<VisualBytecode>::failure(
            Error(ErrorCode::InvalidFormat, "visual graph validation failed"));
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
    return Result<VisualBytecode>::success(std::move(bytecode));
}

Result<VisualVmResult> VisualBytecodeVm::execute(const VisualBytecode& bytecode,
                                                 std::map<std::string, double> variables,
                                                 std::size_t maximumInstructions) const {
    if (maximumInstructions == 0U) {
        return Result<VisualVmResult>::failure(
            Error(ErrorCode::InvalidArgument, "VM instruction budget is zero"));
    }
    std::vector<double> stack;
    stack.reserve(32U);
    std::size_t instructionPointer = 0;
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
            if (opcode == VisualOpcode::Add)
                stack.push_back(lhs.value() + rhs.value());
            else if (opcode == VisualOpcode::Multiply)
                stack.push_back(lhs.value() * rhs.value());
            else
                stack.push_back(lhs.value() < rhs.value() ? 1.0 : 0.0);
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
            return Result<VisualVmResult>::success({value.value(), std::move(variables), executed});
        }
        default:
            return Result<VisualVmResult>::failure(
                Error(ErrorCode::InvalidFormat, "unknown VM opcode"));
        }
        if (stack.size() > 256U) {
            return Result<VisualVmResult>::failure(
                Error(ErrorCode::CapacityExceeded, "VM stack limit exceeded"));
        }
    }
    return Result<VisualVmResult>::failure(
        Error(ErrorCode::InvalidFormat, "VM program ended without return"));
}

} // namespace fabgl
