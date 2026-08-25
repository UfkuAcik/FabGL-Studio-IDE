#include "fabgl/serialization/scene_serializer.h"

#include "fabgl/scene/builtin_components.h"
#include "fabgl/scene/scene.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fabgl {
namespace {

constexpr std::uint64_t MaximumRecordCount = 100000U;

struct PropertyRecord final {
    const PropertyMetadata* metadata = nullptr;
    PropertyValue value;
};

struct ComponentRecord final {
    ComponentTypeGuid typeId;
    const TypeMetadata* metadata = nullptr;
    bool enabled = true;
    std::vector<PropertyRecord> properties;
};

struct EntityRecord final {
    EntityGuid id;
    std::string name;
    bool active = true;
    std::optional<EntityGuid> parent;
    Vec3 position;
    Vec3 rotation;
    Vec3 scale{1.0F, 1.0F, 1.0F};
    std::vector<ComponentRecord> components;
};

class LineReader final {
  public:
    explicit LineReader(std::string_view text) : stream_(std::string(text)) {}

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
            return Result<std::string>::success(line.substr(first, last - first + 1));
        }
        return Result<std::string>::failure(
            Error(ErrorCode::InvalidFormat, "unexpected end of scene data")
                .addContext("expected", expected)
                .addContext("line", std::to_string(lineNumber_ + 1)));
    }

    [[nodiscard]] std::size_t lineNumber() const noexcept {
        return lineNumber_;
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
                    Error(ErrorCode::InvalidFormat, "unexpected data after scene_end")
                        .addContext("line", std::to_string(lineNumber_)));
            }
        }
        return Result<void>::success();
    }

  private:
    std::istringstream stream_;
    std::size_t lineNumber_ = 0;
};

Error parseError(const LineReader& reader, std::string message) {
    return Error(ErrorCode::InvalidFormat, std::move(message))
        .addContext("line", std::to_string(reader.lineNumber()));
}

bool streamFinished(std::istringstream& stream) {
    stream >> std::ws;
    return stream.eof();
}

template <typename Integer> std::optional<Integer> parseInteger(std::string_view token) {
    Integer value{};
    const auto* begin = token.data();
    const auto* end = begin + token.size();
    const auto parsed = std::from_chars(begin, end, value, 10);
    if (parsed.ec != std::errc{} || parsed.ptr != end)
        return std::nullopt;
    return value;
}

Result<std::string> parseSingleToken(LineReader& reader, const char* expectedKey) {
    auto line = reader.next(expectedKey);
    if (!line)
        return Result<std::string>::failure(line.error());
    std::istringstream stream(line.value());
    std::string key;
    std::string value;
    if (!(stream >> key >> value) || key != expectedKey || !streamFinished(stream)) {
        return Result<std::string>::failure(
            parseError(reader, std::string("expected '") + expectedKey + " <value>'"));
    }
    return Result<std::string>::success(std::move(value));
}

Result<std::size_t> parseCount(LineReader& reader, const char* expectedKey) {
    auto token = parseSingleToken(reader, expectedKey);
    if (!token)
        return Result<std::size_t>::failure(token.error());
    const auto parsed = parseInteger<std::uint64_t>(token.value());
    if (!parsed || *parsed > MaximumRecordCount) {
        return Result<std::size_t>::failure(
            parseError(reader, std::string(expectedKey) + " is invalid or exceeds the limit")
                .addContext("maximum", std::to_string(MaximumRecordCount)));
    }
    return Result<std::size_t>::success(static_cast<std::size_t>(*parsed));
}

Result<bool> parseBoolean(LineReader& reader, const char* expectedKey) {
    auto token = parseSingleToken(reader, expectedKey);
    if (!token)
        return Result<bool>::failure(token.error());
    if (token.value() != "0" && token.value() != "1") {
        return Result<bool>::failure(
            parseError(reader, std::string(expectedKey) + " must be 0 or 1"));
    }
    return Result<bool>::success(token.value() == "1");
}

std::string encodeControlCharacters(std::string_view value) {
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

Result<std::string> decodeControlCharacters(std::string_view value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] != '\\') {
            decoded += value[index];
            continue;
        }
        if (++index >= value.size()) {
            return Result<std::string>::failure(
                Error(ErrorCode::InvalidFormat, "unterminated string escape"));
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
                Error(ErrorCode::InvalidFormat, "unsupported string escape")
                    .addContext("escape", std::string(1, value[index])));
        }
    }
    return Result<std::string>::success(std::move(decoded));
}

Result<std::string> parseQuotedValue(LineReader& reader, const char* expectedKey) {
    auto line = reader.next(expectedKey);
    if (!line)
        return Result<std::string>::failure(line.error());
    std::istringstream stream(line.value());
    std::string key;
    std::string encoded;
    if (!(stream >> key >> std::quoted(encoded)) || key != expectedKey || !streamFinished(stream)) {
        return Result<std::string>::failure(
            parseError(reader, std::string("expected '") + expectedKey + " \"text\"'"));
    }
    auto decoded = decodeControlCharacters(encoded);
    if (!decoded) {
        return Result<std::string>::failure(
            decoded.error().withContext("line", std::to_string(reader.lineNumber())));
    }
    return decoded;
}

Result<Vec3> parseVec3(LineReader& reader, const char* expectedKey) {
    auto line = reader.next(expectedKey);
    if (!line)
        return Result<Vec3>::failure(line.error());
    std::istringstream stream(line.value());
    std::string key;
    Vec3 value;
    if (!(stream >> key >> value.x >> value.y >> value.z) || key != expectedKey ||
        !streamFinished(stream) || !std::isfinite(value.x) || !std::isfinite(value.y) ||
        !std::isfinite(value.z)) {
        return Result<Vec3>::failure(
            parseError(reader, std::string("expected finite Vec3 for '") + expectedKey + "'"));
    }
    return Result<Vec3>::success(value);
}

Result<void> expectLiteral(LineReader& reader, const char* literal) {
    auto line = reader.next(literal);
    if (!line)
        return Result<void>::failure(line.error());
    if (line.value() != literal) {
        return Result<void>::failure(parseError(reader, std::string("expected '") + literal + "'"));
    }
    return Result<void>::success();
}

template <typename GuidType>
Result<GuidType> parseGuidToken(const std::string& value, LineReader& reader, const char* field) {
    auto guid = GuidType::parse(value);
    if (!guid) {
        return Result<GuidType>::failure(
            guid.error()
                .withContext("field", field)
                .withContext("line", std::to_string(reader.lineNumber())));
    }
    if (guid.value().isNil()) {
        return Result<GuidType>::failure(parseError(reader, std::string(field) + " cannot be nil"));
    }
    return guid;
}

template <typename GuidType>
Result<GuidType> parseNullableGuid(std::string_view token, const LineReader& reader,
                                   std::string_view propertyName) {
    if (token == "nil")
        return Result<GuidType>::success(GuidType{});
    auto parsed = GuidType::parse(token);
    if (!parsed) {
        return Result<GuidType>::failure(
            parsed.error()
                .withContext("property", std::string(propertyName))
                .withContext("line", std::to_string(reader.lineNumber())));
    }
    return parsed;
}

std::string_view propertyTypeTag(PropertyType type) {
    switch (type) {
    case PropertyType::Boolean:
        return "bool";
    case PropertyType::SignedInteger:
        return "sint";
    case PropertyType::UnsignedInteger:
        return "uint";
    case PropertyType::Float:
        return "float";
    case PropertyType::Fixed:
        return "fixed";
    case PropertyType::String:
        return "string";
    case PropertyType::Enumeration:
        return "enum";
    case PropertyType::BitFlags:
        return "flags";
    case PropertyType::Vec2:
        return "vec2";
    case PropertyType::Vec3:
        return "vec3";
    case PropertyType::EulerAngles:
        return "euler";
    case PropertyType::Quaternion:
        return "quat";
    case PropertyType::Rect:
        return "rect";
    case PropertyType::Color:
        return "color";
    case PropertyType::AssetReference:
        return "asset";
    case PropertyType::EntityReference:
        return "entity";
    case PropertyType::ComponentReference:
        return "component";
    case PropertyType::List:
        return "list";
    case PropertyType::Curve:
        return "curve";
    case PropertyType::AnimationCurve:
        return "animation_curve";
    case PropertyType::ActionReference:
        return "action";
    case PropertyType::EventReference:
        return "event";
    }
    return "unknown";
}

Error propertyValueError(const LineReader& reader, std::string_view propertyName) {
    return parseError(reader, "invalid property value")
        .addContext("property", std::string(propertyName));
}

Result<PropertyListElement> parseListElement(LineReader& reader, std::istringstream& stream,
                                             PropertyType type, std::string_view propertyName) {
    const auto invalid = [&]() {
        return Result<PropertyListElement>::failure(propertyValueError(reader, propertyName));
    };
    std::string token;
    auto readFloat = [&]() -> std::optional<double> {
        double value = 0.0;
        if (!(stream >> value) || !std::isfinite(value))
            return std::nullopt;
        return value;
    };
    switch (type) {
    case PropertyType::Boolean:
        if (!(stream >> token) || (token != "0" && token != "1"))
            return invalid();
        return Result<PropertyListElement>::success(token == "1");
    case PropertyType::SignedInteger:
    case PropertyType::Enumeration: {
        if (!(stream >> token))
            return invalid();
        const auto value = parseInteger<std::int64_t>(token);
        return value ? Result<PropertyListElement>::success(*value) : invalid();
    }
    case PropertyType::UnsignedInteger:
    case PropertyType::BitFlags: {
        if (!(stream >> token))
            return invalid();
        const auto value = parseInteger<std::uint64_t>(token);
        return value ? Result<PropertyListElement>::success(*value) : invalid();
    }
    case PropertyType::Float: {
        const auto value = readFloat();
        return value ? Result<PropertyListElement>::success(*value) : invalid();
    }
    case PropertyType::Fixed: {
        if (!(stream >> token))
            return invalid();
        const auto value = parseInteger<std::int32_t>(token);
        return value ? Result<PropertyListElement>::success(Fixed::fromRaw(*value)) : invalid();
    }
    case PropertyType::String: {
        std::string encoded;
        if (!(stream >> std::quoted(encoded)))
            return invalid();
        auto decoded = decodeControlCharacters(encoded);
        if (!decoded || decoded.value().size() > MaximumPropertyStringLength)
            return invalid();
        return Result<PropertyListElement>::success(std::move(decoded.value()));
    }
    case PropertyType::Vec2: {
        const auto x = readFloat();
        const auto y = readFloat();
        if (!x || !y)
            return invalid();
        return Result<PropertyListElement>::success(Vec2{static_cast<float>(*x),
                                                         static_cast<float>(*y)});
    }
    case PropertyType::Vec3: {
        const auto x = readFloat();
        const auto y = readFloat();
        const auto z = readFloat();
        if (!x || !y || !z)
            return invalid();
        return Result<PropertyListElement>::success(
            Vec3{static_cast<float>(*x), static_cast<float>(*y), static_cast<float>(*z)});
    }
    case PropertyType::EulerAngles: {
        const auto x = readFloat();
        const auto y = readFloat();
        const auto z = readFloat();
        if (!x || !y || !z)
            return invalid();
        return Result<PropertyListElement>::success(EulerAngles{
            static_cast<float>(*x), static_cast<float>(*y), static_cast<float>(*z)});
    }
    case PropertyType::Quaternion: {
        const auto x = readFloat();
        const auto y = readFloat();
        const auto z = readFloat();
        const auto w = readFloat();
        if (!x || !y || !z || !w)
            return invalid();
        return Result<PropertyListElement>::success(Quaternion{static_cast<float>(*x),
                                                               static_cast<float>(*y),
                                                               static_cast<float>(*z),
                                                               static_cast<float>(*w)});
    }
    case PropertyType::Rect: {
        const auto x = readFloat();
        const auto y = readFloat();
        const auto width = readFloat();
        const auto height = readFloat();
        if (!x || !y || !width || !height)
            return invalid();
        return Result<PropertyListElement>::success(
            Rect{static_cast<float>(*x), static_cast<float>(*y), static_cast<float>(*width),
                 static_cast<float>(*height)});
    }
    case PropertyType::Color: {
        std::string red;
        std::string green;
        std::string blue;
        std::string alpha;
        if (!(stream >> red >> green >> blue >> alpha))
            return invalid();
        const auto r = parseInteger<unsigned int>(red);
        const auto g = parseInteger<unsigned int>(green);
        const auto b = parseInteger<unsigned int>(blue);
        const auto a = parseInteger<unsigned int>(alpha);
        if (!r || !g || !b || !a || *r > 255U || *g > 255U || *b > 255U || *a > 255U)
            return invalid();
        return Result<PropertyListElement>::success(
            Color{static_cast<std::uint8_t>(*r), static_cast<std::uint8_t>(*g),
                  static_cast<std::uint8_t>(*b), static_cast<std::uint8_t>(*a)});
    }
    case PropertyType::AssetReference: {
        if (!(stream >> token))
            return invalid();
        auto value = parseNullableGuid<AssetGuid>(token, reader, propertyName);
        return value ? Result<PropertyListElement>::success(value.value())
                     : Result<PropertyListElement>::failure(value.error());
    }
    case PropertyType::EntityReference: {
        if (!(stream >> token))
            return invalid();
        auto value = parseNullableGuid<EntityGuid>(token, reader, propertyName);
        return value ? Result<PropertyListElement>::success(value.value())
                     : Result<PropertyListElement>::failure(value.error());
    }
    case PropertyType::ComponentReference: {
        std::string entityToken;
        std::string componentToken;
        if (!(stream >> entityToken >> componentToken))
            return invalid();
        auto entity = parseNullableGuid<EntityGuid>(entityToken, reader, propertyName);
        auto component = parseNullableGuid<ComponentTypeGuid>(componentToken, reader, propertyName);
        if (!entity)
            return Result<PropertyListElement>::failure(entity.error());
        if (!component)
            return Result<PropertyListElement>::failure(component.error());
        return Result<PropertyListElement>::success(
            ComponentReference{entity.value(), component.value()});
    }
    case PropertyType::ActionReference:
    case PropertyType::EventReference: {
        std::string encoded;
        if (!(stream >> std::quoted(encoded)))
            return invalid();
        auto decoded = decodeControlCharacters(encoded);
        if (!decoded || decoded.value().size() > MaximumActionOrEventNameLength)
            return invalid();
        if (type == PropertyType::ActionReference)
            return Result<PropertyListElement>::success(ActionReference{std::move(decoded.value())});
        return Result<PropertyListElement>::success(EventReference{std::move(decoded.value())});
    }
    case PropertyType::List:
    case PropertyType::Curve:
    case PropertyType::AnimationCurve:
        return invalid();
    }
    return invalid();
}

Result<PropertyValue> parsePropertyValue(LineReader& reader, std::istringstream& stream,
                                         const PropertyMetadata& metadata) {
    switch (metadata.type) {
    case PropertyType::Boolean: {
        std::string token;
        if (!(stream >> token) || !streamFinished(stream) || (token != "0" && token != "1"))
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        return Result<PropertyValue>::success(PropertyValue(token == "1"));
    }
    case PropertyType::SignedInteger:
    case PropertyType::Enumeration: {
        std::string token;
        if (!(stream >> token) || !streamFinished(stream))
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        const auto value = parseInteger<std::int64_t>(token);
        if (!value)
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        return Result<PropertyValue>::success(PropertyValue(*value));
    }
    case PropertyType::UnsignedInteger:
    case PropertyType::BitFlags: {
        std::string token;
        if (!(stream >> token) || !streamFinished(stream))
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        const auto value = parseInteger<std::uint64_t>(token);
        if (!value)
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        return Result<PropertyValue>::success(PropertyValue(*value));
    }
    case PropertyType::Float: {
        double value = 0.0;
        if (!(stream >> value) || !streamFinished(stream) || !std::isfinite(value))
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        return Result<PropertyValue>::success(PropertyValue(value));
    }
    case PropertyType::Fixed: {
        std::string token;
        if (!(stream >> token) || !streamFinished(stream))
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        const auto value = parseInteger<std::int32_t>(token);
        if (!value)
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        return Result<PropertyValue>::success(PropertyValue(Fixed::fromRaw(*value)));
    }
    case PropertyType::String: {
        std::string encoded;
        if (!(stream >> std::quoted(encoded)) || !streamFinished(stream))
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        auto value = decodeControlCharacters(encoded);
        if (!value) {
            return Result<PropertyValue>::failure(
                value.error()
                    .withContext("property", metadata.name)
                    .withContext("line", std::to_string(reader.lineNumber())));
        }
        if (value.value().size() > MaximumPropertyStringLength)
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        return Result<PropertyValue>::success(PropertyValue(std::move(value.value())));
    }
    case PropertyType::Vec2: {
        Vec2 value;
        if (!(stream >> value.x >> value.y) || !streamFinished(stream) || !std::isfinite(value.x) ||
            !std::isfinite(value.y)) {
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        }
        return Result<PropertyValue>::success(PropertyValue(value));
    }
    case PropertyType::Vec3: {
        Vec3 value;
        if (!(stream >> value.x >> value.y >> value.z) || !streamFinished(stream) ||
            !std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z)) {
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        }
        return Result<PropertyValue>::success(PropertyValue(value));
    }
    case PropertyType::EulerAngles: {
        EulerAngles value;
        if (!(stream >> value.x >> value.y >> value.z) || !streamFinished(stream) ||
            !std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        return Result<PropertyValue>::success(PropertyValue(value));
    }
    case PropertyType::Quaternion: {
        Quaternion value;
        if (!(stream >> value.x >> value.y >> value.z >> value.w) || !streamFinished(stream) ||
            !std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z) ||
            !std::isfinite(value.w))
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        return Result<PropertyValue>::success(PropertyValue(value));
    }
    case PropertyType::Rect: {
        Rect value;
        if (!(stream >> value.x >> value.y >> value.width >> value.height) ||
            !streamFinished(stream) || !std::isfinite(value.x) || !std::isfinite(value.y) ||
            !std::isfinite(value.width) || !std::isfinite(value.height)) {
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        }
        return Result<PropertyValue>::success(PropertyValue(value));
    }
    case PropertyType::Color: {
        std::string red;
        std::string green;
        std::string blue;
        std::string alpha;
        if (!(stream >> red >> green >> blue >> alpha) || !streamFinished(stream))
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        const auto r = parseInteger<std::uint32_t>(red);
        const auto g = parseInteger<std::uint32_t>(green);
        const auto b = parseInteger<std::uint32_t>(blue);
        const auto a = parseInteger<std::uint32_t>(alpha);
        if (!r || !g || !b || !a || *r > 255U || *g > 255U || *b > 255U || *a > 255U)
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        return Result<PropertyValue>::success(
            PropertyValue(Color{static_cast<std::uint8_t>(*r), static_cast<std::uint8_t>(*g),
                                static_cast<std::uint8_t>(*b), static_cast<std::uint8_t>(*a)}));
    }
    case PropertyType::AssetReference: {
        std::string token;
        if (!(stream >> token) || !streamFinished(stream))
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        auto value = parseNullableGuid<AssetGuid>(token, reader, metadata.name);
        if (!value)
            return Result<PropertyValue>::failure(value.error());
        return Result<PropertyValue>::success(PropertyValue(value.value()));
    }
    case PropertyType::EntityReference: {
        std::string token;
        if (!(stream >> token) || !streamFinished(stream))
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        auto value = parseNullableGuid<EntityGuid>(token, reader, metadata.name);
        if (!value)
            return Result<PropertyValue>::failure(value.error());
        return Result<PropertyValue>::success(PropertyValue(value.value()));
    }
    case PropertyType::ComponentReference: {
        std::string entityToken;
        std::string componentToken;
        if (!(stream >> entityToken >> componentToken) || !streamFinished(stream))
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        auto entity = parseNullableGuid<EntityGuid>(entityToken, reader, metadata.name);
        auto component = parseNullableGuid<ComponentTypeGuid>(componentToken, reader, metadata.name);
        if (!entity)
            return Result<PropertyValue>::failure(entity.error());
        if (!component)
            return Result<PropertyValue>::failure(component.error());
        return Result<PropertyValue>::success(
            PropertyValue(ComponentReference{entity.value(), component.value()}));
    }
    case PropertyType::List: {
        std::string elementTag;
        std::string countToken;
        if (!(stream >> elementTag >> countToken) ||
            elementTag != propertyTypeTag(metadata.listElementType))
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        const auto count = parseInteger<std::size_t>(countToken);
        if (!count || *count > MaximumPropertyListItems)
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        PropertyList list{metadata.listElementType, {}};
        list.values.reserve(*count);
        for (std::size_t index = 0; index < *count; ++index) {
            auto element = parseListElement(reader, stream, metadata.listElementType, metadata.name);
            if (!element)
                return Result<PropertyValue>::failure(element.error());
            list.values.push_back(std::move(element.value()));
        }
        if (!streamFinished(stream))
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        return Result<PropertyValue>::success(PropertyValue(std::move(list)));
    }
    case PropertyType::Curve: {
        std::string countToken;
        if (!(stream >> countToken))
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        const auto count = parseInteger<std::size_t>(countToken);
        if (!count || *count > MaximumCurvePoints)
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        Curve curve;
        curve.points.reserve(*count);
        for (std::size_t index = 0; index < *count; ++index) {
            CurvePoint point;
            if (!(stream >> point.position >> point.value) || !std::isfinite(point.position) ||
                !std::isfinite(point.value))
                return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
            curve.points.push_back(point);
        }
        if (!streamFinished(stream))
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        return Result<PropertyValue>::success(PropertyValue(std::move(curve)));
    }
    case PropertyType::AnimationCurve: {
        std::string countToken;
        if (!(stream >> countToken))
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        const auto count = parseInteger<std::size_t>(countToken);
        if (!count || *count > MaximumCurvePoints)
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        PropertyAnimationCurve curve;
        curve.keys.reserve(*count);
        for (std::size_t index = 0; index < *count; ++index) {
            AnimationCurveKey key;
            if (!(stream >> key.time >> key.value >> key.inTangent >> key.outTangent) ||
                !std::isfinite(key.time) || !std::isfinite(key.value) ||
                !std::isfinite(key.inTangent) || !std::isfinite(key.outTangent))
                return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
            curve.keys.push_back(key);
        }
        if (!streamFinished(stream))
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        return Result<PropertyValue>::success(PropertyValue(std::move(curve)));
    }
    case PropertyType::ActionReference:
    case PropertyType::EventReference: {
        std::string encoded;
        if (!(stream >> std::quoted(encoded)) || !streamFinished(stream))
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        auto decoded = decodeControlCharacters(encoded);
        if (!decoded || decoded.value().size() > MaximumActionOrEventNameLength)
            return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
        if (metadata.type == PropertyType::ActionReference)
            return Result<PropertyValue>::success(
                PropertyValue(ActionReference{std::move(decoded.value())}));
        return Result<PropertyValue>::success(
            PropertyValue(EventReference{std::move(decoded.value())}));
    }
    }
    return Result<PropertyValue>::failure(propertyValueError(reader, metadata.name));
}

Result<PropertyRecord> parseProperty(LineReader& reader, const TypeMetadata& typeMetadata) {
    auto line = reader.next("property");
    if (!line)
        return Result<PropertyRecord>::failure(line.error());
    std::istringstream stream(line.value());
    std::string key;
    std::string propertyName;
    std::string typeTag;
    if (!(stream >> key >> std::quoted(propertyName) >> typeTag) || key != "property") {
        return Result<PropertyRecord>::failure(
            parseError(reader, "expected 'property \"name\" <type> <value>'"));
    }

    const auto* metadata = typeMetadata.findProperty(propertyName);
    if (metadata == nullptr || !hasFlag(metadata->flags, PropertyFlags::Serialize)) {
        return Result<PropertyRecord>::failure(
            Error(ErrorCode::NotFound, "serialized component property is not registered")
                .addContext("property", propertyName)
                .addContext("component", typeMetadata.name)
                .addContext("line", std::to_string(reader.lineNumber())));
    }
    const auto expectedTag = propertyTypeTag(metadata->type);
    const bool legacyEuler = metadata->type == PropertyType::EulerAngles && typeTag == "vec3";
    if (typeTag != expectedTag && !legacyEuler) {
        return Result<PropertyRecord>::failure(
            Error(ErrorCode::TypeMismatch, "serialized property type does not match metadata")
                .addContext("property", propertyName)
                .addContext("component", typeMetadata.name)
                .addContext("expected", std::string(expectedTag))
                .addContext("actual", typeTag)
                .addContext("line", std::to_string(reader.lineNumber())));
    }
    auto value = parsePropertyValue(reader, stream, *metadata);
    if (!value)
        return Result<PropertyRecord>::failure(value.error());
    auto valid = validatePropertyValue(*metadata, value.value());
    if (!valid) {
        return Result<PropertyRecord>::failure(
            valid.error().withContext("line", std::to_string(reader.lineNumber())));
    }
    return Result<PropertyRecord>::success({metadata, std::move(value.value())});
}

std::size_t serializablePropertyCount(const TypeMetadata& metadata) {
    std::size_t count = 0;
    for (const auto& property : metadata.properties) {
        if (hasFlag(property.flags, PropertyFlags::Serialize))
            ++count;
    }
    return count;
}

void writeListElement(std::ostringstream& stream, PropertyType type,
                      const PropertyListElement& value) {
    switch (type) {
    case PropertyType::Boolean:
        stream << (std::get<bool>(value) ? 1 : 0);
        break;
    case PropertyType::SignedInteger:
    case PropertyType::Enumeration:
        stream << std::get<std::int64_t>(value);
        break;
    case PropertyType::UnsignedInteger:
    case PropertyType::BitFlags:
        stream << std::get<std::uint64_t>(value);
        break;
    case PropertyType::Float:
        stream << std::get<double>(value);
        break;
    case PropertyType::Fixed:
        stream << std::get<Fixed>(value).raw();
        break;
    case PropertyType::String:
        stream << std::quoted(encodeControlCharacters(std::get<std::string>(value)));
        break;
    case PropertyType::Vec2: {
        const auto typed = std::get<Vec2>(value);
        stream << typed.x << ' ' << typed.y;
        break;
    }
    case PropertyType::Vec3: {
        const auto typed = std::get<Vec3>(value);
        stream << typed.x << ' ' << typed.y << ' ' << typed.z;
        break;
    }
    case PropertyType::EulerAngles: {
        const auto typed = std::get<EulerAngles>(value);
        stream << typed.x << ' ' << typed.y << ' ' << typed.z;
        break;
    }
    case PropertyType::Quaternion: {
        const auto typed = std::get<Quaternion>(value);
        stream << typed.x << ' ' << typed.y << ' ' << typed.z << ' ' << typed.w;
        break;
    }
    case PropertyType::Rect: {
        const auto typed = std::get<Rect>(value);
        stream << typed.x << ' ' << typed.y << ' ' << typed.width << ' ' << typed.height;
        break;
    }
    case PropertyType::Color: {
        const auto typed = std::get<Color>(value);
        stream << static_cast<unsigned int>(typed.r) << ' ' << static_cast<unsigned int>(typed.g)
               << ' ' << static_cast<unsigned int>(typed.b) << ' '
               << static_cast<unsigned int>(typed.a);
        break;
    }
    case PropertyType::AssetReference: {
        const auto typed = std::get<AssetGuid>(value);
        stream << (typed.isNil() ? "nil" : typed.toString());
        break;
    }
    case PropertyType::EntityReference: {
        const auto typed = std::get<EntityGuid>(value);
        stream << (typed.isNil() ? "nil" : typed.toString());
        break;
    }
    case PropertyType::ComponentReference: {
        const auto typed = std::get<ComponentReference>(value);
        stream << (typed.entity.isNil() ? "nil" : typed.entity.toString()) << ' '
               << (typed.component.isNil() ? "nil" : typed.component.toString());
        break;
    }
    case PropertyType::ActionReference:
        stream << std::quoted(
            encodeControlCharacters(std::get<ActionReference>(value).name));
        break;
    case PropertyType::EventReference:
        stream << std::quoted(
            encodeControlCharacters(std::get<EventReference>(value).name));
        break;
    case PropertyType::List:
    case PropertyType::Curve:
    case PropertyType::AnimationCurve:
        break;
    }
}

Result<void> writePropertyValue(std::ostringstream& stream, const PropertyMetadata& metadata,
                                const PropertyValue& value) {
    const auto mismatch = [&metadata] {
        return Result<void>::failure(
            Error(ErrorCode::TypeMismatch, "reflected property returned an incompatible value")
                .addContext("property", metadata.name)
                .addContext("expected", std::string(propertyTypeTag(metadata.type))));
    };

    auto valid = validatePropertyValue(metadata, value);
    if (!valid)
        return Result<void>::failure(valid.error());
    stream << propertyTypeTag(metadata.type) << ' ';
    switch (metadata.type) {
    case PropertyType::Boolean: {
        const auto* typed = std::get_if<bool>(&value);
        if (typed == nullptr)
            return mismatch();
        stream << (*typed ? 1 : 0);
        break;
    }
    case PropertyType::SignedInteger:
    case PropertyType::Enumeration: {
        const auto* typed = std::get_if<std::int64_t>(&value);
        if (typed == nullptr)
            return mismatch();
        stream << *typed;
        break;
    }
    case PropertyType::UnsignedInteger:
    case PropertyType::BitFlags: {
        const auto* typed = std::get_if<std::uint64_t>(&value);
        if (typed == nullptr)
            return mismatch();
        stream << *typed;
        break;
    }
    case PropertyType::Float: {
        const auto* typed = std::get_if<double>(&value);
        if (typed == nullptr)
            return mismatch();
        if (!std::isfinite(*typed)) {
            return Result<void>::failure(
                Error(ErrorCode::SerializationFailed, "cannot serialize a non-finite float")
                    .addContext("property", metadata.name));
        }
        stream << *typed;
        break;
    }
    case PropertyType::Fixed: {
        const auto* typed = std::get_if<Fixed>(&value);
        if (typed == nullptr)
            return mismatch();
        stream << typed->raw();
        break;
    }
    case PropertyType::String: {
        const auto* typed = std::get_if<std::string>(&value);
        if (typed == nullptr)
            return mismatch();
        stream << std::quoted(encodeControlCharacters(*typed));
        break;
    }
    case PropertyType::Vec2: {
        const auto* typed = std::get_if<Vec2>(&value);
        if (typed == nullptr)
            return mismatch();
        if (!std::isfinite(typed->x) || !std::isfinite(typed->y)) {
            return Result<void>::failure(
                Error(ErrorCode::SerializationFailed, "cannot serialize a non-finite Vec2")
                    .addContext("property", metadata.name));
        }
        stream << typed->x << ' ' << typed->y;
        break;
    }
    case PropertyType::Vec3: {
        const auto* typed = std::get_if<Vec3>(&value);
        if (typed == nullptr)
            return mismatch();
        if (!std::isfinite(typed->x) || !std::isfinite(typed->y) || !std::isfinite(typed->z)) {
            return Result<void>::failure(
                Error(ErrorCode::SerializationFailed, "cannot serialize a non-finite Vec3")
                    .addContext("property", metadata.name));
        }
        stream << typed->x << ' ' << typed->y << ' ' << typed->z;
        break;
    }
    case PropertyType::EulerAngles: {
        const auto* typed = std::get_if<EulerAngles>(&value);
        if (typed == nullptr)
            return mismatch();
        stream << typed->x << ' ' << typed->y << ' ' << typed->z;
        break;
    }
    case PropertyType::Quaternion: {
        const auto* typed = std::get_if<Quaternion>(&value);
        if (typed == nullptr)
            return mismatch();
        stream << typed->x << ' ' << typed->y << ' ' << typed->z << ' ' << typed->w;
        break;
    }
    case PropertyType::Rect: {
        const auto* typed = std::get_if<Rect>(&value);
        if (typed == nullptr)
            return mismatch();
        if (!std::isfinite(typed->x) || !std::isfinite(typed->y) || !std::isfinite(typed->width) ||
            !std::isfinite(typed->height)) {
            return Result<void>::failure(
                Error(ErrorCode::SerializationFailed, "cannot serialize a non-finite Rect")
                    .addContext("property", metadata.name));
        }
        stream << typed->x << ' ' << typed->y << ' ' << typed->width << ' ' << typed->height;
        break;
    }
    case PropertyType::Color: {
        const auto* typed = std::get_if<Color>(&value);
        if (typed == nullptr)
            return mismatch();
        stream << static_cast<unsigned int>(typed->r) << ' ' << static_cast<unsigned int>(typed->g)
               << ' ' << static_cast<unsigned int>(typed->b) << ' '
               << static_cast<unsigned int>(typed->a);
        break;
    }
    case PropertyType::AssetReference: {
        const auto* typed = std::get_if<AssetGuid>(&value);
        if (typed == nullptr)
            return mismatch();
        stream << (typed->isNil() ? "nil" : typed->toString());
        break;
    }
    case PropertyType::EntityReference: {
        const auto* typed = std::get_if<EntityGuid>(&value);
        if (typed == nullptr)
            return mismatch();
        stream << (typed->isNil() ? "nil" : typed->toString());
        break;
    }
    case PropertyType::ComponentReference: {
        const auto* typed = std::get_if<ComponentReference>(&value);
        if (typed == nullptr)
            return mismatch();
        stream << (typed->entity.isNil() ? "nil" : typed->entity.toString()) << ' '
               << (typed->component.isNil() ? "nil" : typed->component.toString());
        break;
    }
    case PropertyType::List: {
        const auto* typed = std::get_if<PropertyList>(&value);
        if (typed == nullptr)
            return mismatch();
        stream << propertyTypeTag(typed->elementType) << ' ' << typed->values.size();
        for (const auto& element : typed->values) {
            stream << ' ';
            writeListElement(stream, typed->elementType, element);
        }
        break;
    }
    case PropertyType::Curve: {
        const auto* typed = std::get_if<Curve>(&value);
        if (typed == nullptr)
            return mismatch();
        stream << typed->points.size();
        for (const auto& point : typed->points)
            stream << ' ' << point.position << ' ' << point.value;
        break;
    }
    case PropertyType::AnimationCurve: {
        const auto* typed = std::get_if<PropertyAnimationCurve>(&value);
        if (typed == nullptr)
            return mismatch();
        stream << typed->keys.size();
        for (const auto& key : typed->keys) {
            stream << ' ' << key.time << ' ' << key.value << ' ' << key.inTangent << ' '
                   << key.outTangent;
        }
        break;
    }
    case PropertyType::ActionReference: {
        const auto* typed = std::get_if<ActionReference>(&value);
        if (typed == nullptr)
            return mismatch();
        stream << std::quoted(encodeControlCharacters(typed->name));
        break;
    }
    case PropertyType::EventReference: {
        const auto* typed = std::get_if<EventReference>(&value);
        if (typed == nullptr)
            return mismatch();
        stream << std::quoted(encodeControlCharacters(typed->name));
        break;
    }
    }
    return Result<void>::success();
}

Result<const TypeMetadata*> validateSerializableComponent(const Component& component,
                                                          const ReflectionRegistry& registry,
                                                          EntityGuid entityId) {
    const auto* componentMetadata = component.metadata();
    if (componentMetadata == nullptr || componentMetadata->typeId != component.typeId()) {
        return Result<const TypeMetadata*>::failure(
            Error(ErrorCode::SerializationFailed,
                  "component has no valid reflection metadata for scene serialization")
                .addContext("entity", entityId.toString())
                .addContext("component", std::string(component.typeName())));
    }
    const auto* registered = registry.find(component.typeId());
    if (registered == nullptr) {
        return Result<const TypeMetadata*>::failure(
            Error(ErrorCode::SerializationFailed,
                  "custom or gameplay-script component has no registered scene factory")
                .addContext("entity", entityId.toString())
                .addContext("component", componentMetadata->name)
                .addContext("type_id", component.typeId().toString()));
    }
    if (componentMetadata->name != registered->name) {
        return Result<const TypeMetadata*>::failure(
            Error(ErrorCode::TypeMismatch, "component metadata name does not match registry")
                .addContext("entity", entityId.toString())
                .addContext("expected", registered->name)
                .addContext("actual", componentMetadata->name));
    }
    if (component.typeId() != TransformComponent::staticTypeId() &&
        dynamic_cast<const DataComponent*>(&component) == nullptr) {
        return Result<const TypeMetadata*>::failure(
            Error(ErrorCode::SerializationFailed,
                  "registered component does not use the safe builtin data-component factory")
                .addContext("entity", entityId.toString())
                .addContext("component", registered->name));
    }

    for (const auto& property : componentMetadata->properties) {
        if (!hasFlag(property.flags, PropertyFlags::Serialize))
            continue;
        const auto* registeredProperty = registered->findProperty(property.name);
        if (registeredProperty == nullptr ||
            !hasFlag(registeredProperty->flags, PropertyFlags::Serialize)) {
            return Result<const TypeMetadata*>::failure(
                Error(ErrorCode::SerializationFailed,
                      "component contains an unregistered serializable property")
                    .addContext("entity", entityId.toString())
                    .addContext("component", registered->name)
                    .addContext("property", property.name));
        }
        if (registeredProperty->type != property.type) {
            return Result<const TypeMetadata*>::failure(
                Error(ErrorCode::TypeMismatch, "component property type does not match registry")
                    .addContext("entity", entityId.toString())
                    .addContext("component", registered->name)
                    .addContext("property", property.name));
        }
    }
    for (const auto& property : registered->properties) {
        if (!hasFlag(property.flags, PropertyFlags::Serialize))
            continue;
        const auto* componentProperty = componentMetadata->findProperty(property.name);
        if (componentProperty == nullptr ||
            !hasFlag(componentProperty->flags, PropertyFlags::Serialize) ||
            componentProperty->type != property.type) {
            return Result<const TypeMetadata*>::failure(
                Error(ErrorCode::SerializationFailed,
                      "component is missing a registered serializable property")
                    .addContext("entity", entityId.toString())
                    .addContext("component", registered->name)
                    .addContext("property", property.name));
        }
    }
    return Result<const TypeMetadata*>::success(registered);
}

Result<std::vector<EntityRecord>> parseVersionOneEntities(LineReader& reader) {
    std::vector<EntityRecord> records;
    for (;;) {
        auto marker = reader.next("entity_begin or scene_end");
        if (!marker)
            return Result<std::vector<EntityRecord>>::failure(marker.error());
        if (marker.value() == "scene_end")
            break;
        if (marker.value() != "entity_begin") {
            return Result<std::vector<EntityRecord>>::failure(
                parseError(reader, "expected 'entity_begin' or 'scene_end'"));
        }

        EntityRecord record;
        auto guidToken = parseSingleToken(reader, "guid");
        if (!guidToken)
            return Result<std::vector<EntityRecord>>::failure(guidToken.error());
        auto guid = parseGuidToken<EntityGuid>(guidToken.value(), reader, "guid");
        if (!guid)
            return Result<std::vector<EntityRecord>>::failure(guid.error());
        record.id = guid.value();

        auto name = parseQuotedValue(reader, "name");
        if (!name)
            return Result<std::vector<EntityRecord>>::failure(name.error());
        record.name = std::move(name.value());

        auto active = parseBoolean(reader, "active");
        if (!active)
            return Result<std::vector<EntityRecord>>::failure(active.error());
        record.active = active.value();

        auto parent = parseSingleToken(reader, "parent");
        if (!parent)
            return Result<std::vector<EntityRecord>>::failure(parent.error());
        if (parent.value() != "nil") {
            auto parentGuid = parseGuidToken<EntityGuid>(parent.value(), reader, "parent");
            if (!parentGuid)
                return Result<std::vector<EntityRecord>>::failure(parentGuid.error());
            record.parent = parentGuid.value();
        }

        auto position = parseVec3(reader, "position");
        if (!position)
            return Result<std::vector<EntityRecord>>::failure(position.error());
        record.position = position.value();
        auto rotation = parseVec3(reader, "rotation");
        if (!rotation)
            return Result<std::vector<EntityRecord>>::failure(rotation.error());
        record.rotation = rotation.value();
        auto scale = parseVec3(reader, "scale");
        if (!scale)
            return Result<std::vector<EntityRecord>>::failure(scale.error());
        record.scale = scale.value();
        auto entityEnd = expectLiteral(reader, "entity_end");
        if (!entityEnd)
            return Result<std::vector<EntityRecord>>::failure(entityEnd.error());
        records.push_back(std::move(record));
    }
    return Result<std::vector<EntityRecord>>::success(std::move(records));
}

Result<ComponentRecord> parseVersionTwoComponent(LineReader& reader,
                                                 const ReflectionRegistry& registry,
                                                 EntityGuid entityId) {
    auto begin = expectLiteral(reader, "component_begin");
    if (!begin)
        return Result<ComponentRecord>::failure(begin.error());

    ComponentRecord record;
    auto typeIdToken = parseSingleToken(reader, "type_id");
    if (!typeIdToken)
        return Result<ComponentRecord>::failure(typeIdToken.error());
    auto typeId = parseGuidToken<ComponentTypeGuid>(typeIdToken.value(), reader, "type_id");
    if (!typeId)
        return Result<ComponentRecord>::failure(typeId.error());
    record.typeId = typeId.value();
    record.metadata = registry.find(record.typeId);
    if (record.metadata == nullptr) {
        return Result<ComponentRecord>::failure(
            Error(ErrorCode::NotFound, "serialized component type is not registered")
                .addContext("entity", entityId.toString())
                .addContext("type_id", record.typeId.toString())
                .addContext("line", std::to_string(reader.lineNumber())));
    }

    auto typeName = parseQuotedValue(reader, "type_name");
    if (!typeName)
        return Result<ComponentRecord>::failure(typeName.error());
    if (typeName.value() != record.metadata->name) {
        return Result<ComponentRecord>::failure(
            Error(ErrorCode::TypeMismatch, "serialized component name does not match its type ID")
                .addContext("entity", entityId.toString())
                .addContext("type_id", record.typeId.toString())
                .addContext("expected", record.metadata->name)
                .addContext("actual", typeName.value())
                .addContext("line", std::to_string(reader.lineNumber())));
    }

    auto enabled = parseBoolean(reader, "enabled");
    if (!enabled)
        return Result<ComponentRecord>::failure(enabled.error());
    record.enabled = enabled.value();

    auto propertyCount = parseCount(reader, "property_count");
    if (!propertyCount)
        return Result<ComponentRecord>::failure(propertyCount.error());
    std::unordered_set<std::string> propertyNames;
    record.properties.reserve(propertyCount.value());
    for (std::size_t index = 0; index < propertyCount.value(); ++index) {
        auto property = parseProperty(reader, *record.metadata);
        if (!property) {
            return Result<ComponentRecord>::failure(
                property.error()
                    .withContext("entity", entityId.toString())
                    .withContext("component", record.metadata->name));
        }
        const auto& propertyName = property.value().metadata->name;
        if (!propertyNames.insert(propertyName).second) {
            return Result<ComponentRecord>::failure(
                Error(ErrorCode::AlreadyExists, "serialized component repeats a property")
                    .addContext("entity", entityId.toString())
                    .addContext("component", record.metadata->name)
                    .addContext("property", propertyName)
                    .addContext("line", std::to_string(reader.lineNumber())));
        }
        record.properties.push_back(std::move(property.value()));
    }
    for (const auto& property : record.metadata->properties) {
        if (hasFlag(property.flags, PropertyFlags::Serialize) &&
            propertyNames.find(property.name) == propertyNames.end()) {
            return Result<ComponentRecord>::failure(
                Error(ErrorCode::InvalidFormat,
                      "serialized component is missing a required reflected property")
                    .addContext("entity", entityId.toString())
                    .addContext("component", record.metadata->name)
                    .addContext("property", property.name));
        }
    }
    auto end = expectLiteral(reader, "component_end");
    if (!end)
        return Result<ComponentRecord>::failure(end.error());
    return Result<ComponentRecord>::success(std::move(record));
}

Result<std::vector<EntityRecord>> parseVersionTwoEntities(LineReader& reader,
                                                          const ReflectionRegistry& registry) {
    std::vector<EntityRecord> records;
    for (;;) {
        auto marker = reader.next("entity_begin or scene_end");
        if (!marker)
            return Result<std::vector<EntityRecord>>::failure(marker.error());
        if (marker.value() == "scene_end")
            break;
        if (marker.value() != "entity_begin") {
            return Result<std::vector<EntityRecord>>::failure(
                parseError(reader, "expected 'entity_begin' or 'scene_end'"));
        }

        EntityRecord record;
        auto guidToken = parseSingleToken(reader, "guid");
        if (!guidToken)
            return Result<std::vector<EntityRecord>>::failure(guidToken.error());
        auto guid = parseGuidToken<EntityGuid>(guidToken.value(), reader, "guid");
        if (!guid)
            return Result<std::vector<EntityRecord>>::failure(guid.error());
        record.id = guid.value();

        auto name = parseQuotedValue(reader, "name");
        if (!name)
            return Result<std::vector<EntityRecord>>::failure(name.error());
        record.name = std::move(name.value());

        auto active = parseBoolean(reader, "active");
        if (!active)
            return Result<std::vector<EntityRecord>>::failure(active.error());
        record.active = active.value();

        auto parent = parseSingleToken(reader, "parent");
        if (!parent)
            return Result<std::vector<EntityRecord>>::failure(parent.error());
        if (parent.value() != "nil") {
            auto parentGuid = parseGuidToken<EntityGuid>(parent.value(), reader, "parent");
            if (!parentGuid)
                return Result<std::vector<EntityRecord>>::failure(parentGuid.error());
            record.parent = parentGuid.value();
        }

        auto componentCount = parseCount(reader, "component_count");
        if (!componentCount)
            return Result<std::vector<EntityRecord>>::failure(componentCount.error());
        std::unordered_set<ComponentTypeGuid, StrongGuidHash<ComponentTypeGuidTag>> componentTypes;
        record.components.reserve(componentCount.value());
        for (std::size_t index = 0; index < componentCount.value(); ++index) {
            auto component = parseVersionTwoComponent(reader, registry, record.id);
            if (!component)
                return Result<std::vector<EntityRecord>>::failure(component.error());
            if (!componentTypes.insert(component.value().typeId).second) {
                return Result<std::vector<EntityRecord>>::failure(
                    Error(ErrorCode::AlreadyExists, "entity repeats a serialized component type")
                        .addContext("entity", record.id.toString())
                        .addContext("component", component.value().metadata->name));
            }
            record.components.push_back(std::move(component.value()));
        }
        if (componentTypes.find(TransformComponent::staticTypeId()) == componentTypes.end()) {
            return Result<std::vector<EntityRecord>>::failure(
                Error(ErrorCode::InvalidFormat, "serialized entity is missing Transform")
                    .addContext("entity", record.id.toString()));
        }
        auto entityEnd = expectLiteral(reader, "entity_end");
        if (!entityEnd)
            return Result<std::vector<EntityRecord>>::failure(entityEnd.error());
        records.push_back(std::move(record));
    }
    return Result<std::vector<EntityRecord>>::success(std::move(records));
}

Result<void> validateEntityReferences(const std::vector<EntityRecord>& records) {
    std::unordered_set<EntityGuid, StrongGuidHash<EntityGuidTag>> entityIds;
    std::unordered_map<EntityGuid,
                       std::unordered_set<ComponentTypeGuid, StrongGuidHash<ComponentTypeGuidTag>>,
                       StrongGuidHash<EntityGuidTag>>
        componentTypes;
    for (const auto& record : records) {
        if (!entityIds.insert(record.id).second) {
            return Result<void>::failure(
                Error(ErrorCode::AlreadyExists, "scene repeats an entity GUID")
                    .addContext("entity", record.id.toString()));
        }
        auto& types = componentTypes[record.id];
        for (const auto& component : record.components)
            types.insert(component.typeId);
    }
    const auto validateEntity = [&entityIds](EntityGuid reference) {
        return reference.isNil() || entityIds.find(reference) != entityIds.end();
    };
    const auto validateComponent = [&componentTypes](const ComponentReference& reference) {
        if (reference.entity.isNil() && reference.component.isNil())
            return true;
        const auto entity = componentTypes.find(reference.entity);
        return entity != componentTypes.end() &&
               entity->second.find(reference.component) != entity->second.end();
    };
    for (const auto& entity : records) {
        for (const auto& component : entity.components) {
            for (const auto& property : component.properties) {
                bool valid = true;
                std::string referenceText;
                if (property.metadata->type == PropertyType::EntityReference) {
                    const auto referenced = std::get<EntityGuid>(property.value);
                    valid = validateEntity(referenced);
                    referenceText = referenced.toString();
                } else if (property.metadata->type == PropertyType::ComponentReference) {
                    const auto referenced = std::get<ComponentReference>(property.value);
                    valid = validateComponent(referenced);
                    referenceText = referenced.entity.toString() + "/" +
                                    referenced.component.toString();
                } else if (property.metadata->type == PropertyType::List) {
                    const auto& list = std::get<PropertyList>(property.value);
                    if (list.elementType == PropertyType::EntityReference) {
                        for (const auto& item : list.values) {
                            const auto referenced = std::get<EntityGuid>(item);
                            if (!validateEntity(referenced)) {
                                valid = false;
                                referenceText = referenced.toString();
                                break;
                            }
                        }
                    } else if (list.elementType == PropertyType::ComponentReference) {
                        for (const auto& item : list.values) {
                            const auto referenced = std::get<ComponentReference>(item);
                            if (!validateComponent(referenced)) {
                                valid = false;
                                referenceText = referenced.entity.toString() + "/" +
                                                referenced.component.toString();
                                break;
                            }
                        }
                    }
                }
                if (!valid) {
                    return Result<void>::failure(
                        Error(ErrorCode::NotFound,
                              "serialized property reference does not exist in the scene")
                            .addContext("entity", entity.id.toString())
                            .addContext("component", component.metadata->name)
                            .addContext("property", property.metadata->name)
                            .addContext("reference", referenceText));
                }
            }
        }
    }
    return Result<void>::success();
}

Result<void> applyVersionTwoComponents(Entity& entity, const EntityRecord& record,
                                       const ReflectionRegistry& registry) {
    for (const auto& componentRecord : record.components) {
        Component* component = nullptr;
        std::unique_ptr<DataComponent> dataComponent;
        if (componentRecord.typeId == TransformComponent::staticTypeId()) {
            component = &entity.transform();
        } else {
            auto created = createBuiltinDataComponent(registry, componentRecord.typeId);
            if (!created) {
                return Result<void>::failure(
                    created.error()
                        .withContext("entity", entity.id().toString())
                        .withContext("component", componentRecord.metadata->name));
            }
            dataComponent = std::move(created.value());
            component = dataComponent.get();
        }

        component->setEnabled(componentRecord.enabled);
        for (const auto& property : componentRecord.properties) {
            const auto* writableProperty =
                component->metadata()->findProperty(property.metadata->name);
            if (writableProperty == nullptr) {
                return Result<void>::failure(
                    Error(ErrorCode::NotFound,
                          "deserialized component instance is missing a reflected property")
                        .addContext("entity", entity.id().toString())
                        .addContext("component", componentRecord.metadata->name)
                        .addContext("property", property.metadata->name));
            }
            auto written = writableProperty->write(component, property.value);
            if (!written) {
                return Result<void>::failure(
                    written.error()
                        .withContext("entity", entity.id().toString())
                        .withContext("component", componentRecord.metadata->name)
                        .withContext("property", property.metadata->name));
            }
        }
        if (dataComponent) {
            auto attached = entity.addComponent(std::move(dataComponent));
            if (!attached) {
                return Result<void>::failure(
                    attached.error()
                        .withContext("entity", entity.id().toString())
                        .withContext("component", componentRecord.metadata->name));
            }
        }
    }
    return Result<void>::success();
}

Result<std::unique_ptr<Scene>> buildScene(std::string name, SceneGuid sceneId,
                                          const std::vector<EntityRecord>& records, int version,
                                          const ReflectionRegistry& registry) {
    auto references = validateEntityReferences(records);
    if (!references)
        return Result<std::unique_ptr<Scene>>::failure(references.error());

    auto scene = std::make_unique<Scene>(std::move(name), sceneId);
    for (const auto& record : records) {
        auto entity = scene->createEntity(record.name, record.id);
        if (!entity) {
            return Result<std::unique_ptr<Scene>>::failure(
                entity.error().withContext("operation", "entity creation during deserialization"));
        }
        entity.value()->setActive(record.active);
        if (version == 1) {
            entity.value()->transform().setLocalPosition(record.position);
            entity.value()->transform().setLocalRotation(record.rotation);
            entity.value()->transform().setLocalScale(record.scale);
        } else {
            auto applied = applyVersionTwoComponents(*entity.value(), record, registry);
            if (!applied)
                return Result<std::unique_ptr<Scene>>::failure(applied.error());
        }
    }
    for (const auto& record : records) {
        if (!record.parent)
            continue;
        auto parented = scene->setParent(record.id, *record.parent);
        if (!parented) {
            return Result<std::unique_ptr<Scene>>::failure(
                parented.error().withContext("entity", record.id.toString()));
        }
    }
    return Result<std::unique_ptr<Scene>>::success(std::move(scene));
}

} // namespace

Result<std::string> SceneSerializer::serialize(const Scene& scene) {
    if (scene.id().isNil()) {
        return Result<std::string>::failure(
            Error(ErrorCode::SerializationFailed, "scene GUID cannot be nil"));
    }

    ReflectionRegistry registry;
    auto registered = registerBuiltinComponentTypes(registry);
    if (!registered) {
        return Result<std::string>::failure(
            registered.error().withContext("operation", "building scene serializer registry"));
    }

    std::ostringstream stream;
    stream << std::setprecision(std::numeric_limits<double>::max_digits10);
    stream << "fglscene " << CurrentVersion << '\n';
    stream << "scene_guid " << scene.id().toString() << '\n';
    stream << "scene_name " << std::quoted(encodeControlCharacters(scene.name())) << '\n';

    for (const auto* entity : scene.entities()) {
        stream << "entity_begin\n";
        stream << "guid " << entity->id().toString() << '\n';
        stream << "name " << std::quoted(encodeControlCharacters(entity->name())) << '\n';
        stream << "active " << (entity->active() ? 1 : 0) << '\n';
        stream << "parent "
               << (entity->transform().parent() ? entity->transform().parent()->toString() : "nil")
               << '\n';
        const auto components = entity->components();
        stream << "component_count " << components.size() << '\n';
        for (const auto* component : components) {
            auto metadata = validateSerializableComponent(*component, registry, entity->id());
            if (!metadata)
                return Result<std::string>::failure(metadata.error());

            stream << "component_begin\n";
            stream << "type_id " << component->typeId().toString() << '\n';
            stream << "type_name " << std::quoted(metadata.value()->name) << '\n';
            stream << "enabled " << (component->enabled() ? 1 : 0) << '\n';
            stream << "property_count " << serializablePropertyCount(*metadata.value()) << '\n';
            for (const auto& property : metadata.value()->properties) {
                if (!hasFlag(property.flags, PropertyFlags::Serialize))
                    continue;
                const auto* readableProperty = component->metadata()->findProperty(property.name);
                if (readableProperty == nullptr) {
                    return Result<std::string>::failure(
                        Error(ErrorCode::NotFound,
                              "component instance is missing a reflected property reader")
                            .addContext("entity", entity->id().toString())
                            .addContext("component", metadata.value()->name)
                            .addContext("property", property.name));
                }
                auto value = readableProperty->read(component);
                if (!value) {
                    return Result<std::string>::failure(
                        value.error()
                            .withContext("entity", entity->id().toString())
                            .withContext("component", metadata.value()->name)
                            .withContext("property", property.name));
                }
                const auto entityReferenceValid = [&scene](EntityGuid reference) {
                    return reference.isNil() || scene.findEntity(reference) != nullptr;
                };
                const auto componentReferenceValid = [&scene](const ComponentReference& reference) {
                    if (reference.entity.isNil() && reference.component.isNil())
                        return true;
                    const auto* target = scene.findEntity(reference.entity);
                    return target != nullptr && target->getComponent(reference.component) != nullptr;
                };
                bool referenceValid = true;
                std::string referenceText;
                if (property.type == PropertyType::EntityReference) {
                    const auto reference = std::get<EntityGuid>(value.value());
                    referenceValid = entityReferenceValid(reference);
                    referenceText = reference.toString();
                } else if (property.type == PropertyType::ComponentReference) {
                    const auto reference = std::get<ComponentReference>(value.value());
                    referenceValid = componentReferenceValid(reference);
                    referenceText =
                        reference.entity.toString() + "/" + reference.component.toString();
                } else if (property.type == PropertyType::List) {
                    const auto& list = std::get<PropertyList>(value.value());
                    if (list.elementType == PropertyType::EntityReference) {
                        for (const auto& item : list.values) {
                            const auto reference = std::get<EntityGuid>(item);
                            if (!entityReferenceValid(reference)) {
                                referenceValid = false;
                                referenceText = reference.toString();
                                break;
                            }
                        }
                    } else if (list.elementType == PropertyType::ComponentReference) {
                        for (const auto& item : list.values) {
                            const auto reference = std::get<ComponentReference>(item);
                            if (!componentReferenceValid(reference)) {
                                referenceValid = false;
                                referenceText = reference.entity.toString() + "/" +
                                                reference.component.toString();
                                break;
                            }
                        }
                    }
                }
                if (!referenceValid) {
                    return Result<std::string>::failure(
                        Error(ErrorCode::NotFound,
                              "property reference points outside the scene or to a missing component")
                            .addContext("entity", entity->id().toString())
                            .addContext("component", metadata.value()->name)
                            .addContext("property", property.name)
                            .addContext("reference", referenceText));
                }
                stream << "property " << std::quoted(property.name) << ' ';
                auto written = writePropertyValue(stream, property, value.value());
                if (!written) {
                    return Result<std::string>::failure(
                        written.error()
                            .withContext("entity", entity->id().toString())
                            .withContext("component", metadata.value()->name));
                }
                stream << '\n';
            }
            stream << "component_end\n";
        }
        stream << "entity_end\n";
    }
    stream << "scene_end\n";
    return Result<std::string>::success(stream.str());
}

Result<std::unique_ptr<Scene>> SceneSerializer::deserialize(std::string_view text) {
    LineReader reader(text);
    auto header = reader.next("fglscene <version>");
    if (!header)
        return Result<std::unique_ptr<Scene>>::failure(header.error());

    int version = 0;
    {
        std::istringstream stream(header.value());
        std::string magic;
        if (!(stream >> magic >> version) || magic != "fglscene" || !streamFinished(stream)) {
            return Result<std::unique_ptr<Scene>>::failure(
                parseError(reader, "invalid scene header"));
        }
        if (version < 1 || version > CurrentVersion) {
            return Result<std::unique_ptr<Scene>>::failure(
                Error(ErrorCode::UnsupportedVersion, "unsupported scene format version")
                    .addContext("version", std::to_string(version))
                    .addContext("supported", "1-" + std::to_string(CurrentVersion)));
        }
    }

    auto sceneGuidToken = parseSingleToken(reader, "scene_guid");
    if (!sceneGuidToken)
        return Result<std::unique_ptr<Scene>>::failure(sceneGuidToken.error());
    auto sceneGuid = parseGuidToken<SceneGuid>(sceneGuidToken.value(), reader, "scene_guid");
    if (!sceneGuid)
        return Result<std::unique_ptr<Scene>>::failure(sceneGuid.error());
    auto sceneName = parseQuotedValue(reader, "scene_name");
    if (!sceneName)
        return Result<std::unique_ptr<Scene>>::failure(sceneName.error());

    ReflectionRegistry registry;
    auto registered = registerBuiltinComponentTypes(registry);
    if (!registered) {
        return Result<std::unique_ptr<Scene>>::failure(
            registered.error().withContext("operation", "building scene deserializer registry"));
    }

    Result<std::vector<EntityRecord>> records =
        version == 1 ? parseVersionOneEntities(reader) : parseVersionTwoEntities(reader, registry);
    if (!records)
        return Result<std::unique_ptr<Scene>>::failure(records.error());
    auto end = reader.requireEnd();
    if (!end)
        return Result<std::unique_ptr<Scene>>::failure(end.error());
    return buildScene(std::move(sceneName.value()), sceneGuid.value(), records.value(), version,
                      registry);
}

} // namespace fabgl
