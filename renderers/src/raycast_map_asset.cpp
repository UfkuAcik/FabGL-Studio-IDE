#include <fabgl/rendering/raycast_map_asset.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

namespace fabgl::rendering {

namespace {

constexpr int RaycastMapFormatVersion = 1;

[[nodiscard]] Error formatError(std::string message, const std::size_t line = 0U) {
    Error error(ErrorCode::InvalidFormat, std::move(message));
    if (line != 0U) {
        error.addContext("line", std::to_string(line));
    }
    return error;
}

class LineReader final {
  public:
    explicit LineReader(const std::string_view source) : stream_(std::string(source)) {}

    [[nodiscard]] Result<std::string> next(const std::string_view expected) {
        std::string line;
        if (!std::getline(stream_, line)) {
            return Result<std::string>::failure(formatError(
                "raycast map is truncated; expected " + std::string(expected), line_ + 1U));
        }
        ++line_;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line.back() == ' ' || line.back() == '\t') {
            return Result<std::string>::failure(
                formatError("raycast map contains an empty or non-canonical line", line_));
        }
        return Result<std::string>::success(std::move(line));
    }

    [[nodiscard]] Result<void> requireEnd() {
        std::string trailing;
        if (std::getline(stream_, trailing)) {
            return Result<void>::failure(
                formatError("raycast map contains data after end", line_ + 1U));
        }
        return Result<void>::success();
    }

    [[nodiscard]] std::size_t line() const noexcept {
        return line_;
    }

  private:
    std::istringstream stream_;
    std::size_t line_ = 0U;
};

template <typename... Values>
[[nodiscard]] bool parseExact(const std::string& line, Values&... values) {
    std::istringstream stream(line);
    if (!((stream >> values) && ...)) {
        return false;
    }
    stream >> std::ws;
    return stream.eof();
}

[[nodiscard]] int hexNibble(const char value) noexcept {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

} // namespace

Result<void> validateRaycastMapAsset(const RaycastMapAsset& asset,
                                     const RaycastMapFormatLimits& limits) {
    if (asset.guid.isNil()) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "raycast map asset GUID is nil"));
    }
    if (limits.maximumWidth < 1 || limits.maximumHeight < 1 || limits.maximumCells < 1U ||
        limits.maximumPaletteColors < 2U || limits.maximumPaletteColors > 256U) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "raycast map format limits are invalid"));
    }
    const auto& map = asset.map;
    if (!map.valid() || map.width > limits.maximumWidth || map.height > limits.maximumHeight ||
        map.wallPalette.size() > limits.maximumPaletteColors ||
        map.cells.size() > limits.maximumCells) {
        return Result<void>::failure(
            Error(ErrorCode::CapacityExceeded, "raycast map exceeds format limits"));
    }
    if (map.wallPalette.size() < 2U) {
        return Result<void>::failure(Error(ErrorCode::InvalidArgument,
                                           "raycast map palette requires floor and wall colors"));
    }
    const auto paletteSize = map.wallPalette.size();
    if (std::any_of(map.cells.begin(), map.cells.end(), [paletteSize](const std::uint8_t cell) {
            return static_cast<std::size_t>(cell) >= paletteSize;
        })) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "raycast map cell exceeds palette"));
    }
    for (auto x = 0; x < map.width; ++x) {
        const auto top = map.cells[static_cast<std::size_t>(x)];
        const auto bottom = map.cells[static_cast<std::size_t>(map.height - 1) *
                                          static_cast<std::size_t>(map.width) +
                                      static_cast<std::size_t>(x)];
        if (top == 0U || bottom == 0U) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "raycast map outer boundary must be solid"));
        }
    }
    for (auto y = 0; y < map.height; ++y) {
        const auto row = static_cast<std::size_t>(y) * static_cast<std::size_t>(map.width);
        if (map.cells[row] == 0U ||
            map.cells[row + static_cast<std::size_t>(map.width - 1)] == 0U) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "raycast map outer boundary must be solid"));
        }
    }
    return Result<void>::success();
}

Result<std::string> serializeRaycastMapAsset(const RaycastMapAsset& asset,
                                             const RaycastMapFormatLimits& limits) {
    auto valid = validateRaycastMapAsset(asset, limits);
    if (!valid) {
        return Result<std::string>::failure(valid.error());
    }
    std::ostringstream stream;
    stream << "fglray " << RaycastMapFormatVersion << '\n';
    stream << "guid " << asset.guid.toString() << '\n';
    stream << "size " << asset.map.width << ' ' << asset.map.height << '\n';
    stream << "palette " << asset.map.wallPalette.size() << '\n';
    for (const auto color : asset.map.wallPalette) {
        stream << "color " << static_cast<unsigned>(color.r) << ' '
               << static_cast<unsigned>(color.g) << ' ' << static_cast<unsigned>(color.b) << ' '
               << static_cast<unsigned>(color.a) << '\n';
    }
    stream << "cells " << asset.map.cells.size() << '\n';
    stream << std::hex << std::nouppercase << std::setfill('0');
    for (auto y = 0; y < asset.map.height; ++y) {
        stream << "row ";
        const auto row = static_cast<std::size_t>(y) * static_cast<std::size_t>(asset.map.width);
        for (auto x = 0; x < asset.map.width; ++x) {
            stream << std::setw(2)
                   << static_cast<unsigned>(asset.map.cells[row + static_cast<std::size_t>(x)]);
        }
        stream << '\n';
    }
    stream << "end\n";
    auto result = stream.str();
    if (result.size() > limits.maximumSourceBytes) {
        return Result<std::string>::failure(
            Error(ErrorCode::CapacityExceeded, "serialized raycast map exceeds source limit"));
    }
    return Result<std::string>::success(std::move(result));
}

Result<RaycastMapAsset> deserializeRaycastMapAsset(const std::string_view text,
                                                   const RaycastMapFormatLimits& limits) {
    if (text.empty() || text.size() > limits.maximumSourceBytes) {
        return Result<RaycastMapAsset>::failure(
            Error(ErrorCode::CapacityExceeded, "raycast map source is empty or exceeds limit"));
    }
    LineReader reader(text);
    auto header = reader.next("fglray <version>");
    std::string magic;
    int version = 0;
    if (!header || !parseExact(header.value(), magic, version) || magic != "fglray") {
        return Result<RaycastMapAsset>::failure(formatError("invalid raycast map header", 1U));
    }
    if (version != RaycastMapFormatVersion) {
        return Result<RaycastMapAsset>::failure(
            Error(ErrorCode::UnsupportedVersion, "unsupported raycast map version")
                .addContext("version", std::to_string(version)));
    }

    RaycastMapAsset asset;
    auto guidLine = reader.next("guid <asset-guid>");
    std::string guidMarker;
    std::string guidToken;
    if (!guidLine || !parseExact(guidLine.value(), guidMarker, guidToken) || guidMarker != "guid") {
        return Result<RaycastMapAsset>::failure(
            formatError("invalid raycast map GUID record", reader.line()));
    }
    auto guid = AssetGuid::parse(guidToken);
    if (!guid || guid.value().isNil()) {
        return Result<RaycastMapAsset>::failure(
            formatError("invalid raycast map asset GUID", reader.line()));
    }
    asset.guid = guid.value();

    auto sizeLine = reader.next("size <width> <height>");
    std::string sizeMarker;
    if (!sizeLine || !parseExact(sizeLine.value(), sizeMarker, asset.map.width, asset.map.height) ||
        sizeMarker != "size" || asset.map.width < 1 || asset.map.height < 1 ||
        asset.map.width > limits.maximumWidth || asset.map.height > limits.maximumHeight ||
        static_cast<std::size_t>(asset.map.width) >
            limits.maximumCells / static_cast<std::size_t>(asset.map.height)) {
        return Result<RaycastMapAsset>::failure(
            formatError("invalid or excessive raycast map size", reader.line()));
    }

    auto paletteLine = reader.next("palette <count>");
    std::string paletteMarker;
    std::size_t paletteCount = 0U;
    if (!paletteLine || !parseExact(paletteLine.value(), paletteMarker, paletteCount) ||
        paletteMarker != "palette" || paletteCount < 2U ||
        paletteCount > limits.maximumPaletteColors) {
        return Result<RaycastMapAsset>::failure(
            formatError("invalid raycast map palette count", reader.line()));
    }
    asset.map.wallPalette.reserve(paletteCount);
    for (std::size_t index = 0U; index < paletteCount; ++index) {
        auto colorLine = reader.next("color <r> <g> <b> <a>");
        std::string colorMarker;
        unsigned red = 0U;
        unsigned green = 0U;
        unsigned blue = 0U;
        unsigned alpha = 0U;
        if (!colorLine || !parseExact(colorLine.value(), colorMarker, red, green, blue, alpha) ||
            colorMarker != "color" || red > 255U || green > 255U || blue > 255U || alpha > 255U) {
            return Result<RaycastMapAsset>::failure(
                formatError("invalid raycast map palette color", reader.line()));
        }
        asset.map.wallPalette.push_back(
            {static_cast<std::uint8_t>(red), static_cast<std::uint8_t>(green),
             static_cast<std::uint8_t>(blue), static_cast<std::uint8_t>(alpha)});
    }

    const auto expectedCells =
        static_cast<std::size_t>(asset.map.width) * static_cast<std::size_t>(asset.map.height);
    auto cellsLine = reader.next("cells <count>");
    std::string cellsMarker;
    std::size_t declaredCells = 0U;
    if (!cellsLine || !parseExact(cellsLine.value(), cellsMarker, declaredCells) ||
        cellsMarker != "cells" || declaredCells != expectedCells) {
        return Result<RaycastMapAsset>::failure(
            formatError("invalid raycast map cell count", reader.line()));
    }
    asset.map.cells.reserve(expectedCells);
    for (auto y = 0; y < asset.map.height; ++y) {
        auto rowLine = reader.next("row <hex-cells>");
        std::string rowMarker;
        std::string rowToken;
        if (!rowLine || !parseExact(rowLine.value(), rowMarker, rowToken) || rowMarker != "row" ||
            rowToken.size() != static_cast<std::size_t>(asset.map.width) * 2U) {
            return Result<RaycastMapAsset>::failure(
                formatError("invalid raycast map row", reader.line()));
        }
        for (auto x = 0; x < asset.map.width; ++x) {
            const auto offset = static_cast<std::size_t>(x) * 2U;
            const auto high = hexNibble(rowToken[offset]);
            const auto low = hexNibble(rowToken[offset + 1U]);
            if (high < 0 || low < 0) {
                return Result<RaycastMapAsset>::failure(
                    formatError("raycast map row is not hexadecimal", reader.line()));
            }
            asset.map.cells.push_back(static_cast<std::uint8_t>((high << 4) | low));
        }
    }
    auto endLine = reader.next("end");
    if (!endLine || endLine.value() != "end") {
        return Result<RaycastMapAsset>::failure(
            formatError("raycast map is missing end marker", reader.line()));
    }
    auto ended = reader.requireEnd();
    if (!ended) {
        return Result<RaycastMapAsset>::failure(ended.error());
    }
    auto valid = validateRaycastMapAsset(asset, limits);
    if (!valid) {
        return Result<RaycastMapAsset>::failure(valid.error());
    }
    return Result<RaycastMapAsset>::success(std::move(asset));
}

} // namespace fabgl::rendering
