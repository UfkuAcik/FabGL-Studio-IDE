#include "fabgl/visual/visual_graph.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace fabgl {
namespace {

constexpr std::uint32_t VisualGraphFormatVersion = 1U;

class LineReader final {
  public:
    explicit LineReader(const std::string_view text) : stream_(std::string(text)) {
        stream_.imbue(std::locale::classic());
    }

    [[nodiscard]] Result<std::string> next(const char* expected) {
        std::string line;
        while (std::getline(stream_, line)) {
            ++lineNumber_;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            const auto first = line.find_first_not_of(" \t");
            if (first == std::string::npos || line[first] == '#')
                continue;
            const auto last = line.find_last_not_of(" \t");
            return Result<std::string>::success(line.substr(first, last - first + 1U));
        }
        return Result<std::string>::failure(
            Error(ErrorCode::InvalidFormat, "unexpected end of visual graph data")
                .addContext("expected", expected)
                .addContext("line", std::to_string(lineNumber_ + 1U)));
    }

    [[nodiscard]] Result<void> requireEnd() {
        std::string line;
        while (std::getline(stream_, line)) {
            ++lineNumber_;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            const auto first = line.find_first_not_of(" \t");
            if (first != std::string::npos && line[first] != '#') {
                return Result<void>::failure(
                    Error(ErrorCode::InvalidFormat,
                          "unexpected data after visual graph terminator")
                        .addContext("line", std::to_string(lineNumber_)));
            }
        }
        return Result<void>::success();
    }

    [[nodiscard]] std::size_t lineNumber() const noexcept {
        return lineNumber_;
    }

  private:
    std::istringstream stream_;
    std::size_t lineNumber_ = 0U;
};

Error parseError(const LineReader& reader, std::string message) {
    return Error(ErrorCode::InvalidFormat, std::move(message))
        .addContext("line", std::to_string(reader.lineNumber()));
}

bool streamFinished(std::istringstream& stream) {
    stream >> std::ws;
    return stream.eof();
}

template <typename Integer> std::optional<Integer> parseInteger(const std::string_view token) {
    Integer value{};
    const auto* begin = token.data();
    const auto* end = begin + token.size();
    const auto parsed = std::from_chars(begin, end, value, 10);
    if (parsed.ec != std::errc{} || parsed.ptr != end)
        return std::nullopt;
    return value;
}

std::string encodeControlCharacters(const std::string_view value) {
    std::string encoded;
    encoded.reserve(value.size());
    for (const auto character : value) {
        switch (character) {
        case '\\':
            encoded += "\\\\";
            break;
        case '\n':
            encoded += "\\n";
            break;
        case '\r':
            encoded += "\\r";
            break;
        case '\t':
            encoded += "\\t";
            break;
        default:
            encoded += character;
            break;
        }
    }
    return encoded;
}

Result<std::string> decodeControlCharacters(const std::string_view value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (auto index = std::size_t{0}; index < value.size(); ++index) {
        if (value[index] != '\\') {
            decoded += value[index];
            continue;
        }
        ++index;
        if (index >= value.size()) {
            return Result<std::string>::failure(
                Error(ErrorCode::InvalidFormat, "unterminated visual graph string escape"));
        }
        switch (value[index]) {
        case '\\':
            decoded += '\\';
            break;
        case 'n':
            decoded += '\n';
            break;
        case 'r':
            decoded += '\r';
            break;
        case 't':
            decoded += '\t';
            break;
        default:
            return Result<std::string>::failure(
                Error(ErrorCode::InvalidFormat, "unsupported visual graph string escape")
                    .addContext("escape", std::string(1U, value[index])));
        }
    }
    return Result<std::string>::success(std::move(decoded));
}

Result<std::string> parseToken(LineReader& reader, const char* expectedKey) {
    auto line = reader.next(expectedKey);
    if (!line)
        return Result<std::string>::failure(line.error());
    std::istringstream stream(line.value());
    stream.imbue(std::locale::classic());
    std::string key;
    std::string token;
    if (!(stream >> key >> token) || key != expectedKey || !streamFinished(stream)) {
        return Result<std::string>::failure(
            parseError(reader, std::string("expected '") + expectedKey + " <value>'"));
    }
    return Result<std::string>::success(std::move(token));
}

Result<std::string> parseQuoted(LineReader& reader, const char* expectedKey,
                                const std::size_t maximumBytes) {
    auto line = reader.next(expectedKey);
    if (!line)
        return Result<std::string>::failure(line.error());
    std::istringstream stream(line.value());
    stream.imbue(std::locale::classic());
    std::string key;
    std::string encoded;
    if (!(stream >> key >> std::quoted(encoded)) || key != expectedKey ||
        !streamFinished(stream)) {
        return Result<std::string>::failure(
            parseError(reader, std::string("expected '") + expectedKey + " \"text\"'"));
    }
    auto decoded = decodeControlCharacters(encoded);
    if (!decoded) {
        return Result<std::string>::failure(
            decoded.error().withContext("line", std::to_string(reader.lineNumber())));
    }
    if (decoded.value().size() > maximumBytes) {
        return Result<std::string>::failure(
            parseError(reader, std::string(expectedKey) + " exceeds the string limit")
                .addContext("maximum", std::to_string(maximumBytes)));
    }
    return decoded;
}

Result<std::size_t> parseCount(LineReader& reader, const char* expectedKey,
                               const std::size_t maximum) {
    auto token = parseToken(reader, expectedKey);
    if (!token)
        return Result<std::size_t>::failure(token.error());
    const auto parsed = parseInteger<std::uint64_t>(token.value());
    if (!parsed || *parsed > static_cast<std::uint64_t>(maximum) ||
        *parsed > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return Result<std::size_t>::failure(
            parseError(reader, std::string(expectedKey) + " is invalid or exceeds the limit")
                .addContext("maximum", std::to_string(maximum)));
    }
    return Result<std::size_t>::success(static_cast<std::size_t>(*parsed));
}

Result<std::uint16_t> parseId(LineReader& reader, const char* expectedKey) {
    auto token = parseToken(reader, expectedKey);
    if (!token)
        return Result<std::uint16_t>::failure(token.error());
    const auto parsed = parseInteger<std::uint32_t>(token.value());
    if (!parsed || *parsed == 0U || *parsed > std::numeric_limits<std::uint16_t>::max()) {
        return Result<std::uint16_t>::failure(
            parseError(reader, std::string(expectedKey) + " must be a non-zero uint16"));
    }
    return Result<std::uint16_t>::success(static_cast<std::uint16_t>(*parsed));
}

Result<bool> parseBoolean(LineReader& reader, const char* expectedKey) {
    auto token = parseToken(reader, expectedKey);
    if (!token)
        return Result<bool>::failure(token.error());
    if (token.value() != "0" && token.value() != "1") {
        return Result<bool>::failure(
            parseError(reader, std::string(expectedKey) + " must be 0 or 1"));
    }
    return Result<bool>::success(token.value() == "1");
}

Result<double> parseNumber(LineReader& reader, const char* expectedKey) {
    auto line = reader.next(expectedKey);
    if (!line)
        return Result<double>::failure(line.error());
    std::istringstream stream(line.value());
    stream.imbue(std::locale::classic());
    std::string key;
    double value = 0.0;
    if (!(stream >> key >> value) || key != expectedKey || !streamFinished(stream) ||
        !std::isfinite(value)) {
        return Result<double>::failure(
            parseError(reader, std::string(expectedKey) + " must be a finite number"));
    }
    return Result<double>::success(value == 0.0 ? 0.0 : value);
}

Result<VisualNodeLayout> parseLayout(LineReader& reader) {
    auto line = reader.next("layout");
    if (!line)
        return Result<VisualNodeLayout>::failure(line.error());
    std::istringstream stream(line.value());
    stream.imbue(std::locale::classic());
    std::string key;
    VisualNodeLayout layout;
    if (!(stream >> key >> layout.x >> layout.y >> layout.width >> layout.height) ||
        key != "layout" || !streamFinished(stream) || !std::isfinite(layout.x) ||
        !std::isfinite(layout.y) || !std::isfinite(layout.width) ||
        !std::isfinite(layout.height) || layout.width <= 0.0F || layout.height <= 0.0F) {
        return Result<VisualNodeLayout>::failure(
            parseError(reader, "layout must contain four finite values and a positive size"));
    }
    if (layout.x == 0.0F)
        layout.x = 0.0F;
    if (layout.y == 0.0F)
        layout.y = 0.0F;
    return Result<VisualNodeLayout>::success(layout);
}

Result<void> expectLiteral(LineReader& reader, const char* literal) {
    auto line = reader.next(literal);
    if (!line)
        return Result<void>::failure(line.error());
    if (line.value() != literal) {
        return Result<void>::failure(
            parseError(reader, std::string("expected '") + literal + "'"));
    }
    return Result<void>::success();
}

template <typename GuidType>
Result<GuidType> parseRequiredGuid(LineReader& reader, const char* expectedKey) {
    auto token = parseToken(reader, expectedKey);
    if (!token)
        return Result<GuidType>::failure(token.error());
    auto guid = GuidType::parse(token.value());
    if (!guid) {
        return Result<GuidType>::failure(
            guid.error().withContext("line", std::to_string(reader.lineNumber())));
    }
    if (guid.value().isNil()) {
        return Result<GuidType>::failure(
            parseError(reader, std::string(expectedKey) + " cannot be nil"));
    }
    return guid;
}

template <typename GuidType>
Result<std::optional<GuidType>> parseOptionalGuid(LineReader& reader, const char* expectedKey) {
    auto token = parseToken(reader, expectedKey);
    if (!token)
        return Result<std::optional<GuidType>>::failure(token.error());
    if (token.value() == "nil")
        return Result<std::optional<GuidType>>::success(std::nullopt);
    auto guid = GuidType::parse(token.value());
    if (!guid) {
        return Result<std::optional<GuidType>>::failure(
            guid.error().withContext("line", std::to_string(reader.lineNumber())));
    }
    if (guid.value().isNil()) {
        return Result<std::optional<GuidType>>::failure(
            parseError(reader, std::string(expectedKey) + " cannot contain an explicit nil GUID"));
    }
    return Result<std::optional<GuidType>>::success(guid.value());
}

std::string_view valueTypeTag(const VisualValueType type) noexcept {
    switch (type) {
    case VisualValueType::Flow:
        return "flow";
    case VisualValueType::Number:
        return "number";
    case VisualValueType::Boolean:
        return "boolean";
    }
    return "unknown";
}

std::optional<VisualValueType> parseValueType(const std::string_view tag) noexcept {
    if (tag == "flow")
        return VisualValueType::Flow;
    if (tag == "number")
        return VisualValueType::Number;
    if (tag == "boolean")
        return VisualValueType::Boolean;
    return std::nullopt;
}

std::string_view directionTag(const VisualPinDirection direction) noexcept {
    switch (direction) {
    case VisualPinDirection::Input:
        return "input";
    case VisualPinDirection::Output:
        return "output";
    }
    return "unknown";
}

std::optional<VisualPinDirection> parseDirection(const std::string_view tag) noexcept {
    if (tag == "input")
        return VisualPinDirection::Input;
    if (tag == "output")
        return VisualPinDirection::Output;
    return std::nullopt;
}

Result<VisualPin> parsePin(LineReader& reader, const std::size_t maximumStringBytes) {
    auto line = reader.next("pin");
    if (!line)
        return Result<VisualPin>::failure(line.error());
    std::istringstream stream(line.value());
    stream.imbue(std::locale::classic());
    std::string key;
    std::string idToken;
    std::string encodedName;
    std::string typeToken;
    std::string directionToken;
    if (!(stream >> key >> idToken >> std::quoted(encodedName) >> typeToken >> directionToken) ||
        key != "pin" || !streamFinished(stream)) {
        return Result<VisualPin>::failure(
            parseError(reader, "expected 'pin <id> \"name\" <type> <direction>'"));
    }
    const auto parsedId = parseInteger<std::uint32_t>(idToken);
    const auto type = parseValueType(typeToken);
    const auto direction = parseDirection(directionToken);
    auto name = decodeControlCharacters(encodedName);
    if (!parsedId || *parsedId == 0U || *parsedId > std::numeric_limits<std::uint16_t>::max() ||
        !type || !direction || !name || name.value().empty() ||
        name.value().size() > maximumStringBytes) {
        return Result<VisualPin>::failure(parseError(reader, "visual pin record is invalid"));
    }
    return Result<VisualPin>::success(
        {static_cast<VisualPinId>(*parsedId), std::move(name.value()), *type, *direction});
}

Result<VisualEdge> parseEdge(LineReader& reader) {
    auto line = reader.next("edge");
    if (!line)
        return Result<VisualEdge>::failure(line.error());
    std::istringstream stream(line.value());
    stream.imbue(std::locale::classic());
    std::string key;
    std::string sourceNode;
    std::string sourcePin;
    std::string targetNode;
    std::string targetPin;
    if (!(stream >> key >> sourceNode >> sourcePin >> targetNode >> targetPin) || key != "edge" ||
        !streamFinished(stream)) {
        return Result<VisualEdge>::failure(
            parseError(reader, "expected 'edge <source-node> <source-pin> <target-node> <target-pin>'"));
    }
    const auto parsedSourceNode = parseInteger<std::uint32_t>(sourceNode);
    const auto parsedSourcePin = parseInteger<std::uint32_t>(sourcePin);
    const auto parsedTargetNode = parseInteger<std::uint32_t>(targetNode);
    const auto parsedTargetPin = parseInteger<std::uint32_t>(targetPin);
    constexpr auto Maximum = std::numeric_limits<std::uint16_t>::max();
    if (!parsedSourceNode || !parsedSourcePin || !parsedTargetNode || !parsedTargetPin ||
        *parsedSourceNode == 0U || *parsedSourcePin == 0U || *parsedTargetNode == 0U ||
        *parsedTargetPin == 0U || *parsedSourceNode > Maximum || *parsedSourcePin > Maximum ||
        *parsedTargetNode > Maximum || *parsedTargetPin > Maximum) {
        return Result<VisualEdge>::failure(parseError(reader, "visual edge IDs are invalid"));
    }
    return Result<VisualEdge>::success(
        {static_cast<VisualNodeId>(*parsedSourceNode), static_cast<VisualPinId>(*parsedSourcePin),
         static_cast<VisualNodeId>(*parsedTargetNode), static_cast<VisualPinId>(*parsedTargetPin)});
}

const VisualNodeDefinition* definitionForNode(const VisualNode& node,
                                              const VisualNodeRegistry& registry) noexcept {
    return node.builtinType == VisualBuiltinNodeType::Legacy ? registry.findLegacy(node.kind)
                                                             : registry.find(node.builtinType);
}

bool validLimits(const VisualGraphFormatLimits& limits) noexcept {
    return limits.maximumSourceBytes > 0U && limits.maximumNodes > 0U &&
           limits.maximumNodes <= std::numeric_limits<std::uint16_t>::max() &&
           limits.maximumEdges > 0U && limits.maximumPins > 0U &&
           limits.maximumComments <= std::numeric_limits<std::uint16_t>::max() &&
           limits.maximumStringBytes > 0U;
}

bool finiteLayout(const VisualNodeLayout& layout) noexcept {
    return std::isfinite(layout.x) && std::isfinite(layout.y) &&
           std::isfinite(layout.width) && std::isfinite(layout.height) &&
           layout.width > 0.0F && layout.height > 0.0F;
}

bool validOptionalGuid(const std::optional<AssetGuid>& value) noexcept {
    return !value || !value->isNil();
}

bool validOptionalGuid(const std::optional<EntityGuid>& value) noexcept {
    return !value || !value->isNil();
}

bool validOptionalGuid(const std::optional<ComponentTypeGuid>& value) noexcept {
    return !value || !value->isNil();
}

bool stringFits(const std::string_view value, const VisualGraphFormatLimits& limits) noexcept {
    return value.size() <= limits.maximumStringBytes;
}

Result<void> validateForFormat(const VisualGraph& graph, const VisualNodeRegistry& registry,
                               const VisualGraphFormatLimits& limits) {
    if (!validLimits(limits)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "visual graph format limits are invalid"));
    }
    if (graph.guid().isNil() || graph.name().empty() || !stringFits(graph.name(), limits)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidFormat, "visual graph GUID or name is invalid"));
    }
    if (graph.nodes().size() > limits.maximumNodes || graph.edges().size() > limits.maximumEdges ||
        graph.comments().size() > limits.maximumComments) {
        return Result<void>::failure(
            Error(ErrorCode::CapacityExceeded, "visual graph exceeds a format record limit"));
    }
    auto pinCount = std::size_t{0};
    for (const auto& pair : graph.nodes()) {
        const auto& node = pair.second;
        const auto* definition = definitionForNode(node, registry);
        if (definition == nullptr) {
            return Result<void>::failure(
                Error(ErrorCode::NotFound, "visual graph contains an unknown node type")
                    .addContext("node", std::to_string(node.id)));
        }
        if (node.pins.size() > limits.maximumPins - pinCount) {
            return Result<void>::failure(
                Error(ErrorCode::CapacityExceeded, "visual graph exceeds the pin limit"));
        }
        pinCount += node.pins.size();
        if (pinCount > limits.maximumPins || !std::isfinite(node.numberValue) ||
            !finiteLayout(node.layout) || !stringFits(node.name, limits) ||
            !stringFits(node.variableName, limits) || !stringFits(node.callbackName, limits) ||
            !stringFits(node.callbackPayload, limits) ||
            !stringFits(definition->stableName, limits) || !validOptionalGuid(node.assetReference) ||
            !validOptionalGuid(node.entityReference) || !validOptionalGuid(node.componentReference)) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidFormat, "visual node cannot be represented in v1")
                    .addContext("node", std::to_string(node.id)));
        }
        for (const auto& pin : node.pins) {
            if (!stringFits(pin.name, limits)) {
                return Result<void>::failure(
                    Error(ErrorCode::CapacityExceeded, "visual pin name exceeds the string limit")
                        .addContext("node", std::to_string(node.id)));
            }
        }
    }
    for (const auto& comment : graph.comments()) {
        if (!stringFits(comment.title, limits) || !finiteLayout(comment.layout)) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidFormat, "visual comment cannot be represented in v1")
                    .addContext("comment", std::to_string(comment.id)));
        }
    }
    const auto report = VisualGraphValidator::validate(graph, {}, registry);
    if (report.hasErrors()) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidFormat, "visual graph validation failed before serialization"));
    }
    return Result<void>::success();
}

void writeQuoted(std::ostringstream& stream, const char* key, const std::string_view value) {
    stream << key << ' ' << std::quoted(encodeControlCharacters(value)) << '\n';
}

void writeLayout(std::ostringstream& stream, const VisualNodeLayout& layout) {
    const auto oldPrecision = stream.precision();
    stream << std::setprecision(std::numeric_limits<float>::max_digits10) << "layout "
           << (layout.x == 0.0F ? 0.0F : layout.x) << ' '
           << (layout.y == 0.0F ? 0.0F : layout.y) << ' ' << layout.width << ' ' << layout.height
           << '\n';
    stream.precision(oldPrecision);
}

template <typename GuidType>
void writeOptionalGuid(std::ostringstream& stream, const char* key,
                       const std::optional<GuidType>& guid) {
    stream << key << ' ' << (guid ? guid->toString() : std::string("nil")) << '\n';
}

Result<void> validateParsedGraph(const VisualGraph& graph, const VisualNodeRegistry& registry,
                                 const VisualGraphFormatLimits& limits) {
    auto valid = validateForFormat(graph, registry, limits);
    if (!valid && valid.error().code() == ErrorCode::CapacityExceeded)
        return valid;
    if (!valid) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidFormat, "deserialized visual graph is invalid")
                .addContext("reason", valid.error().message()));
    }
    return Result<void>::success();
}

} // namespace

Result<std::string> serializeVisualGraph(const VisualGraph& graph,
                                         const VisualNodeRegistry& registry,
                                         const VisualGraphFormatLimits& limits) {
    auto valid = validateForFormat(graph, registry, limits);
    if (!valid)
        return Result<std::string>::failure(valid.error());

    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << "fglvisual " << VisualGraphFormatVersion << '\n';
    stream << "graph_guid " << graph.guid().toString() << '\n';
    writeQuoted(stream, "graph_name", graph.name());
    stream << "entry " << graph.entryNode() << '\n';

    std::vector<const VisualCommentBox*> comments;
    comments.reserve(graph.comments().size());
    for (const auto& comment : graph.comments())
        comments.push_back(&comment);
    std::sort(comments.begin(), comments.end(), [](const auto* lhs, const auto* rhs) {
        return lhs->id < rhs->id;
    });
    stream << "comment_count " << comments.size() << '\n';
    for (const auto* comment : comments) {
        stream << "comment_begin\n";
        stream << "id " << comment->id << '\n';
        writeQuoted(stream, "title", comment->title);
        writeLayout(stream, comment->layout);
        stream << "comment_end\n";
    }

    stream << "node_count " << graph.nodes().size() << '\n';
    for (const auto& pair : graph.nodes()) {
        const auto& node = pair.second;
        const auto* definition = definitionForNode(node, registry);
        if (definition == nullptr) {
            return Result<std::string>::failure(
                Error(ErrorCode::NotFound, "visual node type disappeared during serialization"));
        }
        stream << "node_begin\n";
        stream << "id " << node.id << '\n';
        writeQuoted(stream, "type", definition->stableName);
        writeQuoted(stream, "name", node.name);
        writeLayout(stream, node.layout);
        stream << std::setprecision(std::numeric_limits<double>::max_digits10) << "number "
               << (node.numberValue == 0.0 ? 0.0 : node.numberValue) << '\n';
        stream << "boolean " << (node.booleanValue ? 1 : 0) << '\n';
        writeQuoted(stream, "variable", node.variableName);
        writeQuoted(stream, "callback", node.callbackName);
        writeQuoted(stream, "payload", node.callbackPayload);
        writeOptionalGuid(stream, "asset", node.assetReference);
        writeOptionalGuid(stream, "entity", node.entityReference);
        writeOptionalGuid(stream, "component", node.componentReference);
        stream << "pin_count " << node.pins.size() << '\n';
        for (const auto& pin : node.pins) {
            stream << "pin " << pin.id << ' ' << std::quoted(encodeControlCharacters(pin.name))
                   << ' ' << valueTypeTag(pin.type) << ' ' << directionTag(pin.direction) << '\n';
        }
        stream << "node_end\n";
    }

    auto edges = graph.edges();
    std::sort(edges.begin(), edges.end(), [](const VisualEdge& lhs, const VisualEdge& rhs) {
        return std::tie(lhs.sourceNode, lhs.sourcePin, lhs.targetNode, lhs.targetPin) <
               std::tie(rhs.sourceNode, rhs.sourcePin, rhs.targetNode, rhs.targetPin);
    });
    stream << "edge_count " << edges.size() << '\n';
    for (const auto& edge : edges) {
        stream << "edge " << edge.sourceNode << ' ' << edge.sourcePin << ' ' << edge.targetNode
               << ' ' << edge.targetPin << '\n';
    }
    stream << "graph_end\n";
    auto output = stream.str();
    if (output.size() > limits.maximumSourceBytes) {
        return Result<std::string>::failure(
            Error(ErrorCode::CapacityExceeded, "serialized visual graph exceeds the source limit"));
    }
    return Result<std::string>::success(std::move(output));
}

Result<VisualGraph> deserializeVisualGraph(const std::string_view text,
                                           const VisualNodeRegistry& registry,
                                           const VisualGraphFormatLimits& limits) {
    if (!validLimits(limits)) {
        return Result<VisualGraph>::failure(
            Error(ErrorCode::InvalidArgument, "visual graph format limits are invalid"));
    }
    if (text.size() > limits.maximumSourceBytes) {
        return Result<VisualGraph>::failure(
            Error(ErrorCode::CapacityExceeded, "visual graph source exceeds the byte limit"));
    }

    LineReader reader(text);
    auto header = reader.next("fglvisual <version>");
    if (!header)
        return Result<VisualGraph>::failure(header.error());
    std::istringstream headerStream(header.value());
    headerStream.imbue(std::locale::classic());
    std::string magic;
    std::string versionToken;
    if (!(headerStream >> magic >> versionToken) || magic != "fglvisual" ||
        !streamFinished(headerStream)) {
        return Result<VisualGraph>::failure(
            parseError(reader, "expected 'fglvisual <version>'"));
    }
    const auto version = parseInteger<std::uint32_t>(versionToken);
    if (!version) {
        return Result<VisualGraph>::failure(
            parseError(reader, "visual graph version is invalid"));
    }
    if (*version != VisualGraphFormatVersion) {
        return Result<VisualGraph>::failure(
            Error(ErrorCode::UnsupportedVersion, "unsupported visual graph format version")
                .addContext("version", std::to_string(*version)));
    }

    auto guid = parseRequiredGuid<AssetGuid>(reader, "graph_guid");
    if (!guid)
        return Result<VisualGraph>::failure(guid.error());
    auto name = parseQuoted(reader, "graph_name", limits.maximumStringBytes);
    if (!name)
        return Result<VisualGraph>::failure(name.error());
    if (name.value().empty()) {
        return Result<VisualGraph>::failure(parseError(reader, "graph_name cannot be empty"));
    }
    auto entry = parseId(reader, "entry");
    if (!entry)
        return Result<VisualGraph>::failure(entry.error());

    VisualGraph graph;
    graph.setGuid(guid.value());
    graph.setName(std::move(name.value()));
    graph.setEntryNode(entry.value());

    auto commentCount = parseCount(reader, "comment_count", limits.maximumComments);
    if (!commentCount)
        return Result<VisualGraph>::failure(commentCount.error());
    for (auto index = std::size_t{0}; index < commentCount.value(); ++index) {
        auto begin = expectLiteral(reader, "comment_begin");
        if (!begin)
            return Result<VisualGraph>::failure(begin.error());
        auto id = parseId(reader, "id");
        if (!id)
            return Result<VisualGraph>::failure(id.error());
        auto title = parseQuoted(reader, "title", limits.maximumStringBytes);
        if (!title)
            return Result<VisualGraph>::failure(title.error());
        auto layout = parseLayout(reader);
        if (!layout)
            return Result<VisualGraph>::failure(layout.error());
        auto end = expectLiteral(reader, "comment_end");
        if (!end)
            return Result<VisualGraph>::failure(end.error());
        auto added = graph.addCommentBox({id.value(), std::move(title.value()), layout.value()});
        if (!added) {
            return Result<VisualGraph>::failure(
                parseError(reader, "visual comment record is invalid")
                    .addContext("reason", added.error().message()));
        }
    }

    auto nodeCount = parseCount(reader, "node_count", limits.maximumNodes);
    if (!nodeCount)
        return Result<VisualGraph>::failure(nodeCount.error());
    auto totalPins = std::size_t{0};
    for (auto index = std::size_t{0}; index < nodeCount.value(); ++index) {
        auto begin = expectLiteral(reader, "node_begin");
        if (!begin)
            return Result<VisualGraph>::failure(begin.error());
        auto id = parseId(reader, "id");
        if (!id)
            return Result<VisualGraph>::failure(id.error());
        auto stableType = parseQuoted(reader, "type", limits.maximumStringBytes);
        if (!stableType)
            return Result<VisualGraph>::failure(stableType.error());
        const auto* definition = registry.find(stableType.value());
        if (definition == nullptr) {
            return Result<VisualGraph>::failure(
                Error(ErrorCode::NotFound, "visual graph node type is not registered")
                    .addContext("type", stableType.value())
                    .addContext("line", std::to_string(reader.lineNumber())));
        }
        auto nodeName = parseQuoted(reader, "name", limits.maximumStringBytes);
        if (!nodeName)
            return Result<VisualGraph>::failure(nodeName.error());
        auto layout = parseLayout(reader);
        if (!layout)
            return Result<VisualGraph>::failure(layout.error());
        auto number = parseNumber(reader, "number");
        if (!number)
            return Result<VisualGraph>::failure(number.error());
        auto boolean = parseBoolean(reader, "boolean");
        if (!boolean)
            return Result<VisualGraph>::failure(boolean.error());
        auto variable = parseQuoted(reader, "variable", limits.maximumStringBytes);
        if (!variable)
            return Result<VisualGraph>::failure(variable.error());
        auto callback = parseQuoted(reader, "callback", limits.maximumStringBytes);
        if (!callback)
            return Result<VisualGraph>::failure(callback.error());
        auto payload = parseQuoted(reader, "payload", limits.maximumStringBytes);
        if (!payload)
            return Result<VisualGraph>::failure(payload.error());
        auto asset = parseOptionalGuid<AssetGuid>(reader, "asset");
        if (!asset)
            return Result<VisualGraph>::failure(asset.error());
        auto entity = parseOptionalGuid<EntityGuid>(reader, "entity");
        if (!entity)
            return Result<VisualGraph>::failure(entity.error());
        auto component = parseOptionalGuid<ComponentTypeGuid>(reader, "component");
        if (!component)
            return Result<VisualGraph>::failure(component.error());
        auto pinCount = parseCount(reader, "pin_count", limits.maximumPins);
        if (!pinCount)
            return Result<VisualGraph>::failure(pinCount.error());
        if (pinCount.value() > limits.maximumPins - totalPins) {
            return Result<VisualGraph>::failure(
                parseError(reader, "visual graph exceeds the cumulative pin limit"));
        }
        totalPins += pinCount.value();
        if (totalPins > limits.maximumPins) {
            return Result<VisualGraph>::failure(
                parseError(reader, "visual graph exceeds the cumulative pin limit"));
        }

        VisualNode node;
        node.id = id.value();
        node.kind = definition->legacyKind.value_or(VisualNodeKind::ConstantNumber);
        node.builtinType = definition->type;
        node.name = std::move(nodeName.value());
        node.layout = layout.value();
        node.numberValue = number.value();
        node.booleanValue = boolean.value();
        node.variableName = std::move(variable.value());
        node.callbackName = std::move(callback.value());
        node.callbackPayload = std::move(payload.value());
        node.assetReference = std::move(asset.value());
        node.entityReference = std::move(entity.value());
        node.componentReference = std::move(component.value());
        node.pins.reserve(pinCount.value());
        for (auto pinIndex = std::size_t{0}; pinIndex < pinCount.value(); ++pinIndex) {
            auto pin = parsePin(reader, limits.maximumStringBytes);
            if (!pin)
                return Result<VisualGraph>::failure(pin.error());
            node.pins.push_back(std::move(pin.value()));
        }
        auto end = expectLiteral(reader, "node_end");
        if (!end)
            return Result<VisualGraph>::failure(end.error());
        if (node.pins.size() != definition->pins.size()) {
            return Result<VisualGraph>::failure(
                parseError(reader, "visual node pin count does not match its registered type"));
        }
        for (auto pinIndex = std::size_t{0}; pinIndex < node.pins.size(); ++pinIndex) {
            const auto& actual = node.pins[pinIndex];
            const auto& expected = definition->pins[pinIndex];
            if (actual.id != expected.id || actual.name != expected.name ||
                actual.type != expected.type || actual.direction != expected.direction) {
                return Result<VisualGraph>::failure(
                    parseError(reader, "visual node pin schema does not match its registered type"));
            }
        }
        auto added = graph.addNode(std::move(node));
        if (!added) {
            return Result<VisualGraph>::failure(
                parseError(reader, "visual node record is invalid")
                    .addContext("reason", added.error().message()));
        }
    }

    auto edgeCount = parseCount(reader, "edge_count", limits.maximumEdges);
    if (!edgeCount)
        return Result<VisualGraph>::failure(edgeCount.error());
    for (auto index = std::size_t{0}; index < edgeCount.value(); ++index) {
        auto edge = parseEdge(reader);
        if (!edge)
            return Result<VisualGraph>::failure(edge.error());
        auto added = graph.addEdge(edge.value());
        if (!added) {
            return Result<VisualGraph>::failure(
                parseError(reader, "visual edge record is invalid")
                    .addContext("reason", added.error().message()));
        }
    }
    auto terminator = expectLiteral(reader, "graph_end");
    if (!terminator)
        return Result<VisualGraph>::failure(terminator.error());
    auto finished = reader.requireEnd();
    if (!finished)
        return Result<VisualGraph>::failure(finished.error());
    auto valid = validateParsedGraph(graph, registry, limits);
    if (!valid)
        return Result<VisualGraph>::failure(valid.error());
    return Result<VisualGraph>::success(std::move(graph));
}

} // namespace fabgl
