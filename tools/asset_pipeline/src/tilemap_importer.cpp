#include <fabgl/assets/tilemap_importer.h>

#include <algorithm>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <tuple>
#include <utility>

namespace fabgl::assets {
namespace {

constexpr std::size_t LegacyTilemapHeaderSize = 20U;
constexpr std::size_t TilemapHeaderSize = 56U;
constexpr std::size_t TilesetHeaderSize = 64U;
constexpr std::size_t MaximumTilemapEncodedBytes = 64U * 1024U * 1024U;
constexpr std::size_t MaximumTilesetEncodedBytes = 4U * 1024U * 1024U;

[[nodiscard]] Error formatError(std::string message) {
    return Error(ErrorCode::InvalidFormat, std::move(message));
}

[[nodiscard]] std::string_view trim(std::string_view value) noexcept {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
        value.remove_prefix(1U);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
        value.remove_suffix(1U);
    return value;
}

[[nodiscard]] bool parseUnsigned(std::string_view token, std::uint32_t& value) noexcept {
    token = trim(token);
    if (token.empty() || token.front() == '-' || token.front() == '+')
        return false;
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value, 10);
    return parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size();
}

[[nodiscard]] bool validUtf8(const std::string_view text) noexcept {
    auto index = std::size_t{0U};
    while (index < text.size()) {
        const auto first = static_cast<std::uint8_t>(text[index++]);
        if (first < 0x80U) {
            if (first < 0x20U || first == 0x7FU)
                return false;
            continue;
        }
        std::uint32_t value = 0U;
        std::uint32_t minimum = 0U;
        auto continuation = 0U;
        if ((first & 0xE0U) == 0xC0U) {
            value = first & 0x1FU;
            minimum = 0x80U;
            continuation = 1U;
        } else if ((first & 0xF0U) == 0xE0U) {
            value = first & 0x0FU;
            minimum = 0x800U;
            continuation = 2U;
        } else if ((first & 0xF8U) == 0xF0U) {
            value = first & 0x07U;
            minimum = 0x10000U;
            continuation = 3U;
        } else {
            return false;
        }
        if (index + continuation > text.size())
            return false;
        for (auto byte = 0U; byte < continuation; ++byte) {
            const auto next = static_cast<std::uint8_t>(text[index++]);
            if ((next & 0xC0U) != 0x80U)
                return false;
            value = (value << 6U) | (next & 0x3FU);
        }
        if (value < minimum || value > 0x10FFFFU || (value >= 0x80U && value <= 0x9FU) ||
            (value >= 0xD800U && value <= 0xDFFFU))
            return false;
    }
    return true;
}

[[nodiscard]] bool finiteRect(const Rect value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.width) &&
           std::isfinite(value.height);
}

[[nodiscard]] std::uint64_t area(const std::uint32_t width, const std::uint32_t height) noexcept {
    return static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
}

void appendU16(std::vector<std::uint8_t>& output, const std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void appendU32(std::vector<std::uint8_t>& output, const std::uint32_t value) {
    for (unsigned int shift = 0U; shift < 32U; shift += 8U)
        output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
}

void appendFloat(std::vector<std::uint8_t>& output, float value) {
    if (value == 0.0F)
        value = 0.0F;
    appendU32(output, std::bit_cast<std::uint32_t>(value));
}

void appendGuid(std::vector<std::uint8_t>& output, const AssetGuid guid) {
    output.insert(output.end(), guid.bytes().begin(), guid.bytes().end());
}

void appendString(std::vector<std::uint8_t>& output, const std::string_view value) {
    appendU16(output, static_cast<std::uint16_t>(value.size()));
    output.insert(output.end(), value.begin(), value.end());
}

void patchU32(std::vector<std::uint8_t>& output, const std::size_t offset,
              const std::uint32_t value) {
    for (unsigned int byte = 0U; byte < 4U; ++byte)
        output[offset + byte] = static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xFFU);
}

[[nodiscard]] bool addEncodedSize(std::size_t& size, const std::size_t additional,
                                  const std::size_t maximum) noexcept {
    if (size > maximum || additional > maximum - size)
        return false;
    size += additional;
    return true;
}

[[nodiscard]] std::uint8_t requiredElementBytes(const std::vector<std::uint32_t>& tiles) noexcept {
    const auto maximum = tiles.empty() ? 0U : *std::max_element(tiles.begin(), tiles.end());
    if (maximum <= std::numeric_limits<std::uint8_t>::max())
        return 1U;
    if (maximum <= std::numeric_limits<std::uint16_t>::max())
        return 2U;
    return 4U;
}

[[nodiscard]] bool addCellEncodedSize(std::size_t& size, const std::vector<std::uint32_t>& cells,
                                      const std::size_t maximum) noexcept {
    if (!addEncodedSize(size, 8U, maximum))
        return false;
    const auto elementBytes = requiredElementBytes(cells);
    return cells.size() <= (maximum - size) / elementBytes &&
           addEncodedSize(size, cells.size() * elementBytes, maximum);
}

void appendCells(std::vector<std::uint8_t>& output, const std::vector<std::uint32_t>& cells) {
    appendU32(output, static_cast<std::uint32_t>(cells.size()));
    const auto elementBytes = requiredElementBytes(cells);
    output.push_back(elementBytes);
    output.insert(output.end(), {0U, 0U, 0U});
    for (const auto tile : cells) {
        for (std::uint8_t byte = 0U; byte < elementBytes; ++byte)
            output.push_back(static_cast<std::uint8_t>((tile >> (byte * 8U)) & 0xFFU));
    }
}

class Reader final {
  public:
    explicit Reader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

    [[nodiscard]] bool readU8(std::uint8_t& value) noexcept {
        if (remaining() < 1U)
            return false;
        value = bytes_[position_++];
        return true;
    }

    [[nodiscard]] bool readU16(std::uint16_t& value) noexcept {
        if (remaining() < 2U)
            return false;
        value =
            static_cast<std::uint16_t>(bytes_[position_]) |
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes_[position_ + 1U]) << 8U);
        position_ += 2U;
        return true;
    }

    [[nodiscard]] bool readU32(std::uint32_t& value) noexcept {
        if (remaining() < 4U)
            return false;
        value = 0U;
        for (unsigned int shift = 0U; shift < 32U; shift += 8U)
            value |= static_cast<std::uint32_t>(bytes_[position_++]) << shift;
        return true;
    }

    [[nodiscard]] bool readFloat(float& value) noexcept {
        std::uint32_t bits = 0U;
        if (!readU32(bits))
            return false;
        value = std::bit_cast<float>(bits);
        return std::isfinite(value) && bits != 0x80000000U;
    }

    [[nodiscard]] bool readGuid(AssetGuid& value) noexcept {
        if (remaining() < 16U)
            return false;
        detail::GuidBytes bytes{};
        std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(position_), bytes.size(),
                    bytes.begin());
        position_ += bytes.size();
        value = AssetGuid(bytes);
        return true;
    }

    [[nodiscard]] bool readString(std::string& value, const std::uint32_t maximumBytes) {
        std::uint16_t length = 0U;
        if (!readU16(length) || length == 0U || length > maximumBytes || remaining() < length)
            return false;
        value.assign(reinterpret_cast<const char*>(bytes_.data() + position_), length);
        position_ += length;
        return validUtf8(value);
    }

    [[nodiscard]] bool readCells(std::vector<std::uint32_t>& cells,
                                 const std::uint64_t maximumCells) {
        std::uint32_t count = 0U;
        std::uint8_t elementBytes = 0U;
        std::uint8_t reserved = 0U;
        if (!readU32(count) || !readU8(elementBytes) || !readU8(reserved) || reserved != 0U ||
            !readU8(reserved) || reserved != 0U || !readU8(reserved) || reserved != 0U ||
            count > maximumCells ||
            (elementBytes != 1U && elementBytes != 2U && elementBytes != 4U) ||
            static_cast<std::uint64_t>(count) * elementBytes > remaining())
            return false;
        cells.reserve(count);
        for (std::uint32_t index = 0U; index < count; ++index) {
            std::uint32_t tile = 0U;
            for (std::uint8_t byte = 0U; byte < elementBytes; ++byte)
                tile |= static_cast<std::uint32_t>(bytes_[position_++]) << (byte * 8U);
            cells.push_back(tile);
        }
        return requiredElementBytes(cells) == elementBytes;
    }

    [[nodiscard]] bool skip(const std::size_t count) noexcept {
        if (remaining() < count)
            return false;
        position_ += count;
        return true;
    }
    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - position_;
    }

  private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t position_ = 0U;
};

class JsonTilemapParser final {
  public:
    explicit JsonTilemapParser(const std::string_view source) : source_(source) {}

    [[nodiscard]] Result<Tilemap> parse(const TilemapLimits& limits) {
        skipWhitespace();
        if (!consume('{'))
            return fail("JSON tilemap must begin with an object");
        bool haveWidth = false;
        bool haveHeight = false;
        bool haveTiles = false;
        Tilemap tilemap;
        skipWhitespace();
        if (consume('}'))
            return fail("JSON tilemap object is empty");
        while (true) {
            std::string key;
            if (!parseString(key))
                return fail("JSON tilemap key must be a plain JSON string");
            skipWhitespace();
            if (!consume(':'))
                return fail("JSON tilemap key is missing ':'");
            skipWhitespace();
            if (key == "width") {
                if (haveWidth || !parseNumber(tilemap.width))
                    return fail("JSON tilemap width is duplicate or invalid");
                haveWidth = true;
            } else if (key == "height") {
                if (haveHeight || !parseNumber(tilemap.height))
                    return fail("JSON tilemap height is duplicate or invalid");
                haveHeight = true;
            } else if (key == "tiles") {
                if (haveTiles || !parseArray(tilemap.tiles, limits.maximumCells))
                    return fail("JSON tilemap tiles must be a bounded flat integer array");
                haveTiles = true;
            } else {
                return fail("JSON tilemap contains an unsupported key");
            }
            skipWhitespace();
            if (consume('}'))
                break;
            if (!consume(','))
                return fail("JSON tilemap members must be comma-separated");
            skipWhitespace();
        }
        skipWhitespace();
        if (position_ != source_.size())
            return fail("JSON tilemap has trailing data");
        if (!haveWidth || !haveHeight || !haveTiles || !tilemap.valid(limits))
            return fail("JSON tilemap dimensions do not match its tile array");
        return Result<Tilemap>::success(std::move(tilemap));
    }

  private:
    void skipWhitespace() noexcept {
        while (position_ < source_.size() &&
               std::isspace(static_cast<unsigned char>(source_[position_])) != 0)
            ++position_;
    }
    [[nodiscard]] bool consume(const char expected) noexcept {
        if (position_ >= source_.size() || source_[position_] != expected)
            return false;
        ++position_;
        return true;
    }
    [[nodiscard]] bool parseString(std::string& value) {
        if (!consume('"'))
            return false;
        while (position_ < source_.size() && source_[position_] != '"') {
            const auto character = source_[position_++];
            if (character == '\\' || static_cast<unsigned char>(character) < 0x20U)
                return false;
            value.push_back(character);
        }
        return consume('"');
    }
    [[nodiscard]] bool parseNumber(std::uint32_t& value) {
        const auto begin = position_;
        while (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9')
            ++position_;
        return begin != position_ && parseUnsigned(source_.substr(begin, position_ - begin), value);
    }
    [[nodiscard]] bool parseArray(std::vector<std::uint32_t>& values,
                                  const std::uint32_t maximumCells) {
        if (!consume('['))
            return false;
        skipWhitespace();
        if (consume(']'))
            return true;
        while (true) {
            std::uint32_t value = 0U;
            if (!parseNumber(value) || values.size() >= maximumCells)
                return false;
            values.push_back(value);
            skipWhitespace();
            if (consume(']'))
                return true;
            if (!consume(','))
                return false;
            skipWhitespace();
        }
    }
    [[nodiscard]] static Result<Tilemap> fail(const char* message) {
        return Result<Tilemap>::failure(formatError(message));
    }
    std::string_view source_;
    std::size_t position_ = 0U;
};

[[nodiscard]] std::vector<TilemapLayer> normalizedLayers(const Tilemap& tilemap) {
    if (!tilemap.layers.empty())
        return tilemap.layers;
    return {{"Ground", TilemapLayerKind::Tiles, tilemap.tiles, 1.0F, 1.0F, 255U, true}};
}

[[nodiscard]] Result<ImportedAsset> importTilemapRequest(const AssetImportRequest& request,
                                                         const bool json) {
    const auto source = std::string_view(reinterpret_cast<const char*>(request.sourceBytes.data()),
                                         request.sourceBytes.size());
    auto parsed = json ? importJsonTilemap(source) : importCsvTilemap(source);
    if (!parsed)
        return Result<ImportedAsset>::failure(parsed.error());
    parsed.value().guid = request.guid;
    auto encoded = encodeTilemap(parsed.value());
    if (!encoded)
        return Result<ImportedAsset>::failure(encoded.error());
    ImportedAsset output;
    output.payload = std::move(encoded.value());
    output.flashBytes = output.payload.size();
    output.internalRamBytes = parsed.value().tiles.size() * sizeof(std::uint32_t);
    output.estimatedDecodeMicros = static_cast<std::uint32_t>(parsed.value().tiles.size() / 8U);
    return Result<ImportedAsset>::success(std::move(output));
}

[[nodiscard]] Result<Tilemap> inspectLegacyTilemap(const std::vector<std::uint8_t>& bytes,
                                                   const TilemapLimits& limits) {
    if (bytes.size() < LegacyTilemapHeaderSize)
        return Result<Tilemap>::failure(formatError("legacy tilemap header is truncated"));
    Reader reader(bytes);
    if (!reader.skip(4U))
        return Result<Tilemap>::failure(formatError("legacy tilemap header is truncated"));
    std::uint16_t version = 0U;
    std::uint16_t flags = 0U;
    std::uint16_t width = 0U;
    std::uint16_t height = 0U;
    std::uint32_t count = 0U;
    std::uint8_t elementBytes = 0U;
    std::uint8_t reserved = 0U;
    if (!reader.readU16(version) || version != 1U || !reader.readU16(flags) || flags != 0U ||
        !reader.readU16(width) || !reader.readU16(height) || !reader.readU32(count) ||
        !reader.readU8(elementBytes) ||
        (elementBytes != 1U && elementBytes != 2U && elementBytes != 4U) ||
        !reader.readU8(reserved) || reserved != 0U || !reader.readU8(reserved) || reserved != 0U ||
        !reader.readU8(reserved) || reserved != 0U || count > limits.maximumCells ||
        static_cast<std::uint64_t>(count) * elementBytes != reader.remaining())
        return Result<Tilemap>::failure(formatError("legacy tilemap header is invalid"));
    Tilemap result;
    result.width = width;
    result.height = height;
    result.tiles.reserve(count);
    for (std::uint32_t index = 0U; index < count; ++index) {
        std::uint32_t tile = 0U;
        for (std::uint8_t byte = 0U; byte < elementBytes; ++byte) {
            std::uint8_t value = 0U;
            if (!reader.readU8(value))
                return Result<Tilemap>::failure(formatError("legacy tilemap is truncated"));
            tile |= static_cast<std::uint32_t>(value) << (byte * 8U);
        }
        result.tiles.push_back(tile);
    }
    if (!result.valid(limits) || requiredElementBytes(result.tiles) != elementBytes)
        return Result<Tilemap>::failure(formatError("legacy tilemap is not canonical"));
    return Result<Tilemap>::success(std::move(result));
}

} // namespace

bool Tilemap::valid(const TilemapLimits& limits) const noexcept {
    const auto cellCount = area(width, height);
    if (width == 0U || height == 0U || width > limits.maximumWidth ||
        height > limits.maximumHeight || cellCount > limits.maximumCells || tileWidth == 0U ||
        tileHeight == 0U || layers.size() > limits.maximumLayers ||
        objects.size() > limits.maximumObjects || chunks.size() > limits.maximumChunks ||
        animations.size() > limits.maximumAnimations || tilesets.size() > limits.maximumTilesets)
        return false;

    if (layers.empty() && tiles.size() != cellCount)
        return false;
    if (!layers.empty() && !tiles.empty() && tiles != layers.front().cells)
        return false;
    for (std::size_t index = 0U; index < layers.size(); ++index) {
        const auto& layer = layers[index];
        if (layer.name.empty() || layer.name.size() > limits.maximumStringBytes ||
            !validUtf8(layer.name) ||
            static_cast<std::uint8_t>(layer.kind) >
                static_cast<std::uint8_t>(TilemapLayerKind::Objects) ||
            layer.cells.size() != cellCount || !std::isfinite(layer.parallaxX) ||
            !std::isfinite(layer.parallaxY) || layer.parallaxX < 0.0F || layer.parallaxY < 0.0F)
            return false;
        for (std::size_t previous = 0U; previous < index; ++previous) {
            if (layers[previous].name == layer.name)
                return false;
        }
    }

    for (std::size_t index = 0U; index < tilesets.size(); ++index) {
        const auto& reference = tilesets[index];
        const auto end = static_cast<std::uint64_t>(reference.firstTile) + reference.tileCount;
        if (reference.tileset.isNil() || reference.tileCount == 0U ||
            end > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1U)
            return false;
        for (std::size_t previous = 0U; previous < index; ++previous) {
            const auto& prior = tilesets[previous];
            const auto priorEnd = static_cast<std::uint64_t>(prior.firstTile) + prior.tileCount;
            if (prior.tileset == reference.tileset ||
                (reference.firstTile < priorEnd && prior.firstTile < end))
                return false;
        }
    }
    const auto mappedTile = [this](const std::uint32_t tile) {
        if (tile == 0U || tilesets.empty())
            return true;
        return std::any_of(tilesets.begin(), tilesets.end(), [tile](const auto& reference) {
            const auto end = static_cast<std::uint64_t>(reference.firstTile) +
                             static_cast<std::uint64_t>(reference.tileCount);
            return tile >= reference.firstTile && tile < end;
        });
    };
    if (layers.empty() && !std::all_of(tiles.begin(), tiles.end(), mappedTile))
        return false;
    for (const auto& layer : layers) {
        if (!std::all_of(layer.cells.begin(), layer.cells.end(), mappedTile))
            return false;
    }

    const auto layerCount = layers.empty() ? std::size_t{1U} : layers.size();
    for (std::size_t index = 0U; index < objects.size(); ++index) {
        const auto& object = objects[index];
        if (object.id == 0U || object.layer >= layerCount || object.type.empty() ||
            object.type.size() > limits.maximumStringBytes || !validUtf8(object.type) ||
            !finiteRect(object.bounds) || object.bounds.x < 0.0F || object.bounds.y < 0.0F ||
            object.bounds.width < 0.0F || object.bounds.height < 0.0F ||
            object.bounds.x + object.bounds.width > static_cast<float>(width) ||
            object.bounds.y + object.bounds.height > static_cast<float>(height))
            return false;
        for (std::size_t previous = 0U; previous < index; ++previous) {
            if (objects[previous].id == object.id)
                return false;
        }
    }

    for (const auto& chunk : chunks) {
        const auto chunkCells = area(chunk.width, chunk.height);
        if (chunk.layer >= layerCount || chunk.width == 0U || chunk.height == 0U ||
            chunk.x >= width || chunk.y >= height || chunk.width > width - chunk.x ||
            chunk.height > height - chunk.y || chunkCells > limits.maximumCells ||
            chunk.cells.size() != chunkCells)
            return false;
        const auto& layerCells = layers.empty() ? tiles : layers[chunk.layer].cells;
        for (std::uint32_t y = 0U; y < chunk.height; ++y) {
            for (std::uint32_t x = 0U; x < chunk.width; ++x) {
                const auto mapOffset = static_cast<std::size_t>(chunk.y + y) * width + chunk.x + x;
                const auto chunkOffset = static_cast<std::size_t>(y) * chunk.width + x;
                if (chunk.cells[chunkOffset] != layerCells[mapOffset])
                    return false;
            }
        }
    }

    std::size_t totalFrames = 0U;
    for (std::size_t index = 0U; index < animations.size(); ++index) {
        const auto& animation = animations[index];
        if (animation.frames.empty() ||
            animation.frames.size() > limits.maximumAnimationFrames - totalFrames ||
            !mappedTile(animation.outputTile))
            return false;
        for (std::size_t previous = 0U; previous < index; ++previous) {
            if (animations[previous].outputTile == animation.outputTile)
                return false;
        }
        totalFrames += animation.frames.size();
        if (!std::all_of(animation.frames.begin(), animation.frames.end(), [&](const auto& frame) {
                return frame.durationMilliseconds > 0U && frame.durationMilliseconds <= 60'000U &&
                       mappedTile(frame.tile);
            }))
            return false;
    }
    return true;
}

bool Tileset::valid(const TilesetLimits& limits) const noexcept {
    if (guid.isNil() || sourceImage.isNil() || name.empty() ||
        name.size() > limits.maximumStringBytes || !validUtf8(name) || tileWidth == 0U ||
        tileHeight == 0U || tileCount == 0U || tileCount > limits.maximumTiles || columns == 0U ||
        columns > tileCount || collisionTiles.size() > limits.maximumCollisionTiles)
        return false;
    return std::is_sorted(collisionTiles.begin(), collisionTiles.end()) &&
           std::adjacent_find(collisionTiles.begin(), collisionTiles.end()) ==
               collisionTiles.end() &&
           std::all_of(collisionTiles.begin(), collisionTiles.end(),
                       [this](const auto tile) { return tile < tileCount; });
}

Result<Tilemap> importCsvTilemap(const std::string_view source, const TilemapLimits& limits) {
    if (source.empty() || source.size() > 16U * 1024U * 1024U)
        return Result<Tilemap>::failure(formatError("CSV tilemap source is empty or too large"));
    Tilemap result;
    auto position = std::size_t{0U};
    while (position <= source.size()) {
        const auto end = source.find('\n', position);
        auto line = source.substr(position, end == std::string_view::npos ? source.size() - position
                                                                          : end - position);
        line = trim(line);
        if (!line.empty() && line.front() != '#') {
            std::uint32_t rowWidth = 0U;
            auto fieldPosition = std::size_t{0U};
            while (fieldPosition <= line.size()) {
                const auto comma = line.find(',', fieldPosition);
                const auto token = line.substr(fieldPosition, comma == std::string_view::npos
                                                                  ? line.size() - fieldPosition
                                                                  : comma - fieldPosition);
                std::uint32_t value = 0U;
                if (!parseUnsigned(token, value))
                    return Result<Tilemap>::failure(formatError("CSV tilemap has an invalid cell"));
                if (result.tiles.size() >= limits.maximumCells)
                    return Result<Tilemap>::failure(
                        Error(ErrorCode::CapacityExceeded, "CSV tilemap exceeds the cell limit"));
                result.tiles.push_back(value);
                ++rowWidth;
                if (comma == std::string_view::npos)
                    break;
                fieldPosition = comma + 1U;
            }
            if (result.width == 0U)
                result.width = rowWidth;
            else if (rowWidth != result.width)
                return Result<Tilemap>::failure(
                    formatError("CSV tilemap rows have different widths"));
            ++result.height;
            if (result.width > limits.maximumWidth || result.height > limits.maximumHeight)
                return Result<Tilemap>::failure(
                    Error(ErrorCode::CapacityExceeded, "CSV tilemap exceeds dimension limits"));
        }
        if (end == std::string_view::npos)
            break;
        position = end + 1U;
    }
    if (!result.valid(limits))
        return Result<Tilemap>::failure(formatError("CSV tilemap has no rectangular tile data"));
    return Result<Tilemap>::success(std::move(result));
}

Result<Tilemap> importJsonTilemap(const std::string_view source, const TilemapLimits& limits) {
    if (source.empty() || source.size() > 16U * 1024U * 1024U)
        return Result<Tilemap>::failure(formatError("JSON tilemap source is empty or too large"));
    return JsonTilemapParser(source).parse(limits);
}

Result<std::vector<std::uint8_t>> encodeTilemap(const Tilemap& tilemap) {
    if (!tilemap.valid())
        return Result<std::vector<std::uint8_t>>::failure(
            Error(ErrorCode::InvalidArgument, "tilemap cannot be encoded"));
    const auto layers = normalizedLayers(tilemap);
    if (layers.size() > std::numeric_limits<std::uint16_t>::max() ||
        tilemap.tilesets.size() > std::numeric_limits<std::uint16_t>::max() ||
        tilemap.animations.size() > std::numeric_limits<std::uint16_t>::max() ||
        tilemap.objects.size() > std::numeric_limits<std::uint32_t>::max() ||
        tilemap.chunks.size() > std::numeric_limits<std::uint32_t>::max())
        return Result<std::vector<std::uint8_t>>::failure(
            Error(ErrorCode::CapacityExceeded, "tilemap record counts cannot be encoded"));

    auto tilesets = tilemap.tilesets;
    std::sort(tilesets.begin(), tilesets.end(), [](const auto& lhs, const auto& rhs) {
        return std::tie(lhs.firstTile, lhs.tileset) < std::tie(rhs.firstTile, rhs.tileset);
    });
    auto objects = tilemap.objects;
    std::sort(objects.begin(), objects.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.id < rhs.id; });
    auto chunks = tilemap.chunks;
    std::sort(chunks.begin(), chunks.end(), [](const auto& lhs, const auto& rhs) {
        return std::tie(lhs.layer, lhs.y, lhs.x, lhs.height, lhs.width) <
               std::tie(rhs.layer, rhs.y, rhs.x, rhs.height, rhs.width);
    });
    auto animations = tilemap.animations;
    std::sort(animations.begin(), animations.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.outputTile < rhs.outputTile; });

    auto encodedSize = TilemapHeaderSize;
    if (!addEncodedSize(encodedSize, tilesets.size() * 24U, MaximumTilemapEncodedBytes))
        return Result<std::vector<std::uint8_t>>::failure(
            Error(ErrorCode::CapacityExceeded, "tilemap encoding exceeds the byte limit"));
    for (const auto& layer : layers) {
        if (!addEncodedSize(encodedSize, 14U + layer.name.size(), MaximumTilemapEncodedBytes) ||
            !addCellEncodedSize(encodedSize, layer.cells, MaximumTilemapEncodedBytes))
            return Result<std::vector<std::uint8_t>>::failure(
                Error(ErrorCode::CapacityExceeded, "tilemap encoding exceeds the byte limit"));
    }
    for (const auto& object : objects) {
        if (!addEncodedSize(encodedSize, 40U + object.type.size(), MaximumTilemapEncodedBytes))
            return Result<std::vector<std::uint8_t>>::failure(
                Error(ErrorCode::CapacityExceeded, "tilemap encoding exceeds the byte limit"));
    }
    for (const auto& chunk : chunks) {
        if (!addEncodedSize(encodedSize, 20U, MaximumTilemapEncodedBytes) ||
            !addCellEncodedSize(encodedSize, chunk.cells, MaximumTilemapEncodedBytes))
            return Result<std::vector<std::uint8_t>>::failure(
                Error(ErrorCode::CapacityExceeded, "tilemap encoding exceeds the byte limit"));
    }
    for (const auto& animation : animations) {
        if (!addEncodedSize(encodedSize, 8U + animation.frames.size() * 8U,
                            MaximumTilemapEncodedBytes))
            return Result<std::vector<std::uint8_t>>::failure(
                Error(ErrorCode::CapacityExceeded, "tilemap encoding exceeds the byte limit"));
    }

    std::vector<std::uint8_t> output;
    output.reserve(encodedSize);
    output.insert(output.end(), {'F', 'G', 'L', 'T'});
    appendU16(output, 2U);
    appendU16(output, 0U);
    appendU32(output, 0U);
    appendU32(output, tilemap.width);
    appendU32(output, tilemap.height);
    appendU16(output, tilemap.tileWidth);
    appendU16(output, tilemap.tileHeight);
    appendU16(output, static_cast<std::uint16_t>(layers.size()));
    appendU16(output, static_cast<std::uint16_t>(tilesets.size()));
    appendU32(output, static_cast<std::uint32_t>(objects.size()));
    appendU32(output, static_cast<std::uint32_t>(chunks.size()));
    appendU16(output, static_cast<std::uint16_t>(animations.size()));
    appendU16(output, 0U);
    appendGuid(output, tilemap.guid);

    for (const auto& reference : tilesets) {
        appendGuid(output, reference.tileset);
        appendU32(output, reference.firstTile);
        appendU32(output, reference.tileCount);
    }
    for (const auto& layer : layers) {
        appendString(output, layer.name);
        output.push_back(static_cast<std::uint8_t>(layer.kind));
        output.push_back(layer.visible ? 1U : 0U);
        output.push_back(layer.opacity);
        output.push_back(0U);
        appendFloat(output, layer.parallaxX);
        appendFloat(output, layer.parallaxY);
        appendCells(output, layer.cells);
    }
    for (const auto& object : objects) {
        appendU32(output, object.id);
        appendU16(output, object.layer);
        appendString(output, object.type);
        appendFloat(output, object.bounds.x);
        appendFloat(output, object.bounds.y);
        appendFloat(output, object.bounds.width);
        appendFloat(output, object.bounds.height);
        appendGuid(output, object.asset);
    }
    for (const auto& chunk : chunks) {
        appendU16(output, chunk.layer);
        appendU16(output, 0U);
        appendU32(output, chunk.x);
        appendU32(output, chunk.y);
        appendU32(output, chunk.width);
        appendU32(output, chunk.height);
        appendCells(output, chunk.cells);
    }
    for (const auto& animation : animations) {
        appendU32(output, animation.outputTile);
        appendU16(output, static_cast<std::uint16_t>(animation.frames.size()));
        appendU16(output, 0U);
        for (const auto& frame : animation.frames) {
            appendU32(output, frame.tile);
            appendU32(output, frame.durationMilliseconds);
        }
    }
    if (output.size() != encodedSize || output.size() > std::numeric_limits<std::uint32_t>::max())
        return Result<std::vector<std::uint8_t>>::failure(
            Error(ErrorCode::InvalidState, "tilemap encoder size calculation disagrees"));
    patchU32(output, 8U, static_cast<std::uint32_t>(output.size()));
    return Result<std::vector<std::uint8_t>>::success(std::move(output));
}

Result<Tilemap> inspectTilemap(const std::vector<std::uint8_t>& bytes,
                               const TilemapLimits& limits) {
    if (bytes.size() < 8U || bytes[0] != 'F' || bytes[1] != 'G' || bytes[2] != 'L' ||
        bytes[3] != 'T')
        return Result<Tilemap>::failure(formatError("tilemap magic is invalid"));
    if (bytes.size() > limits.maximumEncodedBytes)
        return Result<Tilemap>::failure(formatError("tilemap byte length exceeds its limit"));
    const auto version = static_cast<std::uint16_t>(bytes[4]) |
                         static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[5]) << 8U);
    if (version == 1U)
        return inspectLegacyTilemap(bytes, limits);
    if (version != 2U)
        return Result<Tilemap>::failure(
            Error(ErrorCode::UnsupportedVersion, "tilemap version is unsupported"));
    if (bytes.size() < TilemapHeaderSize)
        return Result<Tilemap>::failure(formatError("tilemap byte length is invalid"));

    Reader reader(bytes);
    if (!reader.skip(4U))
        return Result<Tilemap>::failure(formatError("tilemap header is truncated"));
    std::uint16_t readVersion = 0U;
    std::uint16_t flags = 0U;
    std::uint32_t encodedSize = 0U;
    Tilemap result;
    std::uint16_t layerCount = 0U;
    std::uint16_t tilesetCount = 0U;
    std::uint32_t objectCount = 0U;
    std::uint32_t chunkCount = 0U;
    std::uint16_t animationCount = 0U;
    std::uint16_t reserved16 = 0U;
    if (!reader.readU16(readVersion) || readVersion != 2U || !reader.readU16(flags) ||
        flags != 0U || !reader.readU32(encodedSize) || encodedSize != bytes.size() ||
        !reader.readU32(result.width) || !reader.readU32(result.height) ||
        !reader.readU16(result.tileWidth) || !reader.readU16(result.tileHeight) ||
        !reader.readU16(layerCount) || layerCount == 0U || layerCount > limits.maximumLayers ||
        !reader.readU16(tilesetCount) || tilesetCount > limits.maximumTilesets ||
        !reader.readU32(objectCount) || objectCount > limits.maximumObjects ||
        !reader.readU32(chunkCount) || chunkCount > limits.maximumChunks ||
        !reader.readU16(animationCount) || animationCount > limits.maximumAnimations ||
        !reader.readU16(reserved16) || reserved16 != 0U || !reader.readGuid(result.guid))
        return Result<Tilemap>::failure(formatError("tilemap header is invalid"));

    result.tilesets.reserve(tilesetCount);
    for (std::uint16_t index = 0U; index < tilesetCount; ++index) {
        TilemapTilesetReference reference;
        if (!reader.readGuid(reference.tileset) || !reader.readU32(reference.firstTile) ||
            !reader.readU32(reference.tileCount))
            return Result<Tilemap>::failure(
                formatError("tilemap tileset references are truncated"));
        result.tilesets.push_back(reference);
    }
    result.layers.reserve(layerCount);
    const auto mapCells = area(result.width, result.height);
    for (std::uint16_t index = 0U; index < layerCount; ++index) {
        TilemapLayer layer;
        std::uint8_t kind = 0U;
        std::uint8_t visible = 0U;
        std::uint8_t reserved = 0U;
        if (!reader.readString(layer.name, limits.maximumStringBytes) || !reader.readU8(kind) ||
            kind > static_cast<std::uint8_t>(TilemapLayerKind::Objects) ||
            !reader.readU8(visible) || visible > 1U || !reader.readU8(layer.opacity) ||
            !reader.readU8(reserved) || reserved != 0U || !reader.readFloat(layer.parallaxX) ||
            !reader.readFloat(layer.parallaxY) ||
            !reader.readCells(layer.cells, limits.maximumCells) || layer.cells.size() != mapCells)
            return Result<Tilemap>::failure(formatError("tilemap layer record is invalid"));
        layer.kind = static_cast<TilemapLayerKind>(kind);
        layer.visible = visible != 0U;
        result.layers.push_back(std::move(layer));
    }
    result.tiles = result.layers.front().cells;

    result.objects.reserve(objectCount);
    for (std::uint32_t index = 0U; index < objectCount; ++index) {
        TilemapObject object;
        if (!reader.readU32(object.id) || !reader.readU16(object.layer) ||
            !reader.readString(object.type, limits.maximumStringBytes) ||
            !reader.readFloat(object.bounds.x) || !reader.readFloat(object.bounds.y) ||
            !reader.readFloat(object.bounds.width) || !reader.readFloat(object.bounds.height) ||
            !reader.readGuid(object.asset))
            return Result<Tilemap>::failure(formatError("tilemap object record is invalid"));
        result.objects.push_back(std::move(object));
    }
    result.chunks.reserve(chunkCount);
    for (std::uint32_t index = 0U; index < chunkCount; ++index) {
        TilemapChunk chunk;
        if (!reader.readU16(chunk.layer) || !reader.readU16(reserved16) || reserved16 != 0U ||
            !reader.readU32(chunk.x) || !reader.readU32(chunk.y) || !reader.readU32(chunk.width) ||
            !reader.readU32(chunk.height) || !reader.readCells(chunk.cells, limits.maximumCells))
            return Result<Tilemap>::failure(formatError("tilemap chunk record is invalid"));
        result.chunks.push_back(std::move(chunk));
    }
    result.animations.reserve(animationCount);
    std::size_t frameTotal = 0U;
    for (std::uint16_t index = 0U; index < animationCount; ++index) {
        TileAnimation animation;
        std::uint16_t frameCount = 0U;
        if (!reader.readU32(animation.outputTile) || !reader.readU16(frameCount) ||
            !reader.readU16(reserved16) || reserved16 != 0U || frameCount == 0U ||
            frameCount > limits.maximumAnimationFrames - frameTotal)
            return Result<Tilemap>::failure(formatError("tilemap animation record is invalid"));
        frameTotal += frameCount;
        animation.frames.reserve(frameCount);
        for (std::uint16_t frame = 0U; frame < frameCount; ++frame) {
            TileAnimationFrame value;
            if (!reader.readU32(value.tile) || !reader.readU32(value.durationMilliseconds))
                return Result<Tilemap>::failure(
                    formatError("tilemap animation frames are truncated"));
            animation.frames.push_back(value);
        }
        result.animations.push_back(std::move(animation));
    }
    if (reader.remaining() != 0U || !result.valid(limits))
        return Result<Tilemap>::failure(formatError("tilemap records or limits are invalid"));
    auto canonical = encodeTilemap(result);
    if (!canonical || canonical.value() != bytes)
        return Result<Tilemap>::failure(formatError("tilemap encoding is not canonical"));
    return Result<Tilemap>::success(std::move(result));
}

Result<std::vector<std::uint8_t>> encodeTileset(const Tileset& tileset) {
    if (!tileset.valid())
        return Result<std::vector<std::uint8_t>>::failure(
            Error(ErrorCode::InvalidArgument, "tileset cannot be encoded"));
    auto encodedSize = TilesetHeaderSize;
    if (!addEncodedSize(encodedSize, 2U + tileset.name.size(), MaximumTilesetEncodedBytes) ||
        !addEncodedSize(encodedSize, tileset.collisionTiles.size() * sizeof(std::uint32_t),
                        MaximumTilesetEncodedBytes))
        return Result<std::vector<std::uint8_t>>::failure(
            Error(ErrorCode::CapacityExceeded, "tileset encoding exceeds the byte limit"));
    std::vector<std::uint8_t> output;
    output.reserve(encodedSize);
    output.insert(output.end(), {'F', 'G', 'L', 'X'});
    appendU16(output, 1U);
    appendU16(output, 0U);
    appendU32(output, 0U);
    appendGuid(output, tileset.guid);
    appendGuid(output, tileset.sourceImage);
    appendU16(output, tileset.tileWidth);
    appendU16(output, tileset.tileHeight);
    appendU16(output, tileset.margin);
    appendU16(output, tileset.spacing);
    appendU32(output, tileset.tileCount);
    appendU32(output, tileset.columns);
    appendU32(output, static_cast<std::uint32_t>(tileset.collisionTiles.size()));
    appendString(output, tileset.name);
    for (const auto tile : tileset.collisionTiles)
        appendU32(output, tile);
    if (output.size() != encodedSize || output.size() > std::numeric_limits<std::uint32_t>::max())
        return Result<std::vector<std::uint8_t>>::failure(
            Error(ErrorCode::InvalidState, "tileset encoder size calculation disagrees"));
    patchU32(output, 8U, static_cast<std::uint32_t>(output.size()));
    return Result<std::vector<std::uint8_t>>::success(std::move(output));
}

Result<Tileset> inspectTileset(const std::vector<std::uint8_t>& bytes,
                               const TilesetLimits& limits) {
    if (bytes.size() < TilesetHeaderSize || bytes.size() > limits.maximumEncodedBytes ||
        bytes[0] != 'F' || bytes[1] != 'G' || bytes[2] != 'L' || bytes[3] != 'X')
        return Result<Tileset>::failure(formatError("tileset magic or byte length is invalid"));
    const auto encodedVersion =
        static_cast<std::uint16_t>(bytes[4]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[5]) << 8U);
    if (encodedVersion != 1U)
        return Result<Tileset>::failure(
            Error(ErrorCode::UnsupportedVersion, "tileset version is unsupported"));
    Reader reader(bytes);
    if (!reader.skip(4U))
        return Result<Tileset>::failure(formatError("tileset header is truncated"));
    std::uint16_t version = 0U;
    std::uint16_t flags = 0U;
    std::uint32_t encodedSize = 0U;
    std::uint32_t collisionCount = 0U;
    Tileset result;
    if (!reader.readU16(version) || version != 1U || !reader.readU16(flags) || flags != 0U ||
        !reader.readU32(encodedSize) || encodedSize != bytes.size() ||
        !reader.readGuid(result.guid) || !reader.readGuid(result.sourceImage) ||
        !reader.readU16(result.tileWidth) || !reader.readU16(result.tileHeight) ||
        !reader.readU16(result.margin) || !reader.readU16(result.spacing) ||
        !reader.readU32(result.tileCount) || !reader.readU32(result.columns) ||
        !reader.readU32(collisionCount) || collisionCount > limits.maximumCollisionTiles ||
        !reader.readString(result.name, limits.maximumStringBytes))
        return Result<Tileset>::failure(formatError("tileset header is invalid"));
    result.collisionTiles.reserve(collisionCount);
    for (std::uint32_t index = 0U; index < collisionCount; ++index) {
        std::uint32_t tile = 0U;
        if (!reader.readU32(tile))
            return Result<Tileset>::failure(formatError("tileset collision table is truncated"));
        result.collisionTiles.push_back(tile);
    }
    if (reader.remaining() != 0U || !result.valid(limits))
        return Result<Tileset>::failure(formatError("tileset records or limits are invalid"));
    auto canonical = encodeTileset(result);
    if (!canonical || canonical.value() != bytes)
        return Result<Tileset>::failure(formatError("tileset encoding is not canonical"));
    return Result<Tileset>::success(std::move(result));
}

std::string_view CsvTilemapImporter::id() const noexcept {
    return "fabgl.tilemap.csv";
}
std::uint32_t CsvTilemapImporter::version() const noexcept {
    return 2U;
}
AssetKind CsvTilemapImporter::kind() const noexcept {
    return AssetKind::Tilemap;
}
std::vector<std::string> CsvTilemapImporter::extensions() const {
    return {"csv"};
}
Result<ImportedAsset> CsvTilemapImporter::import(const AssetImportRequest& request) const {
    return importTilemapRequest(request, false);
}

std::string_view JsonTilemapImporter::id() const noexcept {
    return "fabgl.tilemap.json";
}
std::uint32_t JsonTilemapImporter::version() const noexcept {
    return 2U;
}
AssetKind JsonTilemapImporter::kind() const noexcept {
    return AssetKind::Tilemap;
}
std::vector<std::string> JsonTilemapImporter::extensions() const {
    return {"json"};
}
Result<ImportedAsset> JsonTilemapImporter::import(const AssetImportRequest& request) const {
    return importTilemapRequest(request, true);
}

} // namespace fabgl::assets
