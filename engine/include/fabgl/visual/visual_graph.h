#pragma once

#include "fabgl/core/guid.h"
#include "fabgl/core/result.h"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fabgl {

using VisualNodeId = std::uint16_t;
using VisualPinId = std::uint16_t;

enum class VisualValueType {
    Flow,
    Number,
    Boolean,
};

enum class VisualPinDirection {
    Input,
    Output,
};

enum class VisualNodeKind {
    Entry,
    ConstantNumber,
    ConstantBoolean,
    GetVariable,
    Add,
    Multiply,
    Less,
    Return,
    AssetReference,
    EntityReference,
};

struct VisualPin final {
    VisualPinId id = 0;
    std::string name;
    VisualValueType type = VisualValueType::Number;
    VisualPinDirection direction = VisualPinDirection::Input;
};

struct VisualNode final {
    VisualNodeId id = 0;
    VisualNodeKind kind = VisualNodeKind::ConstantNumber;
    std::string name;
    std::vector<VisualPin> pins;
    double numberValue = 0.0;
    bool booleanValue = false;
    std::string variableName;
    std::optional<AssetGuid> assetReference;
    std::optional<EntityGuid> entityReference;
};

struct VisualEdge final {
    VisualNodeId sourceNode = 0;
    VisualPinId sourcePin = 0;
    VisualNodeId targetNode = 0;
    VisualPinId targetPin = 0;
};

class VisualGraph final {
  public:
    [[nodiscard]] Result<void> addNode(VisualNode node);
    [[nodiscard]] Result<void> addEdge(VisualEdge edge);
    void setEntryNode(VisualNodeId id) noexcept {
        entryNode_ = id;
    }

    [[nodiscard]] const VisualNode* findNode(VisualNodeId id) const noexcept;
    [[nodiscard]] const VisualPin* findPin(VisualNodeId node, VisualPinId pin) const noexcept;
    [[nodiscard]] VisualNodeId entryNode() const noexcept {
        return entryNode_;
    }
    [[nodiscard]] const std::map<VisualNodeId, VisualNode>& nodes() const noexcept {
        return nodes_;
    }
    [[nodiscard]] const std::vector<VisualEdge>& edges() const noexcept {
        return edges_;
    }

  private:
    std::map<VisualNodeId, VisualNode> nodes_;
    std::vector<VisualEdge> edges_;
    VisualNodeId entryNode_ = 0;
};

enum class VisualIssueSeverity {
    Warning,
    Error,
};

enum class VisualIssueCode {
    MissingEntry,
    MissingNode,
    MissingPin,
    InvalidPinDirection,
    TypeMismatch,
    MultipleInputConnections,
    Cycle,
    UnreachableNode,
    MissingReference,
    InvalidNode,
};

struct VisualValidationIssue final {
    VisualIssueSeverity severity = VisualIssueSeverity::Error;
    VisualIssueCode code = VisualIssueCode::InvalidNode;
    VisualNodeId node = 0;
    std::string message;
};

struct VisualValidationReport final {
    std::vector<VisualValidationIssue> issues;
    [[nodiscard]] bool hasErrors() const noexcept;
};

struct VisualReferenceResolver final {
    std::function<bool(AssetGuid)> assetExists;
    std::function<bool(EntityGuid)> entityExists;
};

class VisualGraphValidator final {
  public:
    [[nodiscard]] static VisualValidationReport
    validate(const VisualGraph& graph, const VisualReferenceResolver& references = {});
};

enum class VisualOpcode : std::uint8_t {
    PushConstant = 1,
    LoadVariable,
    StoreVariable,
    Add,
    Multiply,
    Less,
    JumpIfFalse,
    Jump,
    Return,
};

struct VisualBytecode final {
    std::vector<std::uint8_t> code;
    std::vector<double> constants;
    std::vector<std::string> variables;
};

class VisualGraphCompiler final {
  public:
    [[nodiscard]] static Result<VisualBytecode>
    compile(const VisualGraph& graph, const VisualReferenceResolver& references = {});
};

struct VisualVmResult final {
    double returnValue = 0.0;
    std::map<std::string, double> variables;
    std::size_t executedInstructions = 0;
};

class VisualBytecodeVm final {
  public:
    [[nodiscard]] Result<VisualVmResult> execute(const VisualBytecode& bytecode,
                                                 std::map<std::string, double> variables = {},
                                                 std::size_t maximumInstructions = 4096U) const;
};

} // namespace fabgl
