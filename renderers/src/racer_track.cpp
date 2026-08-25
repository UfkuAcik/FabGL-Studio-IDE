#include <fabgl/rendering/racer_track.h>

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

namespace fabgl::rendering {
namespace {

constexpr std::uint32_t RacerTrackFormatVersion = 1U;

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
            Error(ErrorCode::InvalidFormat, "unexpected end of racer track data")
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
                    Error(ErrorCode::InvalidFormat, "unexpected data after track_end")
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
                Error(ErrorCode::InvalidFormat, "unterminated racer track string escape"));
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
                Error(ErrorCode::InvalidFormat, "unsupported racer track string escape")
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
    if (!(stream >> key >> std::quoted(encoded)) || key != expectedKey || !streamFinished(stream)) {
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
            parseError(reader, std::string(expectedKey) + " exceeds the string limit"));
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

Result<std::uint32_t> parseUnsigned(LineReader& reader, const char* expectedKey) {
    auto token = parseToken(reader, expectedKey);
    if (!token)
        return Result<std::uint32_t>::failure(token.error());
    const auto parsed = parseInteger<std::uint32_t>(token.value());
    if (!parsed) {
        return Result<std::uint32_t>::failure(
            parseError(reader, std::string(expectedKey) + " must be a uint32"));
    }
    return Result<std::uint32_t>::success(*parsed);
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
        return Result<void>::failure(parseError(reader, std::string("expected '") + literal + "'"));
    }
    return Result<void>::success();
}

Result<AssetGuid> parseGuidToken(const std::string_view token, const LineReader& reader,
                                 const char* field) {
    auto parsed = AssetGuid::parse(token);
    if (!parsed) {
        return Result<AssetGuid>::failure(
            parsed.error()
                .withContext("field", field)
                .withContext("line", std::to_string(reader.lineNumber())));
    }
    if (parsed.value().isNil()) {
        return Result<AssetGuid>::failure(
            parseError(reader, std::string(field) + " cannot be nil"));
    }
    return parsed;
}

Result<AssetGuid> parseGuid(LineReader& reader, const char* expectedKey) {
    auto token = parseToken(reader, expectedKey);
    if (!token)
        return Result<AssetGuid>::failure(token.error());
    return parseGuidToken(token.value(), reader, expectedKey);
}

std::string_view weatherTag(const RacerWeatherKind kind) noexcept {
    switch (kind) {
    case RacerWeatherKind::Clear:
        return "clear";
    case RacerWeatherKind::Rain:
        return "rain";
    case RacerWeatherKind::Fog:
        return "fog";
    case RacerWeatherKind::Storm:
        return "storm";
    }
    return "unknown";
}

std::optional<RacerWeatherKind> parseWeather(const std::string_view tag) noexcept {
    if (tag == "clear")
        return RacerWeatherKind::Clear;
    if (tag == "rain")
        return RacerWeatherKind::Rain;
    if (tag == "fog")
        return RacerWeatherKind::Fog;
    if (tag == "storm")
        return RacerWeatherKind::Storm;
    return std::nullopt;
}

bool limitsValid(const RacerTrackFormatLimits& limits) noexcept {
    return limits.maximumSourceBytes > 0U && limits.maximumSegments > 0U &&
           limits.maximumStringBytes > 0U;
}

bool finite(const RoadSegment& segment) noexcept {
    return std::isfinite(segment.curve) && std::isfinite(segment.hill) &&
           std::isfinite(segment.width);
}

float canonicalFloat(const float value) noexcept {
    return value == 0.0F ? 0.0F : value;
}

bool parseColor(std::istringstream& stream, Color& color) {
    std::string red;
    std::string green;
    std::string blue;
    std::string alpha;
    if (!(stream >> red >> green >> blue >> alpha))
        return false;
    const auto r = parseInteger<std::uint32_t>(red);
    const auto g = parseInteger<std::uint32_t>(green);
    const auto b = parseInteger<std::uint32_t>(blue);
    const auto a = parseInteger<std::uint32_t>(alpha);
    if (!r || !g || !b || !a || *r > 255U || *g > 255U || *b > 255U || *a > 255U)
        return false;
    color = {static_cast<std::uint8_t>(*r), static_cast<std::uint8_t>(*g),
             static_cast<std::uint8_t>(*b), static_cast<std::uint8_t>(*a)};
    return true;
}

void writeColor(std::ostringstream& stream, const Color color) {
    stream << static_cast<unsigned int>(color.r) << ' ' << static_cast<unsigned int>(color.g) << ' '
           << static_cast<unsigned int>(color.b) << ' ' << static_cast<unsigned int>(color.a);
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

Result<RacerWeatherMetadata> parseWeatherRecord(LineReader& reader) {
    auto line = reader.next("weather");
    if (!line)
        return Result<RacerWeatherMetadata>::failure(line.error());
    std::istringstream stream(line.value());
    stream.imbue(std::locale::classic());
    std::string key;
    std::string kindToken;
    std::string seedToken;
    RacerWeatherMetadata weather;
    if (!(stream >> key >> kindToken >> weather.intensity >> weather.visibility >> weather.wind) ||
        key != "weather" || !parseColor(stream, weather.tint) || !(stream >> seedToken) ||
        !streamFinished(stream) || !std::isfinite(weather.intensity) ||
        !std::isfinite(weather.visibility) || !std::isfinite(weather.wind)) {
        return Result<RacerWeatherMetadata>::failure(
            parseError(reader, "weather record is invalid"));
    }
    const auto kind = parseWeather(kindToken);
    const auto seed = parseInteger<std::uint32_t>(seedToken);
    if (!kind || !seed) {
        return Result<RacerWeatherMetadata>::failure(
            parseError(reader, "weather kind or seed is invalid"));
    }
    weather.kind = *kind;
    weather.intensity = canonicalFloat(weather.intensity);
    weather.visibility = canonicalFloat(weather.visibility);
    weather.wind = canonicalFloat(weather.wind);
    weather.seed = *seed;
    return Result<RacerWeatherMetadata>::success(weather);
}

Result<RoadSegment> parseSegment(LineReader& reader) {
    auto line = reader.next("segment");
    if (!line)
        return Result<RoadSegment>::failure(line.error());
    std::istringstream stream(line.value());
    stream.imbue(std::locale::classic());
    std::string key;
    RoadSegment segment;
    if (!(stream >> key >> segment.curve >> segment.hill >> segment.width) || key != "segment" ||
        !parseColor(stream, segment.road) || !parseColor(stream, segment.grass) ||
        !parseColor(stream, segment.rumble) || !streamFinished(stream) || !finite(segment)) {
        return Result<RoadSegment>::failure(parseError(reader, "road segment record is invalid"));
    }
    segment.curve = canonicalFloat(segment.curve);
    segment.hill = canonicalFloat(segment.hill);
    segment.width = canonicalFloat(segment.width);
    return Result<RoadSegment>::success(segment);
}

Result<RacerCheckpoint> parseCheckpoint(LineReader& reader, const std::size_t maximumStringBytes) {
    auto line = reader.next("checkpoint");
    if (!line)
        return Result<RacerCheckpoint>::failure(line.error());
    std::istringstream stream(line.value());
    stream.imbue(std::locale::classic());
    std::string key;
    std::string segmentToken;
    std::string encodedName;
    if (!(stream >> key >> segmentToken >> std::quoted(encodedName)) || key != "checkpoint" ||
        !streamFinished(stream)) {
        return Result<RacerCheckpoint>::failure(parseError(reader, "checkpoint record is invalid"));
    }
    const auto segment = parseInteger<std::uint32_t>(segmentToken);
    auto name = decodeControlCharacters(encodedName);
    if (!segment || !name || name.value().size() > maximumStringBytes) {
        return Result<RacerCheckpoint>::failure(
            parseError(reader, "checkpoint segment or name is invalid"));
    }
    return Result<RacerCheckpoint>::success({*segment, std::move(name.value())});
}

Result<RacerRoadsideObject> parseRoadside(LineReader& reader) {
    auto line = reader.next("roadside");
    if (!line)
        return Result<RacerRoadsideObject>::failure(line.error());
    std::istringstream stream(line.value());
    stream.imbue(std::locale::classic());
    std::string key;
    std::string idToken;
    std::string segmentToken;
    std::string guidToken;
    RacerRoadsideObject object;
    if (!(stream >> key >> idToken >> segmentToken >> object.lateral >> object.scale >>
          guidToken) ||
        key != "roadside" || !parseColor(stream, object.tint) || !streamFinished(stream) ||
        !std::isfinite(object.lateral) || !std::isfinite(object.scale)) {
        return Result<RacerRoadsideObject>::failure(
            parseError(reader, "roadside object record is invalid"));
    }
    const auto id = parseInteger<std::uint32_t>(idToken);
    const auto segment = parseInteger<std::uint32_t>(segmentToken);
    auto sprite = parseGuidToken(guidToken, reader, "roadside.sprite");
    if (!id || *id == 0U || *id > std::numeric_limits<std::uint16_t>::max() || !segment ||
        !sprite) {
        return Result<RacerRoadsideObject>::failure(
            parseError(reader, "roadside object identifiers are invalid"));
    }
    object.id = static_cast<std::uint16_t>(*id);
    object.segment = *segment;
    object.lateral = canonicalFloat(object.lateral);
    object.scale = canonicalFloat(object.scale);
    object.sprite = sprite.value();
    return Result<RacerRoadsideObject>::success(object);
}

Result<RacerBackgroundLayer> parseBackground(LineReader& reader) {
    auto line = reader.next("background");
    if (!line)
        return Result<RacerBackgroundLayer>::failure(line.error());
    std::istringstream stream(line.value());
    stream.imbue(std::locale::classic());
    std::string key;
    std::string idToken;
    std::string guidToken;
    RacerBackgroundLayer layer;
    if (!(stream >> key >> idToken >> guidToken >> layer.parallax >> layer.verticalOffset >>
          layer.scale) ||
        key != "background" || !parseColor(stream, layer.tint) || !streamFinished(stream) ||
        !std::isfinite(layer.parallax) || !std::isfinite(layer.verticalOffset) ||
        !std::isfinite(layer.scale)) {
        return Result<RacerBackgroundLayer>::failure(
            parseError(reader, "background layer record is invalid"));
    }
    const auto id = parseInteger<std::uint32_t>(idToken);
    auto sprite = parseGuidToken(guidToken, reader, "background.sprite");
    if (!id || *id == 0U || *id > std::numeric_limits<std::uint16_t>::max() || !sprite) {
        return Result<RacerBackgroundLayer>::failure(
            parseError(reader, "background layer identifiers are invalid"));
    }
    layer.id = static_cast<std::uint16_t>(*id);
    layer.sprite = sprite.value();
    layer.parallax = canonicalFloat(layer.parallax);
    layer.verticalOffset = canonicalFloat(layer.verticalOffset);
    layer.scale = canonicalFloat(layer.scale);
    return Result<RacerBackgroundLayer>::success(layer);
}

Result<RacerOpponentSpawn> parseOpponent(LineReader& reader) {
    auto line = reader.next("opponent");
    if (!line)
        return Result<RacerOpponentSpawn>::failure(line.error());
    std::istringstream stream(line.value());
    stream.imbue(std::locale::classic());
    std::string key;
    std::string idToken;
    std::string segmentToken;
    std::string guidToken;
    RacerOpponentSpawn spawn;
    if (!(stream >> key >> idToken >> segmentToken >> spawn.lateral >> spawn.targetSpeed >>
          spawn.skill >> guidToken) ||
        key != "opponent" || !streamFinished(stream) || !std::isfinite(spawn.lateral) ||
        !std::isfinite(spawn.targetSpeed) || !std::isfinite(spawn.skill)) {
        return Result<RacerOpponentSpawn>::failure(
            parseError(reader, "opponent spawn record is invalid"));
    }
    const auto id = parseInteger<std::uint32_t>(idToken);
    const auto segment = parseInteger<std::uint32_t>(segmentToken);
    auto sprite = parseGuidToken(guidToken, reader, "opponent.sprite");
    if (!id || *id == 0U || *id > std::numeric_limits<std::uint16_t>::max() || !segment ||
        !sprite) {
        return Result<RacerOpponentSpawn>::failure(
            parseError(reader, "opponent spawn identifiers are invalid"));
    }
    spawn.id = static_cast<std::uint16_t>(*id);
    spawn.segment = *segment;
    spawn.lateral = canonicalFloat(spawn.lateral);
    spawn.targetSpeed = canonicalFloat(spawn.targetSpeed);
    spawn.skill = canonicalFloat(spawn.skill);
    spawn.sprite = sprite.value();
    return Result<RacerOpponentSpawn>::success(spawn);
}

Result<void> parsedValidation(const Result<void>& validation) {
    if (validation)
        return Result<void>::success();
    if (validation.error().code() == ErrorCode::CapacityExceeded)
        return Result<void>::failure(validation.error());
    return Result<void>::failure(Error(ErrorCode::InvalidFormat, "racer track failed validation")
                                     .addContext("reason", validation.error().message()));
}

} // namespace

Result<void> validateRacerTrack(const RacerTrackAsset& track,
                                const RacerTrackFormatLimits& limits) {
    if (!limitsValid(limits)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "racer track limits are invalid"));
    }
    if (track.guid.isNil() || track.name.empty() || track.name.size() > limits.maximumStringBytes ||
        !std::isfinite(track.segmentLength) || track.segmentLength <= 0.0F) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "racer track identity or segment length is invalid"));
    }
    if (track.segments.empty() || track.segments.size() > limits.maximumSegments ||
        track.checkpoints.empty() || track.checkpoints.size() > limits.maximumCheckpoints ||
        track.roadsideObjects.size() > limits.maximumRoadsideObjects ||
        track.backgroundLayers.size() > limits.maximumBackgroundLayers ||
        track.opponentSpawns.size() > limits.maximumOpponentSpawns) {
        return Result<void>::failure(
            Error(ErrorCode::CapacityExceeded, "racer track record count is invalid or excessive"));
    }
    if (track.startSegment >= track.segments.size() ||
        track.finishSegment >= track.segments.size()) {
        return Result<void>::failure(
            Error(ErrorCode::NotFound, "racer start or finish segment is out of range"));
    }
    if (weatherTag(track.weather.kind) == "unknown" || !std::isfinite(track.weather.intensity) ||
        track.weather.intensity < 0.0F || track.weather.intensity > 1.0F ||
        !std::isfinite(track.weather.visibility) || track.weather.visibility <= 0.0F ||
        track.weather.visibility > 1.0F || !std::isfinite(track.weather.wind) ||
        track.weather.wind < -1.0F || track.weather.wind > 1.0F) {
        return Result<void>::failure(Error(ErrorCode::InvalidArgument, "racer weather is invalid"));
    }
    for (const auto& segment : track.segments) {
        if (!finite(segment) || segment.width < 0.25F || segment.width > 2.0F) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "racer road segment is invalid"));
        }
    }

    std::set<std::uint32_t> checkpointSegments;
    for (const auto& checkpoint : track.checkpoints) {
        if (checkpoint.segment >= track.segments.size() || checkpoint.name.empty() ||
            checkpoint.name.size() > limits.maximumStringBytes ||
            !checkpointSegments.insert(checkpoint.segment).second) {
            return Result<void>::failure(Error(ErrorCode::InvalidArgument,
                                               "racer checkpoint is invalid or repeats a segment"));
        }
    }
    std::set<std::uint16_t> roadsideIds;
    for (const auto& object : track.roadsideObjects) {
        if (object.id == 0U || !roadsideIds.insert(object.id).second ||
            object.segment >= track.segments.size() || !std::isfinite(object.lateral) ||
            object.lateral < -4.0F || object.lateral > 4.0F || !std::isfinite(object.scale) ||
            object.scale <= 0.0F || object.scale > 8.0F || object.sprite.isNil()) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "racer roadside object is invalid"));
        }
    }
    std::set<std::uint16_t> backgroundIds;
    for (const auto& layer : track.backgroundLayers) {
        if (layer.id == 0U || !backgroundIds.insert(layer.id).second || layer.sprite.isNil() ||
            !std::isfinite(layer.parallax) || layer.parallax < 0.0F || layer.parallax > 4.0F ||
            !std::isfinite(layer.verticalOffset) || layer.verticalOffset < -4096.0F ||
            layer.verticalOffset > 4096.0F || !std::isfinite(layer.scale) || layer.scale <= 0.0F ||
            layer.scale > 8.0F) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "racer background layer is invalid"));
        }
    }
    std::set<std::uint16_t> opponentIds;
    for (const auto& spawn : track.opponentSpawns) {
        if (spawn.id == 0U || !opponentIds.insert(spawn.id).second ||
            spawn.segment >= track.segments.size() || !std::isfinite(spawn.lateral) ||
            spawn.lateral < -2.0F || spawn.lateral > 2.0F || !std::isfinite(spawn.targetSpeed) ||
            spawn.targetSpeed <= 0.0F || spawn.targetSpeed > 1000.0F ||
            !std::isfinite(spawn.skill) || spawn.skill < 0.0F || spawn.skill > 1.0F ||
            spawn.sprite.isNil()) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "racer opponent spawn is invalid"));
        }
    }
    return Result<void>::success();
}

Result<std::string> serializeRacerTrack(const RacerTrackAsset& track,
                                        const RacerTrackFormatLimits& limits) {
    auto valid = validateRacerTrack(track, limits);
    if (!valid)
        return Result<std::string>::failure(valid.error());

    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << "fgltrack " << RacerTrackFormatVersion << '\n';
    stream << "track_guid " << track.guid.toString() << '\n';
    writeQuoted(stream, "track_name", track.name);
    writeFloat(stream, "segment_length", track.segmentLength);
    stream << "start_segment " << track.startSegment << '\n';
    stream << "finish_segment " << track.finishSegment << '\n';
    stream << std::setprecision(std::numeric_limits<float>::max_digits10) << "weather "
           << weatherTag(track.weather.kind) << ' ' << canonicalFloat(track.weather.intensity)
           << ' ' << canonicalFloat(track.weather.visibility) << ' '
           << canonicalFloat(track.weather.wind) << ' ';
    writeColor(stream, track.weather.tint);
    stream << ' ' << track.weather.seed << '\n';

    stream << "segment_count " << track.segments.size() << '\n';
    for (const auto& segment : track.segments) {
        stream << std::setprecision(std::numeric_limits<float>::max_digits10) << "segment "
               << canonicalFloat(segment.curve) << ' ' << canonicalFloat(segment.hill) << ' '
               << canonicalFloat(segment.width) << ' ';
        writeColor(stream, segment.road);
        stream << ' ';
        writeColor(stream, segment.grass);
        stream << ' ';
        writeColor(stream, segment.rumble);
        stream << '\n';
    }
    stream << "checkpoint_count " << track.checkpoints.size() << '\n';
    for (const auto& checkpoint : track.checkpoints) {
        stream << "checkpoint " << checkpoint.segment << ' '
               << std::quoted(encodeControlCharacters(checkpoint.name)) << '\n';
    }

    auto roadside = track.roadsideObjects;
    std::sort(roadside.begin(), roadside.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.id < rhs.id; });
    stream << "roadside_count " << roadside.size() << '\n';
    for (const auto& object : roadside) {
        stream << std::setprecision(std::numeric_limits<float>::max_digits10) << "roadside "
               << object.id << ' ' << object.segment << ' ' << canonicalFloat(object.lateral) << ' '
               << canonicalFloat(object.scale) << ' ' << object.sprite.toString() << ' ';
        writeColor(stream, object.tint);
        stream << '\n';
    }

    auto backgrounds = track.backgroundLayers;
    std::sort(backgrounds.begin(), backgrounds.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.id < rhs.id; });
    stream << "background_count " << backgrounds.size() << '\n';
    for (const auto& layer : backgrounds) {
        stream << std::setprecision(std::numeric_limits<float>::max_digits10) << "background "
               << layer.id << ' ' << layer.sprite.toString() << ' '
               << canonicalFloat(layer.parallax) << ' ' << canonicalFloat(layer.verticalOffset)
               << ' ' << canonicalFloat(layer.scale) << ' ';
        writeColor(stream, layer.tint);
        stream << '\n';
    }

    auto opponents = track.opponentSpawns;
    std::sort(opponents.begin(), opponents.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.id < rhs.id; });
    stream << "opponent_count " << opponents.size() << '\n';
    for (const auto& spawn : opponents) {
        stream << std::setprecision(std::numeric_limits<float>::max_digits10) << "opponent "
               << spawn.id << ' ' << spawn.segment << ' ' << canonicalFloat(spawn.lateral) << ' '
               << canonicalFloat(spawn.targetSpeed) << ' ' << canonicalFloat(spawn.skill) << ' '
               << spawn.sprite.toString() << '\n';
    }
    stream << "track_end\n";
    auto output = stream.str();
    if (output.size() > limits.maximumSourceBytes) {
        return Result<std::string>::failure(
            Error(ErrorCode::CapacityExceeded, "serialized racer track exceeds the source limit"));
    }
    return Result<std::string>::success(std::move(output));
}

Result<RacerTrackAsset> deserializeRacerTrack(const std::string_view text,
                                              const RacerTrackFormatLimits& limits) {
    if (!limitsValid(limits)) {
        return Result<RacerTrackAsset>::failure(
            Error(ErrorCode::InvalidArgument, "racer track limits are invalid"));
    }
    if (text.size() > limits.maximumSourceBytes) {
        return Result<RacerTrackAsset>::failure(
            Error(ErrorCode::CapacityExceeded, "racer track source exceeds the byte limit"));
    }
    LineReader reader(text);
    auto header = reader.next("fgltrack <version>");
    if (!header)
        return Result<RacerTrackAsset>::failure(header.error());
    std::istringstream headerStream(header.value());
    headerStream.imbue(std::locale::classic());
    std::string magic;
    std::string versionToken;
    if (!(headerStream >> magic >> versionToken) || magic != "fgltrack" ||
        !streamFinished(headerStream)) {
        return Result<RacerTrackAsset>::failure(
            parseError(reader, "expected 'fgltrack <version>'"));
    }
    const auto version = parseInteger<std::uint32_t>(versionToken);
    if (!version)
        return Result<RacerTrackAsset>::failure(parseError(reader, "track version is invalid"));
    if (*version != RacerTrackFormatVersion) {
        return Result<RacerTrackAsset>::failure(
            Error(ErrorCode::UnsupportedVersion, "unsupported racer track version")
                .addContext("version", std::to_string(*version)));
    }

    RacerTrackAsset track;
    auto guid = parseGuid(reader, "track_guid");
    if (!guid)
        return Result<RacerTrackAsset>::failure(guid.error());
    track.guid = guid.value();
    auto name = parseQuoted(reader, "track_name", limits.maximumStringBytes);
    if (!name)
        return Result<RacerTrackAsset>::failure(name.error());
    track.name = std::move(name.value());
    auto segmentLength = parseFloat(reader, "segment_length");
    if (!segmentLength)
        return Result<RacerTrackAsset>::failure(segmentLength.error());
    track.segmentLength = segmentLength.value();
    auto start = parseUnsigned(reader, "start_segment");
    if (!start)
        return Result<RacerTrackAsset>::failure(start.error());
    track.startSegment = start.value();
    auto finish = parseUnsigned(reader, "finish_segment");
    if (!finish)
        return Result<RacerTrackAsset>::failure(finish.error());
    track.finishSegment = finish.value();
    auto weather = parseWeatherRecord(reader);
    if (!weather)
        return Result<RacerTrackAsset>::failure(weather.error());
    track.weather = weather.value();

    auto segmentCount = parseCount(reader, "segment_count", limits.maximumSegments);
    if (!segmentCount)
        return Result<RacerTrackAsset>::failure(segmentCount.error());
    track.segments.reserve(segmentCount.value());
    for (auto index = std::size_t{0}; index < segmentCount.value(); ++index) {
        auto segment = parseSegment(reader);
        if (!segment)
            return Result<RacerTrackAsset>::failure(segment.error());
        track.segments.push_back(segment.value());
    }

    auto checkpointCount = parseCount(reader, "checkpoint_count", limits.maximumCheckpoints);
    if (!checkpointCount)
        return Result<RacerTrackAsset>::failure(checkpointCount.error());
    track.checkpoints.reserve(checkpointCount.value());
    for (auto index = std::size_t{0}; index < checkpointCount.value(); ++index) {
        auto checkpoint = parseCheckpoint(reader, limits.maximumStringBytes);
        if (!checkpoint)
            return Result<RacerTrackAsset>::failure(checkpoint.error());
        track.checkpoints.push_back(std::move(checkpoint.value()));
    }

    auto roadsideCount = parseCount(reader, "roadside_count", limits.maximumRoadsideObjects);
    if (!roadsideCount)
        return Result<RacerTrackAsset>::failure(roadsideCount.error());
    track.roadsideObjects.reserve(roadsideCount.value());
    for (auto index = std::size_t{0}; index < roadsideCount.value(); ++index) {
        auto object = parseRoadside(reader);
        if (!object)
            return Result<RacerTrackAsset>::failure(object.error());
        track.roadsideObjects.push_back(object.value());
    }

    auto backgroundCount = parseCount(reader, "background_count", limits.maximumBackgroundLayers);
    if (!backgroundCount)
        return Result<RacerTrackAsset>::failure(backgroundCount.error());
    track.backgroundLayers.reserve(backgroundCount.value());
    for (auto index = std::size_t{0}; index < backgroundCount.value(); ++index) {
        auto layer = parseBackground(reader);
        if (!layer)
            return Result<RacerTrackAsset>::failure(layer.error());
        track.backgroundLayers.push_back(layer.value());
    }

    auto opponentCount = parseCount(reader, "opponent_count", limits.maximumOpponentSpawns);
    if (!opponentCount)
        return Result<RacerTrackAsset>::failure(opponentCount.error());
    track.opponentSpawns.reserve(opponentCount.value());
    for (auto index = std::size_t{0}; index < opponentCount.value(); ++index) {
        auto spawn = parseOpponent(reader);
        if (!spawn)
            return Result<RacerTrackAsset>::failure(spawn.error());
        track.opponentSpawns.push_back(spawn.value());
    }
    auto terminator = expectLiteral(reader, "track_end");
    if (!terminator)
        return Result<RacerTrackAsset>::failure(terminator.error());
    auto finished = reader.requireEnd();
    if (!finished)
        return Result<RacerTrackAsset>::failure(finished.error());
    auto valid = parsedValidation(validateRacerTrack(track, limits));
    if (!valid)
        return Result<RacerTrackAsset>::failure(valid.error());
    return Result<RacerTrackAsset>::success(std::move(track));
}

} // namespace fabgl::rendering
