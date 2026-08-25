#include "project_format.h"

#include <fabgl/assets/file_io.h>
#include <fabgl/core/guid.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace fabgl::project {
namespace {

constexpr std::size_t MaximumProjectBytes = 1024U * 1024U;
constexpr std::size_t MaximumJsonDepth = 32U;
constexpr std::size_t MaximumJsonValues = 100000U;
constexpr std::size_t MaximumJsonArrayValues = 4096U;
constexpr std::size_t MaximumJsonObjectFields = 512U;
constexpr std::size_t MaximumJsonStringBytes = 16U * 1024U;
constexpr std::size_t MaximumLegacyAssetIndexBytes = 4U * 1024U * 1024U;
constexpr std::size_t MaximumBuildArguments = 64U;
constexpr std::size_t MaximumAssets = 4096U;
constexpr std::size_t MaximumInputContexts = 32U;
constexpr std::size_t MaximumInputValuesPerContext = 128U;
constexpr std::size_t MaximumBindingsPerValue = 16U;
constexpr std::size_t MaximumTotalBindings = 2048U;
constexpr std::size_t MaximumPackageDependencies = 256U;

struct JsonValue final {
    enum class Type {
        Null,
        Boolean,
        Number,
        String,
        Array,
        Object,
    };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;
};

[[nodiscard]] std::string escapeJson(std::string_view value);

void writeCanonicalJson(std::ostringstream& output, const JsonValue& value) {
    switch (value.type) {
    case JsonValue::Type::Null:
        output << "null";
        break;
    case JsonValue::Type::Boolean:
        output << (value.boolean ? "true" : "false");
        break;
    case JsonValue::Type::Number:
        output << std::setprecision(17) << value.number;
        break;
    case JsonValue::Type::String:
        output << '"' << escapeJson(value.string) << '"';
        break;
    case JsonValue::Type::Array:
        output << '[';
        for (std::size_t index = 0U; index < value.array.size(); ++index) {
            if (index != 0U)
                output << ',';
            writeCanonicalJson(output, value.array[index]);
        }
        output << ']';
        break;
    case JsonValue::Type::Object: {
        auto fields = value.object;
        std::sort(fields.begin(), fields.end(),
                  [](const auto& left, const auto& right) { return left.first < right.first; });
        output << '{';
        for (std::size_t index = 0U; index < fields.size(); ++index) {
            if (index != 0U)
                output << ',';
            output << '"' << escapeJson(fields[index].first) << "\":";
            writeCanonicalJson(output, fields[index].second);
        }
        output << '}';
        break;
    }
    }
}

[[nodiscard]] Result<std::string> canonicalSettings(const JsonValue& value) {
    if (value.type != JsonValue::Type::Object) {
        return Result<std::string>::failure(
            Error(ErrorCode::InvalidFormat, "asset import settings must be a JSON object"));
    }
    std::ostringstream output;
    writeCanonicalJson(output, value);
    auto text = output.str();
    if (text.size() > 16U * 1024U) {
        return Result<std::string>::failure(
            Error(ErrorCode::CapacityExceeded, "asset import settings exceed 16 KiB"));
    }
    return Result<std::string>::success(std::move(text));
}

class JsonParser final {
  public:
    explicit JsonParser(std::string_view input) : input_(input) {}

    Result<JsonValue> parse() {
        skipWhitespace();
        auto value = parseValue(0U);
        if (!value)
            return value;
        skipWhitespace();
        if (position_ != input_.size())
            return failure("unexpected data after project JSON");
        return value;
    }

  private:
    Result<JsonValue> parseValue(std::size_t depth) {
        if (depth > MaximumJsonDepth)
            return failure("project JSON nesting limit exceeded");
        if (++valueCount_ > MaximumJsonValues)
            return failure("project JSON value-count limit exceeded");
        skipWhitespace();
        if (position_ >= input_.size())
            return failure("missing JSON value");
        switch (input_[position_]) {
        case '{':
            return parseObject(depth);
        case '[':
            return parseArray(depth);
        case '"': {
            auto text = parseString();
            if (!text)
                return Result<JsonValue>::failure(text.error());
            JsonValue value;
            value.type = JsonValue::Type::String;
            value.string = std::move(text.value());
            return Result<JsonValue>::success(std::move(value));
        }
        case 't':
            return parseLiteral("true", JsonValue::Type::Boolean, true);
        case 'f':
            return parseLiteral("false", JsonValue::Type::Boolean, false);
        case 'n':
            return parseLiteral("null", JsonValue::Type::Null, false);
        default:
            return parseNumber();
        }
    }

    Result<JsonValue> parseObject(std::size_t depth) {
        ++position_;
        JsonValue value;
        value.type = JsonValue::Type::Object;
        std::set<std::string> keys;
        skipWhitespace();
        if (consume('}'))
            return Result<JsonValue>::success(std::move(value));
        while (true) {
            if (value.object.size() >= MaximumJsonObjectFields)
                return failure("project JSON object field-count limit exceeded");
            auto key = parseString();
            if (!key)
                return Result<JsonValue>::failure(key.error());
            if (!keys.insert(key.value()).second) {
                return Result<JsonValue>::failure(
                    error("duplicate JSON object field").addContext("field", key.value()));
            }
            skipWhitespace();
            if (!consume(':'))
                return failure("expected ':' after JSON object field");
            auto child = parseValue(depth + 1U);
            if (!child)
                return child;
            value.object.emplace_back(std::move(key.value()), std::move(child.value()));
            skipWhitespace();
            if (consume('}'))
                return Result<JsonValue>::success(std::move(value));
            if (!consume(','))
                return failure("expected ',' or '}' in JSON object");
            skipWhitespace();
        }
    }

    Result<JsonValue> parseArray(std::size_t depth) {
        ++position_;
        JsonValue value;
        value.type = JsonValue::Type::Array;
        skipWhitespace();
        if (consume(']'))
            return Result<JsonValue>::success(std::move(value));
        while (true) {
            if (value.array.size() >= MaximumJsonArrayValues)
                return failure("project JSON array size limit exceeded");
            auto child = parseValue(depth + 1U);
            if (!child)
                return child;
            value.array.push_back(std::move(child.value()));
            skipWhitespace();
            if (consume(']'))
                return Result<JsonValue>::success(std::move(value));
            if (!consume(','))
                return failure("expected ',' or ']' in JSON array");
            skipWhitespace();
        }
    }

    Result<JsonValue> parseNumber() {
        const auto start = position_;
        if (consume('-') && position_ >= input_.size())
            return failure("truncated JSON number");
        if (consume('0')) {
            if (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9')
                return failure("JSON number has a leading zero");
        } else {
            if (position_ >= input_.size() || input_[position_] < '1' || input_[position_] > '9') {
                return failure("invalid JSON value");
            }
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') {
                ++position_;
            }
        }
        if (consume('.')) {
            if (position_ >= input_.size() || input_[position_] < '0' || input_[position_] > '9') {
                return failure("JSON fraction requires a digit");
            }
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') {
                ++position_;
            }
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() &&
                (input_[position_] == '+' || input_[position_] == '-')) {
                ++position_;
            }
            if (position_ >= input_.size() || input_[position_] < '0' || input_[position_] > '9') {
                return failure("JSON exponent requires a digit");
            }
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') {
                ++position_;
            }
        }
        const auto token = input_.substr(start, position_ - start);
        double parsed = 0.0;
        const auto converted = std::from_chars(token.data(), token.data() + token.size(), parsed,
                                               std::chars_format::general);
        if (converted.ec != std::errc{} || converted.ptr != token.data() + token.size() ||
            !std::isfinite(parsed)) {
            return failure("JSON number is invalid or non-finite");
        }
        JsonValue value;
        value.type = JsonValue::Type::Number;
        value.number = parsed;
        return Result<JsonValue>::success(std::move(value));
    }

    Result<JsonValue> parseLiteral(std::string_view literal, JsonValue::Type type, bool boolean) {
        if (input_.substr(position_, literal.size()) != literal)
            return failure("invalid JSON literal");
        position_ += literal.size();
        JsonValue value;
        value.type = type;
        value.boolean = boolean;
        return Result<JsonValue>::success(std::move(value));
    }

    Result<std::string> parseString() {
        if (!consume('"'))
            return Result<std::string>::failure(error("expected JSON string"));
        std::string output;
        while (position_ < input_.size()) {
            const auto character = input_[position_++];
            if (character == '"') {
                if (!validUtf8(output))
                    return Result<std::string>::failure(error("JSON string is not valid UTF-8"));
                return Result<std::string>::success(std::move(output));
            }
            if (static_cast<unsigned char>(character) < 0x20U)
                return Result<std::string>::failure(error("control character in JSON string"));
            if (character != '\\') {
                output += character;
            } else {
                if (position_ >= input_.size())
                    return Result<std::string>::failure(error("unterminated JSON escape"));
                const auto escaped = input_[position_++];
                switch (escaped) {
                case '"':
                    output += '"';
                    break;
                case '\\':
                    output += '\\';
                    break;
                case '/':
                    output += '/';
                    break;
                case 'b':
                    output += '\b';
                    break;
                case 'f':
                    output += '\f';
                    break;
                case 'n':
                    output += '\n';
                    break;
                case 'r':
                    output += '\r';
                    break;
                case 't':
                    output += '\t';
                    break;
                case 'u': {
                    auto codePoint = parseHex4();
                    if (!codePoint)
                        return Result<std::string>::failure(codePoint.error());
                    auto decoded = codePoint.value();
                    if (decoded >= 0xD800U && decoded <= 0xDBFFU) {
                        if (input_.size() - position_ < 6U || input_[position_] != '\\' ||
                            input_[position_ + 1U] != 'u') {
                            return Result<std::string>::failure(
                                error("high surrogate lacks low surrogate"));
                        }
                        position_ += 2U;
                        auto low = parseHex4();
                        if (!low)
                            return Result<std::string>::failure(low.error());
                        if (low.value() < 0xDC00U || low.value() > 0xDFFFU)
                            return Result<std::string>::failure(error("invalid low surrogate"));
                        decoded = 0x10000U + ((decoded - 0xD800U) << 10U) + (low.value() - 0xDC00U);
                    } else if (decoded >= 0xDC00U && decoded <= 0xDFFFU) {
                        return Result<std::string>::failure(error("unexpected low surrogate"));
                    }
                    appendUtf8(output, decoded);
                    break;
                }
                default:
                    return Result<std::string>::failure(error("invalid JSON escape"));
                }
            }
            if (output.size() > MaximumJsonStringBytes)
                return Result<std::string>::failure(error("JSON string size limit exceeded"));
        }
        return Result<std::string>::failure(error("unterminated JSON string"));
    }

    Result<std::uint32_t> parseHex4() {
        if (input_.size() - position_ < 4U)
            return Result<std::uint32_t>::failure(error("truncated Unicode escape"));
        std::uint32_t value = 0U;
        for (auto index = 0; index < 4; ++index) {
            const auto character = input_[position_++];
            value <<= 4U;
            if (character >= '0' && character <= '9')
                value |= static_cast<std::uint32_t>(character - '0');
            else if (character >= 'a' && character <= 'f')
                value |= static_cast<std::uint32_t>(character - 'a' + 10);
            else if (character >= 'A' && character <= 'F')
                value |= static_cast<std::uint32_t>(character - 'A' + 10);
            else
                return Result<std::uint32_t>::failure(error("invalid Unicode escape"));
        }
        return Result<std::uint32_t>::success(value);
    }

    static void appendUtf8(std::string& output, std::uint32_t codePoint) {
        if (codePoint <= 0x7FU) {
            output += static_cast<char>(codePoint);
        } else if (codePoint <= 0x7FFU) {
            output += static_cast<char>(0xC0U | (codePoint >> 6U));
            output += static_cast<char>(0x80U | (codePoint & 0x3FU));
        } else if (codePoint <= 0xFFFFU) {
            output += static_cast<char>(0xE0U | (codePoint >> 12U));
            output += static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU));
            output += static_cast<char>(0x80U | (codePoint & 0x3FU));
        } else {
            output += static_cast<char>(0xF0U | (codePoint >> 18U));
            output += static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU));
            output += static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU));
            output += static_cast<char>(0x80U | (codePoint & 0x3FU));
        }
    }

    static bool validUtf8(std::string_view value) noexcept {
        std::size_t index = 0U;
        while (index < value.size()) {
            const auto first = static_cast<unsigned char>(value[index++]);
            if (first <= 0x7FU)
                continue;
            std::size_t continuationCount = 0U;
            std::uint32_t codePoint = 0U;
            if (first >= 0xC2U && first <= 0xDFU) {
                continuationCount = 1U;
                codePoint = first & 0x1FU;
            } else if (first >= 0xE0U && first <= 0xEFU) {
                continuationCount = 2U;
                codePoint = first & 0x0FU;
            } else if (first >= 0xF0U && first <= 0xF4U) {
                continuationCount = 3U;
                codePoint = first & 0x07U;
            } else {
                return false;
            }
            if (value.size() - index < continuationCount)
                return false;
            for (std::size_t continuation = 0U; continuation < continuationCount; ++continuation) {
                const auto byte = static_cast<unsigned char>(value[index++]);
                if ((byte & 0xC0U) != 0x80U)
                    return false;
                codePoint = (codePoint << 6U) | (byte & 0x3FU);
            }
            if ((continuationCount == 2U && codePoint < 0x800U) ||
                (continuationCount == 3U && codePoint < 0x10000U) || codePoint > 0x10FFFFU ||
                (codePoint >= 0xD800U && codePoint <= 0xDFFFU)) {
                return false;
            }
        }
        return true;
    }

    void skipWhitespace() noexcept {
        while (position_ < input_.size() &&
               (input_[position_] == ' ' || input_[position_] == '\t' ||
                input_[position_] == '\r' || input_[position_] == '\n')) {
            ++position_;
        }
    }

    bool consume(char character) noexcept {
        if (position_ < input_.size() && input_[position_] == character) {
            ++position_;
            return true;
        }
        return false;
    }

    Error error(std::string message) const {
        return Error(ErrorCode::InvalidFormat, std::move(message))
            .addContext("byte", std::to_string(position_));
    }

    Result<JsonValue> failure(std::string message) const {
        return Result<JsonValue>::failure(error(std::move(message)));
    }

    std::string_view input_;
    std::size_t position_ = 0U;
    std::size_t valueCount_ = 0U;
};

const JsonValue* findField(const JsonValue& object, std::string_view name) {
    const auto found = std::find_if(object.object.begin(), object.object.end(),
                                    [name](const auto& field) { return field.first == name; });
    return found == object.object.end() ? nullptr : &found->second;
}

Error fieldError(std::string message, std::string_view field) {
    return Error(ErrorCode::InvalidFormat, std::move(message))
        .addContext("field", std::string(field));
}

Result<const JsonValue*> requiredField(const JsonValue& object, std::string_view name,
                                       JsonValue::Type type) {
    const auto* value = findField(object, name);
    if (value == nullptr)
        return Result<const JsonValue*>::failure(
            fieldError("required project field is missing", name));
    if (value->type != type)
        return Result<const JsonValue*>::failure(
            fieldError("project field has the wrong JSON type", name));
    return Result<const JsonValue*>::success(value);
}

Result<void> rejectUnknownFields(const JsonValue& object, const std::set<std::string_view>& allowed,
                                 std::string_view objectName) {
    for (const auto& field : object.object) {
        if (allowed.find(field.first) == allowed.end()) {
            return Result<void>::failure(Error(ErrorCode::InvalidFormat, "unknown project field")
                                             .addContext("object", std::string(objectName))
                                             .addContext("field", field.first));
        }
    }
    return Result<void>::success();
}

Result<int> integerValue(const JsonValue& value, std::string_view field, int minimum, int maximum) {
    if (value.type != JsonValue::Type::Number || std::trunc(value.number) != value.number ||
        value.number < static_cast<double>(minimum) ||
        value.number > static_cast<double>(maximum)) {
        return Result<int>::failure(
            fieldError("project integer is outside its valid range", field));
    }
    return Result<int>::success(static_cast<int>(value.number));
}

bool validText(std::string_view value, std::size_t maximumBytes, bool allowEmpty = false) {
    if ((!allowEmpty && value.empty()) || value.size() > maximumBytes)
        return false;
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20U || byte == 0x7FU)
            return false;
    }
    return true;
}

bool validStableId(std::string_view value) {
    if (value.empty() || value.size() > 80U ||
        !std::isalnum(static_cast<unsigned char>(value.front()))) {
        return false;
    }
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (!std::islower(byte) && !std::isdigit(byte) && character != '.' && character != '_' &&
            character != '-') {
            return false;
        }
    }
    return true;
}

std::string canonicalAssetPath(std::string_view value) {
    std::string path(value);
    std::replace(path.begin(), path.end(), '\\', '/');
    while (path.find("//") != std::string::npos)
        path.replace(path.find("//"), 2U, "/");
    return path;
}

std::string portablePathKey(std::string_view value) {
    auto path = canonicalAssetPath(value);
    std::transform(path.begin(), path.end(), path.begin(), [](const unsigned char character) {
        return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a')
                                                    : static_cast<char>(character);
    });
    return path;
}

Result<void> validateInputValue(const InputValueDefinition& value, std::string_view category,
                                std::size_t& totalBindings, ErrorCode code) {
    if (!validText(value.name, 80U) || value.bindings.empty() ||
        value.bindings.size() > MaximumBindingsPerValue) {
        return Result<void>::failure(
            Error(code, "project input value name or binding count is invalid")
                .addContext("category", std::string(category))
                .addContext("name", value.name));
    }
    if (totalBindings > MaximumTotalBindings - value.bindings.size()) {
        return Result<void>::failure(Error(code, "project input binding-count limit exceeded"));
    }
    totalBindings += value.bindings.size();
    std::set<std::string> controls;
    for (const auto& binding : value.bindings) {
        if (!validText(binding.control, 128U) || !controls.insert(binding.control).second ||
            !std::isfinite(binding.scale) || std::fabs(binding.scale) > 16.0F ||
            !std::isfinite(binding.threshold) || binding.threshold < 0.0F ||
            binding.threshold > 1.0F) {
            return Result<void>::failure(Error(code, "project input binding is invalid")
                                             .addContext("category", std::string(category))
                                             .addContext("name", value.name)
                                             .addContext("control", binding.control));
        }
    }
    return Result<void>::success();
}

Result<void> validateManifestModel(const Manifest& manifest, ErrorCode code) {
    if (!AssetGuid::parse(manifest.projectGuid) || !validText(manifest.name, 160U) ||
        manifest.projectRoot != "." || manifest.startupScene.size() > 240U ||
        !assets::isSafeRelativePath(manifest.startupScene) ||
        !validText(manifest.previewDemo, 80U, true) || !validText(manifest.buildProgram, 1024U) ||
        manifest.buildArguments.size() > MaximumBuildArguments) {
        return Result<void>::failure(
            Error(code, "project identity, path, or build model is invalid"));
    }
    for (const auto& argument : manifest.buildArguments) {
        if (!validText(argument, MaximumJsonStringBytes, true))
            return Result<void>::failure(Error(code, "project build argument is invalid"));
    }
    if (manifest.assets.size() > MaximumAssets)
        return Result<void>::failure(Error(code, "project asset-count limit exceeded"));
    std::set<std::string> assetGuids;
    std::set<std::string> assetPaths;
    for (const auto& asset : manifest.assets) {
        const auto path = canonicalAssetPath(asset.path);
        const auto guid = asset.guid.toString();
        if (asset.guid.isNil() || !assetGuids.insert(guid).second || !validText(path, 512U) ||
            !assets::isSafeRelativePath(path) || !assetPaths.insert(portablePathKey(path)).second ||
            !validStableId(asset.type)) {
            return Result<void>::failure(
                Error(code, "project asset GUID, path, or type is invalid or duplicated")
                    .addContext("assetGuid", guid)
                    .addContext("assetPath", asset.path)
                    .addContext("assetType", asset.type));
        }
        if (asset.dependencies.size() > MaximumAssets ||
            asset.esp32Target == assets::AssetTarget::Pc) {
            return Result<void>::failure(Error(code, "project asset import metadata is invalid")
                                             .addContext("assetGuid", guid));
        }
        auto settings = JsonParser(asset.importSettings).parse();
        if (!settings || settings.value().type != JsonValue::Type::Object ||
            asset.importSettings.size() > 16U * 1024U) {
            return Result<void>::failure(Error(code, "project asset import settings are invalid")
                                             .addContext("assetGuid", guid));
        }
        const auto extensionPosition = path.find_last_of('.');
        std::string extension = extensionPosition == std::string::npos
                                    ? std::string{}
                                    : path.substr(extensionPosition + 1U);
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](const unsigned char character) {
                           return static_cast<char>(std::tolower(character));
                       });
        if (extension == "png" || extension == "jpg" || extension == "jpeg" || extension == "bmp") {
            auto decoded = decodeProjectImageImportSettings(asset.importSettings);
            if (!decoded)
                return Result<void>::failure(decoded.error().withContext("assetGuid", guid));
            const bool atlas = decoded.value().outputKind == assets::ImageOutputKind::SpriteAtlas;
            if ((atlas && asset.type != "sprite.atlas") || (!atlas && asset.type != "image")) {
                return Result<void>::failure(
                    Error(code,
                          "image source type must match its canonical image or sprite-atlas output")
                        .addContext("assetGuid", guid)
                        .addContext("assetType", asset.type)
                        .addContext("output", atlas ? "sprite.atlas" : "image"));
            }
        } else if (extension == "wav") {
            auto decoded = decodeProjectAudioImportSettings(asset.importSettings);
            if (!decoded)
                return Result<void>::failure(decoded.error().withContext("assetGuid", guid));
        }
    }
    for (const auto& asset : manifest.assets) {
        std::set<AssetGuid> dependencies;
        for (const auto dependency : asset.dependencies) {
            if (dependency.isNil() || dependency == asset.guid ||
                !dependencies.insert(dependency).second ||
                assetGuids.find(dependency.toString()) == assetGuids.end()) {
                return Result<void>::failure(Error(code, "project asset dependency is invalid")
                                                 .addContext("assetGuid", asset.guid.toString())
                                                 .addContext("dependency", dependency.toString()));
            }
        }
    }
    if (manifest.inputContexts.size() > MaximumInputContexts)
        return Result<void>::failure(Error(code, "project input context-count limit exceeded"));
    std::set<std::string> contextNames;
    std::size_t totalBindings = 0U;
    for (const auto& context : manifest.inputContexts) {
        if (!validText(context.name, 80U) || !contextNames.insert(context.name).second ||
            context.priority < -1000000 || context.priority > 1000000 ||
            context.actions.size() > MaximumInputValuesPerContext ||
            context.axes.size() > MaximumInputValuesPerContext) {
            return Result<void>::failure(Error(code, "project input context is invalid")
                                             .addContext("context", context.name));
        }
        std::set<std::string> actionNames;
        for (const auto& action : context.actions) {
            if (!actionNames.insert(action.name).second)
                return Result<void>::failure(Error(code, "duplicate project input action")
                                                 .addContext("context", context.name)
                                                 .addContext("action", action.name));
            auto valid = validateInputValue(action, "action", totalBindings, code);
            if (!valid)
                return valid;
        }
        std::set<std::string> axisNames;
        for (const auto& axis : context.axes) {
            if (!axisNames.insert(axis.name).second)
                return Result<void>::failure(Error(code, "duplicate project input axis")
                                                 .addContext("context", context.name)
                                                 .addContext("axis", axis.name));
            auto valid = validateInputValue(axis, "axis", totalBindings, code);
            if (!valid)
                return valid;
        }
    }
    if (manifest.packageDependencies.size() > MaximumPackageDependencies)
        return Result<void>::failure(
            Error(code, "project package dependency-count limit exceeded"));
    std::set<std::string> packageIds;
    for (const auto& package : manifest.packageDependencies) {
        if (!validStableId(package.id) || !packageIds.insert(package.id).second) {
            return Result<void>::failure(
                Error(code, "project package dependency ID is invalid or duplicated")
                    .addContext("package", package.id));
        }
        const auto versionText = package.version.toString();
        if (!VersionRequirement::parse(versionText)) {
            return Result<void>::failure(
                Error(code, "project package version requirement is invalid")
                    .addContext("package", package.id));
        }
    }
    if (!validStableId(manifest.targetProfiles.pc) ||
        !validStableId(manifest.targetProfiles.esp32)) {
        return Result<void>::failure(Error(code, "selected project target profile is invalid"));
    }
    if (manifest.performance.version != PerformanceBudgetSettings::CurrentVersion ||
        !validPerformanceBudgetProfile(manifest.performance.pcProfile) ||
        !validPerformanceBudgetProfile(manifest.performance.esp32Profile) ||
        !validPerformanceBudget(manifest.performance.pcCustom) ||
        !validPerformanceBudget(manifest.performance.esp32Custom)) {
        return Result<void>::failure(Error(code, "project performance budget is invalid"));
    }
    return Result<void>::success();
}

Result<void> decodeAssets(const JsonValue& value, Manifest& manifest) {
    if (value.type != JsonValue::Type::Array || value.array.size() > MaximumAssets)
        return Result<void>::failure(fieldError("assets must be a bounded array", "assets"));
    for (const auto& assetValue : value.array) {
        if (assetValue.type != JsonValue::Type::Object)
            return Result<void>::failure(fieldError("asset entry must be an object", "assets"));
        auto unknown =
            rejectUnknownFields(assetValue, {"guid", "path", "type", "import"}, "asset entry");
        if (!unknown)
            return unknown;
        auto guid = requiredField(assetValue, "guid", JsonValue::Type::String);
        auto path = requiredField(assetValue, "path", JsonValue::Type::String);
        auto type = requiredField(assetValue, "type", JsonValue::Type::String);
        if (!guid)
            return Result<void>::failure(guid.error());
        if (!path)
            return Result<void>::failure(path.error());
        if (!type)
            return Result<void>::failure(type.error());
        auto parsedGuid = AssetGuid::parse(guid.value()->string);
        if (!parsedGuid || parsedGuid.value().isNil())
            return Result<void>::failure(fieldError("asset GUID is invalid", "assets.guid"));
        ProjectAssetEntry entry;
        entry.guid = parsedGuid.value();
        entry.path = canonicalAssetPath(path.value()->string);
        entry.type = type.value()->string;
        if (const auto* import = findField(assetValue, "import")) {
            if (import->type != JsonValue::Type::Object) {
                return Result<void>::failure(
                    fieldError("asset import metadata must be an object", "assets.import"));
            }
            unknown = rejectUnknownFields(*import, {"settings", "esp32Target", "dependencies"},
                                          "asset import metadata");
            if (!unknown)
                return unknown;
            if (const auto* settings = findField(*import, "settings")) {
                auto canonical = canonicalSettings(*settings);
                if (!canonical)
                    return Result<void>::failure(canonical.error());
                entry.importSettings = std::move(canonical.value());
            }
            if (const auto* target = findField(*import, "esp32Target")) {
                if (target->type != JsonValue::Type::String) {
                    return Result<void>::failure(fieldError("asset ESP32 target must be a string",
                                                            "assets.import.esp32Target"));
                }
                if (target->string == "flash")
                    entry.esp32Target = assets::AssetTarget::Esp32Flash;
                else if (target->string == "psram")
                    entry.esp32Target = assets::AssetTarget::Esp32Psram;
                else if (target->string == "sd")
                    entry.esp32Target = assets::AssetTarget::Esp32Sd;
                else
                    return Result<void>::failure(
                        fieldError("asset ESP32 target must be flash, psram, or sd",
                                   "assets.import.esp32Target"));
            }
            if (const auto* dependencies = findField(*import, "dependencies")) {
                if (dependencies->type != JsonValue::Type::Array ||
                    dependencies->array.size() > MaximumAssets) {
                    return Result<void>::failure(fieldError("asset dependency list is invalid",
                                                            "assets.import.dependencies"));
                }
                for (const auto& dependency : dependencies->array) {
                    if (dependency.type != JsonValue::Type::String) {
                        return Result<void>::failure(
                            fieldError("asset dependency GUID must be a string",
                                       "assets.import.dependencies"));
                    }
                    auto parsed = AssetGuid::parse(dependency.string);
                    if (!parsed || parsed.value().isNil()) {
                        return Result<void>::failure(fieldError("asset dependency GUID is invalid",
                                                                "assets.import.dependencies"));
                    }
                    entry.dependencies.push_back(parsed.value());
                }
            }
            entry.hasImportMetadata = true;
        }
        manifest.assets.push_back(std::move(entry));
    }
    return Result<void>::success();
}

Result<InputBindingDefinition> decodeBinding(const JsonValue& value) {
    if (value.type != JsonValue::Type::Object)
        return Result<InputBindingDefinition>::failure(
            fieldError("input binding must be an object", "binding"));
    auto unknown = rejectUnknownFields(value, {"control", "scale", "threshold"}, "input binding");
    if (!unknown)
        return Result<InputBindingDefinition>::failure(unknown.error());
    auto control = requiredField(value, "control", JsonValue::Type::String);
    auto scale = requiredField(value, "scale", JsonValue::Type::Number);
    auto threshold = requiredField(value, "threshold", JsonValue::Type::Number);
    if (!control)
        return Result<InputBindingDefinition>::failure(control.error());
    if (!scale)
        return Result<InputBindingDefinition>::failure(scale.error());
    if (!threshold)
        return Result<InputBindingDefinition>::failure(threshold.error());
    if (scale.value()->number < -16.0 || scale.value()->number > 16.0 ||
        threshold.value()->number < 0.0 || threshold.value()->number > 1.0) {
        return Result<InputBindingDefinition>::failure(
            fieldError("input binding number is outside its valid range", "binding"));
    }
    InputBindingDefinition binding;
    binding.control = control.value()->string;
    binding.scale = static_cast<float>(scale.value()->number);
    binding.threshold = static_cast<float>(threshold.value()->number);
    if (!std::isfinite(binding.scale) || !std::isfinite(binding.threshold))
        return Result<InputBindingDefinition>::failure(
            fieldError("input binding is non-finite", "binding"));
    return Result<InputBindingDefinition>::success(std::move(binding));
}

Result<std::vector<InputValueDefinition>> decodeInputValues(const JsonValue& values,
                                                            std::string_view category) {
    if (values.type != JsonValue::Type::Array ||
        values.array.size() > MaximumInputValuesPerContext) {
        return Result<std::vector<InputValueDefinition>>::failure(
            fieldError("input value array is invalid or over limit", category));
    }
    std::vector<InputValueDefinition> decoded;
    decoded.reserve(values.array.size());
    for (const auto& value : values.array) {
        if (value.type != JsonValue::Type::Object)
            return Result<std::vector<InputValueDefinition>>::failure(
                fieldError("input value must be an object", category));
        auto unknown = rejectUnknownFields(value, {"name", "bindings"}, category);
        if (!unknown)
            return Result<std::vector<InputValueDefinition>>::failure(unknown.error());
        auto name = requiredField(value, "name", JsonValue::Type::String);
        auto bindings = requiredField(value, "bindings", JsonValue::Type::Array);
        if (!name)
            return Result<std::vector<InputValueDefinition>>::failure(name.error());
        if (!bindings)
            return Result<std::vector<InputValueDefinition>>::failure(bindings.error());
        if (bindings.value()->array.empty() ||
            bindings.value()->array.size() > MaximumBindingsPerValue) {
            return Result<std::vector<InputValueDefinition>>::failure(
                fieldError("input binding array is empty or over limit", category));
        }
        InputValueDefinition definition;
        definition.name = name.value()->string;
        for (const auto& bindingValue : bindings.value()->array) {
            auto binding = decodeBinding(bindingValue);
            if (!binding)
                return Result<std::vector<InputValueDefinition>>::failure(binding.error());
            definition.bindings.push_back(std::move(binding.value()));
        }
        decoded.push_back(std::move(definition));
    }
    return Result<std::vector<InputValueDefinition>>::success(std::move(decoded));
}

Result<void> decodeInput(const JsonValue& value, Manifest& manifest) {
    if (value.type != JsonValue::Type::Object)
        return Result<void>::failure(fieldError("input must be an object", "input"));
    auto unknown = rejectUnknownFields(value, {"contexts"}, "input");
    if (!unknown)
        return unknown;
    auto contexts = requiredField(value, "contexts", JsonValue::Type::Array);
    if (!contexts)
        return Result<void>::failure(contexts.error());
    if (contexts.value()->array.size() > MaximumInputContexts)
        return Result<void>::failure(
            fieldError("input context-count limit exceeded", "input.contexts"));
    for (const auto& valueContext : contexts.value()->array) {
        if (valueContext.type != JsonValue::Type::Object)
            return Result<void>::failure(
                fieldError("input context must be an object", "input.contexts"));
        unknown = rejectUnknownFields(
            valueContext, {"name", "priority", "enabled", "actions", "axes"}, "input context");
        if (!unknown)
            return unknown;
        auto name = requiredField(valueContext, "name", JsonValue::Type::String);
        auto priority = requiredField(valueContext, "priority", JsonValue::Type::Number);
        auto enabled = requiredField(valueContext, "enabled", JsonValue::Type::Boolean);
        auto actions = requiredField(valueContext, "actions", JsonValue::Type::Array);
        auto axes = requiredField(valueContext, "axes", JsonValue::Type::Array);
        if (!name)
            return Result<void>::failure(name.error());
        if (!priority)
            return Result<void>::failure(priority.error());
        if (!enabled)
            return Result<void>::failure(enabled.error());
        if (!actions)
            return Result<void>::failure(actions.error());
        if (!axes)
            return Result<void>::failure(axes.error());
        auto decodedPriority =
            integerValue(*priority.value(), "input.context.priority", -1000000, 1000000);
        if (!decodedPriority)
            return Result<void>::failure(decodedPriority.error());
        auto decodedActions = decodeInputValues(*actions.value(), "actions");
        auto decodedAxes = decodeInputValues(*axes.value(), "axes");
        if (!decodedActions)
            return Result<void>::failure(decodedActions.error());
        if (!decodedAxes)
            return Result<void>::failure(decodedAxes.error());
        manifest.inputContexts.push_back(
            {name.value()->string, decodedPriority.value(), enabled.value()->boolean,
             std::move(decodedActions.value()), std::move(decodedAxes.value())});
    }
    return Result<void>::success();
}

Result<void> decodePackages(const JsonValue& value, Manifest& manifest) {
    if (value.type != JsonValue::Type::Array || value.array.size() > MaximumPackageDependencies)
        return Result<void>::failure(fieldError("packages must be a bounded array", "packages"));
    for (const auto& packageValue : value.array) {
        if (packageValue.type != JsonValue::Type::Object)
            return Result<void>::failure(
                fieldError("package dependency must be an object", "packages"));
        auto unknown = rejectUnknownFields(packageValue, {"id", "version"}, "package dependency");
        if (!unknown)
            return unknown;
        auto id = requiredField(packageValue, "id", JsonValue::Type::String);
        auto version = requiredField(packageValue, "version", JsonValue::Type::String);
        if (!id)
            return Result<void>::failure(id.error());
        if (!version)
            return Result<void>::failure(version.error());
        auto requirement = VersionRequirement::parse(version.value()->string);
        if (!requirement)
            return Result<void>::failure(
                fieldError("package version range is invalid", "packages.version"));
        manifest.packageDependencies.push_back({id.value()->string, requirement.value()});
    }
    return Result<void>::success();
}

Result<void> decodeTargets(const JsonValue& value, Manifest& manifest) {
    if (value.type != JsonValue::Type::Object)
        return Result<void>::failure(
            fieldError("targetProfiles must be an object", "targetProfiles"));
    auto unknown = rejectUnknownFields(value, {"pc", "esp32"}, "targetProfiles");
    if (!unknown)
        return unknown;
    auto pc = requiredField(value, "pc", JsonValue::Type::String);
    auto esp32 = requiredField(value, "esp32", JsonValue::Type::String);
    if (!pc)
        return Result<void>::failure(pc.error());
    if (!esp32)
        return Result<void>::failure(esp32.error());
    manifest.targetProfiles.pc = pc.value()->string;
    manifest.targetProfiles.esp32 = esp32.value()->string;
    return Result<void>::success();
}

Result<void> decodePerformanceValues(const JsonValue& value, PerformanceBudgetValues& budget,
                                     const std::string_view fieldPrefix) {
    if (value.type != JsonValue::Type::Object) {
        return Result<void>::failure(
            fieldError("performance custom budget must be an object", fieldPrefix));
    }
    const std::set<std::string_view> fields = {
        "frameTotalMs",     "fixedUpdateMs", "updateMs",
        "physicsMs",        "animationMs",   "aiMs",
        "renderMs",         "audioMs",       "assetStreamingMs",
        "entities",         "components",    "drawCalls",
        "sprites",          "triangles",     "rays",
        "particles",        "audioVoices",   "assetResidentBytes",
        "internalRamBytes", "psramBytes",    "flashBytes",
        "sdBytes"};
    auto unknown = rejectUnknownFields(value, fields, fieldPrefix);
    if (!unknown)
        return unknown;

    const auto readDouble = [&](const std::string_view name,
                                double PerformanceBudgetValues::*member) -> Result<void> {
        const auto* field = findField(value, name);
        if (field == nullptr)
            return Result<void>::success();
        if (field->type != JsonValue::Type::Number || !std::isfinite(field->number) ||
            field->number <= 0.0 || field->number > 10000.0) {
            return Result<void>::failure(fieldError("performance time budget is invalid", name));
        }
        budget.*member = field->number;
        return Result<void>::success();
    };
    const auto readU32 = [&](const std::string_view name,
                             std::uint32_t PerformanceBudgetValues::*member) -> Result<void> {
        const auto* field = findField(value, name);
        if (field == nullptr)
            return Result<void>::success();
        if (field->type != JsonValue::Type::Number || std::trunc(field->number) != field->number ||
            field->number <= 0.0 ||
            field->number > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
            return Result<void>::failure(fieldError("performance count budget is invalid", name));
        }
        budget.*member = static_cast<std::uint32_t>(field->number);
        return Result<void>::success();
    };
    const auto readU64 = [&](const std::string_view name,
                             std::uint64_t PerformanceBudgetValues::*member,
                             const bool allowZero = false) -> Result<void> {
        const auto* field = findField(value, name);
        if (field == nullptr)
            return Result<void>::success();
        constexpr double MaximumExactBudgetBytes = 1024.0 * 1024.0 * 1024.0 * 1024.0;
        if (field->type != JsonValue::Type::Number || std::trunc(field->number) != field->number ||
            field->number < (allowZero ? 0.0 : 1.0) || field->number > MaximumExactBudgetBytes) {
            return Result<void>::failure(fieldError("performance byte budget is invalid", name));
        }
        budget.*member = static_cast<std::uint64_t>(field->number);
        return Result<void>::success();
    };

    const std::array timeFields = {
        std::pair{"frameTotalMs", &PerformanceBudgetValues::frameTotalMilliseconds},
        std::pair{"fixedUpdateMs", &PerformanceBudgetValues::fixedUpdateMilliseconds},
        std::pair{"updateMs", &PerformanceBudgetValues::updateMilliseconds},
        std::pair{"physicsMs", &PerformanceBudgetValues::physicsMilliseconds},
        std::pair{"animationMs", &PerformanceBudgetValues::animationMilliseconds},
        std::pair{"aiMs", &PerformanceBudgetValues::aiMilliseconds},
        std::pair{"renderMs", &PerformanceBudgetValues::renderMilliseconds},
        std::pair{"audioMs", &PerformanceBudgetValues::audioMilliseconds},
        std::pair{"assetStreamingMs", &PerformanceBudgetValues::assetStreamingMilliseconds}};
    for (const auto& [name, member] : timeFields) {
        auto decoded = readDouble(name, member);
        if (!decoded)
            return decoded;
    }
    const std::array countFields = {
        std::pair{"entities", &PerformanceBudgetValues::entities},
        std::pair{"components", &PerformanceBudgetValues::components},
        std::pair{"drawCalls", &PerformanceBudgetValues::drawCalls},
        std::pair{"sprites", &PerformanceBudgetValues::sprites},
        std::pair{"triangles", &PerformanceBudgetValues::triangles},
        std::pair{"rays", &PerformanceBudgetValues::rays},
        std::pair{"particles", &PerformanceBudgetValues::particles},
        std::pair{"audioVoices", &PerformanceBudgetValues::audioVoices}};
    for (const auto& [name, member] : countFields) {
        auto decoded = readU32(name, member);
        if (!decoded)
            return decoded;
    }
    auto decoded = readU64("assetResidentBytes", &PerformanceBudgetValues::assetResidentBytes);
    if (!decoded)
        return decoded;
    decoded = readU64("internalRamBytes", &PerformanceBudgetValues::internalRamBytes);
    if (!decoded)
        return decoded;
    decoded = readU64("psramBytes", &PerformanceBudgetValues::psramBytes, true);
    if (!decoded)
        return decoded;
    decoded = readU64("flashBytes", &PerformanceBudgetValues::flashBytes);
    if (!decoded)
        return decoded;
    decoded = readU64("sdBytes", &PerformanceBudgetValues::sdBytes, true);
    if (!decoded)
        return decoded;
    if (!validPerformanceBudget(budget)) {
        return Result<void>::failure(
            fieldError("performance custom budget is invalid", fieldPrefix));
    }
    return Result<void>::success();
}

Result<void> decodePerformance(const JsonValue& value, Manifest& manifest) {
    if (value.type != JsonValue::Type::Object) {
        return Result<void>::failure(fieldError("performance must be an object", "performance"));
    }
    auto unknown = rejectUnknownFields(
        value, {"version", "pcProfile", "esp32Profile", "customPc", "customEsp32"}, "performance");
    if (!unknown)
        return unknown;
    auto version = requiredField(value, "version", JsonValue::Type::Number);
    auto pcProfile = requiredField(value, "pcProfile", JsonValue::Type::String);
    auto esp32Profile = requiredField(value, "esp32Profile", JsonValue::Type::String);
    if (!version)
        return Result<void>::failure(version.error());
    if (!pcProfile)
        return Result<void>::failure(pcProfile.error());
    if (!esp32Profile)
        return Result<void>::failure(esp32Profile.error());
    auto decodedVersion = integerValue(*version.value(), "performance.version", 1,
                                       PerformanceBudgetSettings::CurrentVersion);
    if (!decodedVersion)
        return Result<void>::failure(decodedVersion.error());
    manifest.performance.version = decodedVersion.value();
    if (!parsePerformanceBudgetProfile(pcProfile.value()->string, manifest.performance.pcProfile) ||
        !parsePerformanceBudgetProfile(esp32Profile.value()->string,
                                       manifest.performance.esp32Profile)) {
        return Result<void>::failure(fieldError(
            "performance profile must be safe, balanced, maximum, or custom", "performance"));
    }
    if (const auto* custom = findField(value, "customPc")) {
        auto decoded =
            decodePerformanceValues(*custom, manifest.performance.pcCustom, "performance.customPc");
        if (!decoded)
            return decoded;
    }
    if (const auto* custom = findField(value, "customEsp32")) {
        auto decoded = decodePerformanceValues(*custom, manifest.performance.esp32Custom,
                                               "performance.customEsp32");
        if (!decoded)
            return decoded;
    }
    return Result<void>::success();
}

Result<void> decodeBuild(const JsonValue& value, Manifest& manifest, bool requireFields) {
    if (value.type != JsonValue::Type::Object)
        return Result<void>::failure(fieldError("build must be an object", "build"));
    auto unknown = rejectUnknownFields(value, {"program", "arguments"}, "build");
    if (!unknown)
        return unknown;
    const auto* program = findField(value, "program");
    const auto* arguments = findField(value, "arguments");
    if (requireFields && (program == nullptr || arguments == nullptr))
        return Result<void>::failure(fieldError("build is missing a required field", "build"));
    if (program != nullptr) {
        if (program->type != JsonValue::Type::String)
            return Result<void>::failure(
                fieldError("build.program must be a string", "build.program"));
        manifest.buildProgram = program->string;
    }
    if (arguments != nullptr) {
        if (arguments->type != JsonValue::Type::Array ||
            arguments->array.size() > MaximumBuildArguments) {
            return Result<void>::failure(
                fieldError("build.arguments must be a bounded array", "build.arguments"));
        }
        manifest.buildArguments.clear();
        for (const auto& argument : arguments->array) {
            if (argument.type != JsonValue::Type::String)
                return Result<void>::failure(
                    fieldError("build argument must be a string", "build.arguments"));
            manifest.buildArguments.push_back(argument.string);
        }
    }
    return Result<void>::success();
}

Result<Manifest> decodeManifest(const JsonValue& root) {
    if (root.type != JsonValue::Type::Object)
        return Result<Manifest>::failure(fieldError("project JSON root must be an object", "root"));
    auto kind = requiredField(root, "kind", JsonValue::Type::String);
    auto versionValue = requiredField(root, "formatVersion", JsonValue::Type::Number);
    auto name = requiredField(root, "name", JsonValue::Type::String);
    auto projectRoot = requiredField(root, "projectRoot", JsonValue::Type::String);
    if (!kind)
        return Result<Manifest>::failure(kind.error());
    if (!versionValue)
        return Result<Manifest>::failure(versionValue.error());
    if (!name)
        return Result<Manifest>::failure(name.error());
    if (!projectRoot)
        return Result<Manifest>::failure(projectRoot.error());
    auto version =
        integerValue(*versionValue.value(), "formatVersion", 0, std::numeric_limits<int>::max());
    if (!version)
        return Result<Manifest>::failure(version.error());
    if (version.value() > Manifest::CurrentVersion) {
        return Result<Manifest>::failure(
            Error(ErrorCode::UnsupportedVersion, "unsupported project format version")
                .addContext("version", std::to_string(version.value())));
    }
    if ((version.value() == 0 && kind.value()->string != "FabGLProject") ||
        (version.value() != 0 && kind.value()->string != "FabGLStudioProject")) {
        return Result<Manifest>::failure(
            fieldError("project kind does not match format version", "kind"));
    }

    const std::set<std::string_view> legacyFields = {"kind",        "formatVersion", "projectGuid",
                                                     "name",        "projectRoot",   "startupScene",
                                                     "previewDemo", "scene",         "build"};
    const std::set<std::string_view> currentFields = {
        "kind",         "formatVersion",  "projectGuid", "name",   "projectRoot",
        "startupScene", "previewDemo",    "build",       "assets", "input",
        "packages",     "targetProfiles", "performance"};
    auto unknown = rejectUnknownFields(
        root, version.value() == Manifest::CurrentVersion ? currentFields : legacyFields,
        "project");
    if (!unknown)
        return Result<Manifest>::failure(unknown.error());

    Manifest manifest;
    manifest.sourceVersion = version.value();
    manifest.name = name.value()->string;
    manifest.projectRoot = projectRoot.value()->string;
    if (const auto* guid = findField(root, "projectGuid")) {
        if (guid->type != JsonValue::Type::String)
            return Result<Manifest>::failure(
                fieldError("projectGuid must be a string", "projectGuid"));
        manifest.projectGuid = guid->string;
    }
    if (const auto* scene = findField(root, "startupScene")) {
        if (scene->type != JsonValue::Type::String)
            return Result<Manifest>::failure(
                fieldError("startupScene must be a string", "startupScene"));
        manifest.startupScene = scene->string;
    }
    if (const auto* preview = findField(root, "previewDemo")) {
        if (preview->type != JsonValue::Type::String)
            return Result<Manifest>::failure(
                fieldError("previewDemo must be a string", "previewDemo"));
        manifest.previewDemo = preview->string;
    }
    if (const auto* build = findField(root, "build")) {
        auto decoded = decodeBuild(*build, manifest, version.value() == Manifest::CurrentVersion);
        if (!decoded)
            return Result<Manifest>::failure(decoded.error());
    } else if (version.value() == Manifest::CurrentVersion) {
        return Result<Manifest>::failure(fieldError("required project field is missing", "build"));
    }

    if (version.value() == Manifest::CurrentVersion) {
        auto guid = requiredField(root, "projectGuid", JsonValue::Type::String);
        auto startup = requiredField(root, "startupScene", JsonValue::Type::String);
        auto input = requiredField(root, "input", JsonValue::Type::Object);
        auto packages = requiredField(root, "packages", JsonValue::Type::Array);
        auto targets = requiredField(root, "targetProfiles", JsonValue::Type::Object);
        if (!guid)
            return Result<Manifest>::failure(guid.error());
        if (!startup)
            return Result<Manifest>::failure(startup.error());
        if (!input)
            return Result<Manifest>::failure(input.error());
        if (!packages)
            return Result<Manifest>::failure(packages.error());
        if (!targets)
            return Result<Manifest>::failure(targets.error());
        if (const auto* assets = findField(root, "assets")) {
            auto decodedAssets = decodeAssets(*assets, manifest);
            if (!decodedAssets)
                return Result<Manifest>::failure(decodedAssets.error());
        }
        auto decodedInput = decodeInput(*input.value(), manifest);
        auto decodedPackages = decodePackages(*packages.value(), manifest);
        auto decodedTargets = decodeTargets(*targets.value(), manifest);
        if (!decodedInput)
            return Result<Manifest>::failure(decodedInput.error());
        if (!decodedPackages)
            return Result<Manifest>::failure(decodedPackages.error());
        if (!decodedTargets)
            return Result<Manifest>::failure(decodedTargets.error());
        if (const auto* performance = findField(root, "performance")) {
            auto decodedPerformance = decodePerformance(*performance, manifest);
            if (!decodedPerformance)
                return Result<Manifest>::failure(decodedPerformance.error());
        }
    } else if (manifest.sourceVersion == 1) {
        if (manifest.projectGuid.empty()) {
            manifest.projectGuid =
                AssetGuid::fromStableName("legacy-project:" + manifest.name).toString();
        }
    }

    if (manifest.sourceVersion != 0 || !manifest.projectGuid.empty()) {
        auto valid = validateManifestModel(manifest, ErrorCode::InvalidFormat);
        if (!valid)
            return Result<Manifest>::failure(valid.error());
    } else if (!validText(manifest.name, 160U) || manifest.projectRoot != "." ||
               !assets::isSafeRelativePath(manifest.startupScene)) {
        return Result<Manifest>::failure(
            Error(ErrorCode::InvalidFormat, "legacy project is invalid"));
    }
    return Result<Manifest>::success(std::move(manifest));
}

std::string escapeJson(std::string_view value) {
    std::string output;
    for (const auto character : value) {
        switch (character) {
        case '"':
            output += "\\\"";
            break;
        case '\\':
            output += "\\\\";
            break;
        case '\b':
            output += "\\b";
            break;
        case '\f':
            output += "\\f";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(character) < 0x20U) {
                constexpr char digits[] = "0123456789abcdef";
                const auto byte = static_cast<unsigned char>(character);
                output += "\\u00";
                output += digits[(byte >> 4U) & 0x0FU];
                output += digits[byte & 0x0FU];
            } else {
                output += character;
            }
            break;
        }
    }
    return output;
}

std::string formatFloat(float value) {
    if (value == 0.0F)
        return "0";
    char buffer[64]{};
    const auto converted =
        std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general,
                      std::numeric_limits<float>::max_digits10);
    return converted.ec == std::errc{} ? std::string(buffer, converted.ptr) : std::string("0");
}

void sortInputValues(std::vector<InputValueDefinition>& values) {
    std::sort(values.begin(), values.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.name < rhs.name; });
    for (auto& value : values) {
        std::sort(value.bindings.begin(), value.bindings.end(),
                  [](const auto& lhs, const auto& rhs) {
                      return std::tie(lhs.control, lhs.scale, lhs.threshold) <
                             std::tie(rhs.control, rhs.scale, rhs.threshold);
                  });
    }
}

void writePerformanceValues(std::ostringstream& output, const PerformanceBudgetValues& budget,
                            const std::string_view indent) {
    output << "{\n"
           << indent << "  \"frameTotalMs\": " << std::setprecision(17)
           << budget.frameTotalMilliseconds << ",\n"
           << indent << "  \"fixedUpdateMs\": " << budget.fixedUpdateMilliseconds << ",\n"
           << indent << "  \"updateMs\": " << budget.updateMilliseconds << ",\n"
           << indent << "  \"physicsMs\": " << budget.physicsMilliseconds << ",\n"
           << indent << "  \"animationMs\": " << budget.animationMilliseconds << ",\n"
           << indent << "  \"aiMs\": " << budget.aiMilliseconds << ",\n"
           << indent << "  \"renderMs\": " << budget.renderMilliseconds << ",\n"
           << indent << "  \"audioMs\": " << budget.audioMilliseconds << ",\n"
           << indent << "  \"assetStreamingMs\": " << budget.assetStreamingMilliseconds << ",\n"
           << indent << "  \"entities\": " << budget.entities << ",\n"
           << indent << "  \"components\": " << budget.components << ",\n"
           << indent << "  \"drawCalls\": " << budget.drawCalls << ",\n"
           << indent << "  \"sprites\": " << budget.sprites << ",\n"
           << indent << "  \"triangles\": " << budget.triangles << ",\n"
           << indent << "  \"rays\": " << budget.rays << ",\n"
           << indent << "  \"particles\": " << budget.particles << ",\n"
           << indent << "  \"audioVoices\": " << budget.audioVoices << ",\n"
           << indent << "  \"assetResidentBytes\": " << budget.assetResidentBytes << ",\n"
           << indent << "  \"internalRamBytes\": " << budget.internalRamBytes << ",\n"
           << indent << "  \"psramBytes\": " << budget.psramBytes << ",\n"
           << indent << "  \"flashBytes\": " << budget.flashBytes << ",\n"
           << indent << "  \"sdBytes\": " << budget.sdBytes << '\n'
           << indent << '}';
}

void writeInputValues(std::ostringstream& output, const std::vector<InputValueDefinition>& values,
                      std::string_view indent) {
    output << '[';
    if (values.empty()) {
        output << ']';
        return;
    }
    output << '\n';
    for (std::size_t valueIndex = 0U; valueIndex < values.size(); ++valueIndex) {
        const auto& value = values[valueIndex];
        output << indent << "  {\n"
               << indent << "    \"name\": \"" << escapeJson(value.name) << "\",\n"
               << indent << "    \"bindings\": [\n";
        for (std::size_t bindingIndex = 0U; bindingIndex < value.bindings.size(); ++bindingIndex) {
            const auto& binding = value.bindings[bindingIndex];
            output << indent << "      {\"control\": \"" << escapeJson(binding.control)
                   << "\", \"scale\": " << formatFloat(binding.scale)
                   << ", \"threshold\": " << formatFloat(binding.threshold) << '}';
            output << (bindingIndex + 1U == value.bindings.size() ? "\n" : ",\n");
        }
        output << indent << "    ]\n" << indent << "  }";
        output << (valueIndex + 1U == values.size() ? "\n" : ",\n");
    }
    output << indent << ']';
}

} // namespace

Result<Manifest> parseManifest(std::string_view json) {
    if (json.empty() || json.size() > MaximumProjectBytes) {
        return Result<Manifest>::failure(
            Error(ErrorCode::CapacityExceeded, "project manifest size is outside limits"));
    }
    auto root = JsonParser(json).parse();
    if (!root)
        return Result<Manifest>::failure(root.error());
    return decodeManifest(root.value());
}

Result<bool> mergeLegacyAssetIndex(const std::string_view json, Manifest& manifest) {
    if (json.empty() || json.size() > MaximumLegacyAssetIndexBytes) {
        return Result<bool>::failure(
            Error(ErrorCode::CapacityExceeded, "legacy asset index size is outside limits"));
    }
    auto root = JsonParser(json).parse();
    if (!root)
        return Result<bool>::failure(root.error());
    if (root.value().type != JsonValue::Type::Object) {
        return Result<bool>::failure(
            Error(ErrorCode::InvalidFormat, "legacy asset index must be a JSON object"));
    }
    auto unknown =
        rejectUnknownFields(root.value(), {"kind", "version", "assets"}, "legacy asset index");
    if (!unknown)
        return Result<bool>::failure(unknown.error());
    auto kind = requiredField(root.value(), "kind", JsonValue::Type::String);
    auto version = requiredField(root.value(), "version", JsonValue::Type::Number);
    auto records = requiredField(root.value(), "assets", JsonValue::Type::Array);
    if (!kind)
        return Result<bool>::failure(kind.error());
    if (!version)
        return Result<bool>::failure(version.error());
    if (!records)
        return Result<bool>::failure(records.error());
    auto versionNumber = integerValue(*version.value(), "version", 1, 1);
    if (kind.value()->string != "fabgl.asset-index" || !versionNumber) {
        return Result<bool>::failure(
            Error(ErrorCode::UnsupportedVersion, "legacy asset index kind/version is unsupported"));
    }
    if (records.value()->array.size() > MaximumAssets) {
        return Result<bool>::failure(
            Error(ErrorCode::CapacityExceeded, "legacy asset index contains too many records"));
    }

    std::set<AssetGuid> indexGuids;
    std::set<std::string> indexPaths;
    bool changed = false;
    for (const auto& value : records.value()->array) {
        if (value.type != JsonValue::Type::Object) {
            return Result<bool>::failure(
                Error(ErrorCode::InvalidFormat, "legacy asset index record must be an object"));
        }
        unknown = rejectUnknownFields(value,
                                      {"guid", "path", "type", "settings", "esp32Target",
                                       "dependencies", "source", "imported"},
                                      "legacy asset index record");
        if (!unknown)
            return Result<bool>::failure(unknown.error());
        auto guidText = requiredField(value, "guid", JsonValue::Type::String);
        auto path = requiredField(value, "path", JsonValue::Type::String);
        auto type = requiredField(value, "type", JsonValue::Type::String);
        if (!guidText)
            return Result<bool>::failure(guidText.error());
        if (!path)
            return Result<bool>::failure(path.error());
        if (!type)
            return Result<bool>::failure(type.error());
        auto guid = AssetGuid::parse(guidText.value()->string);
        if (!guid || guid.value().isNil()) {
            return Result<bool>::failure(
                fieldError("legacy asset index GUID is invalid", "assets.guid"));
        }
        const auto canonicalPath = canonicalAssetPath(path.value()->string);
        if (!indexGuids.insert(guid.value()).second ||
            !indexPaths.insert(portablePathKey(canonicalPath)).second) {
            return Result<bool>::failure(
                Error(ErrorCode::AlreadyExists, "legacy asset index has duplicate GUIDs or paths")
                    .addContext("assetGuid", guid.value().toString())
                    .addContext("assetPath", canonicalPath));
        }

        ProjectAssetEntry migrated(guid.value(), canonicalPath, type.value()->string);
        if (const auto* settings = findField(value, "settings")) {
            if (settings->type != JsonValue::Type::String) {
                return Result<bool>::failure(
                    fieldError("legacy asset settings must be a JSON string", "assets.settings"));
            }
            const auto settingsText =
                settings->string.empty() ? std::string("{}") : settings->string;
            auto parsedSettings = JsonParser(settingsText).parse();
            if (!parsedSettings)
                return Result<bool>::failure(parsedSettings.error());
            auto canonical = canonicalSettings(parsedSettings.value());
            if (!canonical)
                return Result<bool>::failure(canonical.error());
            migrated.importSettings = std::move(canonical.value());
        }
        if (const auto* target = findField(value, "esp32Target")) {
            if (target->type != JsonValue::Type::String) {
                return Result<bool>::failure(
                    fieldError("legacy asset ESP32 target must be a string", "assets.esp32Target"));
            }
            if (target->string == "flash")
                migrated.esp32Target = assets::AssetTarget::Esp32Flash;
            else if (target->string == "psram")
                migrated.esp32Target = assets::AssetTarget::Esp32Psram;
            else if (target->string == "sd")
                migrated.esp32Target = assets::AssetTarget::Esp32Sd;
            else
                return Result<bool>::failure(fieldError(
                    "legacy asset ESP32 target must be flash, psram, or sd", "assets.esp32Target"));
        }
        if (const auto* dependencies = findField(value, "dependencies")) {
            if (dependencies->type != JsonValue::Type::Array ||
                dependencies->array.size() > MaximumAssets) {
                return Result<bool>::failure(
                    fieldError("legacy asset dependency list is invalid", "assets.dependencies"));
            }
            for (const auto& dependency : dependencies->array) {
                if (dependency.type != JsonValue::Type::String) {
                    return Result<bool>::failure(fieldError(
                        "legacy asset dependency GUID must be a string", "assets.dependencies"));
                }
                auto parsed = AssetGuid::parse(dependency.string);
                if (!parsed || parsed.value().isNil()) {
                    return Result<bool>::failure(fieldError(
                        "legacy asset dependency GUID is invalid", "assets.dependencies"));
                }
                migrated.dependencies.push_back(parsed.value());
            }
        }
        migrated.hasImportMetadata = true;

        const auto existing =
            std::find_if(manifest.assets.begin(), manifest.assets.end(),
                         [&migrated](const auto& entry) { return entry.guid == migrated.guid; });
        if (existing == manifest.assets.end()) {
            manifest.assets.push_back(std::move(migrated));
            changed = true;
            continue;
        }
        if (portablePathKey(existing->path) != portablePathKey(migrated.path) ||
            existing->type != migrated.type) {
            return Result<bool>::failure(
                Error(ErrorCode::InvalidState,
                      "legacy asset index conflicts with the canonical manifest")
                    .addContext("assetGuid", migrated.guid.toString())
                    .addContext("manifestPath", existing->path)
                    .addContext("indexPath", migrated.path));
        }
        if (!existing->hasImportMetadata) {
            existing->importSettings = std::move(migrated.importSettings);
            existing->esp32Target = migrated.esp32Target;
            existing->dependencies = std::move(migrated.dependencies);
            existing->hasImportMetadata = true;
            changed = true;
        }
    }
    auto valid = validateManifestModel(manifest, ErrorCode::InvalidFormat);
    if (!valid)
        return Result<bool>::failure(valid.error());
    return Result<bool>::success(changed);
}

Result<std::string> serializeManifest(const Manifest& manifest) {
    auto valid = validateManifestModel(manifest, ErrorCode::InvalidArgument);
    if (!valid)
        return Result<std::string>::failure(valid.error());

    auto contexts = manifest.inputContexts;
    std::sort(contexts.begin(), contexts.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.name < rhs.name; });
    for (auto& context : contexts) {
        sortInputValues(context.actions);
        sortInputValues(context.axes);
    }
    auto packages = manifest.packageDependencies;
    std::sort(packages.begin(), packages.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.id < rhs.id; });
    auto assets = manifest.assets;
    std::sort(assets.begin(), assets.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.guid.toString() < rhs.guid.toString();
    });
    for (auto& asset : assets) {
        auto settings = JsonParser(asset.importSettings).parse();
        if (!settings)
            return Result<std::string>::failure(settings.error());
        auto canonical = canonicalSettings(settings.value());
        if (!canonical)
            return Result<std::string>::failure(canonical.error());
        asset.importSettings = std::move(canonical.value());
        std::sort(asset.dependencies.begin(), asset.dependencies.end());
    }

    std::ostringstream output;
    output << "{\n"
           << "  \"kind\": \"FabGLStudioProject\",\n"
           << "  \"formatVersion\": " << Manifest::CurrentVersion << ",\n"
           << "  \"projectGuid\": \"" << escapeJson(manifest.projectGuid) << "\",\n"
           << "  \"name\": \"" << escapeJson(manifest.name) << "\",\n"
           << "  \"projectRoot\": \".\",\n"
           << "  \"startupScene\": \"" << escapeJson(manifest.startupScene) << "\",\n";
    if (!manifest.previewDemo.empty())
        output << "  \"previewDemo\": \"" << escapeJson(manifest.previewDemo) << "\",\n";
    output << "  \"build\": {\n"
           << "    \"program\": \"" << escapeJson(manifest.buildProgram) << "\",\n"
           << "    \"arguments\": [";
    for (std::size_t index = 0U; index < manifest.buildArguments.size(); ++index) {
        if (index != 0U)
            output << ", ";
        output << '"' << escapeJson(manifest.buildArguments[index]) << '"';
    }
    output << "]\n  },\n"
           << "  \"assets\": [";
    if (assets.empty()) {
        output << "],\n";
    } else {
        output << '\n';
        for (std::size_t index = 0U; index < assets.size(); ++index) {
            output << "    {\"guid\": \"" << assets[index].guid.toString() << "\", \"path\": \""
                   << escapeJson(canonicalAssetPath(assets[index].path)) << "\", \"type\": \""
                   << escapeJson(assets[index].type) << '"';
            if (assets[index].hasImportMetadata) {
                const auto target =
                    assets[index].esp32Target == fabgl::assets::AssetTarget::Esp32Psram ? "psram"
                    : assets[index].esp32Target == fabgl::assets::AssetTarget::Esp32Sd  ? "sd"
                                                                                        : "flash";
                output << ", \"import\": {\"settings\": " << assets[index].importSettings
                       << ", \"esp32Target\": \"" << target << "\", \"dependencies\": [";
                for (std::size_t dependency = 0U; dependency < assets[index].dependencies.size();
                     ++dependency) {
                    if (dependency != 0U)
                        output << ", ";
                    output << '"' << assets[index].dependencies[dependency].toString() << '"';
                }
                output << "]}";
            }
            output << '}';
            output << (index + 1U == assets.size() ? "\n" : ",\n");
        }
        output << "  ],\n";
    }
    output << "  \"input\": {\n"
           << "    \"contexts\": [";
    if (contexts.empty()) {
        output << "]\n";
    } else {
        output << '\n';
        for (std::size_t contextIndex = 0U; contextIndex < contexts.size(); ++contextIndex) {
            const auto& context = contexts[contextIndex];
            output << "      {\n"
                   << "        \"name\": \"" << escapeJson(context.name) << "\",\n"
                   << "        \"priority\": " << context.priority << ",\n"
                   << "        \"enabled\": " << (context.enabled ? "true" : "false") << ",\n"
                   << "        \"actions\": ";
            writeInputValues(output, context.actions, "        ");
            output << ",\n        \"axes\": ";
            writeInputValues(output, context.axes, "        ");
            output << "\n      }";
            output << (contextIndex + 1U == contexts.size() ? "\n" : ",\n");
        }
        output << "    ]\n";
    }
    output << "  },\n"
           << "  \"packages\": [";
    if (packages.empty()) {
        output << "],\n";
    } else {
        output << '\n';
        for (std::size_t index = 0U; index < packages.size(); ++index) {
            output << "    {\"id\": \"" << escapeJson(packages[index].id) << "\", \"version\": \""
                   << packages[index].version.toString() << "\"}";
            output << (index + 1U == packages.size() ? "\n" : ",\n");
        }
        output << "  ],\n";
    }
    output << "  \"targetProfiles\": {\n"
           << "    \"pc\": \"" << escapeJson(manifest.targetProfiles.pc) << "\",\n"
           << "    \"esp32\": \"" << escapeJson(manifest.targetProfiles.esp32) << "\"\n"
           << "  },\n"
           << "  \"performance\": {\n"
           << "    \"version\": " << PerformanceBudgetSettings::CurrentVersion << ",\n"
           << "    \"pcProfile\": \"" << performanceBudgetProfileId(manifest.performance.pcProfile)
           << "\",\n"
           << "    \"esp32Profile\": \""
           << performanceBudgetProfileId(manifest.performance.esp32Profile) << "\",\n"
           << "    \"customPc\": ";
    writePerformanceValues(output, manifest.performance.pcCustom, "    ");
    output << ",\n    \"customEsp32\": ";
    writePerformanceValues(output, manifest.performance.esp32Custom, "    ");
    output << "\n  }\n"
           << "}\n";
    auto encoded = output.str();
    if (encoded.size() > MaximumProjectBytes) {
        return Result<std::string>::failure(
            Error(ErrorCode::CapacityExceeded, "serialized project manifest exceeds size limit"));
    }
    return Result<std::string>::success(std::move(encoded));
}

Result<assets::ImageImportSettings> decodeProjectImageImportSettings(const std::string_view json) {
    auto parsed = JsonParser(json).parse();
    if (!parsed || parsed.value().type != JsonValue::Type::Object) {
        return Result<assets::ImageImportSettings>::failure(
            parsed ? Error(ErrorCode::InvalidFormat, "image import settings must be an object")
                   : parsed.error());
    }
    const auto& object = parsed.value();
    auto unknown =
        rejectUnknownFields(object,
                            {"targetWidth", "targetHeight", "paletteSize", "alphaThreshold",
                             "dither", "reserveTransparentIndex", "crop", "slice", "atlas", "pivot",
                             "pixelsPerUnit", "compression", "residency"},
                            "image import settings");
    if (!unknown)
        return Result<assets::ImageImportSettings>::failure(unknown.error());
    const auto integer = [&object](const std::string_view name, const int fallback,
                                   const int minimum, const int maximum) -> Result<int> {
        const auto* value = findField(object, name);
        if (value == nullptr)
            return Result<int>::success(fallback);
        if (value->type != JsonValue::Type::Number || std::trunc(value->number) != value->number ||
            value->number < minimum || value->number > maximum) {
            return Result<int>::failure(
                fieldError("image import setting is outside its valid range", name));
        }
        return Result<int>::success(static_cast<int>(value->number));
    };
    const auto boolean = [&object](const std::string_view name,
                                   const bool fallback) -> Result<bool> {
        const auto* value = findField(object, name);
        if (value == nullptr)
            return Result<bool>::success(fallback);
        if (value->type != JsonValue::Type::Boolean)
            return Result<bool>::failure(fieldError("image import setting must be boolean", name));
        return Result<bool>::success(value->boolean);
    };
    auto width = integer("targetWidth", 0, 0, 16384);
    auto height = integer("targetHeight", 0, 0, 16384);
    auto palette = integer("paletteSize", 16, 2, 256);
    auto alpha = integer("alphaThreshold", 127, 0, 255);
    auto dither = boolean("dither", false);
    auto transparent = boolean("reserveTransparentIndex", true);
    if (!width || !height || !palette || !alpha || !dither || !transparent) {
        const auto error = !width     ? width.error()
                           : !height  ? height.error()
                           : !palette ? palette.error()
                           : !alpha   ? alpha.error()
                           : !dither  ? dither.error()
                                      : transparent.error();
        return Result<assets::ImageImportSettings>::failure(error);
    }
    assets::ImageImportSettings settings;
    settings.targetWidth = width.value();
    settings.targetHeight = height.value();
    settings.paletteSize = palette.value();
    settings.alphaThreshold = static_cast<std::uint8_t>(alpha.value());
    settings.dither = dither.value();
    settings.reserveTransparentIndex = transparent.value();

    if (const auto* crop = findField(object, "crop")) {
        if (crop->type != JsonValue::Type::Object) {
            return Result<assets::ImageImportSettings>::failure(
                fieldError("image crop must be an object", "crop"));
        }
        unknown = rejectUnknownFields(*crop, {"x", "y", "width", "height"}, "image crop");
        if (!unknown)
            return Result<assets::ImageImportSettings>::failure(unknown.error());
        const auto cropInteger = [crop](const std::string_view name, const int minimum,
                                        const int maximum) -> Result<int> {
            const auto* value = findField(*crop, name);
            if (value == nullptr || value->type != JsonValue::Type::Number ||
                std::trunc(value->number) != value->number || value->number < minimum ||
                value->number > maximum) {
                return Result<int>::failure(
                    fieldError("image crop field is missing or outside its valid range", name));
            }
            return Result<int>::success(static_cast<int>(value->number));
        };
        auto x = cropInteger("x", 0, 8191);
        auto y = cropInteger("y", 0, 8191);
        auto cropWidth = cropInteger("width", 1, 8192);
        auto cropHeight = cropInteger("height", 1, 8192);
        if (!x || !y || !cropWidth || !cropHeight) {
            const auto error = !x           ? x.error()
                               : !y         ? y.error()
                               : !cropWidth ? cropWidth.error()
                                            : cropHeight.error();
            return Result<assets::ImageImportSettings>::failure(error);
        }
        settings.cropEnabled = true;
        settings.crop = {x.value(), y.value(), cropWidth.value(), cropHeight.value()};
    }

    if (const auto* slice = findField(object, "slice")) {
        if (slice->type != JsonValue::Type::Object) {
            return Result<assets::ImageImportSettings>::failure(
                fieldError("sprite slice must be an object", "slice"));
        }
        unknown = rejectUnknownFields(
            *slice, {"mode", "frameWidth", "frameHeight", "margin", "spacing"}, "sprite slice");
        if (!unknown)
            return Result<assets::ImageImportSettings>::failure(unknown.error());
        const auto* mode = findField(*slice, "mode");
        if (mode == nullptr || mode->type != JsonValue::Type::String || mode->string != "grid") {
            return Result<assets::ImageImportSettings>::failure(
                fieldError("sprite slice mode must be grid", "slice.mode"));
        }
        const auto sliceInteger = [slice](const std::string_view name, const int fallback,
                                          const int minimum, const int maximum) -> Result<int> {
            const auto* value = findField(*slice, name);
            if (value == nullptr)
                return Result<int>::success(fallback);
            if (value->type != JsonValue::Type::Number ||
                std::trunc(value->number) != value->number || value->number < minimum ||
                value->number > maximum) {
                return Result<int>::failure(
                    fieldError("sprite slice field is outside its valid range", name));
            }
            return Result<int>::success(static_cast<int>(value->number));
        };
        auto frameWidth = sliceInteger("frameWidth", 0, 1, 4096);
        auto frameHeight = sliceInteger("frameHeight", 0, 1, 4096);
        auto margin = sliceInteger("margin", 0, 0, 4096);
        auto spacing = sliceInteger("spacing", 0, 0, 4096);
        if (!frameWidth || !frameHeight || !margin || !spacing || frameWidth.value() <= 0 ||
            frameHeight.value() <= 0) {
            const auto error =
                !frameWidth    ? frameWidth.error()
                : !frameHeight ? frameHeight.error()
                : !margin      ? margin.error()
                : !spacing     ? spacing.error()
                               : fieldError("sprite slice frame dimensions are required", "slice");
            return Result<assets::ImageImportSettings>::failure(error);
        }
        settings.sliceMode = assets::ImageSliceMode::Grid;
        settings.frameWidth = frameWidth.value();
        settings.frameHeight = frameHeight.value();
        settings.frameMargin = margin.value();
        settings.frameSpacing = spacing.value();
    }

    if (const auto* atlas = findField(object, "atlas")) {
        if (atlas->type != JsonValue::Type::Object) {
            return Result<assets::ImageImportSettings>::failure(
                fieldError("sprite atlas must be an object", "atlas"));
        }
        unknown = rejectUnknownFields(*atlas, {"enabled", "maxWidth", "padding", "powerOfTwo"},
                                      "sprite atlas");
        if (!unknown)
            return Result<assets::ImageImportSettings>::failure(unknown.error());
        const auto* enabled = findField(*atlas, "enabled");
        if (enabled == nullptr || enabled->type != JsonValue::Type::Boolean) {
            return Result<assets::ImageImportSettings>::failure(
                fieldError("sprite atlas enabled flag must be boolean", "atlas.enabled"));
        }
        const auto atlasInteger = [atlas](const std::string_view name, const int fallback,
                                          const int minimum, const int maximum) -> Result<int> {
            const auto* value = findField(*atlas, name);
            if (value == nullptr)
                return Result<int>::success(fallback);
            if (value->type != JsonValue::Type::Number ||
                std::trunc(value->number) != value->number || value->number < minimum ||
                value->number > maximum) {
                return Result<int>::failure(
                    fieldError("sprite atlas field is outside its valid range", name));
            }
            return Result<int>::success(static_cast<int>(value->number));
        };
        auto maximumWidth = atlasInteger("maxWidth", 1024, 1, 4096);
        auto padding = atlasInteger("padding", 1, 0, 64);
        const auto* powerOfTwo = findField(*atlas, "powerOfTwo");
        if (!maximumWidth || !padding ||
            (powerOfTwo != nullptr && powerOfTwo->type != JsonValue::Type::Boolean)) {
            const auto error = !maximumWidth ? maximumWidth.error()
                               : !padding    ? padding.error()
                                             : fieldError("sprite atlas powerOfTwo must be boolean",
                                                          "atlas.powerOfTwo");
            return Result<assets::ImageImportSettings>::failure(error);
        }
        settings.atlasMaximumWidth = maximumWidth.value();
        settings.atlasPadding = padding.value();
        settings.atlasPowerOfTwo = powerOfTwo == nullptr ? true : powerOfTwo->boolean;
        settings.outputKind = enabled->boolean ? assets::ImageOutputKind::SpriteAtlas
                                               : assets::ImageOutputKind::Image;
        if (!enabled->boolean &&
            (findField(*atlas, "maxWidth") != nullptr || findField(*atlas, "padding") != nullptr ||
             findField(*atlas, "powerOfTwo") != nullptr)) {
            return Result<assets::ImageImportSettings>::failure(
                fieldError("disabled sprite atlas must not contain packing fields", "atlas"));
        }
    }

    if (const auto* pivot = findField(object, "pivot")) {
        if (pivot->type != JsonValue::Type::Object) {
            return Result<assets::ImageImportSettings>::failure(
                fieldError("image pivot must be an object", "pivot"));
        }
        unknown = rejectUnknownFields(*pivot, {"x", "y"}, "image pivot");
        if (!unknown)
            return Result<assets::ImageImportSettings>::failure(unknown.error());
        const auto coordinate = [pivot](const std::string_view name) -> Result<float> {
            const auto* value = findField(*pivot, name);
            if (value == nullptr || value->type != JsonValue::Type::Number ||
                !std::isfinite(value->number) || value->number < 0.0 || value->number > 1.0) {
                return Result<float>::failure(
                    fieldError("image pivot coordinate is missing or invalid", name));
            }
            return Result<float>::success(static_cast<float>(value->number));
        };
        auto x = coordinate("x");
        auto y = coordinate("y");
        if (!x || !y)
            return Result<assets::ImageImportSettings>::failure(!x ? x.error() : y.error());
        settings.pivotX = x.value();
        settings.pivotY = y.value();
    }
    if (const auto* pixelsPerUnit = findField(object, "pixelsPerUnit")) {
        if (pixelsPerUnit->type != JsonValue::Type::Number ||
            !std::isfinite(pixelsPerUnit->number) || pixelsPerUnit->number <= 0.0 ||
            pixelsPerUnit->number > 100000.0) {
            return Result<assets::ImageImportSettings>::failure(
                fieldError("pixelsPerUnit is outside its valid range", "pixelsPerUnit"));
        }
        settings.pixelsPerUnit = static_cast<float>(pixelsPerUnit->number);
    }
    if (const auto* compression = findField(object, "compression")) {
        if (compression->type != JsonValue::Type::String || compression->string != "rle") {
            return Result<assets::ImageImportSettings>::failure(
                fieldError("image compression must be rle", "compression"));
        }
        settings.compression = assets::ImageCompression::Rle;
    }
    if (const auto* residency = findField(object, "residency")) {
        if (residency->type != JsonValue::Type::String ||
            (residency->string != "preload" && residency->string != "stream")) {
            return Result<assets::ImageImportSettings>::failure(
                fieldError("image residency must be preload or stream", "residency"));
        }
        settings.residency = residency->string == "stream" ? assets::ImageResidency::Stream
                                                           : assets::ImageResidency::Preload;
    }
    if (settings.sliceMode == assets::ImageSliceMode::Grid &&
        settings.outputKind != assets::ImageOutputKind::SpriteAtlas) {
        return Result<assets::ImageImportSettings>::failure(
            fieldError("grid slicing requires enabled atlas output", "slice"));
    }
    if (settings.outputKind == assets::ImageOutputKind::SpriteAtlas &&
        (settings.targetWidth != 0 || settings.targetHeight != 0)) {
        return Result<assets::ImageImportSettings>::failure(
            fieldError("sprite atlas output cannot apply whole-atlas target dimensions", "atlas"));
    }
    return Result<assets::ImageImportSettings>::success(settings);
}

Result<ProjectAudioImportOptions> decodeProjectAudioImportSettings(const std::string_view json) {
    auto parsed = JsonParser(json).parse();
    if (!parsed || parsed.value().type != JsonValue::Type::Object) {
        return Result<ProjectAudioImportOptions>::failure(
            parsed ? Error(ErrorCode::InvalidFormat, "audio import settings must be an object")
                   : parsed.error());
    }
    const auto& object = parsed.value();
    auto unknown =
        rejectUnknownFields(object,
                            {"targetSampleRate", "normalize", "trimSilence", "silenceThreshold",
                             "streaming", "loopStart", "loopEnd", "encoding"},
                            "audio import settings");
    if (!unknown)
        return Result<ProjectAudioImportOptions>::failure(unknown.error());
    const auto integer = [&object](const std::string_view name, const std::uint32_t fallback,
                                   const std::uint32_t minimum,
                                   const std::uint32_t maximum) -> Result<std::uint32_t> {
        const auto* value = findField(object, name);
        if (value == nullptr)
            return Result<std::uint32_t>::success(fallback);
        if (value->type != JsonValue::Type::Number || std::trunc(value->number) != value->number ||
            value->number < minimum || value->number > maximum) {
            return Result<std::uint32_t>::failure(
                fieldError("audio import setting is outside its valid range", name));
        }
        return Result<std::uint32_t>::success(static_cast<std::uint32_t>(value->number));
    };
    const auto boolean = [&object](const std::string_view name,
                                   const bool fallback) -> Result<bool> {
        const auto* value = findField(object, name);
        if (value == nullptr)
            return Result<bool>::success(fallback);
        if (value->type != JsonValue::Type::Boolean)
            return Result<bool>::failure(fieldError("audio import setting must be boolean", name));
        return Result<bool>::success(value->boolean);
    };
    auto sampleRate = integer("targetSampleRate", 22050U, 1U, 96000U);
    auto loopStart = integer("loopStart", 0U, 0U, std::numeric_limits<std::uint32_t>::max());
    auto loopEnd = integer("loopEnd", 0U, 0U, std::numeric_limits<std::uint32_t>::max());
    auto normalize = boolean("normalize", true);
    auto trim = boolean("trimSilence", true);
    auto streaming = boolean("streaming", false);
    if (!sampleRate || !loopStart || !loopEnd || !normalize || !trim || !streaming) {
        const auto error = !sampleRate  ? sampleRate.error()
                           : !loopStart ? loopStart.error()
                           : !loopEnd   ? loopEnd.error()
                           : !normalize ? normalize.error()
                           : !trim      ? trim.error()
                                        : streaming.error();
        return Result<ProjectAudioImportOptions>::failure(error);
    }
    ProjectAudioImportOptions options;
    options.settings.targetSampleRate = sampleRate.value();
    options.settings.loopStart = loopStart.value();
    options.settings.loopEnd = loopEnd.value();
    options.settings.normalize = normalize.value();
    options.settings.trimSilence = trim.value();
    options.settings.streaming = streaming.value();
    if (const auto* threshold = findField(object, "silenceThreshold")) {
        if (threshold->type != JsonValue::Type::Number || !std::isfinite(threshold->number) ||
            threshold->number < 0.0 || threshold->number > 1.0) {
            return Result<ProjectAudioImportOptions>::failure(
                fieldError("audio silence threshold is invalid", "silenceThreshold"));
        }
        options.settings.silenceThreshold = static_cast<float>(threshold->number);
    }
    if (const auto* encoding = findField(object, "encoding")) {
        if (encoding->type != JsonValue::Type::String ||
            (encoding->string != "pcm16" && encoding->string != "delta8")) {
            return Result<ProjectAudioImportOptions>::failure(
                fieldError("audio encoding must be pcm16 or delta8", "encoding"));
        }
        options.encoding = encoding->string == "pcm16" ? assets::AudioEncoding::Pcm16
                                                       : assets::AudioEncoding::Delta8;
    }
    return Result<ProjectAudioImportOptions>::success(options);
}

} // namespace fabgl::project
