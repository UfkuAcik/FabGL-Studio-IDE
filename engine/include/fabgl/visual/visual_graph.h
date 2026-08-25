#pragma once

#include "fabgl/core/guid.h"
#include "fabgl/core/result.h"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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

// Kept separate from VisualNodeKind so pre-v1 editor code can retain its exhaustive legacy
// switches while files and runtime code use stable, extensible typed definitions.
enum class VisualBuiltinNodeType : std::uint16_t {
    Legacy = 0,
    EventStart,
    FlowBranch,
    FlowSequence,
    FlowDelay,
    VariableGet,
    VariableSet,
    FunctionCall,
    EntityAction,
    ComponentAction,
    VectorLength3,
    InputAction,
    CollisionEvent,
    AudioPlay,
    AnimationPlay,
    SceneLoad,
    UiAction,
    NumberConstant,
    BooleanConstant,
    MathAdd,
    MathMultiply,
    CompareLess,
    FlowReturn,
    AssetReference,
    EntityReference,
    EventUpdate,
    EventFixedUpdate,
    EventLateUpdate,
    EventCollisionStay,
    EventCollisionExit,
    EventTriggerEnter,
    EventTriggerExit,
};

enum class VisualNodeCategory : std::uint8_t {
    Event,
    Branch,
    Sequence,
    Delay,
    Variable,
    Function,
    Entity,
    Component,
    Vector,
    Input,
    Collision,
    Audio,
    Animation,
    Scene,
    UI,
    Math,
    Reference,
};

enum class VisualExecutionKind : std::uint8_t {
    Entry,
    Branch,
    Sequence,
    SetVariable,
    Return,
    ConstantNumber,
    ConstantBoolean,
    GetVariable,
    Add,
    Multiply,
    Less,
    HostFlow,
    HostValue,
    Unsupported,
};

struct VisualPinDefinition final {
    VisualPinId id = 0;
    std::string name;
    VisualValueType type = VisualValueType::Number;
    VisualPinDirection direction = VisualPinDirection::Input;
    bool connectionRequired = false;
};

struct VisualNodeDefinition final {
    VisualBuiltinNodeType type = VisualBuiltinNodeType::Legacy;
    std::string stableName;
    std::string displayName;
    VisualNodeCategory category = VisualNodeCategory::Math;
    VisualExecutionKind execution = VisualExecutionKind::Unsupported;
    std::optional<VisualNodeKind> legacyKind;
    std::vector<VisualPinDefinition> pins;
    std::string hostCallback;
    std::vector<std::string> hostArgumentPins;
    bool callbackNameRequired = false;
    bool assetReferenceRequired = false;
    bool entityReferenceRequired = false;
    bool componentReferenceRequired = false;
};

struct VisualPin final {
    VisualPinId id = 0;
    std::string name;
    VisualValueType type = VisualValueType::Number;
    VisualPinDirection direction = VisualPinDirection::Input;
};

struct VisualNodeLayout final {
    float x = 0.0F;
    float y = 0.0F;
    float width = 180.0F;
    float height = 80.0F;
};

struct VisualNode final {
    VisualNodeId id = 0;
    VisualNodeKind kind = VisualNodeKind::ConstantNumber;
    VisualBuiltinNodeType builtinType = VisualBuiltinNodeType::Legacy;
    std::string name;
    std::vector<VisualPin> pins;
    VisualNodeLayout layout;
    double numberValue = 0.0;
    bool booleanValue = false;
    std::string variableName;
    std::string callbackName;
    std::string callbackPayload;
    std::optional<AssetGuid> assetReference;
    std::optional<EntityGuid> entityReference;
    std::optional<ComponentTypeGuid> componentReference;
};

class VisualNodeRegistry final {
  public:
    [[nodiscard]] static const VisualNodeRegistry& builtins();
    [[nodiscard]] const VisualNodeDefinition* find(VisualBuiltinNodeType type) const noexcept;
    [[nodiscard]] const VisualNodeDefinition* find(std::string_view stableName) const noexcept;
    [[nodiscard]] const VisualNodeDefinition* findLegacy(VisualNodeKind kind) const noexcept;
    [[nodiscard]] const std::vector<VisualNodeDefinition>& definitions() const noexcept {
        return definitions_;
    }
    [[nodiscard]] Result<VisualNode> create(VisualBuiltinNodeType type, VisualNodeId id,
                                            std::string name) const;

  private:
    explicit VisualNodeRegistry(std::vector<VisualNodeDefinition> definitions);
    std::vector<VisualNodeDefinition> definitions_;
};

struct VisualEdge final {
    VisualNodeId sourceNode = 0;
    VisualPinId sourcePin = 0;
    VisualNodeId targetNode = 0;
    VisualPinId targetPin = 0;
};

struct VisualCommentBox final {
    std::uint16_t id = 0;
    std::string title;
    VisualNodeLayout layout;
};

class VisualGraph final {
  public:
    [[nodiscard]] Result<void> addNode(VisualNode node);
    [[nodiscard]] Result<void> addEdge(VisualEdge edge);
    [[nodiscard]] Result<void> addCommentBox(VisualCommentBox comment);
    void setGuid(AssetGuid guid) noexcept {
        guid_ = guid;
    }
    void setName(std::string name) {
        name_ = std::move(name);
    }
    void setEntryNode(VisualNodeId id) noexcept {
        entryNode_ = id;
    }

    [[nodiscard]] const VisualNode* findNode(VisualNodeId id) const noexcept;
    [[nodiscard]] const VisualPin* findPin(VisualNodeId node, VisualPinId pin) const noexcept;
    [[nodiscard]] VisualNodeId entryNode() const noexcept {
        return entryNode_;
    }
    [[nodiscard]] AssetGuid guid() const noexcept {
        return guid_;
    }
    [[nodiscard]] const std::string& name() const noexcept {
        return name_;
    }
    [[nodiscard]] const std::map<VisualNodeId, VisualNode>& nodes() const noexcept {
        return nodes_;
    }
    [[nodiscard]] const std::vector<VisualEdge>& edges() const noexcept {
        return edges_;
    }
    [[nodiscard]] const std::vector<VisualCommentBox>& comments() const noexcept {
        return comments_;
    }

  private:
    std::map<VisualNodeId, VisualNode> nodes_;
    std::vector<VisualEdge> edges_;
    std::vector<VisualCommentBox> comments_;
    VisualNodeId entryNode_ = 0;
    AssetGuid guid_;
    std::string name_;
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
    MultipleFlowOutputConnections,
    Cycle,
    UnreachableNode,
    MissingReference,
    InvalidNode,
    MissingConnection,
    UnknownNodeType,
    InvalidNodeSchema,
    MissingHostCallback,
    LimitExceeded,
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
    VisualReferenceResolver() = default;
    VisualReferenceResolver(std::function<bool(AssetGuid)> asset,
                            std::function<bool(EntityGuid)> entity)
        : assetExists(std::move(asset)), entityExists(std::move(entity)) {}
    VisualReferenceResolver(std::function<bool(AssetGuid)> asset,
                            std::function<bool(EntityGuid)> entity,
                            std::function<bool(ComponentTypeGuid)> component)
        : assetExists(std::move(asset)), entityExists(std::move(entity)),
          componentExists(std::move(component)) {}

    std::function<bool(AssetGuid)> assetExists;
    std::function<bool(EntityGuid)> entityExists;
    std::function<bool(ComponentTypeGuid)> componentExists;
};

struct VisualHostCallDescriptor final {
    std::string name;
    std::string payload;
    std::uint8_t argumentCount = 0;
    std::optional<AssetGuid> assetReference;
    std::optional<EntityGuid> entityReference;
    std::optional<ComponentTypeGuid> componentReference;
    struct ExecutionContext final {
        enum class Event : std::uint8_t {
            None = 0,
            Start,
            Update,
            FixedUpdate,
            LateUpdate,
            CollisionEnter,
            CollisionStay,
            CollisionExit,
            TriggerEnter,
            TriggerExit,
        };

        Event event = Event::None;
        std::optional<EntityGuid> ownerEntity;
        std::optional<EntityGuid> otherEntity;
        double deltaSeconds = 0.0;
    } execution;
};

using VisualRuntimeEvent = VisualHostCallDescriptor::ExecutionContext::Event;
using VisualHostExecutionContext = VisualHostCallDescriptor::ExecutionContext;

using VisualHostCallback =
    std::function<Result<double>(const VisualHostCallDescriptor&, const std::vector<double>&)>;

class VisualHostCallbackTable final {
  public:
    [[nodiscard]] Result<void> add(std::string name, std::uint8_t argumentCount,
                                   VisualHostCallback callback);
    [[nodiscard]] bool supports(std::string_view name, std::uint8_t argumentCount) const noexcept;
    [[nodiscard]] Result<double> dispatch(const VisualHostCallDescriptor& descriptor,
                                          const std::vector<double>& arguments) const;

  private:
    struct Entry final {
        std::uint8_t argumentCount = 0;
        VisualHostCallback callback;
    };
    std::map<std::string, Entry, std::less<>> callbacks_;
};

class VisualGraphValidator final {
  public:
    [[nodiscard]] static VisualValidationReport
    validate(const VisualGraph& graph, const VisualReferenceResolver& references = {},
             const VisualNodeRegistry& registry = VisualNodeRegistry::builtins(),
             const VisualHostCallbackTable* hostCallbacks = nullptr);
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
    Pop,
    CallHost,
};

inline constexpr std::size_t VisualMaximumBytecodeBytes = 4096U;
inline constexpr std::size_t VisualMaximumCompiledInstructions = 1024U;
inline constexpr std::size_t VisualMaximumVmStack = 256U;
inline constexpr std::size_t VisualMaximumGraphNodes = 512U;
inline constexpr std::size_t VisualMaximumGraphEdges = 1024U;
inline constexpr std::size_t VisualMaximumConstants = 256U;
inline constexpr std::size_t VisualMaximumVariables = 128U;
inline constexpr std::size_t VisualMaximumHostCalls = 64U;
inline constexpr std::uint8_t VisualMaximumHostArguments = 8U;
inline constexpr std::size_t VisualMaximumHostPayloadBytes = 1024U;

struct VisualCompileLimits final {
    std::size_t maximumNodes = VisualMaximumGraphNodes;
    std::size_t maximumEdges = VisualMaximumGraphEdges;
    std::size_t maximumBytecodeBytes = VisualMaximumBytecodeBytes;
    std::size_t maximumInstructions = VisualMaximumCompiledInstructions;
    std::size_t maximumConstants = VisualMaximumConstants;
    std::size_t maximumVariables = VisualMaximumVariables;
    std::size_t maximumHostCalls = VisualMaximumHostCalls;
};

struct VisualBytecode final {
    std::vector<std::uint8_t> code;
    std::vector<double> constants;
    std::vector<std::string> variables;
    std::vector<VisualHostCallDescriptor> hostCalls;
    std::size_t instructionCount = 0;
};

class VisualGraphCompiler final {
  public:
    [[nodiscard]] static Result<VisualBytecode>
    compile(const VisualGraph& graph, const VisualReferenceResolver& references = {},
            const VisualCompileLimits& limits = {},
            const VisualNodeRegistry& registry = VisualNodeRegistry::builtins(),
            const VisualHostCallbackTable* hostCallbacks = nullptr);
};

struct VisualVmContinuation final {
    std::size_t instructionPointer = 0U;
    std::vector<double> stack;
};

struct VisualVmResult final {
    double returnValue = 0.0;
    std::map<std::string, double> variables;
    std::size_t executedInstructions = 0;
    bool completed = true;
    double delaySeconds = 0.0;
    std::optional<VisualVmContinuation> continuation;
};

class VisualBytecodeVm final {
  public:
    [[nodiscard]] Result<VisualVmResult>
    execute(const VisualBytecode& bytecode, std::map<std::string, double> variables = {},
            std::size_t maximumInstructions = VisualMaximumCompiledInstructions,
            const VisualHostCallbackTable* hostCallbacks = nullptr,
            const VisualHostExecutionContext& context = {}) const;
    [[nodiscard]] Result<VisualVmResult>
    resume(const VisualBytecode& bytecode, VisualVmContinuation continuation,
           std::map<std::string, double> variables,
           std::size_t maximumInstructions = VisualMaximumCompiledInstructions,
           const VisualHostCallbackTable* hostCallbacks = nullptr,
           const VisualHostExecutionContext& context = {}) const;
};

struct VisualGraphFormatLimits final {
    std::size_t maximumSourceBytes = 4U * 1024U * 1024U;
    std::size_t maximumNodes = VisualMaximumGraphNodes;
    std::size_t maximumEdges = VisualMaximumGraphEdges;
    std::size_t maximumPins = 4096U;
    std::size_t maximumComments = 128U;
    std::size_t maximumStringBytes = 1024U;
};

[[nodiscard]] Result<std::string>
serializeVisualGraph(const VisualGraph& graph,
                     const VisualNodeRegistry& registry = VisualNodeRegistry::builtins(),
                     const VisualGraphFormatLimits& limits = {});
[[nodiscard]] Result<VisualGraph>
deserializeVisualGraph(std::string_view text,
                       const VisualNodeRegistry& registry = VisualNodeRegistry::builtins(),
                       const VisualGraphFormatLimits& limits = {});

} // namespace fabgl
