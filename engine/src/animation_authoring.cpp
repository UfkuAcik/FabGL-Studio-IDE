#include "fabgl/animation/animation_authoring.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace fabgl {
namespace {

constexpr std::uint32_t AnimationClipFormatVersion = 1U;
constexpr std::uint32_t AnimatorControllerFormatVersion = 1U;

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
            Error(ErrorCode::InvalidFormat, "unexpected end of animation authoring data")
                .addContext("expected", expected)
                .addContext("line", std::to_string(lineNumber_ + 1U)));
    }

    [[nodiscard]] Result<void> requireEnd(const char* terminator) {
        std::string line;
        while (std::getline(stream_, line)) {
            ++lineNumber_;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            const auto first = line.find_first_not_of(" \t");
            if (first != std::string::npos && line[first] != '#') {
                return Result<void>::failure(
                    Error(ErrorCode::InvalidFormat,
                          "unexpected data after animation authoring terminator")
                        .addContext("terminator", terminator)
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
                Error(ErrorCode::InvalidFormat, "unterminated animation string escape"));
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
                Error(ErrorCode::InvalidFormat, "unsupported animation string escape")
                    .addContext("escape", std::string(1U, value[index])));
        }
    }
    return Result<std::string>::success(std::move(decoded));
}

Result<std::string> decodeBounded(LineReader& reader, std::string encoded,
                                  const std::size_t maximumBytes) {
    auto decoded = decodeControlCharacters(encoded);
    if (!decoded) {
        return Result<std::string>::failure(
            decoded.error().withContext("line", std::to_string(reader.lineNumber())));
    }
    if (decoded.value().size() > maximumBytes) {
        return Result<std::string>::failure(
            parseError(reader, "animation string exceeds the byte limit")
                .addContext("maximum", std::to_string(maximumBytes)));
    }
    return decoded;
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
    return decodeBounded(reader, std::move(encoded), maximumBytes);
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

Result<float> parseFloat(LineReader& reader, const char* expectedKey) {
    auto line = reader.next(expectedKey);
    if (!line)
        return Result<float>::failure(line.error());
    std::istringstream stream(line.value());
    stream.imbue(std::locale::classic());
    std::string key;
    float value = 0.0F;
    if (!(stream >> key >> value) || key != expectedKey || !streamFinished(stream) ||
        !std::isfinite(value)) {
        return Result<float>::failure(
            parseError(reader, std::string(expectedKey) + " must be a finite float"));
    }
    return Result<float>::success(value == 0.0F ? 0.0F : value);
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

Result<void> parseHeader(LineReader& reader, const char* expectedMagic,
                         const std::uint32_t expectedVersion) {
    auto line = reader.next("format header");
    if (!line)
        return Result<void>::failure(line.error());
    std::istringstream stream(line.value());
    stream.imbue(std::locale::classic());
    std::string magic;
    std::string versionToken;
    if (!(stream >> magic >> versionToken) || magic != expectedMagic || !streamFinished(stream)) {
        return Result<void>::failure(
            parseError(reader, std::string("expected '") + expectedMagic + " <version>'"));
    }
    const auto version = parseInteger<std::uint32_t>(versionToken);
    if (!version)
        return Result<void>::failure(parseError(reader, "animation format version is invalid"));
    if (*version != expectedVersion) {
        return Result<void>::failure(
            Error(ErrorCode::UnsupportedVersion, "unsupported animation authoring version")
                .addContext("format", expectedMagic)
                .addContext("version", std::to_string(*version)));
    }
    return Result<void>::success();
}

Result<AssetGuid> parseGuid(LineReader& reader, const char* expectedKey) {
    auto token = parseToken(reader, expectedKey);
    if (!token)
        return Result<AssetGuid>::failure(token.error());
    auto parsed = AssetGuid::parse(token.value());
    if (!parsed) {
        return Result<AssetGuid>::failure(
            parsed.error().withContext("line", std::to_string(reader.lineNumber())));
    }
    if (parsed.value().isNil()) {
        return Result<AssetGuid>::failure(
            parseError(reader, std::string(expectedKey) + " cannot be nil"));
    }
    return parsed;
}

std::string_view interpolationTag(const CurveInterpolation interpolation) noexcept {
    switch (interpolation) {
    case CurveInterpolation::Step:
        return "step";
    case CurveInterpolation::Linear:
        return "linear";
    case CurveInterpolation::CubicHermite:
        return "cubic_hermite";
    }
    return "unknown";
}

std::optional<CurveInterpolation> parseInterpolation(const std::string_view tag) noexcept {
    if (tag == "step")
        return CurveInterpolation::Step;
    if (tag == "linear")
        return CurveInterpolation::Linear;
    if (tag == "cubic_hermite")
        return CurveInterpolation::CubicHermite;
    return std::nullopt;
}

std::string_view parameterTypeTag(const AnimatorParameterType type) noexcept {
    switch (type) {
    case AnimatorParameterType::Boolean:
        return "boolean";
    case AnimatorParameterType::Integer:
        return "integer";
    case AnimatorParameterType::Float:
        return "float";
    case AnimatorParameterType::Trigger:
        return "trigger";
    }
    return "unknown";
}

std::optional<AnimatorParameterType> parseParameterType(const std::string_view tag) noexcept {
    if (tag == "boolean")
        return AnimatorParameterType::Boolean;
    if (tag == "integer")
        return AnimatorParameterType::Integer;
    if (tag == "float")
        return AnimatorParameterType::Float;
    if (tag == "trigger")
        return AnimatorParameterType::Trigger;
    return std::nullopt;
}

std::string_view conditionModeTag(const AnimationConditionMode mode) noexcept {
    switch (mode) {
    case AnimationConditionMode::BooleanEquals:
        return "boolean_equals";
    case AnimationConditionMode::IntegerEquals:
        return "integer_equals";
    case AnimationConditionMode::IntegerNotEquals:
        return "integer_not_equals";
    case AnimationConditionMode::IntegerGreater:
        return "integer_greater";
    case AnimationConditionMode::IntegerLess:
        return "integer_less";
    case AnimationConditionMode::FloatGreater:
        return "float_greater";
    case AnimationConditionMode::FloatLess:
        return "float_less";
    case AnimationConditionMode::TriggerSet:
        return "trigger_set";
    }
    return "unknown";
}

std::optional<AnimationConditionMode> parseConditionMode(const std::string_view tag) noexcept {
    if (tag == "boolean_equals")
        return AnimationConditionMode::BooleanEquals;
    if (tag == "integer_equals")
        return AnimationConditionMode::IntegerEquals;
    if (tag == "integer_not_equals")
        return AnimationConditionMode::IntegerNotEquals;
    if (tag == "integer_greater")
        return AnimationConditionMode::IntegerGreater;
    if (tag == "integer_less")
        return AnimationConditionMode::IntegerLess;
    if (tag == "float_greater")
        return AnimationConditionMode::FloatGreater;
    if (tag == "float_less")
        return AnimationConditionMode::FloatLess;
    if (tag == "trigger_set")
        return AnimationConditionMode::TriggerSet;
    return std::nullopt;
}

bool interpolationValid(const CurveInterpolation interpolation) noexcept {
    return interpolationTag(interpolation) != "unknown";
}

bool parameterTypeValid(const AnimatorParameterType type) noexcept {
    return parameterTypeTag(type) != "unknown";
}

bool conditionModeValid(const AnimationConditionMode mode) noexcept {
    return conditionModeTag(mode) != "unknown";
}

bool stringFits(const std::string_view value, const std::size_t maximumBytes) noexcept {
    return value.size() <= maximumBytes;
}

bool clipLimitsValid(const AnimationClipFormatLimits& limits) noexcept {
    return limits.maximumSourceBytes > 0U && limits.maximumStringBytes > 0U;
}

bool controllerLimitsValid(const AnimatorControllerFormatLimits& limits) noexcept {
    return limits.maximumSourceBytes > 0U && limits.maximumStringBytes > 0U;
}

bool conditionMatchesParameter(const AnimationConditionMode mode,
                               const AnimatorParameterType type) noexcept {
    switch (type) {
    case AnimatorParameterType::Boolean:
        return mode == AnimationConditionMode::BooleanEquals;
    case AnimatorParameterType::Integer:
        return mode == AnimationConditionMode::IntegerEquals ||
               mode == AnimationConditionMode::IntegerNotEquals ||
               mode == AnimationConditionMode::IntegerGreater ||
               mode == AnimationConditionMode::IntegerLess;
    case AnimatorParameterType::Float:
        return mode == AnimationConditionMode::FloatGreater ||
               mode == AnimationConditionMode::FloatLess;
    case AnimatorParameterType::Trigger:
        return mode == AnimationConditionMode::TriggerSet;
    }
    return false;
}

float canonicalFloat(const float value) noexcept {
    return value == 0.0F ? 0.0F : value;
}

void writeQuoted(std::ostringstream& stream, const char* key, const std::string_view value) {
    stream << key << ' ' << std::quoted(encodeControlCharacters(value)) << '\n';
}

void writeFloat(std::ostringstream& stream, const char* key, const float value) {
    const auto previousPrecision = stream.precision();
    stream << std::setprecision(std::numeric_limits<float>::max_digits10) << key << ' '
           << canonicalFloat(value) << '\n';
    stream.precision(previousPrecision);
}

bool conditionLess(const AnimationCondition& lhs, const AnimationCondition& rhs) noexcept {
    return std::tuple(lhs.parameter, lhs.mode, lhs.booleanValue, lhs.integerValue,
                      canonicalFloat(lhs.floatValue)) <
           std::tuple(rhs.parameter, rhs.mode, rhs.booleanValue, rhs.integerValue,
                      canonicalFloat(rhs.floatValue));
}

Result<AnimationKey> parseKey(LineReader& reader) {
    auto line = reader.next("key");
    if (!line)
        return Result<AnimationKey>::failure(line.error());
    std::istringstream stream(line.value());
    stream.imbue(std::locale::classic());
    std::string keyToken;
    std::string interpolationToken;
    AnimationKey key;
    if (!(stream >> keyToken >> key.time >> key.value >> key.inTangent >> key.outTangent >>
          interpolationToken) ||
        keyToken != "key" || !streamFinished(stream) || !std::isfinite(key.time) ||
        !std::isfinite(key.value) || !std::isfinite(key.inTangent) ||
        !std::isfinite(key.outTangent)) {
        return Result<AnimationKey>::failure(
            parseError(reader, "animation key record is invalid"));
    }
    const auto interpolation = parseInterpolation(interpolationToken);
    if (!interpolation) {
        return Result<AnimationKey>::failure(
            parseError(reader, "animation key interpolation is unknown")
                .addContext("interpolation", interpolationToken));
    }
    key.time = canonicalFloat(key.time);
    key.value = canonicalFloat(key.value);
    key.inTangent = canonicalFloat(key.inTangent);
    key.outTangent = canonicalFloat(key.outTangent);
    key.interpolation = *interpolation;
    return Result<AnimationKey>::success(key);
}

Result<AnimationEvent> parseEvent(LineReader& reader, const std::size_t maximumStringBytes) {
    auto line = reader.next("event");
    if (!line)
        return Result<AnimationEvent>::failure(line.error());
    std::istringstream stream(line.value());
    stream.imbue(std::locale::classic());
    std::string key;
    std::string encodedName;
    AnimationEvent event;
    if (!(stream >> key >> event.time >> std::quoted(encodedName)) || key != "event" ||
        !streamFinished(stream) || !std::isfinite(event.time)) {
        return Result<AnimationEvent>::failure(
            parseError(reader, "animation event record is invalid"));
    }
    auto name = decodeBounded(reader, std::move(encodedName), maximumStringBytes);
    if (!name)
        return Result<AnimationEvent>::failure(name.error());
    event.time = canonicalFloat(event.time);
    event.name = std::move(name.value());
    return Result<AnimationEvent>::success(std::move(event));
}

Result<std::pair<std::string, AnimatorParameterDefinition>>
parseParameter(LineReader& reader, const std::size_t maximumStringBytes) {
    auto line = reader.next("parameter");
    if (!line)
        return Result<std::pair<std::string, AnimatorParameterDefinition>>::failure(line.error());
    std::istringstream stream(line.value());
    stream.imbue(std::locale::classic());
    std::string key;
    std::string encodedName;
    std::string typeToken;
    if (!(stream >> key >> std::quoted(encodedName) >> typeToken) || key != "parameter") {
        return Result<std::pair<std::string, AnimatorParameterDefinition>>::failure(
            parseError(reader, "animator parameter record is invalid"));
    }
    auto name = decodeBounded(reader, std::move(encodedName), maximumStringBytes);
    if (!name)
        return Result<std::pair<std::string, AnimatorParameterDefinition>>::failure(name.error());
    const auto type = parseParameterType(typeToken);
    if (!type) {
        return Result<std::pair<std::string, AnimatorParameterDefinition>>::failure(
            parseError(reader, "animator parameter type is unknown")
                .addContext("type", typeToken));
    }
    AnimatorParameterDefinition parameter;
    parameter.type = *type;
    switch (*type) {
    case AnimatorParameterType::Boolean: {
        std::string token;
        if (!(stream >> token) || (token != "0" && token != "1")) {
            return Result<std::pair<std::string, AnimatorParameterDefinition>>::failure(
                parseError(reader, "boolean animator parameter default is invalid"));
        }
        parameter.booleanDefault = token == "1";
        break;
    }
    case AnimatorParameterType::Integer: {
        std::string token;
        if (!(stream >> token)) {
            return Result<std::pair<std::string, AnimatorParameterDefinition>>::failure(
                parseError(reader, "integer animator parameter default is missing"));
        }
        const auto value = parseInteger<std::int64_t>(token);
        if (!value) {
            return Result<std::pair<std::string, AnimatorParameterDefinition>>::failure(
                parseError(reader, "integer animator parameter default is invalid"));
        }
        parameter.integerDefault = *value;
        break;
    }
    case AnimatorParameterType::Float:
        if (!(stream >> parameter.floatDefault) || !std::isfinite(parameter.floatDefault)) {
            return Result<std::pair<std::string, AnimatorParameterDefinition>>::failure(
                parseError(reader, "float animator parameter default is invalid"));
        }
        parameter.floatDefault = canonicalFloat(parameter.floatDefault);
        break;
    case AnimatorParameterType::Trigger:
        break;
    }
    if (!streamFinished(stream)) {
        return Result<std::pair<std::string, AnimatorParameterDefinition>>::failure(
            parseError(reader, "animator parameter has trailing data"));
    }
    return Result<std::pair<std::string, AnimatorParameterDefinition>>::success(
        {std::move(name.value()), parameter});
}

Result<std::pair<std::string, AnimatorStateDefinition>>
parseState(LineReader& reader, const std::size_t maximumStringBytes) {
    auto line = reader.next("state");
    if (!line)
        return Result<std::pair<std::string, AnimatorStateDefinition>>::failure(line.error());
    std::istringstream stream(line.value());
    stream.imbue(std::locale::classic());
    std::string key;
    std::string encodedName;
    std::string guidToken;
    if (!(stream >> key >> std::quoted(encodedName) >> guidToken) || key != "state" ||
        !streamFinished(stream)) {
        return Result<std::pair<std::string, AnimatorStateDefinition>>::failure(
            parseError(reader, "animator state record is invalid"));
    }
    auto name = decodeBounded(reader, std::move(encodedName), maximumStringBytes);
    if (!name)
        return Result<std::pair<std::string, AnimatorStateDefinition>>::failure(name.error());
    auto clip = AssetGuid::parse(guidToken);
    if (!clip || clip.value().isNil()) {
        return Result<std::pair<std::string, AnimatorStateDefinition>>::failure(
            parseError(reader, "animator state clip GUID is invalid"));
    }
    return Result<std::pair<std::string, AnimatorStateDefinition>>::success(
        {std::move(name.value()), AnimatorStateDefinition{clip.value()}});
}

Result<AnimationCondition> parseCondition(LineReader& reader,
                                          const std::size_t maximumStringBytes) {
    auto line = reader.next("condition");
    if (!line)
        return Result<AnimationCondition>::failure(line.error());
    std::istringstream stream(line.value());
    stream.imbue(std::locale::classic());
    std::string key;
    std::string encodedParameter;
    std::string modeToken;
    if (!(stream >> key >> std::quoted(encodedParameter) >> modeToken) || key != "condition") {
        return Result<AnimationCondition>::failure(
            parseError(reader, "animator condition record is invalid"));
    }
    auto parameter = decodeBounded(reader, std::move(encodedParameter), maximumStringBytes);
    if (!parameter)
        return Result<AnimationCondition>::failure(parameter.error());
    const auto mode = parseConditionMode(modeToken);
    if (!mode) {
        return Result<AnimationCondition>::failure(
            parseError(reader, "animator condition mode is unknown").addContext("mode", modeToken));
    }
    AnimationCondition condition;
    condition.parameter = std::move(parameter.value());
    condition.mode = *mode;
    switch (*mode) {
    case AnimationConditionMode::BooleanEquals: {
        std::string token;
        if (!(stream >> token) || (token != "0" && token != "1")) {
            return Result<AnimationCondition>::failure(
                parseError(reader, "boolean animator condition value is invalid"));
        }
        condition.booleanValue = token == "1";
        break;
    }
    case AnimationConditionMode::IntegerEquals:
    case AnimationConditionMode::IntegerNotEquals:
    case AnimationConditionMode::IntegerGreater:
    case AnimationConditionMode::IntegerLess: {
        std::string token;
        if (!(stream >> token)) {
            return Result<AnimationCondition>::failure(
                parseError(reader, "integer animator condition value is missing"));
        }
        const auto value = parseInteger<std::int64_t>(token);
        if (!value) {
            return Result<AnimationCondition>::failure(
                parseError(reader, "integer animator condition value is invalid"));
        }
        condition.booleanValue = false;
        condition.integerValue = *value;
        break;
    }
    case AnimationConditionMode::FloatGreater:
    case AnimationConditionMode::FloatLess:
        if (!(stream >> condition.floatValue) || !std::isfinite(condition.floatValue)) {
            return Result<AnimationCondition>::failure(
                parseError(reader, "float animator condition value is invalid"));
        }
        condition.booleanValue = false;
        condition.floatValue = canonicalFloat(condition.floatValue);
        break;
    case AnimationConditionMode::TriggerSet:
        condition.booleanValue = false;
        break;
    }
    if (!streamFinished(stream)) {
        return Result<AnimationCondition>::failure(
            parseError(reader, "animator condition has trailing data"));
    }
    return Result<AnimationCondition>::success(std::move(condition));
}

Result<void> parsedValidationFailure(const Result<void>& validation) {
    if (validation)
        return Result<void>::success();
    if (validation.error().code() == ErrorCode::CapacityExceeded)
        return Result<void>::failure(validation.error());
    return Result<void>::failure(
        Error(ErrorCode::InvalidFormat, "animation authoring data failed validation")
            .addContext("reason", validation.error().message()));
}

} // namespace

Result<void> validateAnimationClipAsset(const AnimationClipAsset& asset,
                                        const AnimationClipFormatLimits& limits) {
    if (!clipLimitsValid(limits)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "animation clip format limits are invalid"));
    }
    if (asset.guid.isNil() || asset.name.empty() ||
        !stringFits(asset.name, limits.maximumStringBytes) ||
        !std::isfinite(asset.durationSeconds) || asset.durationSeconds <= 0.0F) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "animation clip identity or duration is invalid"));
    }
    if (asset.tracks.size() > limits.maximumTracks || asset.events.size() > limits.maximumEvents) {
        return Result<void>::failure(
            Error(ErrorCode::CapacityExceeded, "animation clip exceeds a record limit"));
    }
    auto totalKeys = std::size_t{0};
    for (const auto& [property, curve] : asset.tracks) {
        if (property.empty() || !stringFits(property, limits.maximumStringBytes) ||
            curve.keys().empty()) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "animation track is invalid")
                    .addContext("property", property));
        }
        if (curve.keys().size() > limits.maximumKeys - totalKeys) {
            return Result<void>::failure(
                Error(ErrorCode::CapacityExceeded, "animation clip exceeds the key limit"));
        }
        totalKeys += curve.keys().size();
        auto previousTime = -1.0F;
        for (const auto& key : curve.keys()) {
            if (!std::isfinite(key.time) || !std::isfinite(key.value) ||
                !std::isfinite(key.inTangent) || !std::isfinite(key.outTangent) ||
                key.time < 0.0F || key.time > asset.durationSeconds ||
                key.time <= previousTime || !interpolationValid(key.interpolation)) {
                return Result<void>::failure(
                    Error(ErrorCode::InvalidArgument, "animation track key is invalid")
                        .addContext("property", property));
            }
            previousTime = key.time;
        }
    }
    auto events = asset.events;
    std::sort(events.begin(), events.end(), [](const AnimationEvent& lhs,
                                               const AnimationEvent& rhs) {
        return std::tie(lhs.time, lhs.name) < std::tie(rhs.time, rhs.name);
    });
    for (auto index = std::size_t{0}; index < events.size(); ++index) {
        const auto& event = events[index];
        if (event.name.empty() || !stringFits(event.name, limits.maximumStringBytes) ||
            !std::isfinite(event.time) || event.time < 0.0F ||
            event.time > asset.durationSeconds ||
            (index > 0U && event.time == events[index - 1U].time &&
             event.name == events[index - 1U].name)) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "animation event is invalid or duplicated"));
        }
    }
    return Result<void>::success();
}

Result<void> validateAnimatorControllerAsset(const AnimatorControllerAsset& asset,
                                             const AnimatorControllerFormatLimits& limits) {
    if (!controllerLimitsValid(limits)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "animator controller format limits are invalid"));
    }
    if (asset.guid.isNil() || asset.name.empty() || asset.initialState.empty() ||
        !stringFits(asset.name, limits.maximumStringBytes) ||
        !stringFits(asset.initialState, limits.maximumStringBytes)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "animator controller identity is invalid"));
    }
    if (asset.parameters.size() > limits.maximumParameters ||
        asset.states.size() > limits.maximumStates ||
        asset.transitions.size() > limits.maximumTransitions) {
        return Result<void>::failure(
            Error(ErrorCode::CapacityExceeded, "animator controller exceeds a record limit"));
    }
    if (asset.states.empty() || asset.states.find(asset.initialState) == asset.states.end()) {
        return Result<void>::failure(
            Error(ErrorCode::NotFound, "animator initial state is missing")
                .addContext("state", asset.initialState));
    }
    for (const auto& [name, parameter] : asset.parameters) {
        if (name.empty() || !stringFits(name, limits.maximumStringBytes) ||
            !parameterTypeValid(parameter.type) || !std::isfinite(parameter.floatDefault)) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "animator parameter is invalid")
                    .addContext("parameter", name));
        }
    }
    for (const auto& [name, state] : asset.states) {
        if (name.empty() || !stringFits(name, limits.maximumStringBytes) || state.clip.isNil()) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "animator state or clip reference is invalid")
                    .addContext("state", name));
        }
    }
    auto totalConditions = std::size_t{0};
    for (const auto& transition : asset.transitions) {
        if (transition.fromState.empty() || transition.toState.empty() ||
            !stringFits(transition.fromState, limits.maximumStringBytes) ||
            !stringFits(transition.toState, limits.maximumStringBytes) ||
            asset.states.find(transition.fromState) == asset.states.end() ||
            asset.states.find(transition.toState) == asset.states.end() ||
            (!transition.hasExitTime && transition.conditions.empty()) ||
            !std::isfinite(transition.minimumNormalizedTime) ||
            transition.minimumNormalizedTime < 0.0F || !std::isfinite(transition.exitTime) ||
            transition.exitTime < 0.0F || !std::isfinite(transition.blendDurationSeconds) ||
            transition.blendDurationSeconds < 0.0F) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "animator transition is invalid")
                    .addContext("from", transition.fromState)
                    .addContext("to", transition.toState));
        }
        if (transition.conditions.size() > limits.maximumConditions - totalConditions) {
            return Result<void>::failure(
                Error(ErrorCode::CapacityExceeded,
                      "animator controller exceeds the condition limit"));
        }
        totalConditions += transition.conditions.size();
        for (const auto& condition : transition.conditions) {
            const auto parameter = asset.parameters.find(condition.parameter);
            if (condition.parameter.empty() ||
                !stringFits(condition.parameter, limits.maximumStringBytes) ||
                parameter == asset.parameters.end() || !conditionModeValid(condition.mode) ||
                !conditionMatchesParameter(condition.mode, parameter->second.type) ||
                !std::isfinite(condition.floatValue)) {
                return Result<void>::failure(
                    Error(ErrorCode::TypeMismatch,
                          "animator condition does not match a declared parameter")
                        .addContext("parameter", condition.parameter));
            }
        }
    }
    return Result<void>::success();
}

Result<std::string> serializeAnimationClipAsset(const AnimationClipAsset& asset,
                                                const AnimationClipFormatLimits& limits) {
    auto valid = validateAnimationClipAsset(asset, limits);
    if (!valid)
        return Result<std::string>::failure(valid.error());

    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << "fglanim " << AnimationClipFormatVersion << '\n';
    stream << "clip_guid " << asset.guid.toString() << '\n';
    writeQuoted(stream, "clip_name", asset.name);
    writeFloat(stream, "duration", asset.durationSeconds);
    stream << "looping " << (asset.looping ? 1 : 0) << '\n';
    stream << "track_count " << asset.tracks.size() << '\n';
    for (const auto& [property, curve] : asset.tracks) {
        stream << "track_begin\n";
        writeQuoted(stream, "property", property);
        stream << "key_count " << curve.keys().size() << '\n';
        for (const auto& key : curve.keys()) {
            const auto previousPrecision = stream.precision();
            stream << std::setprecision(std::numeric_limits<float>::max_digits10) << "key "
                   << canonicalFloat(key.time) << ' ' << canonicalFloat(key.value) << ' '
                   << canonicalFloat(key.inTangent) << ' ' << canonicalFloat(key.outTangent) << ' '
                   << interpolationTag(key.interpolation) << '\n';
            stream.precision(previousPrecision);
        }
        stream << "track_end\n";
    }
    auto events = asset.events;
    std::sort(events.begin(), events.end(), [](const AnimationEvent& lhs,
                                               const AnimationEvent& rhs) {
        return std::tie(lhs.time, lhs.name) < std::tie(rhs.time, rhs.name);
    });
    stream << "event_count " << events.size() << '\n';
    for (const auto& event : events) {
        const auto previousPrecision = stream.precision();
        stream << std::setprecision(std::numeric_limits<float>::max_digits10) << "event "
               << canonicalFloat(event.time) << ' '
               << std::quoted(encodeControlCharacters(event.name)) << '\n';
        stream.precision(previousPrecision);
    }
    stream << "clip_end\n";
    auto output = stream.str();
    if (output.size() > limits.maximumSourceBytes) {
        return Result<std::string>::failure(
            Error(ErrorCode::CapacityExceeded, "serialized animation clip exceeds the source limit"));
    }
    return Result<std::string>::success(std::move(output));
}

Result<std::string>
serializeAnimatorControllerAsset(const AnimatorControllerAsset& asset,
                                 const AnimatorControllerFormatLimits& limits) {
    auto valid = validateAnimatorControllerAsset(asset, limits);
    if (!valid)
        return Result<std::string>::failure(valid.error());

    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << "fglcontroller " << AnimatorControllerFormatVersion << '\n';
    stream << "controller_guid " << asset.guid.toString() << '\n';
    writeQuoted(stream, "controller_name", asset.name);
    writeQuoted(stream, "initial_state", asset.initialState);
    stream << "parameter_count " << asset.parameters.size() << '\n';
    for (const auto& [name, parameter] : asset.parameters) {
        stream << "parameter " << std::quoted(encodeControlCharacters(name)) << ' '
               << parameterTypeTag(parameter.type);
        switch (parameter.type) {
        case AnimatorParameterType::Boolean:
            stream << ' ' << (parameter.booleanDefault ? 1 : 0);
            break;
        case AnimatorParameterType::Integer:
            stream << ' ' << parameter.integerDefault;
            break;
        case AnimatorParameterType::Float:
            stream << ' ' << std::setprecision(std::numeric_limits<float>::max_digits10)
                   << canonicalFloat(parameter.floatDefault);
            break;
        case AnimatorParameterType::Trigger:
            break;
        }
        stream << '\n';
    }
    stream << "state_count " << asset.states.size() << '\n';
    for (const auto& [name, state] : asset.states) {
        stream << "state " << std::quoted(encodeControlCharacters(name)) << ' '
               << state.clip.toString() << '\n';
    }
    stream << "transition_count " << asset.transitions.size() << '\n';
    for (const auto& transition : asset.transitions) {
        stream << "transition_begin\n";
        writeQuoted(stream, "from", transition.fromState);
        writeQuoted(stream, "to", transition.toState);
        writeFloat(stream, "minimum_normalized_time", transition.minimumNormalizedTime);
        stream << "has_exit_time " << (transition.hasExitTime ? 1 : 0) << '\n';
        writeFloat(stream, "exit_time", transition.exitTime);
        writeFloat(stream, "blend_duration", transition.blendDurationSeconds);
        auto conditions = transition.conditions;
        std::sort(conditions.begin(), conditions.end(), conditionLess);
        stream << "condition_count " << conditions.size() << '\n';
        for (const auto& condition : conditions) {
            stream << "condition " << std::quoted(encodeControlCharacters(condition.parameter))
                   << ' ' << conditionModeTag(condition.mode);
            switch (condition.mode) {
            case AnimationConditionMode::BooleanEquals:
                stream << ' ' << (condition.booleanValue ? 1 : 0);
                break;
            case AnimationConditionMode::IntegerEquals:
            case AnimationConditionMode::IntegerNotEquals:
            case AnimationConditionMode::IntegerGreater:
            case AnimationConditionMode::IntegerLess:
                stream << ' ' << condition.integerValue;
                break;
            case AnimationConditionMode::FloatGreater:
            case AnimationConditionMode::FloatLess:
                stream << ' ' << std::setprecision(std::numeric_limits<float>::max_digits10)
                       << canonicalFloat(condition.floatValue);
                break;
            case AnimationConditionMode::TriggerSet:
                break;
            }
            stream << '\n';
        }
        stream << "transition_end\n";
    }
    stream << "controller_end\n";
    auto output = stream.str();
    if (output.size() > limits.maximumSourceBytes) {
        return Result<std::string>::failure(
            Error(ErrorCode::CapacityExceeded,
                  "serialized animator controller exceeds the source limit"));
    }
    return Result<std::string>::success(std::move(output));
}

Result<AnimationClipAsset> deserializeAnimationClipAsset(
    const std::string_view text, const AnimationClipFormatLimits& limits) {
    if (!clipLimitsValid(limits)) {
        return Result<AnimationClipAsset>::failure(
            Error(ErrorCode::InvalidArgument, "animation clip format limits are invalid"));
    }
    if (text.size() > limits.maximumSourceBytes) {
        return Result<AnimationClipAsset>::failure(
            Error(ErrorCode::CapacityExceeded, "animation clip source exceeds the byte limit"));
    }

    LineReader reader(text);
    auto header = parseHeader(reader, "fglanim", AnimationClipFormatVersion);
    if (!header)
        return Result<AnimationClipAsset>::failure(header.error());
    auto guid = parseGuid(reader, "clip_guid");
    if (!guid)
        return Result<AnimationClipAsset>::failure(guid.error());
    auto name = parseQuoted(reader, "clip_name", limits.maximumStringBytes);
    if (!name)
        return Result<AnimationClipAsset>::failure(name.error());
    auto duration = parseFloat(reader, "duration");
    if (!duration)
        return Result<AnimationClipAsset>::failure(duration.error());
    auto looping = parseBoolean(reader, "looping");
    if (!looping)
        return Result<AnimationClipAsset>::failure(looping.error());

    AnimationClipAsset asset;
    asset.guid = guid.value();
    asset.name = std::move(name.value());
    asset.durationSeconds = duration.value();
    asset.looping = looping.value();

    auto trackCount = parseCount(reader, "track_count", limits.maximumTracks);
    if (!trackCount)
        return Result<AnimationClipAsset>::failure(trackCount.error());
    auto totalKeys = std::size_t{0};
    for (auto trackIndex = std::size_t{0}; trackIndex < trackCount.value(); ++trackIndex) {
        auto begin = expectLiteral(reader, "track_begin");
        if (!begin)
            return Result<AnimationClipAsset>::failure(begin.error());
        auto property = parseQuoted(reader, "property", limits.maximumStringBytes);
        if (!property)
            return Result<AnimationClipAsset>::failure(property.error());
        auto keyCount = parseCount(reader, "key_count", limits.maximumKeys);
        if (!keyCount)
            return Result<AnimationClipAsset>::failure(keyCount.error());
        if (keyCount.value() > limits.maximumKeys - totalKeys) {
            return Result<AnimationClipAsset>::failure(
                parseError(reader, "animation clip exceeds the cumulative key limit"));
        }
        totalKeys += keyCount.value();
        AnimationCurve curve;
        for (auto keyIndex = std::size_t{0}; keyIndex < keyCount.value(); ++keyIndex) {
            auto key = parseKey(reader);
            if (!key)
                return Result<AnimationClipAsset>::failure(key.error());
            const auto previousSize = curve.keys().size();
            auto added = curve.addKey(key.value());
            if (!added || curve.keys().size() != previousSize + 1U) {
                return Result<AnimationClipAsset>::failure(
                    parseError(reader, "animation key is invalid or duplicated"));
            }
        }
        auto end = expectLiteral(reader, "track_end");
        if (!end)
            return Result<AnimationClipAsset>::failure(end.error());
        if (!asset.tracks.emplace(std::move(property.value()), std::move(curve)).second) {
            return Result<AnimationClipAsset>::failure(
                parseError(reader, "animation track property is duplicated"));
        }
    }

    auto eventCount = parseCount(reader, "event_count", limits.maximumEvents);
    if (!eventCount)
        return Result<AnimationClipAsset>::failure(eventCount.error());
    asset.events.reserve(eventCount.value());
    for (auto eventIndex = std::size_t{0}; eventIndex < eventCount.value(); ++eventIndex) {
        auto event = parseEvent(reader, limits.maximumStringBytes);
        if (!event)
            return Result<AnimationClipAsset>::failure(event.error());
        asset.events.push_back(std::move(event.value()));
    }
    auto terminator = expectLiteral(reader, "clip_end");
    if (!terminator)
        return Result<AnimationClipAsset>::failure(terminator.error());
    auto finished = reader.requireEnd("clip_end");
    if (!finished)
        return Result<AnimationClipAsset>::failure(finished.error());
    auto valid = parsedValidationFailure(validateAnimationClipAsset(asset, limits));
    if (!valid)
        return Result<AnimationClipAsset>::failure(valid.error());
    return Result<AnimationClipAsset>::success(std::move(asset));
}

Result<AnimatorControllerAsset> deserializeAnimatorControllerAsset(
    const std::string_view text, const AnimatorControllerFormatLimits& limits) {
    if (!controllerLimitsValid(limits)) {
        return Result<AnimatorControllerAsset>::failure(
            Error(ErrorCode::InvalidArgument, "animator controller format limits are invalid"));
    }
    if (text.size() > limits.maximumSourceBytes) {
        return Result<AnimatorControllerAsset>::failure(
            Error(ErrorCode::CapacityExceeded,
                  "animator controller source exceeds the byte limit"));
    }

    LineReader reader(text);
    auto header = parseHeader(reader, "fglcontroller", AnimatorControllerFormatVersion);
    if (!header)
        return Result<AnimatorControllerAsset>::failure(header.error());
    auto guid = parseGuid(reader, "controller_guid");
    if (!guid)
        return Result<AnimatorControllerAsset>::failure(guid.error());
    auto name = parseQuoted(reader, "controller_name", limits.maximumStringBytes);
    if (!name)
        return Result<AnimatorControllerAsset>::failure(name.error());
    auto initialState = parseQuoted(reader, "initial_state", limits.maximumStringBytes);
    if (!initialState)
        return Result<AnimatorControllerAsset>::failure(initialState.error());

    AnimatorControllerAsset asset;
    asset.guid = guid.value();
    asset.name = std::move(name.value());
    asset.initialState = std::move(initialState.value());

    auto parameterCount = parseCount(reader, "parameter_count", limits.maximumParameters);
    if (!parameterCount)
        return Result<AnimatorControllerAsset>::failure(parameterCount.error());
    for (auto index = std::size_t{0}; index < parameterCount.value(); ++index) {
        auto parameter = parseParameter(reader, limits.maximumStringBytes);
        if (!parameter)
            return Result<AnimatorControllerAsset>::failure(parameter.error());
        if (!asset.parameters.emplace(std::move(parameter.value().first), parameter.value().second)
                 .second) {
            return Result<AnimatorControllerAsset>::failure(
                parseError(reader, "animator parameter name is duplicated"));
        }
    }

    auto stateCount = parseCount(reader, "state_count", limits.maximumStates);
    if (!stateCount)
        return Result<AnimatorControllerAsset>::failure(stateCount.error());
    for (auto index = std::size_t{0}; index < stateCount.value(); ++index) {
        auto state = parseState(reader, limits.maximumStringBytes);
        if (!state)
            return Result<AnimatorControllerAsset>::failure(state.error());
        if (!asset.states.emplace(std::move(state.value().first), state.value().second).second) {
            return Result<AnimatorControllerAsset>::failure(
                parseError(reader, "animator state name is duplicated"));
        }
    }

    auto transitionCount = parseCount(reader, "transition_count", limits.maximumTransitions);
    if (!transitionCount)
        return Result<AnimatorControllerAsset>::failure(transitionCount.error());
    asset.transitions.reserve(transitionCount.value());
    auto totalConditions = std::size_t{0};
    for (auto index = std::size_t{0}; index < transitionCount.value(); ++index) {
        auto begin = expectLiteral(reader, "transition_begin");
        if (!begin)
            return Result<AnimatorControllerAsset>::failure(begin.error());
        auto from = parseQuoted(reader, "from", limits.maximumStringBytes);
        if (!from)
            return Result<AnimatorControllerAsset>::failure(from.error());
        auto to = parseQuoted(reader, "to", limits.maximumStringBytes);
        if (!to)
            return Result<AnimatorControllerAsset>::failure(to.error());
        auto minimumTime = parseFloat(reader, "minimum_normalized_time");
        if (!minimumTime)
            return Result<AnimatorControllerAsset>::failure(minimumTime.error());
        auto hasExitTime = parseBoolean(reader, "has_exit_time");
        if (!hasExitTime)
            return Result<AnimatorControllerAsset>::failure(hasExitTime.error());
        auto exitTime = parseFloat(reader, "exit_time");
        if (!exitTime)
            return Result<AnimatorControllerAsset>::failure(exitTime.error());
        auto blendDuration = parseFloat(reader, "blend_duration");
        if (!blendDuration)
            return Result<AnimatorControllerAsset>::failure(blendDuration.error());
        auto conditionCount = parseCount(reader, "condition_count", limits.maximumConditions);
        if (!conditionCount)
            return Result<AnimatorControllerAsset>::failure(conditionCount.error());
        if (conditionCount.value() > limits.maximumConditions - totalConditions) {
            return Result<AnimatorControllerAsset>::failure(
                parseError(reader, "animator controller exceeds the cumulative condition limit"));
        }
        totalConditions += conditionCount.value();
        AnimatorTransitionDefinition transition;
        transition.fromState = std::move(from.value());
        transition.toState = std::move(to.value());
        transition.minimumNormalizedTime = minimumTime.value();
        transition.hasExitTime = hasExitTime.value();
        transition.exitTime = exitTime.value();
        transition.blendDurationSeconds = blendDuration.value();
        transition.conditions.reserve(conditionCount.value());
        for (auto conditionIndex = std::size_t{0}; conditionIndex < conditionCount.value();
             ++conditionIndex) {
            auto condition = parseCondition(reader, limits.maximumStringBytes);
            if (!condition)
                return Result<AnimatorControllerAsset>::failure(condition.error());
            transition.conditions.push_back(std::move(condition.value()));
        }
        auto end = expectLiteral(reader, "transition_end");
        if (!end)
            return Result<AnimatorControllerAsset>::failure(end.error());
        asset.transitions.push_back(std::move(transition));
    }
    auto terminator = expectLiteral(reader, "controller_end");
    if (!terminator)
        return Result<AnimatorControllerAsset>::failure(terminator.error());
    auto finished = reader.requireEnd("controller_end");
    if (!finished)
        return Result<AnimatorControllerAsset>::failure(finished.error());
    auto valid = parsedValidationFailure(validateAnimatorControllerAsset(asset, limits));
    if (!valid)
        return Result<AnimatorControllerAsset>::failure(valid.error());
    return Result<AnimatorControllerAsset>::success(std::move(asset));
}

Result<std::shared_ptr<const AnimationClip>>
buildAnimationClip(const AnimationClipAsset& asset, const AnimationClipFormatLimits& limits) {
    auto valid = validateAnimationClipAsset(asset, limits);
    if (!valid)
        return Result<std::shared_ptr<const AnimationClip>>::failure(valid.error());
    auto clip = std::make_shared<AnimationClip>(asset.name, asset.durationSeconds, asset.looping);
    for (const auto& [property, curve] : asset.tracks) {
        auto added = clip->addTrack(property, curve);
        if (!added) {
            return Result<std::shared_ptr<const AnimationClip>>::failure(
                added.error().withContext("property", property));
        }
    }
    auto events = asset.events;
    std::sort(events.begin(), events.end(), [](const AnimationEvent& lhs,
                                               const AnimationEvent& rhs) {
        return std::tie(lhs.time, lhs.name) < std::tie(rhs.time, rhs.name);
    });
    for (auto& event : events) {
        auto added = clip->addEvent(std::move(event));
        if (!added)
            return Result<std::shared_ptr<const AnimationClip>>::failure(added.error());
    }
    return Result<std::shared_ptr<const AnimationClip>>::success(std::move(clip));
}

Result<std::unique_ptr<AnimatorController>>
buildAnimatorController(const AnimatorControllerAsset& asset,
                        const AnimationClipResolver& clipResolver,
                        const AnimatorControllerFormatLimits& limits) {
    if (!clipResolver) {
        return Result<std::unique_ptr<AnimatorController>>::failure(
            Error(ErrorCode::InvalidArgument, "animator controller requires a clip resolver"));
    }
    auto valid = validateAnimatorControllerAsset(asset, limits);
    if (!valid)
        return Result<std::unique_ptr<AnimatorController>>::failure(valid.error());

    auto controller = std::make_unique<AnimatorController>();
    std::map<AssetGuid, std::shared_ptr<const AnimationClip>> resolvedClips;
    for (const auto& [stateName, state] : asset.states) {
        auto cached = resolvedClips.find(state.clip);
        if (cached == resolvedClips.end()) {
            auto resolved = clipResolver(state.clip);
            if (!resolved) {
                return Result<std::unique_ptr<AnimatorController>>::failure(
                    resolved.error()
                        .withContext("state", stateName)
                        .withContext("clip", state.clip.toString()));
            }
            if (!resolved.value() || !resolved.value()->valid()) {
                return Result<std::unique_ptr<AnimatorController>>::failure(
                    Error(ErrorCode::NotFound, "clip resolver returned no valid animation clip")
                        .addContext("state", stateName)
                        .addContext("clip", state.clip.toString()));
            }
            cached = resolvedClips.emplace(state.clip, resolved.value()).first;
        }
        auto added = controller->addState(stateName, cached->second);
        if (!added)
            return Result<std::unique_ptr<AnimatorController>>::failure(added.error());
    }

    for (const auto& [name, parameter] : asset.parameters) {
        switch (parameter.type) {
        case AnimatorParameterType::Boolean:
            controller->setBoolean(name, parameter.booleanDefault);
            break;
        case AnimatorParameterType::Integer:
            controller->setInteger(name, parameter.integerDefault);
            break;
        case AnimatorParameterType::Float:
            controller->setFloat(name, parameter.floatDefault);
            break;
        case AnimatorParameterType::Trigger:
            controller->resetTrigger(name);
            break;
        }
    }
    for (const auto& definition : asset.transitions) {
        AnimationTransition transition;
        transition.fromState = definition.fromState;
        transition.toState = definition.toState;
        transition.minimumNormalizedTime = definition.minimumNormalizedTime;
        transition.conditions = definition.conditions;
        transition.hasExitTime = definition.hasExitTime;
        transition.exitTime = definition.exitTime;
        transition.blendDurationSeconds = definition.blendDurationSeconds;
        auto added = controller->addTransition(std::move(transition));
        if (!added)
            return Result<std::unique_ptr<AnimatorController>>::failure(added.error());
    }
    auto played = controller->play(asset.initialState);
    if (!played)
        return Result<std::unique_ptr<AnimatorController>>::failure(played.error());
    return Result<std::unique_ptr<AnimatorController>>::success(std::move(controller));
}

} // namespace fabgl
