#include "fabgl/serialization/prefab_serializer.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <type_traits>
#include <utility>

namespace fabgl {

namespace {

constexpr std::size_t MaximumInputBytes = 16U * 1024U * 1024U;
constexpr std::size_t MaximumLineBytes = 64U * 1024U;
constexpr std::size_t MaximumComponents = 4096U;
constexpr std::size_t MaximumProperties = 16384U;
constexpr std::size_t MaximumEntities = 4096U;

class LineReader final {
  public:
    explicit LineReader(std::string_view text) : stream_(std::string(text)) {
        stream_.imbue(std::locale::classic());
    }

    [[nodiscard]] bool next(std::string& line) {
        while (std::getline(stream_, line)) {
            ++lineNumber_;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.size() > MaximumLineBytes) {
                oversized_ = true;
                return false;
            }
            const auto first = line.find_first_not_of(" \t");
            if (first == std::string::npos || line[first] == '#')
                continue;
            return true;
        }
        return false;
    }

    [[nodiscard]] std::size_t lineNumber() const noexcept {
        return lineNumber_;
    }
    [[nodiscard]] bool oversized() const noexcept {
        return oversized_;
    }

  private:
    std::istringstream stream_;
    std::size_t lineNumber_ = 0U;
    bool oversized_ = false;
};

Error parseError(const LineReader& reader, std::string message) {
    return Error(ErrorCode::InvalidFormat, std::move(message))
        .addContext("line", std::to_string(reader.lineNumber()));
}

bool streamFinished(std::istringstream& stream) {
    stream >> std::ws;
    return stream.eof();
}

std::string encodeString(std::string_view value) {
    std::string encoded;
    encoded.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '\n':
            encoded += "\\n";
            break;
        case '\r':
            encoded += "\\r";
            break;
        case '\t':
            encoded += "\\t";
            break;
        case '\\':
            encoded += "\\\\";
            break;
        default:
            encoded.push_back(character);
            break;
        }
    }
    return encoded;
}

Result<std::string> decodeString(std::string_view encoded, const LineReader& reader) {
    std::string decoded;
    decoded.reserve(encoded.size());
    for (std::size_t index = 0; index < encoded.size(); ++index) {
        if (encoded[index] != '\\') {
            decoded.push_back(encoded[index]);
            continue;
        }
        if (++index == encoded.size())
            return Result<std::string>::failure(parseError(reader, "unterminated string escape"));
        switch (encoded[index]) {
        case 'n':
            decoded.push_back('\n');
            break;
        case 'r':
            decoded.push_back('\r');
            break;
        case 't':
            decoded.push_back('\t');
            break;
        case '\\':
            decoded.push_back('\\');
            break;
        default:
            return Result<std::string>::failure(
                parseError(reader, "unsupported string escape")
                    .addContext("escape", std::string(1, encoded[index])));
        }
    }
    return Result<std::string>::success(std::move(decoded));
}

template <typename Integer> std::optional<Integer> parseInteger(std::string_view token) {
    Integer value{};
    const auto* begin = token.data();
    const auto* end = token.data() + token.size();
    const auto parsed = std::from_chars(begin, end, value, 10);
    if (parsed.ec != std::errc{} || parsed.ptr != end)
        return std::nullopt;
    return value;
}

template <typename Float> std::optional<Float> parseFloat(std::string_view token) {
    Float value{};
    const auto* begin = token.data();
    const auto* end = token.data() + token.size();
    const auto parsed = std::from_chars(begin, end, value, std::chars_format::general);
    if (parsed.ec != std::errc{} || parsed.ptr != end || !std::isfinite(value))
        return std::nullopt;
    return value;
}

Result<std::string> readLine(LineReader& reader, std::string_view expectedKey) {
    std::string line;
    if (!reader.next(line)) {
        return Result<std::string>::failure(
            parseError(reader, reader.oversized() ? "prefab line exceeds the size limit"
                                                  : "unexpected end of prefab"));
    }
    std::istringstream stream(line);
    stream.imbue(std::locale::classic());
    std::string key;
    std::string value;
    if (!(stream >> key >> value) || key != expectedKey || !streamFinished(stream)) {
        return Result<std::string>::failure(parseError(reader, "invalid prefab field")
                                                .addContext("field", std::string(expectedKey)));
    }
    return Result<std::string>::success(std::move(value));
}

Result<std::string> readQuotedLine(LineReader& reader, std::string_view expectedKey) {
    std::string line;
    if (!reader.next(line))
        return Result<std::string>::failure(parseError(reader, "unexpected end of prefab"));
    std::istringstream stream(line);
    stream.imbue(std::locale::classic());
    std::string key;
    std::string encoded;
    if (!(stream >> key >> std::quoted(encoded)) || key != expectedKey || !streamFinished(stream)) {
        return Result<std::string>::failure(parseError(reader, "invalid quoted prefab field")
                                                .addContext("field", std::string(expectedKey)));
    }
    return decodeString(encoded, reader);
}

Result<void> expectLine(LineReader& reader, std::string_view expected) {
    std::string line;
    if (!reader.next(line) || line != expected) {
        return Result<void>::failure(parseError(reader, "expected prefab marker")
                                         .addContext("marker", std::string(expected)));
    }
    return Result<void>::success();
}

template <typename Guid>
Result<Guid> parseRequiredGuid(std::string_view token, const LineReader& reader,
                               std::string_view field) {
    auto parsed = Guid::parse(token);
    if (!parsed || parsed.value().isNil()) {
        return Result<Guid>::failure(
            parseError(reader, "invalid prefab GUID").addContext("field", std::string(field)));
    }
    return parsed;
}

template <typename Guid>
Result<std::optional<Guid>> parseOptionalGuid(std::string_view token, const LineReader& reader,
                                              std::string_view field) {
    if (token == "nil")
        return Result<std::optional<Guid>>::success(std::nullopt);
    auto parsed = parseRequiredGuid<Guid>(token, reader, field);
    if (!parsed)
        return Result<std::optional<Guid>>::failure(parsed.error());
    return Result<std::optional<Guid>>::success(parsed.value());
}

Result<std::size_t> parseCount(LineReader& reader, std::string_view key, std::size_t maximum) {
    auto token = readLine(reader, key);
    if (!token)
        return Result<std::size_t>::failure(token.error());
    const auto count = parseInteger<std::size_t>(token.value());
    if (!count || *count > maximum) {
        return Result<std::size_t>::failure(
            parseError(reader, "prefab count is invalid or exceeds the limit")
                .addContext("field", std::string(key)));
    }
    return Result<std::size_t>::success(*count);
}

template <typename Value> bool allFinite(const Value&) {
    return true;
}

template <> bool allFinite<double>(const double& value) {
    return std::isfinite(value);
}
template <> bool allFinite<Vec2>(const Vec2& value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}
template <> bool allFinite<Vec3>(const Vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
template <> bool allFinite<EulerAngles>(const EulerAngles& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
template <> bool allFinite<Quaternion>(const Quaternion& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
           std::isfinite(value.w);
}
template <> bool allFinite<Rect>(const Rect& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.width) &&
           std::isfinite(value.height);
}

std::string_view propertyTypeTag(PropertyType type) {
    switch (type) {
    case PropertyType::Boolean:
        return "bool";
    case PropertyType::SignedInteger:
        return "sint";
    case PropertyType::Enumeration:
        return "enum";
    case PropertyType::UnsignedInteger:
        return "uint";
    case PropertyType::BitFlags:
        return "flags";
    case PropertyType::Float:
        return "float";
    case PropertyType::Fixed:
        return "fixed";
    case PropertyType::String:
        return "string";
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

std::optional<PropertyType> propertyTypeFromTag(std::string_view tag) {
    for (const auto type : {PropertyType::Boolean, PropertyType::SignedInteger,
                            PropertyType::UnsignedInteger, PropertyType::Float,
                            PropertyType::Fixed, PropertyType::String, PropertyType::Enumeration,
                            PropertyType::BitFlags, PropertyType::Vec2, PropertyType::Vec3,
                            PropertyType::EulerAngles, PropertyType::Quaternion, PropertyType::Rect,
                            PropertyType::Color, PropertyType::AssetReference,
                            PropertyType::EntityReference, PropertyType::ComponentReference,
                            PropertyType::ActionReference, PropertyType::EventReference}) {
        if (propertyTypeTag(type) == tag)
            return type;
    }
    return std::nullopt;
}

void writeListElement(std::ostringstream& output, PropertyType type,
                      const PropertyListElement& value) {
    switch (type) {
    case PropertyType::Boolean:
        output << (std::get<bool>(value) ? 1 : 0);
        break;
    case PropertyType::SignedInteger:
    case PropertyType::Enumeration:
        output << std::get<std::int64_t>(value);
        break;
    case PropertyType::UnsignedInteger:
    case PropertyType::BitFlags:
        output << std::get<std::uint64_t>(value);
        break;
    case PropertyType::Float:
        output << std::get<double>(value);
        break;
    case PropertyType::Fixed:
        output << std::get<Fixed>(value).raw();
        break;
    case PropertyType::String:
        output << std::quoted(encodeString(std::get<std::string>(value)));
        break;
    case PropertyType::Vec2: {
        const auto typed = std::get<Vec2>(value);
        output << typed.x << ' ' << typed.y;
        break;
    }
    case PropertyType::Vec3: {
        const auto typed = std::get<Vec3>(value);
        output << typed.x << ' ' << typed.y << ' ' << typed.z;
        break;
    }
    case PropertyType::EulerAngles: {
        const auto typed = std::get<EulerAngles>(value);
        output << typed.x << ' ' << typed.y << ' ' << typed.z;
        break;
    }
    case PropertyType::Quaternion: {
        const auto typed = std::get<Quaternion>(value);
        output << typed.x << ' ' << typed.y << ' ' << typed.z << ' ' << typed.w;
        break;
    }
    case PropertyType::Rect: {
        const auto typed = std::get<Rect>(value);
        output << typed.x << ' ' << typed.y << ' ' << typed.width << ' ' << typed.height;
        break;
    }
    case PropertyType::Color: {
        const auto typed = std::get<Color>(value);
        output << static_cast<unsigned int>(typed.r) << ' ' << static_cast<unsigned int>(typed.g)
               << ' ' << static_cast<unsigned int>(typed.b) << ' '
               << static_cast<unsigned int>(typed.a);
        break;
    }
    case PropertyType::AssetReference: {
        const auto typed = std::get<AssetGuid>(value);
        output << (typed.isNil() ? "nil" : typed.toString());
        break;
    }
    case PropertyType::EntityReference: {
        const auto typed = std::get<EntityGuid>(value);
        output << (typed.isNil() ? "nil" : typed.toString());
        break;
    }
    case PropertyType::ComponentReference: {
        const auto typed = std::get<ComponentReference>(value);
        output << (typed.entity.isNil() ? "nil" : typed.entity.toString()) << ' '
               << (typed.component.isNil() ? "nil" : typed.component.toString());
        break;
    }
    case PropertyType::ActionReference:
        output << std::quoted(encodeString(std::get<ActionReference>(value).name));
        break;
    case PropertyType::EventReference:
        output << std::quoted(encodeString(std::get<EventReference>(value).name));
        break;
    case PropertyType::List:
    case PropertyType::Curve:
    case PropertyType::AnimationCurve:
        break;
    }
}

PropertyMetadata standaloneMetadata(std::string_view name, const PropertyValue& value) {
    PropertyMetadata metadata;
    metadata.name = std::string(name);
    metadata.type = std::visit(
        [](const auto& typed) {
            using Value = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<Value, bool>)
                return PropertyType::Boolean;
            else if constexpr (std::is_same_v<Value, std::int64_t>)
                return PropertyType::SignedInteger;
            else if constexpr (std::is_same_v<Value, std::uint64_t>)
                return PropertyType::UnsignedInteger;
            else if constexpr (std::is_same_v<Value, double>)
                return PropertyType::Float;
            else if constexpr (std::is_same_v<Value, Fixed>)
                return PropertyType::Fixed;
            else if constexpr (std::is_same_v<Value, std::string>)
                return PropertyType::String;
            else if constexpr (std::is_same_v<Value, Vec2>)
                return PropertyType::Vec2;
            else if constexpr (std::is_same_v<Value, Vec3>)
                return PropertyType::Vec3;
            else if constexpr (std::is_same_v<Value, EulerAngles>)
                return PropertyType::EulerAngles;
            else if constexpr (std::is_same_v<Value, Quaternion>)
                return PropertyType::Quaternion;
            else if constexpr (std::is_same_v<Value, Rect>)
                return PropertyType::Rect;
            else if constexpr (std::is_same_v<Value, Color>)
                return PropertyType::Color;
            else if constexpr (std::is_same_v<Value, AssetGuid>)
                return PropertyType::AssetReference;
            else if constexpr (std::is_same_v<Value, EntityGuid>)
                return PropertyType::EntityReference;
            else if constexpr (std::is_same_v<Value, ComponentReference>)
                return PropertyType::ComponentReference;
            else if constexpr (std::is_same_v<Value, PropertyList>)
                return PropertyType::List;
            else if constexpr (std::is_same_v<Value, Curve>)
                return PropertyType::Curve;
            else if constexpr (std::is_same_v<Value, PropertyAnimationCurve>)
                return PropertyType::AnimationCurve;
            else if constexpr (std::is_same_v<Value, ActionReference>)
                return PropertyType::ActionReference;
            else
                return PropertyType::EventReference;
        },
        value);
    if (const auto* list = std::get_if<PropertyList>(&value))
        metadata.listElementType = list->elementType;
    return metadata;
}

Result<void> writeProperty(std::ostringstream& output, std::string_view name,
                           const PropertyValue& value) {
    const auto metadata = standaloneMetadata(name, value);
    auto valid = validatePropertyValue(metadata, value);
    if (!valid) {
        return Result<void>::failure(
            Error(ErrorCode::SerializationFailed, valid.error().message())
                .addContext("property", std::string(name)));
    }
    output << "property " << std::quoted(encodeString(name)) << ' ';
    return std::visit(
        [&](const auto& typed) -> Result<void> {
            using Value = std::decay_t<decltype(typed)>;
            if (!allFinite(typed)) {
                return Result<void>::failure(
                    Error(ErrorCode::SerializationFailed, "prefab property is not finite")
                        .addContext("property", std::string(name)));
            }
            if constexpr (std::is_same_v<Value, bool>) {
                output << "bool " << (typed ? 1 : 0);
            } else if constexpr (std::is_same_v<Value, std::int64_t>) {
                output << "sint " << typed;
            } else if constexpr (std::is_same_v<Value, std::uint64_t>) {
                output << "uint " << typed;
            } else if constexpr (std::is_same_v<Value, double>) {
                output << "float " << std::setprecision(std::numeric_limits<double>::max_digits10)
                       << typed;
            } else if constexpr (std::is_same_v<Value, Fixed>) {
                output << "fixed " << typed.raw();
            } else if constexpr (std::is_same_v<Value, std::string>) {
                output << "string " << std::quoted(encodeString(typed));
            } else if constexpr (std::is_same_v<Value, Vec2>) {
                output << "vec2 " << std::setprecision(std::numeric_limits<float>::max_digits10)
                       << typed.x << ' ' << typed.y;
            } else if constexpr (std::is_same_v<Value, Vec3>) {
                output << "vec3 " << std::setprecision(std::numeric_limits<float>::max_digits10)
                       << typed.x << ' ' << typed.y << ' ' << typed.z;
            } else if constexpr (std::is_same_v<Value, EulerAngles>) {
                output << "euler " << std::setprecision(std::numeric_limits<float>::max_digits10)
                       << typed.x << ' ' << typed.y << ' ' << typed.z;
            } else if constexpr (std::is_same_v<Value, Quaternion>) {
                output << "quat " << std::setprecision(std::numeric_limits<float>::max_digits10)
                       << typed.x << ' ' << typed.y << ' ' << typed.z << ' ' << typed.w;
            } else if constexpr (std::is_same_v<Value, Rect>) {
                output << "rect " << std::setprecision(std::numeric_limits<float>::max_digits10)
                       << typed.x << ' ' << typed.y << ' ' << typed.width << ' ' << typed.height;
            } else if constexpr (std::is_same_v<Value, Color>) {
                output << "color " << static_cast<unsigned int>(typed.r) << ' '
                       << static_cast<unsigned int>(typed.g) << ' '
                       << static_cast<unsigned int>(typed.b) << ' '
                       << static_cast<unsigned int>(typed.a);
            } else if constexpr (std::is_same_v<Value, AssetGuid>) {
                output << "asset " << (typed.isNil() ? "nil" : typed.toString());
            } else if constexpr (std::is_same_v<Value, EntityGuid>) {
                output << "entity " << (typed.isNil() ? "nil" : typed.toString());
            } else if constexpr (std::is_same_v<Value, ComponentReference>) {
                output << "component "
                       << (typed.entity.isNil() ? "nil" : typed.entity.toString()) << ' '
                       << (typed.component.isNil() ? "nil" : typed.component.toString());
            } else if constexpr (std::is_same_v<Value, PropertyList>) {
                if (!propertyListElementTypeSupported(typed.elementType) ||
                    typed.values.size() > MaximumPropertyListItems) {
                    return Result<void>::failure(
                        Error(ErrorCode::SerializationFailed, "invalid prefab property list")
                            .addContext("property", std::string(name)));
                }
                output << "list " << propertyTypeTag(typed.elementType) << ' '
                       << typed.values.size();
                for (const auto& element : typed.values) {
                    output << ' ';
                    writeListElement(output, typed.elementType, element);
                }
            } else if constexpr (std::is_same_v<Value, Curve>) {
                if (typed.points.size() > MaximumCurvePoints)
                    return Result<void>::failure(Error(ErrorCode::SerializationFailed,
                                                       "prefab curve has too many points"));
                output << "curve " << typed.points.size();
                for (const auto& point : typed.points)
                    output << ' ' << point.position << ' ' << point.value;
            } else if constexpr (std::is_same_v<Value, PropertyAnimationCurve>) {
                if (typed.keys.size() > MaximumCurvePoints)
                    return Result<void>::failure(Error(ErrorCode::SerializationFailed,
                                                       "prefab animation curve has too many keys"));
                output << "animation_curve " << typed.keys.size();
                for (const auto& key : typed.keys) {
                    output << ' ' << key.time << ' ' << key.value << ' ' << key.inTangent << ' '
                           << key.outTangent;
                }
            } else if constexpr (std::is_same_v<Value, ActionReference>) {
                output << "action " << std::quoted(encodeString(typed.name));
            } else if constexpr (std::is_same_v<Value, EventReference>) {
                output << "event " << std::quoted(encodeString(typed.name));
            }
            output << '\n';
            return Result<void>::success();
        },
        value);
}

Result<PropertyListElement> parseListElement(LineReader& reader, std::istringstream& stream,
                                             PropertyType type) {
    const auto invalid = [&]() {
        return Result<PropertyListElement>::failure(
            parseError(reader, "invalid prefab property list element"));
    };
    std::string token;
    auto readFloatValue = [&]() -> std::optional<double> {
        if (!(stream >> token))
            return std::nullopt;
        return parseFloat<double>(token);
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
        const auto value = readFloatValue();
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
        auto value = decodeString(encoded, reader);
        if (!value || value.value().size() > MaximumPropertyStringLength)
            return invalid();
        return Result<PropertyListElement>::success(std::move(value.value()));
    }
    case PropertyType::Vec2:
    case PropertyType::Vec3:
    case PropertyType::EulerAngles:
    case PropertyType::Quaternion:
    case PropertyType::Rect: {
        const std::size_t count = type == PropertyType::Vec2 ? 2U
                                  : (type == PropertyType::Vec3 ||
                                     type == PropertyType::EulerAngles)
                                      ? 3U
                                      : 4U;
        double values[4]{};
        for (std::size_t index = 0; index < count; ++index) {
            const auto parsed = readFloatValue();
            if (!parsed)
                return invalid();
            values[index] = *parsed;
        }
        if (type == PropertyType::Vec2)
            return Result<PropertyListElement>::success(
                Vec2{static_cast<float>(values[0]), static_cast<float>(values[1])});
        if (type == PropertyType::Vec3)
            return Result<PropertyListElement>::success(Vec3{static_cast<float>(values[0]),
                                                              static_cast<float>(values[1]),
                                                              static_cast<float>(values[2])});
        if (type == PropertyType::EulerAngles)
            return Result<PropertyListElement>::success(EulerAngles{
                static_cast<float>(values[0]), static_cast<float>(values[1]),
                static_cast<float>(values[2])});
        if (type == PropertyType::Quaternion)
            return Result<PropertyListElement>::success(Quaternion{
                static_cast<float>(values[0]), static_cast<float>(values[1]),
                static_cast<float>(values[2]), static_cast<float>(values[3])});
        return Result<PropertyListElement>::success(
            Rect{static_cast<float>(values[0]), static_cast<float>(values[1]),
                 static_cast<float>(values[2]), static_cast<float>(values[3])});
    }
    case PropertyType::Color: {
        unsigned int values[4]{};
        for (auto& item : values) {
            if (!(stream >> token))
                return invalid();
            const auto parsed = parseInteger<unsigned int>(token);
            if (!parsed || *parsed > 255U)
                return invalid();
            item = *parsed;
        }
        return Result<PropertyListElement>::success(
            Color{static_cast<std::uint8_t>(values[0]), static_cast<std::uint8_t>(values[1]),
                  static_cast<std::uint8_t>(values[2]), static_cast<std::uint8_t>(values[3])});
    }
    case PropertyType::AssetReference:
    case PropertyType::EntityReference: {
        if (!(stream >> token))
            return invalid();
        if (type == PropertyType::AssetReference) {
            if (token == "nil")
                return Result<PropertyListElement>::success(AssetGuid{});
            auto value = AssetGuid::parse(token);
            return value ? Result<PropertyListElement>::success(value.value()) : invalid();
        }
        if (token == "nil")
            return Result<PropertyListElement>::success(EntityGuid{});
        auto value = EntityGuid::parse(token);
        return value ? Result<PropertyListElement>::success(value.value()) : invalid();
    }
    case PropertyType::ComponentReference: {
        std::string entityToken;
        std::string componentToken;
        if (!(stream >> entityToken >> componentToken))
            return invalid();
        EntityGuid entity;
        ComponentTypeGuid component;
        if (entityToken != "nil") {
            auto parsed = EntityGuid::parse(entityToken);
            if (!parsed)
                return invalid();
            entity = parsed.value();
        }
        if (componentToken != "nil") {
            auto parsed = ComponentTypeGuid::parse(componentToken);
            if (!parsed)
                return invalid();
            component = parsed.value();
        }
        return Result<PropertyListElement>::success(ComponentReference{entity, component});
    }
    case PropertyType::ActionReference:
    case PropertyType::EventReference: {
        std::string encoded;
        if (!(stream >> std::quoted(encoded)))
            return invalid();
        auto value = decodeString(encoded, reader);
        if (!value || value.value().size() > MaximumActionOrEventNameLength)
            return invalid();
        if (type == PropertyType::ActionReference)
            return Result<PropertyListElement>::success(ActionReference{std::move(value.value())});
        return Result<PropertyListElement>::success(EventReference{std::move(value.value())});
    }
    case PropertyType::List:
    case PropertyType::Curve:
    case PropertyType::AnimationCurve:
        return invalid();
    }
    return invalid();
}

Result<PropertyValue> parsePropertyValue(LineReader& reader, std::istringstream& stream,
                                         std::string_view tag) {
    auto invalid = [&]() {
        return Result<PropertyValue>::failure(parseError(reader, "invalid prefab property value"));
    };
    std::string token;
    if (tag == "bool") {
        if (!(stream >> token) || (token != "0" && token != "1") || !streamFinished(stream))
            return invalid();
        return Result<PropertyValue>::success(token == "1");
    }
    if (tag == "sint") {
        if (!(stream >> token) || !streamFinished(stream))
            return invalid();
        const auto value = parseInteger<std::int64_t>(token);
        return value ? Result<PropertyValue>::success(*value) : invalid();
    }
    if (tag == "uint") {
        if (!(stream >> token) || !streamFinished(stream))
            return invalid();
        const auto value = parseInteger<std::uint64_t>(token);
        return value ? Result<PropertyValue>::success(*value) : invalid();
    }
    if (tag == "float") {
        if (!(stream >> token) || !streamFinished(stream))
            return invalid();
        const auto value = parseFloat<double>(token);
        return value ? Result<PropertyValue>::success(*value) : invalid();
    }
    if (tag == "fixed") {
        if (!(stream >> token) || !streamFinished(stream))
            return invalid();
        const auto value = parseInteger<std::int32_t>(token);
        return value ? Result<PropertyValue>::success(Fixed::fromRaw(*value)) : invalid();
    }
    if (tag == "string") {
        std::string encoded;
        if (!(stream >> std::quoted(encoded)) || !streamFinished(stream))
            return invalid();
        auto decoded = decodeString(encoded, reader);
        if (!decoded)
            return Result<PropertyValue>::failure(decoded.error());
        return Result<PropertyValue>::success(std::move(decoded.value()));
    }
    auto readFloat = [&](float& value) {
        if (!(stream >> token))
            return false;
        const auto parsed = parseFloat<float>(token);
        if (!parsed)
            return false;
        value = *parsed;
        return true;
    };
    if (tag == "vec2") {
        Vec2 value;
        if (!readFloat(value.x) || !readFloat(value.y) || !streamFinished(stream))
            return invalid();
        return Result<PropertyValue>::success(value);
    }
    if (tag == "vec3") {
        Vec3 value;
        if (!readFloat(value.x) || !readFloat(value.y) || !readFloat(value.z) ||
            !streamFinished(stream))
            return invalid();
        return Result<PropertyValue>::success(value);
    }
    if (tag == "euler") {
        EulerAngles value;
        if (!readFloat(value.x) || !readFloat(value.y) || !readFloat(value.z) ||
            !streamFinished(stream))
            return invalid();
        return Result<PropertyValue>::success(value);
    }
    if (tag == "quat") {
        Quaternion value;
        if (!readFloat(value.x) || !readFloat(value.y) || !readFloat(value.z) ||
            !readFloat(value.w) || !streamFinished(stream))
            return invalid();
        return Result<PropertyValue>::success(value);
    }
    if (tag == "rect") {
        Rect value;
        if (!readFloat(value.x) || !readFloat(value.y) || !readFloat(value.width) ||
            !readFloat(value.height) || !streamFinished(stream))
            return invalid();
        return Result<PropertyValue>::success(value);
    }
    if (tag == "color") {
        std::string red;
        std::string green;
        std::string blue;
        std::string alpha;
        if (!(stream >> red >> green >> blue >> alpha) || !streamFinished(stream))
            return invalid();
        const auto r = parseInteger<unsigned int>(red);
        const auto g = parseInteger<unsigned int>(green);
        const auto b = parseInteger<unsigned int>(blue);
        const auto a = parseInteger<unsigned int>(alpha);
        if (!r || !g || !b || !a || *r > 255U || *g > 255U || *b > 255U || *a > 255U)
            return invalid();
        return Result<PropertyValue>::success(
            Color{static_cast<std::uint8_t>(*r), static_cast<std::uint8_t>(*g),
                  static_cast<std::uint8_t>(*b), static_cast<std::uint8_t>(*a)});
    }
    if (tag == "asset") {
        if (!(stream >> token) || !streamFinished(stream))
            return invalid();
        if (token == "nil")
            return Result<PropertyValue>::success(AssetGuid{});
        auto value = parseRequiredGuid<AssetGuid>(token, reader, "property");
        return value ? Result<PropertyValue>::success(value.value())
                     : Result<PropertyValue>::failure(value.error());
    }
    if (tag == "entity") {
        if (!(stream >> token) || !streamFinished(stream))
            return invalid();
        if (token == "nil")
            return Result<PropertyValue>::success(EntityGuid{});
        auto value = parseRequiredGuid<EntityGuid>(token, reader, "property");
        return value ? Result<PropertyValue>::success(value.value())
                     : Result<PropertyValue>::failure(value.error());
    }
    if (tag == "component") {
        std::string entityToken;
        std::string componentToken;
        if (!(stream >> entityToken >> componentToken) || !streamFinished(stream))
            return invalid();
        EntityGuid entity;
        ComponentTypeGuid component;
        if (entityToken != "nil") {
            auto parsed = EntityGuid::parse(entityToken);
            if (!parsed)
                return invalid();
            entity = parsed.value();
        }
        if (componentToken != "nil") {
            auto parsed = ComponentTypeGuid::parse(componentToken);
            if (!parsed)
                return invalid();
            component = parsed.value();
        }
        if (entity.isNil() != component.isNil())
            return invalid();
        return Result<PropertyValue>::success(ComponentReference{entity, component});
    }
    if (tag == "list") {
        std::string elementTag;
        std::string countToken;
        if (!(stream >> elementTag >> countToken))
            return invalid();
        const auto elementType = propertyTypeFromTag(elementTag);
        const auto count = parseInteger<std::size_t>(countToken);
        if (!elementType || !propertyListElementTypeSupported(*elementType) || !count ||
            *count > MaximumPropertyListItems)
            return invalid();
        PropertyList list{*elementType, {}};
        list.values.reserve(*count);
        for (std::size_t index = 0; index < *count; ++index) {
            auto value = parseListElement(reader, stream, *elementType);
            if (!value)
                return Result<PropertyValue>::failure(value.error());
            list.values.push_back(std::move(value.value()));
        }
        if (!streamFinished(stream))
            return invalid();
        return Result<PropertyValue>::success(std::move(list));
    }
    if (tag == "curve" || tag == "animation_curve") {
        std::string countToken;
        if (!(stream >> countToken))
            return invalid();
        const auto count = parseInteger<std::size_t>(countToken);
        if (!count || *count > MaximumCurvePoints)
            return invalid();
        if (tag == "curve") {
            Curve curve;
            curve.points.reserve(*count);
            for (std::size_t index = 0; index < *count; ++index) {
                CurvePoint point;
                if (!(stream >> point.position >> point.value) || !std::isfinite(point.position) ||
                    !std::isfinite(point.value))
                    return invalid();
                curve.points.push_back(point);
            }
            if (!streamFinished(stream))
                return invalid();
            return Result<PropertyValue>::success(std::move(curve));
        }
        PropertyAnimationCurve curve;
        curve.keys.reserve(*count);
        for (std::size_t index = 0; index < *count; ++index) {
            AnimationCurveKey key;
            if (!(stream >> key.time >> key.value >> key.inTangent >> key.outTangent) ||
                !std::isfinite(key.time) || !std::isfinite(key.value) ||
                !std::isfinite(key.inTangent) || !std::isfinite(key.outTangent))
                return invalid();
            curve.keys.push_back(key);
        }
        if (!streamFinished(stream))
            return invalid();
        return Result<PropertyValue>::success(std::move(curve));
    }
    if (tag == "action" || tag == "event") {
        std::string encoded;
        if (!(stream >> std::quoted(encoded)) || !streamFinished(stream))
            return invalid();
        auto value = decodeString(encoded, reader);
        if (!value || value.value().size() > MaximumActionOrEventNameLength)
            return invalid();
        if (tag == "action")
            return Result<PropertyValue>::success(ActionReference{std::move(value.value())});
        return Result<PropertyValue>::success(EventReference{std::move(value.value())});
    }
    return Result<PropertyValue>::failure(
        parseError(reader, "unknown prefab property type").addContext("type", std::string(tag)));
}

Result<PrefabComponentData> readComponent(LineReader& reader) {
    auto marker = expectLine(reader, "component_begin");
    if (!marker)
        return Result<PrefabComponentData>::failure(marker.error());
    auto typeToken = readLine(reader, "type_id");
    if (!typeToken)
        return Result<PrefabComponentData>::failure(typeToken.error());
    auto type = parseRequiredGuid<ComponentTypeGuid>(typeToken.value(), reader, "type_id");
    if (!type)
        return Result<PrefabComponentData>::failure(type.error());
    auto name = readQuotedLine(reader, "type_name");
    if (!name || name.value().empty()) {
        return Result<PrefabComponentData>::failure(
            name ? parseError(reader, "prefab component type name is empty") : name.error());
    }
    auto count = parseCount(reader, "property_count", MaximumProperties);
    if (!count)
        return Result<PrefabComponentData>::failure(count.error());

    PrefabComponentData component{type.value(), std::move(name.value()), {}};
    for (std::size_t index = 0; index < count.value(); ++index) {
        std::string line;
        if (!reader.next(line))
            return Result<PrefabComponentData>::failure(parseError(reader, "missing property"));
        std::istringstream stream(line);
        stream.imbue(std::locale::classic());
        std::string key;
        std::string encodedName;
        std::string tag;
        if (!(stream >> key >> std::quoted(encodedName) >> tag) || key != "property") {
            return Result<PrefabComponentData>::failure(
                parseError(reader, "invalid prefab property"));
        }
        auto propertyName = decodeString(encodedName, reader);
        if (!propertyName || propertyName.value().empty()) {
            return Result<PrefabComponentData>::failure(
                propertyName ? parseError(reader, "prefab property name is empty")
                             : propertyName.error());
        }
        auto value = parsePropertyValue(reader, stream, tag);
        if (!value)
            return Result<PrefabComponentData>::failure(value.error());
        const auto metadata = standaloneMetadata(propertyName.value(), value.value());
        auto valid = validatePropertyValue(metadata, value.value());
        if (!valid) {
            return Result<PrefabComponentData>::failure(
                parseError(reader, valid.error().message())
                    .addContext("property", propertyName.value()));
        }
        if (!component.properties.emplace(std::move(propertyName.value()), std::move(value.value()))
                 .second) {
            return Result<PrefabComponentData>::failure(
                parseError(reader, "duplicate prefab property"));
        }
    }
    marker = expectLine(reader, "component_end");
    if (!marker)
        return Result<PrefabComponentData>::failure(marker.error());
    return Result<PrefabComponentData>::success(std::move(component));
}

Result<std::map<ComponentTypeGuid, PrefabComponentData>> readComponents(LineReader& reader,
                                                                        std::size_t count) {
    std::map<ComponentTypeGuid, PrefabComponentData> components;
    for (std::size_t index = 0; index < count; ++index) {
        auto component = readComponent(reader);
        if (!component)
            return Result<std::map<ComponentTypeGuid, PrefabComponentData>>::failure(
                component.error());
        const auto type = component.value().typeId;
        if (!components.emplace(type, std::move(component.value())).second) {
            return Result<std::map<ComponentTypeGuid, PrefabComponentData>>::failure(
                parseError(reader, "duplicate prefab component")
                    .addContext("component", type.toString()));
        }
    }
    return Result<std::map<ComponentTypeGuid, PrefabComponentData>>::success(std::move(components));
}

void writeComponents(std::ostringstream& output,
                     const std::map<ComponentTypeGuid, PrefabComponentData>& components,
                     Result<void>& status) {
    for (const auto& [type, component] : components) {
        output << "component_begin\n";
        output << "type_id " << type.toString() << '\n';
        output << "type_name " << std::quoted(encodeString(component.typeName)) << '\n';
        output << "property_count " << component.properties.size() << '\n';
        for (const auto& [name, value] : component.properties) {
            auto written = writeProperty(output, name, value);
            if (!written) {
                status = Result<void>::failure(written.error());
                return;
            }
        }
        output << "component_end\n";
    }
}

} // namespace

Result<std::string> PrefabSerializer::serialize(const PrefabAsset& prefab) {
    PrefabLibrary validation;
    auto valid = validation.add(prefab);
    if (!valid)
        return Result<std::string>::failure(valid.error());

    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "fglprefab " << CurrentVersion << '\n';
    output << "asset_guid " << prefab.id.toString() << '\n';
    output << "name " << std::quoted(encodeString(prefab.name)) << '\n';
    output << "nested_base "
           << (prefab.nestedBase ? prefab.nestedBase->toString() : std::string("nil")) << '\n';
    output << "root_component_count " << prefab.components.size() << '\n';
    auto status = Result<void>::success();
    writeComponents(output, prefab.components, status);
    if (!status)
        return Result<std::string>::failure(status.error());

    std::vector<const PrefabEntityData*> orderedEntities;
    orderedEntities.reserve(prefab.entities.size());
    for (const auto& entity : prefab.entities)
        orderedEntities.push_back(&entity);
    std::sort(orderedEntities.begin(), orderedEntities.end(),
              [](const auto* lhs, const auto* rhs) { return lhs->id < rhs->id; });
    output << "entity_count " << orderedEntities.size() << '\n';
    for (const auto* entity : orderedEntities) {
        output << "entity_begin\n";
        output << "entity_guid " << entity->id.toString() << '\n';
        output << "name " << std::quoted(encodeString(entity->name)) << '\n';
        output << "active " << (entity->active ? 1 : 0) << '\n';
        output << "parent " << (entity->parent ? entity->parent->toString() : std::string("nil"))
               << '\n';
        output << "component_count " << entity->components.size() << '\n';
        writeComponents(output, entity->components, status);
        if (!status)
            return Result<std::string>::failure(status.error());
        output << "entity_end\n";
    }
    output << "prefab_end\n";
    return Result<std::string>::success(output.str());
}

Result<PrefabAsset> PrefabSerializer::deserialize(std::string_view text) {
    if (text.size() > MaximumInputBytes) {
        return Result<PrefabAsset>::failure(
            Error(ErrorCode::CapacityExceeded, "prefab exceeds the input size limit"));
    }
    LineReader reader(text);
    std::string header;
    if (!reader.next(header))
        return Result<PrefabAsset>::failure(parseError(reader, "prefab is empty"));
    std::istringstream headerStream(header);
    headerStream.imbue(std::locale::classic());
    std::string magic;
    std::string versionToken;
    if (!(headerStream >> magic >> versionToken) || magic != "fglprefab" ||
        !streamFinished(headerStream)) {
        return Result<PrefabAsset>::failure(parseError(reader, "invalid prefab header"));
    }
    const auto version = parseInteger<int>(versionToken);
    if (!version || (*version != 1 && *version != CurrentVersion)) {
        return Result<PrefabAsset>::failure(
            Error(ErrorCode::UnsupportedVersion, "unsupported prefab version")
                .addContext("version", versionToken));
    }

    auto assetToken = readLine(reader, "asset_guid");
    if (!assetToken)
        return Result<PrefabAsset>::failure(assetToken.error());
    auto assetId = parseRequiredGuid<AssetGuid>(assetToken.value(), reader, "asset_guid");
    if (!assetId)
        return Result<PrefabAsset>::failure(assetId.error());
    auto name = readQuotedLine(reader, "name");
    if (!name || name.value().empty()) {
        return Result<PrefabAsset>::failure(name ? parseError(reader, "prefab name is empty")
                                                 : name.error());
    }
    auto nestedToken = readLine(reader, "nested_base");
    if (!nestedToken)
        return Result<PrefabAsset>::failure(nestedToken.error());
    auto nested = parseOptionalGuid<AssetGuid>(nestedToken.value(), reader, "nested_base");
    if (!nested)
        return Result<PrefabAsset>::failure(nested.error());

    const auto componentCountKey = *version == 1 ? "component_count" : "root_component_count";
    auto componentCount = parseCount(reader, componentCountKey, MaximumComponents);
    if (!componentCount)
        return Result<PrefabAsset>::failure(componentCount.error());
    auto components = readComponents(reader, componentCount.value());
    if (!components)
        return Result<PrefabAsset>::failure(components.error());

    PrefabAsset prefab{assetId.value(),
                       std::move(name.value()),
                       nested.value(),
                       std::move(components.value()),
                       {}};
    if (*version == CurrentVersion) {
        auto entityCount = parseCount(reader, "entity_count", MaximumEntities);
        if (!entityCount)
            return Result<PrefabAsset>::failure(entityCount.error());
        prefab.entities.reserve(entityCount.value());
        for (std::size_t index = 0; index < entityCount.value(); ++index) {
            auto marker = expectLine(reader, "entity_begin");
            if (!marker)
                return Result<PrefabAsset>::failure(marker.error());
            auto entityToken = readLine(reader, "entity_guid");
            if (!entityToken)
                return Result<PrefabAsset>::failure(entityToken.error());
            auto entityId =
                parseRequiredGuid<EntityGuid>(entityToken.value(), reader, "entity_guid");
            if (!entityId)
                return Result<PrefabAsset>::failure(entityId.error());
            auto entityName = readQuotedLine(reader, "name");
            if (!entityName || entityName.value().empty()) {
                return Result<PrefabAsset>::failure(
                    entityName ? parseError(reader, "prefab entity name is empty")
                               : entityName.error());
            }
            auto activeToken = readLine(reader, "active");
            if (!activeToken || (activeToken.value() != "0" && activeToken.value() != "1")) {
                return Result<PrefabAsset>::failure(
                    activeToken ? parseError(reader, "invalid prefab entity active flag")
                                : activeToken.error());
            }
            auto parentToken = readLine(reader, "parent");
            if (!parentToken)
                return Result<PrefabAsset>::failure(parentToken.error());
            auto parent = parseOptionalGuid<EntityGuid>(parentToken.value(), reader, "parent");
            if (!parent)
                return Result<PrefabAsset>::failure(parent.error());
            auto entityComponentCount = parseCount(reader, "component_count", MaximumComponents);
            if (!entityComponentCount)
                return Result<PrefabAsset>::failure(entityComponentCount.error());
            auto entityComponents = readComponents(reader, entityComponentCount.value());
            if (!entityComponents)
                return Result<PrefabAsset>::failure(entityComponents.error());
            marker = expectLine(reader, "entity_end");
            if (!marker)
                return Result<PrefabAsset>::failure(marker.error());
            prefab.entities.push_back({entityId.value(), std::move(entityName.value()),
                                       activeToken.value() == "1", parent.value(),
                                       std::move(entityComponents.value())});
        }
    }

    auto marker = expectLine(reader, "prefab_end");
    if (!marker)
        return Result<PrefabAsset>::failure(marker.error());
    std::string trailing;
    if (reader.next(trailing)) {
        return Result<PrefabAsset>::failure(parseError(reader, "trailing prefab data"));
    }
    if (reader.oversized()) {
        return Result<PrefabAsset>::failure(
            parseError(reader, "prefab line exceeds the size limit"));
    }

    PrefabLibrary validation;
    auto valid = validation.add(prefab);
    if (!valid)
        return Result<PrefabAsset>::failure(valid.error());
    return Result<PrefabAsset>::success(std::move(prefab));
}

} // namespace fabgl
