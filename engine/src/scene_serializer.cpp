#include "fabgl/serialization/scene_serializer.h"

#include "fabgl/scene/scene.h"

#include <cmath>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fabgl {
namespace {

struct EntityRecord final {
    EntityGuid id;
    std::string name;
    bool active = true;
    std::optional<EntityGuid> parent;
    Vec3 position;
    Vec3 rotation;
    Vec3 scale{1.0F, 1.0F, 1.0F};
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

} // namespace

Result<std::string> SceneSerializer::serialize(const Scene& scene) {
    if (scene.id().isNil()) {
        return Result<std::string>::failure(
            Error(ErrorCode::SerializationFailed, "scene GUID cannot be nil"));
    }

    std::ostringstream stream;
    stream << std::setprecision(std::numeric_limits<float>::max_digits10);
    stream << "fglscene " << CurrentVersion << '\n';
    stream << "scene_guid " << scene.id().toString() << '\n';
    stream << "scene_name " << std::quoted(encodeControlCharacters(scene.name())) << '\n';

    for (const auto* entity : scene.entities()) {
        if (entity->components().size() != 1U) {
            return Result<std::string>::failure(
                Error(ErrorCode::SerializationFailed,
                      "scene contains a component without a registered serializer")
                    .addContext("entity", entity->id().toString()));
        }
        const auto& transform = entity->transform();
        stream << "entity_begin\n";
        stream << "guid " << entity->id().toString() << '\n';
        stream << "name " << std::quoted(encodeControlCharacters(entity->name())) << '\n';
        stream << "active " << (entity->active() ? 1 : 0) << '\n';
        stream << "parent " << (transform.parent() ? transform.parent()->toString() : "nil")
               << '\n';
        const auto position = transform.localPosition();
        const auto rotation = transform.localRotation();
        const auto scale = transform.localScale();
        stream << "position " << position.x << ' ' << position.y << ' ' << position.z << '\n';
        stream << "rotation " << rotation.x << ' ' << rotation.y << ' ' << rotation.z << '\n';
        stream << "scale " << scale.x << ' ' << scale.y << ' ' << scale.z << '\n';
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
    {
        std::istringstream stream(header.value());
        std::string magic;
        int version = 0;
        if (!(stream >> magic >> version) || magic != "fglscene" || !streamFinished(stream)) {
            return Result<std::unique_ptr<Scene>>::failure(
                parseError(reader, "invalid scene header"));
        }
        if (version != CurrentVersion) {
            return Result<std::unique_ptr<Scene>>::failure(
                Error(ErrorCode::UnsupportedVersion, "unsupported scene format version")
                    .addContext("version", std::to_string(version))
                    .addContext("supported", std::to_string(CurrentVersion)));
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

    std::vector<EntityRecord> records;
    for (;;) {
        auto marker = reader.next("entity_begin or scene_end");
        if (!marker)
            return Result<std::unique_ptr<Scene>>::failure(marker.error());
        if (marker.value() == "scene_end")
            break;
        if (marker.value() != "entity_begin") {
            return Result<std::unique_ptr<Scene>>::failure(
                parseError(reader, "expected 'entity_begin' or 'scene_end'"));
        }

        EntityRecord record;
        auto guidToken = parseSingleToken(reader, "guid");
        if (!guidToken)
            return Result<std::unique_ptr<Scene>>::failure(guidToken.error());
        auto guid = parseGuidToken<EntityGuid>(guidToken.value(), reader, "guid");
        if (!guid)
            return Result<std::unique_ptr<Scene>>::failure(guid.error());
        record.id = guid.value();

        auto name = parseQuotedValue(reader, "name");
        if (!name)
            return Result<std::unique_ptr<Scene>>::failure(name.error());
        record.name = std::move(name.value());

        auto active = parseSingleToken(reader, "active");
        if (!active)
            return Result<std::unique_ptr<Scene>>::failure(active.error());
        if (active.value() != "0" && active.value() != "1") {
            return Result<std::unique_ptr<Scene>>::failure(
                parseError(reader, "active must be 0 or 1"));
        }
        record.active = active.value() == "1";

        auto parent = parseSingleToken(reader, "parent");
        if (!parent)
            return Result<std::unique_ptr<Scene>>::failure(parent.error());
        if (parent.value() != "nil") {
            auto parentGuid = parseGuidToken<EntityGuid>(parent.value(), reader, "parent");
            if (!parentGuid)
                return Result<std::unique_ptr<Scene>>::failure(parentGuid.error());
            record.parent = parentGuid.value();
        }

        auto position = parseVec3(reader, "position");
        if (!position)
            return Result<std::unique_ptr<Scene>>::failure(position.error());
        record.position = position.value();
        auto rotation = parseVec3(reader, "rotation");
        if (!rotation)
            return Result<std::unique_ptr<Scene>>::failure(rotation.error());
        record.rotation = rotation.value();
        auto scale = parseVec3(reader, "scale");
        if (!scale)
            return Result<std::unique_ptr<Scene>>::failure(scale.error());
        record.scale = scale.value();
        auto entityEnd = expectLiteral(reader, "entity_end");
        if (!entityEnd)
            return Result<std::unique_ptr<Scene>>::failure(entityEnd.error());
        records.push_back(std::move(record));
    }

    auto end = reader.requireEnd();
    if (!end)
        return Result<std::unique_ptr<Scene>>::failure(end.error());

    auto scene = std::make_unique<Scene>(std::move(sceneName.value()), sceneGuid.value());
    for (const auto& record : records) {
        auto entity = scene->createEntity(record.name, record.id);
        if (!entity) {
            return Result<std::unique_ptr<Scene>>::failure(
                entity.error().withContext("lineage", "entity creation during deserialization"));
        }
        entity.value()->transform().setLocalPosition(record.position);
        entity.value()->transform().setLocalRotation(record.rotation);
        entity.value()->transform().setLocalScale(record.scale);
        entity.value()->setActive(record.active);
    }
    for (const auto& record : records) {
        if (record.parent) {
            auto parented = scene->setParent(record.id, *record.parent);
            if (!parented) {
                return Result<std::unique_ptr<Scene>>::failure(
                    parented.error().withContext("entity", record.id.toString()));
            }
        }
    }
    return Result<std::unique_ptr<Scene>>::success(std::move(scene));
}

} // namespace fabgl
