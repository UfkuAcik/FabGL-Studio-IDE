#include "fabgl/toolchain/toolchain_manager.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

#ifdef _WIN32
#include <direct.h>
#include <sys/stat.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fgl::toolchain {
namespace {

struct JsonValue {
    enum class Kind { Null, Boolean, Number, String, Object, Array };
    using Object = std::map<std::string, JsonValue, std::less<>>;
    using Array = std::vector<JsonValue>;

    Kind kind{Kind::Null};
    bool boolean{};
    std::string scalar;
    Object object;
    Array array;
};

class JsonParser final {
  public:
    explicit JsonParser(std::string_view input) : m_input(input) {}

    [[nodiscard]] JsonValue parse() {
        skipWhitespace();
        JsonValue result = parseValue();
        skipWhitespace();
        if (m_position != m_input.size()) {
            fail("unexpected trailing content");
        }
        return result;
    }

  private:
    [[noreturn]] void fail(std::string_view message) const {
        throw std::runtime_error("invalid toolchain JSON at byte " + std::to_string(m_position) +
                                 ": " + std::string(message));
    }

    void skipWhitespace() {
        while (m_position < m_input.size() &&
               std::isspace(static_cast<unsigned char>(m_input[m_position])) != 0) {
            ++m_position;
        }
    }

    [[nodiscard]] char peek() const {
        if (m_position >= m_input.size()) {
            fail("unexpected end of input");
        }
        return m_input[m_position];
    }

    char take() {
        const char result = peek();
        ++m_position;
        return result;
    }

    void expect(const char expected) {
        if (take() != expected) {
            fail("unexpected character");
        }
    }

    [[nodiscard]] JsonValue parseValue() {
        skipWhitespace();
        switch (peek()) {
        case '{':
            return parseObject();
        case '[':
            return parseArray();
        case '"': {
            JsonValue result;
            result.kind = JsonValue::Kind::String;
            result.scalar = parseString();
            return result;
        }
        case 't':
            return parseLiteral("true", JsonValue::Kind::Boolean, true);
        case 'f':
            return parseLiteral("false", JsonValue::Kind::Boolean, false);
        case 'n':
            return parseLiteral("null", JsonValue::Kind::Null, false);
        default:
            if (peek() == '-' || std::isdigit(static_cast<unsigned char>(peek())) != 0) {
                return parseNumber();
            }
            fail("expected a JSON value");
        }
    }

    [[nodiscard]] JsonValue parseObject() {
        JsonValue result;
        result.kind = JsonValue::Kind::Object;
        expect('{');
        skipWhitespace();
        if (peek() == '}') {
            take();
            return result;
        }
        while (true) {
            skipWhitespace();
            if (peek() != '"') {
                fail("object key must be a string");
            }
            std::string key = parseString();
            skipWhitespace();
            expect(':');
            JsonValue value = parseValue();
            const auto [unused, inserted] = result.object.emplace(std::move(key), std::move(value));
            static_cast<void>(unused);
            if (!inserted) {
                fail("duplicate object key");
            }
            skipWhitespace();
            const char separator = take();
            if (separator == '}') {
                return result;
            }
            if (separator != ',') {
                fail("expected ',' or '}'");
            }
        }
    }

    [[nodiscard]] JsonValue parseArray() {
        JsonValue result;
        result.kind = JsonValue::Kind::Array;
        expect('[');
        skipWhitespace();
        if (peek() == ']') {
            take();
            return result;
        }
        while (true) {
            result.array.push_back(parseValue());
            skipWhitespace();
            const char separator = take();
            if (separator == ']') {
                return result;
            }
            if (separator != ',') {
                fail("expected ',' or ']'");
            }
        }
    }

    static void appendUtf8(std::string& output, const unsigned int codePoint) {
        if (codePoint <= 0x7FU) {
            output.push_back(static_cast<char>(codePoint));
        } else if (codePoint <= 0x7FFU) {
            output.push_back(static_cast<char>(0xC0U | (codePoint >> 6U)));
            output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
        } else {
            output.push_back(static_cast<char>(0xE0U | (codePoint >> 12U)));
            output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
        }
    }

    [[nodiscard]] unsigned int parseHex4() {
        unsigned int value = 0;
        for (int index = 0; index < 4; ++index) {
            const char digit = take();
            value <<= 4U;
            if (digit >= '0' && digit <= '9') {
                value += static_cast<unsigned int>(digit - '0');
            } else if (digit >= 'a' && digit <= 'f') {
                value += static_cast<unsigned int>(digit - 'a' + 10);
            } else if (digit >= 'A' && digit <= 'F') {
                value += static_cast<unsigned int>(digit - 'A' + 10);
            } else {
                fail("invalid Unicode escape");
            }
        }
        return value;
    }

    [[nodiscard]] std::string parseString() {
        expect('"');
        std::string result;
        while (true) {
            const char value = take();
            if (value == '"') {
                return result;
            }
            if (static_cast<unsigned char>(value) < 0x20U) {
                fail("control character in string");
            }
            if (value != '\\') {
                result.push_back(value);
                continue;
            }
            const char escaped = take();
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                result.push_back(escaped);
                break;
            case 'b':
                result.push_back('\b');
                break;
            case 'f':
                result.push_back('\f');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            case 'u': {
                const unsigned int codePoint = parseHex4();
                if (codePoint >= 0xD800U && codePoint <= 0xDFFFU) {
                    fail("UTF-16 surrogate escapes are not accepted in the lock file");
                }
                appendUtf8(result, codePoint);
                break;
            }
            default:
                fail("invalid string escape");
            }
        }
    }

    [[nodiscard]] JsonValue parseNumber() {
        const std::size_t start = m_position;
        if (peek() == '-') {
            ++m_position;
        }
        if (peek() == '0') {
            ++m_position;
        } else {
            if (std::isdigit(static_cast<unsigned char>(peek())) == 0) {
                fail("invalid number");
            }
            while (m_position < m_input.size() &&
                   std::isdigit(static_cast<unsigned char>(m_input[m_position])) != 0) {
                ++m_position;
            }
        }
        if (m_position < m_input.size() && m_input[m_position] == '.') {
            ++m_position;
            if (m_position >= m_input.size() ||
                std::isdigit(static_cast<unsigned char>(m_input[m_position])) == 0) {
                fail("invalid fraction");
            }
            while (m_position < m_input.size() &&
                   std::isdigit(static_cast<unsigned char>(m_input[m_position])) != 0) {
                ++m_position;
            }
        }
        if (m_position < m_input.size() &&
            (m_input[m_position] == 'e' || m_input[m_position] == 'E')) {
            ++m_position;
            if (m_position < m_input.size() &&
                (m_input[m_position] == '+' || m_input[m_position] == '-')) {
                ++m_position;
            }
            if (m_position >= m_input.size() ||
                std::isdigit(static_cast<unsigned char>(m_input[m_position])) == 0) {
                fail("invalid exponent");
            }
            while (m_position < m_input.size() &&
                   std::isdigit(static_cast<unsigned char>(m_input[m_position])) != 0) {
                ++m_position;
            }
        }
        JsonValue result;
        result.kind = JsonValue::Kind::Number;
        result.scalar = std::string(m_input.substr(start, m_position - start));
        return result;
    }

    [[nodiscard]] JsonValue parseLiteral(const std::string_view literal, const JsonValue::Kind kind,
                                         const bool boolean) {
        if (m_input.substr(m_position, literal.size()) != literal) {
            fail("invalid literal");
        }
        m_position += literal.size();
        JsonValue result;
        result.kind = kind;
        result.boolean = boolean;
        return result;
    }

    std::string_view m_input;
    std::size_t m_position{};
};

[[nodiscard]] const JsonValue& member(const JsonValue& object, const std::string_view name) {
    if (object.kind != JsonValue::Kind::Object) {
        throw std::runtime_error("manifest value containing '" + std::string(name) +
                                 "' is not an object");
    }
    const auto iterator = object.object.find(name);
    if (iterator == object.object.end()) {
        throw std::runtime_error("required manifest field is missing: " + std::string(name));
    }
    return iterator->second;
}

[[nodiscard]] std::string stringValue(const JsonValue& object, const std::string_view name) {
    const JsonValue& value = member(object, name);
    if (value.kind != JsonValue::Kind::String || value.scalar.empty()) {
        throw std::runtime_error("manifest field must be a non-empty string: " + std::string(name));
    }
    return value.scalar;
}

[[nodiscard]] bool boolValue(const JsonValue& object, const std::string_view name) {
    const JsonValue& value = member(object, name);
    if (value.kind != JsonValue::Kind::Boolean) {
        throw std::runtime_error("manifest field must be Boolean: " + std::string(name));
    }
    return value.boolean;
}

[[nodiscard]] std::uint64_t unsignedValue(const JsonValue& object, const std::string_view name) {
    const JsonValue& value = member(object, name);
    if (value.kind != JsonValue::Kind::Number || value.scalar.empty() ||
        value.scalar.front() == '-' || value.scalar.find_first_of(".eE") != std::string::npos) {
        throw std::runtime_error("manifest field must be an unsigned integer: " +
                                 std::string(name));
    }
    std::uint64_t result{};
    const char* const begin = value.scalar.data();
    const char* const end = begin + value.scalar.size();
    const auto conversion = std::from_chars(begin, end, result);
    if (conversion.ec != std::errc{} || conversion.ptr != end) {
        throw std::runtime_error("invalid unsigned integer field: " + std::string(name));
    }
    return result;
}

[[nodiscard]] int pinValue(const JsonValue& object, const std::string_view name) {
    const std::uint64_t value = unsignedValue(object, name);
    if (value > 39U) {
        throw std::runtime_error("ESP32 GPIO is outside the supported range: " + std::string(name));
    }
    return static_cast<int>(value);
}

[[nodiscard]] std::string readTextFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open toolchain manifest: " + path);
    }
    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
    if (length < 0 || length > 1024 * 1024) {
        throw std::runtime_error("toolchain manifest has an invalid size");
    }
    input.seekg(0, std::ios::beg);
    std::string result(static_cast<std::size_t>(length), '\0');
    input.read(result.data(), length);
    if (!input && length != 0) {
        throw std::runtime_error("failed while reading toolchain manifest");
    }
    return result;
}

[[nodiscard]] bool isHexSha256(const std::string_view value) {
    return value.size() == 64U && std::all_of(value.begin(), value.end(), [](const char character) {
               return std::isxdigit(static_cast<unsigned char>(character)) != 0;
           });
}

[[nodiscard]] std::string normalizedSlashes(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    return value;
}

[[nodiscard]] bool isAbsolutePath(const std::string_view path) {
    return (!path.empty() && path.front() == '/') ||
           (path.size() >= 3U && std::isalpha(static_cast<unsigned char>(path[0])) != 0 &&
            path[1] == ':' && path[2] == '/');
}

[[nodiscard]] std::vector<std::string> pathSegments(const std::string_view path) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find('/', start);
        const std::size_t count = (end == std::string_view::npos ? path.size() : end) - start;
        if (count != 0U) {
            result.emplace_back(path.substr(start, count));
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1U;
    }
    return result;
}

[[nodiscard]] std::string lexicalPath(std::string path) {
    path = normalizedSlashes(std::move(path));
    std::string prefix;
    std::size_t contentStart = 0;
    if (path.size() >= 3U && std::isalpha(static_cast<unsigned char>(path[0])) != 0 &&
        path[1] == ':' && path[2] == '/') {
        prefix = path.substr(0, 3);
        contentStart = 3;
    } else if (!path.empty() && path.front() == '/') {
        prefix = "/";
        contentStart = 1;
    }

    std::vector<std::string> normalized;
    for (const std::string& segment : pathSegments(std::string_view(path).substr(contentStart))) {
        if (segment == ".") {
            continue;
        }
        if (segment == "..") {
            if (!normalized.empty() && normalized.back() != "..") {
                normalized.pop_back();
            } else if (prefix.empty()) {
                normalized.push_back(segment);
            }
            continue;
        }
        normalized.push_back(segment);
    }

    std::string result = prefix;
    for (const std::string& segment : normalized) {
        if (!result.empty() && result.back() != '/') {
            result.push_back('/');
        }
        result += segment;
    }
    if (result.empty()) {
        return ".";
    }
    return result;
}

[[nodiscard]] std::string currentWorkingDirectory() {
    char buffer[4096]{};
#ifdef _WIN32
    if (_getcwd(buffer, static_cast<int>(sizeof(buffer))) == nullptr) {
#else
    if (getcwd(buffer, sizeof(buffer)) == nullptr) {
#endif
        throw std::runtime_error("cannot determine current working directory: " +
                                 std::string(std::strerror(errno)));
    }
    return lexicalPath(buffer);
}

[[nodiscard]] std::string joinPath(const std::string_view left, const std::string_view right) {
    const std::string normalizedRight = normalizedSlashes(std::string(right));
    if (isAbsolutePath(normalizedRight)) {
        return lexicalPath(normalizedRight);
    }
    std::string result = normalizedSlashes(std::string(left));
    if (!result.empty() && result.back() != '/') {
        result.push_back('/');
    }
    result += normalizedRight;
    return lexicalPath(std::move(result));
}

[[nodiscard]] std::string absolutePath(const std::string& path, const std::string& base = {}) {
    const std::string normalized = normalizedSlashes(path);
    if (isAbsolutePath(normalized)) {
        return lexicalPath(normalized);
    }
    return joinPath(base.empty() ? currentWorkingDirectory() : base, normalized);
}

void validateRelativePath(const std::string& path, const std::string_view field) {
    const std::string normalized = normalizedSlashes(path);
    if (normalized.empty() || isAbsolutePath(normalized)) {
        throw std::runtime_error(std::string(field) + " must be a non-empty relative path");
    }
    for (const std::string& component : pathSegments(normalized)) {
        if (component == "..") {
            throw std::runtime_error(std::string(field) + " cannot contain '..'");
        }
    }
}

[[nodiscard]] std::optional<std::string> findOnPath(const std::string_view executableName) {
    const char* const rawPath = std::getenv("PATH");
    if (rawPath == nullptr) {
        return std::nullopt;
    }
#ifdef _WIN32
    constexpr char separator = ';';
#else
    constexpr char separator = ':';
#endif
    std::string pathValue(rawPath);
    std::size_t start = 0;
    while (start <= pathValue.size()) {
        const std::size_t end = pathValue.find(separator, start);
        const std::string_view element(pathValue.data() + start,
                                       (end == std::string::npos ? pathValue.size() : end) - start);
        if (!element.empty()) {
            const std::string candidate = joinPath(element, executableName);
            struct stat status {};
            if (stat(candidate.c_str(), &status) == 0 && (status.st_mode & S_IFREG) != 0) {
                return absolutePath(candidate);
            }
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return std::nullopt;
}

[[nodiscard]] bool isRegularFile(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    struct stat status {};
    return stat(path.c_str(), &status) == 0 && (status.st_mode & S_IFREG) != 0;
}

[[nodiscard]] bool isDirectory(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    struct stat status {};
    return stat(path.c_str(), &status) == 0 && (status.st_mode & S_IFDIR) != 0;
}

[[nodiscard]] const ArtifactLock* findArtifact(const ToolchainManifest& manifest,
                                               const std::string_view id) {
    const auto iterator =
        std::find_if(manifest.artifacts.begin(), manifest.artifacts.end(),
                     [id](const ArtifactLock& artifact) { return artifact.id == id; });
    return iterator == manifest.artifacts.end() ? nullptr : &*iterator;
}

void validateArgument(const std::string_view value) {
    if (value.find('\0') != std::string_view::npos) {
        throw std::invalid_argument("process argument contains a NUL byte");
    }
}

[[nodiscard]] std::string quoteForDisplay(const std::string_view value) {
    const bool needsQuotes =
        value.empty() || value.find_first_of(" \t\"&|;<>^") != std::string_view::npos;
    if (!needsQuotes) {
        return std::string(value);
    }
    std::string result = "\"";
    for (const char character : value) {
        if (character == '\\' || character == '"') {
            result.push_back('\\');
        }
        result.push_back(character);
    }
    result.push_back('"');
    return result;
}

} // namespace

ToolchainManifest loadManifest(const std::string& manifestPath) {
    const JsonValue root = JsonParser(readTextFile(manifestPath)).parse();
    ToolchainManifest manifest;
    const std::uint64_t schemaVersion = unsignedValue(root, "schemaVersion");
    if (schemaVersion != 1U) {
        throw std::runtime_error("unsupported toolchain manifest schema: " +
                                 std::to_string(schemaVersion));
    }
    manifest.schemaVersion = static_cast<int>(schemaVersion);

    const JsonValue& profile = member(root, "profile");
    manifest.profileId = stringValue(profile, "id");
    manifest.displayName = stringValue(profile, "displayName");
    manifest.fqbn = stringValue(profile, "fqbn");
    const JsonValue& compiler = member(profile, "compiler");
    manifest.compilerWarnings = stringValue(compiler, "warnings");
    manifest.compilerCppExtraFlags = stringValue(compiler, "cppExtraFlags");
    if (manifest.compilerWarnings != "default" ||
        manifest.compilerCppExtraFlags != "-Wno-error=narrowing") {
        throw std::runtime_error("unexpected compiler compatibility contract");
    }
    const JsonValue& core = member(profile, "arduinoCore");
    manifest.arduinoCorePackage = stringValue(core, "package");
    manifest.arduinoCoreVersion = stringValue(core, "version");
    manifest.arduinoCoreCommit = stringValue(core, "commit");
    const JsonValue& fabgl = member(profile, "fabgl");
    manifest.fabglVersion = stringValue(fabgl, "version");
    manifest.fabglDistributionVersion = stringValue(fabgl, "distributionVersion");
    manifest.fabglCommit = stringValue(fabgl, "commit");
    const JsonValue& upload = member(profile, "upload");
    manifest.automaticUpload = boolValue(upload, "automatic");
    if (manifest.automaticUpload) {
        throw std::runtime_error("automatic upload must remain disabled in the lock");
    }

    const JsonValue& pins = member(root, "pins");
    const JsonValue& vga = member(pins, "vga");
    manifest.pins.vgaR1 = pinValue(vga, "r1");
    manifest.pins.vgaR0 = pinValue(vga, "r0");
    manifest.pins.vgaG1 = pinValue(vga, "g1");
    manifest.pins.vgaG0 = pinValue(vga, "g0");
    manifest.pins.vgaB1 = pinValue(vga, "b1");
    manifest.pins.vgaB0 = pinValue(vga, "b0");
    manifest.pins.vgaHSync = pinValue(vga, "hsync");
    manifest.pins.vgaVSync = pinValue(vga, "vsync");
    const JsonValue& keyboard = member(pins, "ps2Keyboard");
    manifest.pins.keyboardData = pinValue(keyboard, "data");
    manifest.pins.keyboardClock = pinValue(keyboard, "clock");
    const JsonValue& mouse = member(pins, "ps2Mouse");
    manifest.pins.mouseData = pinValue(mouse, "data");
    manifest.pins.mouseClock = pinValue(mouse, "clock");
    manifest.pins.audioDac = pinValue(member(pins, "audio"), "dac");
    const JsonValue& sd = member(pins, "sd");
    manifest.pins.sdMiso = pinValue(sd, "miso");
    manifest.pins.sdMosi = pinValue(sd, "mosi");
    manifest.pins.sdClock = pinValue(sd, "clock");
    manifest.pins.sdChipSelect = pinValue(sd, "chipSelect");

    const JsonValue& artifacts = member(root, "artifacts");
    if (artifacts.kind != JsonValue::Kind::Array || artifacts.array.empty()) {
        throw std::runtime_error("manifest artifacts must be a non-empty array");
    }
    for (const JsonValue& value : artifacts.array) {
        ArtifactLock artifact;
        artifact.id = stringValue(value, "id");
        artifact.version = stringValue(value, "version");
        artifact.commit = stringValue(value, "commit");
        artifact.url = stringValue(value, "url");
        artifact.fileName = stringValue(value, "fileName");
        artifact.size = unsignedValue(value, "size");
        artifact.sha256 = stringValue(value, "sha256");
        artifact.license = stringValue(value, "license");
        artifact.sourceUrl = stringValue(value, "sourceUrl");
        artifact.platform = stringValue(value, "platform");
        artifact.installDirectory = stringValue(value, "installDirectory");
        artifact.stripSingleRoot = boolValue(value, "stripSingleRoot");
        if (artifact.url.compare(0, 8, "https://") != 0 ||
            artifact.sourceUrl.compare(0, 8, "https://") != 0) {
            throw std::runtime_error("artifact URLs must use HTTPS: " + artifact.id);
        }
        if (artifact.size == 0U || !isHexSha256(artifact.sha256)) {
            throw std::runtime_error("artifact size or SHA-256 is invalid: " + artifact.id);
        }
        if (artifact.fileName.find_first_of("/\\") != std::string::npos ||
            artifact.fileName == "." || artifact.fileName == "..") {
            throw std::runtime_error("artifact fileName is unsafe: " + artifact.id);
        }
        validateRelativePath(artifact.installDirectory, "installDirectory");
        if (std::any_of(
                manifest.artifacts.begin(), manifest.artifacts.end(),
                [&artifact](const ArtifactLock& existing) { return existing.id == artifact.id; })) {
            throw std::runtime_error("duplicate artifact id: " + artifact.id);
        }
        manifest.artifacts.push_back(std::move(artifact));
    }

    const JsonValue& boardManager = member(root, "boardManager");
    manifest.boardManagerPackage = stringValue(boardManager, "package");
    return manifest;
}

ToolchainDetection detectToolchain(const ToolchainManifest& manifest,
                                   const DetectionOptions& options) {
    ToolchainDetection result;
    result.repositoryRoot = absolutePath(options.repositoryRoot);

    const ArtifactLock* const cliArtifact = findArtifact(manifest, "arduino-cli-windows-x86_64");
    const ArtifactLock* const fabglArtifact = findArtifact(manifest, "olimex-fabgl-source");
    if (cliArtifact == nullptr || fabglArtifact == nullptr) {
        throw std::runtime_error("manifest is missing required CLI or FabGL artifacts");
    }

    const std::string managedRoot = joinPath(result.repositoryRoot, ".toolchains");
    const std::string managedCli = joinPath(joinPath(managedRoot, cliArtifact->installDirectory),
#ifdef _WIN32
                                            "arduino-cli.exe");
#else
                                            "arduino-cli");
#endif
    if (options.arduinoCliOverride) {
        result.arduinoCli = absolutePath(*options.arduinoCliOverride, result.repositoryRoot);
        result.cliSource = DetectionSource::Override;
    } else if (isRegularFile(managedCli)) {
        result.arduinoCli = managedCli;
        result.cliSource = DetectionSource::Managed;
    } else if (options.allowPathLookup) {
        const auto systemCli = findOnPath(
#ifdef _WIN32
            "arduino-cli.exe"
#else
            "arduino-cli"
#endif
        );
        if (systemCli) {
            result.arduinoCli = *systemCli;
            result.cliSource = DetectionSource::SystemPath;
        }
    }
    result.cliFound = isRegularFile(result.arduinoCli);

    result.arduinoData = options.arduinoDataOverride
                             ? absolutePath(*options.arduinoDataOverride, result.repositoryRoot)
                             : joinPath(managedRoot, "arduino-data");
    result.arduinoConfig = options.arduinoConfigOverride
                               ? absolutePath(*options.arduinoConfigOverride, result.repositoryRoot)
                               : joinPath(managedRoot, "arduino-cli.yaml");
    result.coreDirectory = joinPath(
        joinPath(joinPath(joinPath(result.arduinoData, "packages"), "esp32"), "hardware/esp32"),
        manifest.arduinoCoreVersion);
    result.coreFound = isDirectory(result.coreDirectory);

    const std::string managedFabgl = joinPath(managedRoot, fabglArtifact->installDirectory);
    if (options.fabglLibraryOverride) {
        result.fabglLibrary = absolutePath(*options.fabglLibraryOverride, result.repositoryRoot);
        result.fabglSource = DetectionSource::Override;
    } else if (isDirectory(managedFabgl)) {
        result.fabglLibrary = managedFabgl;
        result.fabglSource = DetectionSource::Managed;
    }
    result.fabglFound = isRegularFile(joinPath(result.fabglLibrary, "library.properties")) &&
                        isRegularFile(joinPath(result.fabglLibrary, "src/fabgl.h"));

    const bool cliLocked =
        result.cliSource == DetectionSource::Managed &&
        isRegularFile(joinPath(joinPath(managedRoot, cliArtifact->installDirectory),
                               ".fabglstudio-install.json"));
    const bool fabglLocked = result.fabglSource == DetectionSource::Managed &&
                             isRegularFile(joinPath(managedFabgl, ".fabglstudio-install.json"));
    result.releaseLocked = cliLocked && result.coreFound && fabglLocked;

    if (!result.cliFound) {
        result.issues.emplace_back("arduino-cli executable was not found");
    }
    if (!result.coreFound) {
        result.issues.emplace_back("repo-scoped Arduino-ESP32 " + manifest.arduinoCoreVersion +
                                   " was not found");
    }
    if (!result.fabglFound) {
        result.issues.emplace_back(
            "FabGL library root is missing library.properties or src/fabgl.h");
    }
    if (result.buildReady() && !result.releaseLocked) {
        result.issues.emplace_back(
            "build inputs are usable but are not all managed release-lock artifacts");
    }
    return result;
}

ProcessCommand makeCompileCommand(const ToolchainManifest& manifest,
                                  const ToolchainDetection& detection,
                                  const BuildRequest& request) {
    if (!detection.buildReady()) {
        throw std::invalid_argument("toolchain detection is not build-ready");
    }
    if (manifest.automaticUpload) {
        throw std::invalid_argument("automatic upload is forbidden by this command model");
    }
    if (request.jobs == 0U || request.jobs > 256U) {
        throw std::invalid_argument("compile jobs must be between 1 and 256");
    }

    ProcessCommand result;
    result.program = detection.arduinoCli;
    result.workingDirectory = detection.repositoryRoot;
    const std::string sketch = absolutePath(request.sketchDirectory, detection.repositoryRoot);
    const std::string build = absolutePath(request.buildDirectory, detection.repositoryRoot);
    const std::string output = absolutePath(request.outputDirectory, detection.repositoryRoot);

    if (!detection.arduinoConfig.empty() && isRegularFile(detection.arduinoConfig)) {
        result.arguments.emplace_back("--config-file");
        result.arguments.push_back(detection.arduinoConfig);
    }
    result.arguments.emplace_back("compile");
    result.arguments.emplace_back("--fqbn");
    result.arguments.push_back(manifest.fqbn);
    result.arguments.emplace_back("--library");
    result.arguments.push_back(detection.fabglLibrary);
    result.arguments.emplace_back("--build-path");
    result.arguments.push_back(build);
    result.arguments.emplace_back("--output-dir");
    result.arguments.push_back(output);
    result.arguments.emplace_back("--jobs");
    result.arguments.push_back(std::to_string(request.jobs));
    result.arguments.emplace_back("--warnings");
    result.arguments.push_back(manifest.compilerWarnings);
    result.arguments.emplace_back("--build-property");
    result.arguments.push_back("compiler.cpp.extra_flags=" + manifest.compilerCppExtraFlags);
    if (request.clean) {
        result.arguments.emplace_back("--clean");
    }
    if (request.verbose) {
        result.arguments.emplace_back("--verbose");
    }
    result.arguments.push_back(sketch);

    for (const std::string& argument : result.arguments) {
        validateArgument(argument);
    }
    if (containsUploadOperation(result)) {
        throw std::logic_error("compile command unexpectedly contains an upload operation");
    }
    return result;
}

bool containsUploadOperation(const ProcessCommand& command) {
    return std::any_of(
        command.arguments.begin(), command.arguments.end(), [](const std::string_view argument) {
            return argument == "upload" || argument == "--upload" || argument == "-u";
        });
}

std::string renderCommandForDisplay(const ProcessCommand& command) {
    std::ostringstream output;
    output << quoteForDisplay(command.program);
    for (const std::string& argument : command.arguments) {
        output << ' ' << quoteForDisplay(argument);
    }
    return output.str();
}

const char* toString(const DetectionSource source) noexcept {
    switch (source) {
    case DetectionSource::Missing:
        return "missing";
    case DetectionSource::Managed:
        return "managed";
    case DetectionSource::Override:
        return "override";
    case DetectionSource::SystemPath:
        return "system-path";
    }
    return "unknown";
}

} // namespace fgl::toolchain
