#include "project_format.h"

#include <fabgl/assets/file_io.h>
#include <fabgl/core/guid.h>

#include <cctype>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

namespace fabgl::project {

namespace {

class Parser final {
  public:
    explicit Parser(std::string_view input) : input_(input) {}

    Result<Manifest> parse() {
        skipWhitespace();
        if (!consume('{')) {
            return failure("project JSON root must be an object");
        }
        Manifest manifest;
        bool hasKind = false;
        bool hasVersion = false;
        bool hasName = false;
        bool hasRoot = false;
        std::string kind;
        skipWhitespace();
        if (!consume('}')) {
            while (true) {
                auto key = parseString();
                if (!key)
                    return Result<Manifest>::failure(key.error());
                skipWhitespace();
                if (!consume(':'))
                    return failure("expected ':' after project field name");
                skipWhitespace();

                if (key.value() == "kind") {
                    if (hasKind)
                        return failure("duplicate project kind field");
                    auto value = parseString();
                    if (!value)
                        return Result<Manifest>::failure(value.error());
                    kind = std::move(value.value());
                    hasKind = true;
                } else if (key.value() == "formatVersion") {
                    if (hasVersion)
                        return failure("duplicate formatVersion field");
                    auto value = parseInteger();
                    if (!value)
                        return Result<Manifest>::failure(value.error());
                    manifest.sourceVersion = value.value();
                    hasVersion = true;
                } else if (key.value() == "projectGuid") {
                    auto value = parseString();
                    if (!value)
                        return Result<Manifest>::failure(value.error());
                    manifest.projectGuid = std::move(value.value());
                } else if (key.value() == "name") {
                    if (hasName)
                        return failure("duplicate project name field");
                    auto value = parseString();
                    if (!value)
                        return Result<Manifest>::failure(value.error());
                    manifest.name = std::move(value.value());
                    hasName = true;
                } else if (key.value() == "projectRoot") {
                    if (hasRoot)
                        return failure("duplicate projectRoot field");
                    auto value = parseString();
                    if (!value)
                        return Result<Manifest>::failure(value.error());
                    manifest.projectRoot = std::move(value.value());
                    hasRoot = true;
                } else if (key.value() == "startupScene") {
                    auto value = parseString();
                    if (!value)
                        return Result<Manifest>::failure(value.error());
                    manifest.startupScene = std::move(value.value());
                } else if (key.value() == "scene") {
                    // Accepted for compatibility with early v1 writers. The canonical scene
                    // content lives in the separate startupScene file.
                    auto skipped = skipValue(0U);
                    if (!skipped)
                        return Result<Manifest>::failure(skipped.error());
                } else if (key.value() == "build") {
                    auto build = parseBuild(manifest);
                    if (!build)
                        return Result<Manifest>::failure(build.error());
                } else {
                    auto skipped = skipValue(0U);
                    if (!skipped)
                        return Result<Manifest>::failure(skipped.error());
                }
                skipWhitespace();
                if (consume('}'))
                    break;
                if (!consume(','))
                    return failure("expected ',' or '}' in project object");
                skipWhitespace();
            }
        }
        skipWhitespace();
        if (position_ != input_.size())
            return failure("unexpected data after project JSON");
        if (!hasKind || !hasVersion || !hasName || !hasRoot) {
            return failure("project is missing kind, version, name, or projectRoot");
        }
        if ((manifest.sourceVersion == 1 && kind != "FabGLStudioProject") ||
            (manifest.sourceVersion == 0 && kind != "FabGLProject")) {
            return failure("project kind does not match its format version");
        }
        if (manifest.sourceVersion < 0 || manifest.sourceVersion > Manifest::CurrentVersion) {
            return Result<Manifest>::failure(
                Error(ErrorCode::UnsupportedVersion, "unsupported project format version")
                    .addContext("version", std::to_string(manifest.sourceVersion)));
        }
        if (manifest.name.empty())
            return failure("project name cannot be empty");
        if (manifest.projectRoot != ".")
            return failure("projectRoot must be '.'");
        if (!assets::isSafeRelativePath(manifest.startupScene)) {
            return failure("startupScene must be a safe relative path");
        }
        if (manifest.sourceVersion == 1) {
            if (manifest.projectGuid.empty()) {
                // Compatibility with the first graphical editor build, which predates this field.
                manifest.projectGuid =
                    AssetGuid::fromStableName("legacy-project:" + manifest.name).toString();
            } else if (!AssetGuid::parse(manifest.projectGuid)) {
                return failure("projectGuid is not a canonical UUID");
            }
        }
        return Result<Manifest>::success(std::move(manifest));
    }

  private:
    Result<void> parseBuild(Manifest& manifest) {
        if (!consume('{'))
            return Result<void>::failure(error("build must be an object"));
        skipWhitespace();
        if (consume('}'))
            return Result<void>::success();
        while (true) {
            auto key = parseString();
            if (!key)
                return Result<void>::failure(key.error());
            skipWhitespace();
            if (!consume(':'))
                return Result<void>::failure(error("expected ':' in build object"));
            skipWhitespace();
            if (key.value() == "program") {
                auto program = parseString();
                if (!program)
                    return Result<void>::failure(program.error());
                manifest.buildProgram = std::move(program.value());
            } else if (key.value() == "arguments") {
                auto arguments = parseStringArray();
                if (!arguments)
                    return Result<void>::failure(arguments.error());
                manifest.buildArguments = std::move(arguments.value());
            } else {
                auto skipped = skipValue(0U);
                if (!skipped)
                    return skipped;
            }
            skipWhitespace();
            if (consume('}'))
                return Result<void>::success();
            if (!consume(','))
                return Result<void>::failure(error("expected ',' or '}' in build object"));
            skipWhitespace();
        }
    }

    Result<std::vector<std::string>> parseStringArray() {
        if (!consume('[')) {
            return Result<std::vector<std::string>>::failure(
                error("build.arguments must be an array"));
        }
        std::vector<std::string> values;
        skipWhitespace();
        if (consume(']'))
            return Result<std::vector<std::string>>::success(std::move(values));
        while (true) {
            auto value = parseString();
            if (!value)
                return Result<std::vector<std::string>>::failure(value.error());
            values.push_back(std::move(value.value()));
            skipWhitespace();
            if (consume(']'))
                return Result<std::vector<std::string>>::success(std::move(values));
            if (!consume(',')) {
                return Result<std::vector<std::string>>::failure(
                    error("expected ',' or ']' in string array"));
            }
            skipWhitespace();
        }
    }

    Result<std::string> parseString() {
        if (!consume('"'))
            return Result<std::string>::failure(error("expected JSON string"));
        std::string output;
        while (position_ < input_.size()) {
            const auto character = input_[position_++];
            if (character == '"')
                return Result<std::string>::success(std::move(output));
            if (static_cast<unsigned char>(character) < 0x20U) {
                return Result<std::string>::failure(error("control character in JSON string"));
            }
            if (character != '\\') {
                output += character;
                continue;
            }
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
                auto value = codePoint.value();
                if (value >= 0xD800U && value <= 0xDBFFU) {
                    if (position_ > input_.size() || input_.size() - position_ < 6U ||
                        input_[position_] != '\\' || input_[position_ + 1U] != 'u') {
                        return Result<std::string>::failure(
                            error("high surrogate lacks low surrogate"));
                    }
                    position_ += 2U;
                    auto low = parseHex4();
                    if (!low)
                        return Result<std::string>::failure(low.error());
                    if (low.value() < 0xDC00U || low.value() > 0xDFFFU) {
                        return Result<std::string>::failure(error("invalid low surrogate"));
                    }
                    value = 0x10000U + ((value - 0xD800U) << 10U) + (low.value() - 0xDC00U);
                } else if (value >= 0xDC00U && value <= 0xDFFFU) {
                    return Result<std::string>::failure(error("unexpected low surrogate"));
                }
                appendUtf8(output, value);
                break;
            }
            default:
                return Result<std::string>::failure(error("invalid JSON escape"));
            }
        }
        return Result<std::string>::failure(error("unterminated JSON string"));
    }

    Result<std::uint32_t> parseHex4() {
        if (position_ > input_.size() || input_.size() - position_ < 4U) {
            return Result<std::uint32_t>::failure(error("truncated Unicode escape"));
        }
        std::uint32_t value = 0;
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

    Result<int> parseInteger() {
        const auto start = position_;
        if (position_ < input_.size() && input_[position_] == '-')
            ++position_;
        if (position_ >= input_.size() ||
            !std::isdigit(static_cast<unsigned char>(input_[position_]))) {
            return Result<int>::failure(error("expected integer"));
        }
        while (position_ < input_.size() &&
               std::isdigit(static_cast<unsigned char>(input_[position_]))) {
            ++position_;
        }
        if (position_ < input_.size() &&
            (input_[position_] == '.' || input_[position_] == 'e' || input_[position_] == 'E')) {
            return Result<int>::failure(error("formatVersion must be an integer"));
        }
        try {
            const auto value = std::stoll(std::string(input_.substr(start, position_ - start)));
            if (value < std::numeric_limits<int>::min() ||
                value > std::numeric_limits<int>::max()) {
                return Result<int>::failure(error("integer is outside supported range"));
            }
            return Result<int>::success(static_cast<int>(value));
        } catch (...) {
            return Result<int>::failure(error("invalid integer"));
        }
    }

    Result<void> skipValue(std::size_t depth) {
        if (depth > 64U)
            return Result<void>::failure(error("JSON nesting exceeds 64"));
        skipWhitespace();
        if (position_ >= input_.size())
            return Result<void>::failure(error("missing JSON value"));
        if (input_[position_] == '"') {
            auto string = parseString();
            return string ? Result<void>::success() : Result<void>::failure(string.error());
        }
        if (consume('{')) {
            skipWhitespace();
            if (consume('}'))
                return Result<void>::success();
            while (true) {
                auto key = parseString();
                if (!key)
                    return Result<void>::failure(key.error());
                skipWhitespace();
                if (!consume(':'))
                    return Result<void>::failure(error("expected ':' in object"));
                auto value = skipValue(depth + 1U);
                if (!value)
                    return value;
                skipWhitespace();
                if (consume('}'))
                    return Result<void>::success();
                if (!consume(','))
                    return Result<void>::failure(error("expected ',' or '}' in object"));
                skipWhitespace();
            }
        }
        if (consume('[')) {
            skipWhitespace();
            if (consume(']'))
                return Result<void>::success();
            while (true) {
                auto value = skipValue(depth + 1U);
                if (!value)
                    return value;
                skipWhitespace();
                if (consume(']'))
                    return Result<void>::success();
                if (!consume(','))
                    return Result<void>::failure(error("expected ',' or ']' in array"));
                skipWhitespace();
            }
        }
        for (const auto literal :
             {std::string_view("true"), std::string_view("false"), std::string_view("null")}) {
            if (input_.substr(position_, literal.size()) == literal) {
                position_ += literal.size();
                return Result<void>::success();
            }
        }
        const auto start = position_;
        if (position_ < input_.size() && input_[position_] == '-')
            ++position_;
        while (position_ < input_.size() &&
               (std::isdigit(static_cast<unsigned char>(input_[position_])) ||
                input_[position_] == '.' || input_[position_] == 'e' || input_[position_] == 'E' ||
                input_[position_] == '+' || input_[position_] == '-')) {
            ++position_;
        }
        if (position_ == start)
            return Result<void>::failure(error("invalid JSON value"));
        return Result<void>::success();
    }

    void skipWhitespace() noexcept {
        while (position_ < input_.size() &&
               std::isspace(static_cast<unsigned char>(input_[position_])) != 0) {
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

    Result<Manifest> failure(std::string message) const {
        return Result<Manifest>::failure(error(std::move(message)));
    }

    std::string_view input_;
    std::size_t position_ = 0;
};

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
                const char digits[] = "0123456789abcdef";
                output += "\\u00";
                const auto byte = static_cast<std::uint32_t>(static_cast<unsigned char>(character));
                output += digits[static_cast<std::size_t>((byte >> 4U) & 0x0FU)];
                output += digits[static_cast<std::size_t>(byte & 0x0FU)];
            } else {
                output += character;
            }
            break;
        }
    }
    return output;
}

} // namespace

Result<Manifest> parseManifest(std::string_view json) {
    return Parser(json).parse();
}

Result<std::string> serializeManifest(const Manifest& manifest) {
    if (manifest.name.empty() || manifest.projectRoot != "." ||
        !assets::isSafeRelativePath(manifest.startupScene) ||
        !AssetGuid::parse(manifest.projectGuid)) {
        return Result<std::string>::failure(
            Error(ErrorCode::InvalidArgument, "project manifest is invalid"));
    }
    std::ostringstream output;
    output << "{\n"
           << "  \"kind\": \"FabGLStudioProject\",\n"
           << "  \"formatVersion\": " << Manifest::CurrentVersion << ",\n"
           << "  \"projectGuid\": \"" << escapeJson(manifest.projectGuid) << "\",\n"
           << "  \"name\": \"" << escapeJson(manifest.name) << "\",\n"
           << "  \"projectRoot\": \".\",\n"
           << "  \"startupScene\": \"" << escapeJson(manifest.startupScene) << "\",\n"
           << "  \"build\": {\n"
           << "    \"program\": \"" << escapeJson(manifest.buildProgram) << "\",\n"
           << "    \"arguments\": [";
    for (std::size_t index = 0; index < manifest.buildArguments.size(); ++index) {
        if (index != 0U)
            output << ", ";
        output << '"' << escapeJson(manifest.buildArguments[index]) << '"';
    }
    output << "]\n  }\n}\n";
    return Result<std::string>::success(output.str());
}

} // namespace fabgl::project
