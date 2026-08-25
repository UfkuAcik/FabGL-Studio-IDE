#include <fabgl/assets/font_importer.h>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>

namespace fabgl::assets {
namespace {

constexpr std::size_t FontHeaderSize = 24U;
constexpr std::size_t GlyphRecordSize = 20U;

struct SourceGlyph final {
    std::uint32_t codepoint = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::int16_t xOffset = 0;
    std::int16_t yOffset = 0;
    std::int16_t advance = 0;
    std::vector<std::uint8_t> bitmap;
};

[[nodiscard]] Error formatError(std::string message) {
    return Error(ErrorCode::InvalidFormat, std::move(message));
}

[[nodiscard]] std::string_view trim(std::string_view value) noexcept {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' ||
                              value.front() == '\r')) {
        value.remove_prefix(1U);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' ||
                              value.back() == '\r')) {
        value.remove_suffix(1U);
    }
    return value;
}

[[nodiscard]] std::vector<std::string_view> splitLines(const std::string_view source) {
    std::vector<std::string_view> lines;
    auto position = std::size_t{0};
    while (position <= source.size()) {
        const auto end = source.find('\n', position);
        lines.push_back(trim(source.substr(
            position, end == std::string_view::npos ? source.size() - position : end - position)));
        if (end == std::string_view::npos) {
            break;
        }
        position = end + 1U;
    }
    return lines;
}

[[nodiscard]] std::string_view valueAfter(const std::string_view line,
                                          const std::string_view keyword) noexcept {
    if (line.size() <= keyword.size() || line.substr(0U, keyword.size()) != keyword ||
        (line[keyword.size()] != ' ' && line[keyword.size()] != '\t')) {
        return {};
    }
    return trim(line.substr(keyword.size() + 1U));
}

[[nodiscard]] std::vector<std::string_view> words(std::string_view value) {
    std::vector<std::string_view> result;
    while (true) {
        value = trim(value);
        if (value.empty()) {
            break;
        }
        const auto end = value.find_first_of(" \t\r");
        result.push_back(value.substr(0U, end));
        if (end == std::string_view::npos) {
            break;
        }
        value.remove_prefix(end);
    }
    return result;
}

[[nodiscard]] bool parseInteger(const std::string_view token, std::int32_t& value) noexcept {
    if (token.empty() || token.front() == '+') {
        return false;
    }
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value, 10);
    return parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size();
}

[[nodiscard]] bool parseFields(const std::string_view value, const std::size_t count,
                               std::int32_t* output) noexcept {
    const auto fields = words(value);
    if (fields.size() != count) {
        return false;
    }
    for (auto index = std::size_t{0}; index < count; ++index) {
        if (!parseInteger(fields[index], output[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] int hexNibble(const char character) noexcept {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    return -1;
}

[[nodiscard]] bool parseBitmapRow(const std::string_view line, const std::uint16_t width,
                                  std::vector<std::uint8_t>& destination) {
    const auto rowBytes = (static_cast<std::size_t>(width) + 7U) / 8U;
    if (line.size() != rowBytes * 2U) {
        return false;
    }
    for (auto index = std::size_t{0}; index < rowBytes; ++index) {
        const auto high = hexNibble(line[index * 2U]);
        const auto low = hexNibble(line[index * 2U + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        destination.push_back(static_cast<std::uint8_t>((high << 4) | low));
    }
    if ((width % 8U) != 0U) {
        const auto unusedMask = static_cast<std::uint8_t>((1U << (8U - width % 8U)) - 1U);
        if ((destination.back() & unusedMask) != 0U) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] Result<SourceGlyph> parseGlyph(const std::vector<std::string_view>& lines,
                                             std::size_t& index,
                                             const BitmapFontLimits& limits) {
    SourceGlyph glyph;
    auto haveEncoding = false;
    auto haveAdvance = false;
    auto haveBox = false;
    auto haveBitmap = false;
    for (++index; index < lines.size(); ++index) {
        const auto line = lines[index];
        if (line.empty()) {
            continue;
        }
        if (line == "ENDCHAR") {
            if (!haveEncoding || !haveAdvance || !haveBox || !haveBitmap) {
                return Result<SourceGlyph>::failure(formatError("BDF glyph is missing required fields"));
            }
            return Result<SourceGlyph>::success(std::move(glyph));
        }
        if (haveBitmap) {
            return Result<SourceGlyph>::failure(formatError("BDF bitmap must be followed by ENDCHAR"));
        }
        if (const auto encodingValue = valueAfter(line, "ENCODING"); !encodingValue.empty()) {
            std::int32_t encoding = 0;
            if (haveEncoding || !parseInteger(encodingValue, encoding) || encoding < 0 ||
                encoding > 0x10FFFF || (encoding >= 0xD800 && encoding <= 0xDFFF)) {
                return Result<SourceGlyph>::failure(formatError("BDF glyph encoding is invalid"));
            }
            glyph.codepoint = static_cast<std::uint32_t>(encoding);
            haveEncoding = true;
        } else if (const auto advanceValue = valueAfter(line, "DWIDTH"); !advanceValue.empty()) {
            std::int32_t fields[2]{};
            if (haveAdvance || !parseFields(advanceValue, 2U, fields) || fields[1] != 0 ||
                fields[0] < std::numeric_limits<std::int16_t>::min() ||
                fields[0] > std::numeric_limits<std::int16_t>::max()) {
                return Result<SourceGlyph>::failure(formatError("BDF glyph DWIDTH is invalid"));
            }
            glyph.advance = static_cast<std::int16_t>(fields[0]);
            haveAdvance = true;
        } else if (const auto boxValue = valueAfter(line, "BBX"); !boxValue.empty()) {
            std::int32_t fields[4]{};
            if (haveBox || !parseFields(boxValue, 4U, fields) || fields[0] <= 0 || fields[1] <= 0 ||
                fields[0] > limits.maximumGlyphDimension ||
                fields[1] > limits.maximumGlyphDimension ||
                fields[2] < std::numeric_limits<std::int16_t>::min() ||
                fields[2] > std::numeric_limits<std::int16_t>::max() ||
                fields[3] < std::numeric_limits<std::int16_t>::min() ||
                fields[3] > std::numeric_limits<std::int16_t>::max()) {
                return Result<SourceGlyph>::failure(formatError("BDF glyph BBX is invalid"));
            }
            glyph.width = static_cast<std::uint16_t>(fields[0]);
            glyph.height = static_cast<std::uint16_t>(fields[1]);
            glyph.xOffset = static_cast<std::int16_t>(fields[2]);
            glyph.yOffset = static_cast<std::int16_t>(fields[3]);
            haveBox = true;
        } else if (line == "BITMAP") {
            if (!haveBox) {
                return Result<SourceGlyph>::failure(formatError("BDF BITMAP appears before BBX"));
            }
            const auto rowBytes = (static_cast<std::size_t>(glyph.width) + 7U) / 8U;
            glyph.bitmap.reserve(rowBytes * glyph.height);
            for (std::uint16_t row = 0; row < glyph.height; ++row) {
                ++index;
                if (index >= lines.size() ||
                    !parseBitmapRow(lines[index], glyph.width, glyph.bitmap)) {
                    return Result<SourceGlyph>::failure(formatError("BDF bitmap row is invalid"));
                }
            }
            haveBitmap = true;
        } else if (valueAfter(line, "SWIDTH").empty() &&
                   valueAfter(line, "ATTRIBUTES").empty()) {
            return Result<SourceGlyph>::failure(formatError("BDF glyph field is unsupported"));
        }
    }
    return Result<SourceGlyph>::failure(formatError("BDF glyph is missing ENDCHAR"));
}

void setAtlasPixel(BitmapFont& font, const std::uint16_t x, const std::uint16_t y) noexcept {
    const auto stride = (static_cast<std::size_t>(font.atlasWidth) + 7U) / 8U;
    const auto offset = static_cast<std::size_t>(y) * stride + x / 8U;
    font.atlasBits[offset] |= static_cast<std::uint8_t>(0x80U >> (x % 8U));
}

void appendU16(std::vector<std::uint8_t>& output, const std::uint16_t value) {
    const auto wide = static_cast<std::uint32_t>(value);
    output.push_back(static_cast<std::uint8_t>(wide & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((wide >> 8U) & 0xFFU));
}

void appendU32(std::vector<std::uint8_t>& output, const std::uint32_t value) {
    for (unsigned int shift = 0; shift < 32U; shift += 8U) {
        output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

[[nodiscard]] std::uint16_t readU16(const std::vector<std::uint8_t>& bytes,
                                    const std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset]) |
                                      (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U));
}

[[nodiscard]] std::uint32_t readU32(const std::vector<std::uint8_t>& bytes,
                                    const std::size_t offset) noexcept {
    auto value = std::uint32_t{0};
    for (unsigned int shift = 0; shift < 32U; shift += 8U) {
        value |= static_cast<std::uint32_t>(bytes[offset + shift / 8U]) << shift;
    }
    return value;
}

[[nodiscard]] bool rectanglesOverlap(const BitmapGlyph& left,
                                     const BitmapGlyph& right) noexcept {
    return left.x < static_cast<std::uint32_t>(right.x) + right.width &&
           right.x < static_cast<std::uint32_t>(left.x) + left.width &&
           left.y < static_cast<std::uint32_t>(right.y) + right.height &&
           right.y < static_cast<std::uint32_t>(left.y) + left.height;
}

} // namespace

bool BitmapFont::valid(const BitmapFontLimits& limits) const noexcept {
    if (atlasWidth == 0U || atlasHeight == 0U || atlasWidth > limits.maximumAtlasWidth ||
        atlasHeight > limits.maximumAtlasHeight || glyphs.empty() ||
        glyphs.size() > limits.maximumGlyphs || ascent < 0 || descent < 0) {
        return false;
    }
    const auto stride = (static_cast<std::size_t>(atlasWidth) + 7U) / 8U;
    if (atlasBits.size() != stride * atlasHeight) {
        return false;
    }
    for (auto index = std::size_t{0}; index < glyphs.size(); ++index) {
        const auto& glyph = glyphs[index];
        if (glyph.codepoint > 0x10FFFFU ||
            (glyph.codepoint >= 0xD800U && glyph.codepoint <= 0xDFFFU) || glyph.width == 0U ||
            glyph.height == 0U ||
            glyph.width > limits.maximumGlyphDimension ||
            glyph.height > limits.maximumGlyphDimension ||
            static_cast<std::uint32_t>(glyph.x) + glyph.width > atlasWidth ||
            static_cast<std::uint32_t>(glyph.y) + glyph.height > atlasHeight ||
            (index > 0U && glyphs[index - 1U].codepoint >= glyph.codepoint)) {
            return false;
        }
        for (auto previous = std::size_t{0}; previous < index; ++previous) {
            if (rectanglesOverlap(glyphs[previous], glyph)) {
                return false;
            }
        }
    }
    if ((atlasWidth % 8U) != 0U) {
        const auto unusedMask = static_cast<std::uint8_t>((1U << (8U - atlasWidth % 8U)) - 1U);
        for (std::uint16_t row = 0; row < atlasHeight; ++row) {
            if ((atlasBits[static_cast<std::size_t>(row) * stride + stride - 1U] & unusedMask) != 0U) {
                return false;
            }
        }
    }
    return true;
}

Result<BitmapFont> importBdfFont(const std::string_view source,
                                 const BdfImportSettings& settings) {
    if (source.empty() || source.size() > 16U * 1024U * 1024U || settings.padding > 16U ||
        settings.maximumAtlasWidth == 0U ||
        settings.maximumAtlasWidth > settings.limits.maximumAtlasWidth ||
        settings.limits.maximumGlyphs == 0U ||
        settings.limits.maximumGlyphs > std::numeric_limits<std::uint16_t>::max() ||
        settings.limits.maximumGlyphDimension == 0U || settings.limits.maximumAtlasWidth == 0U ||
        settings.limits.maximumAtlasHeight == 0U) {
        return Result<BitmapFont>::failure(
            Error(ErrorCode::InvalidArgument, "BDF source or import settings are invalid"));
    }
    const auto lines = splitLines(source);
    auto first = std::size_t{0};
    while (first < lines.size() && lines[first].empty()) {
        ++first;
    }
    if (first == lines.size() || valueAfter(lines[first], "STARTFONT").empty()) {
        return Result<BitmapFont>::failure(formatError("BDF STARTFONT header is missing"));
    }
    auto haveAscent = false;
    auto haveDescent = false;
    auto haveCharacterCount = false;
    auto haveEndFont = false;
    std::int32_t ascent = 0;
    std::int32_t descent = 0;
    std::int32_t declaredCharacters = 0;
    std::vector<SourceGlyph> sourceGlyphs;
    for (auto index = first + 1U; index < lines.size(); ++index) {
        const auto line = lines[index];
        if (line.empty() || !valueAfter(line, "COMMENT").empty()) {
            continue;
        }
        if (const auto ascentValue = valueAfter(line, "FONT_ASCENT"); !ascentValue.empty()) {
            if (haveAscent || !parseInteger(ascentValue, ascent) || ascent < 0 ||
                ascent > std::numeric_limits<std::int16_t>::max()) {
                return Result<BitmapFont>::failure(formatError("BDF FONT_ASCENT is invalid"));
            }
            haveAscent = true;
        } else if (const auto descentValue = valueAfter(line, "FONT_DESCENT"); !descentValue.empty()) {
            if (haveDescent || !parseInteger(descentValue, descent) || descent < 0 ||
                descent > std::numeric_limits<std::int16_t>::max()) {
                return Result<BitmapFont>::failure(formatError("BDF FONT_DESCENT is invalid"));
            }
            haveDescent = true;
        } else if (const auto countValue = valueAfter(line, "CHARS"); !countValue.empty()) {
            if (haveCharacterCount || !parseInteger(countValue, declaredCharacters) ||
                declaredCharacters <= 0 ||
                declaredCharacters > static_cast<std::int32_t>(settings.limits.maximumGlyphs)) {
                return Result<BitmapFont>::failure(formatError("BDF CHARS count is invalid"));
            }
            haveCharacterCount = true;
        } else if (!valueAfter(line, "STARTCHAR").empty()) {
            if (!haveCharacterCount || sourceGlyphs.size() >= settings.limits.maximumGlyphs) {
                return Result<BitmapFont>::failure(formatError("BDF glyph count exceeds CHARS or limits"));
            }
            auto glyph = parseGlyph(lines, index, settings.limits);
            if (!glyph) {
                return Result<BitmapFont>::failure(glyph.error());
            }
            sourceGlyphs.push_back(std::move(glyph.value()));
        } else if (line == "ENDFONT") {
            haveEndFont = true;
            for (auto trailing = index + 1U; trailing < lines.size(); ++trailing) {
                if (!lines[trailing].empty()) {
                    return Result<BitmapFont>::failure(formatError("BDF has data after ENDFONT"));
                }
            }
            break;
        } else if (line == "STARTFONT" || !valueAfter(line, "STARTFONT").empty()) {
            return Result<BitmapFont>::failure(formatError("BDF has a duplicate STARTFONT"));
        }
    }
    const auto declaredSize = static_cast<std::size_t>(static_cast<std::uint32_t>(declaredCharacters));
    if (!haveAscent || !haveDescent || !haveCharacterCount || !haveEndFont ||
        sourceGlyphs.size() != declaredSize) {
        return Result<BitmapFont>::failure(formatError("BDF required metadata or glyph count is invalid"));
    }
    std::sort(sourceGlyphs.begin(), sourceGlyphs.end(),
              [](const SourceGlyph& left, const SourceGlyph& right) {
                  return left.codepoint < right.codepoint;
              });
    if (std::adjacent_find(sourceGlyphs.begin(), sourceGlyphs.end(),
                           [](const SourceGlyph& left, const SourceGlyph& right) {
                               return left.codepoint == right.codepoint;
                           }) != sourceGlyphs.end()) {
        return Result<BitmapFont>::failure(formatError("BDF contains duplicate encodings"));
    }

    auto desiredWidth = static_cast<std::uint32_t>(settings.padding);
    for (const auto& glyph : sourceGlyphs) {
        desiredWidth += static_cast<std::uint32_t>(glyph.width) +
                        static_cast<std::uint32_t>(settings.padding);
    }
    const auto widestGlyph = std::max_element(
        sourceGlyphs.begin(), sourceGlyphs.end(), [](const SourceGlyph& left, const SourceGlyph& right) {
            return left.width < right.width;
        });
    const auto minimumWidth = static_cast<std::uint32_t>(widestGlyph->width) +
                              static_cast<std::uint32_t>(settings.padding) * 2U;
    if (minimumWidth > settings.maximumAtlasWidth) {
        return Result<BitmapFont>::failure(
            Error(ErrorCode::CapacityExceeded, "BDF glyph does not fit the atlas width"));
    }

    BitmapFont result;
    result.atlasWidth = static_cast<std::uint16_t>(
        std::max(minimumWidth, std::min(desiredWidth, static_cast<std::uint32_t>(settings.maximumAtlasWidth))));
    result.ascent = static_cast<std::int16_t>(ascent);
    result.descent = static_cast<std::int16_t>(descent);
    auto x = static_cast<std::uint32_t>(settings.padding);
    auto y = static_cast<std::uint32_t>(settings.padding);
    auto rowHeight = std::uint32_t{0};
    for (const auto& sourceGlyph : sourceGlyphs) {
        if (x + static_cast<std::uint32_t>(sourceGlyph.width) +
                static_cast<std::uint32_t>(settings.padding) >
            static_cast<std::uint32_t>(result.atlasWidth)) {
            x = static_cast<std::uint32_t>(settings.padding);
            y += rowHeight + static_cast<std::uint32_t>(settings.padding);
            rowHeight = 0U;
        }
        if (y + static_cast<std::uint32_t>(sourceGlyph.height) +
                static_cast<std::uint32_t>(settings.padding) >
            static_cast<std::uint32_t>(settings.limits.maximumAtlasHeight)) {
            return Result<BitmapFont>::failure(
                Error(ErrorCode::CapacityExceeded, "BDF glyphs exceed the atlas height"));
        }
        result.glyphs.push_back({sourceGlyph.codepoint, static_cast<std::uint16_t>(x),
                                 static_cast<std::uint16_t>(y), sourceGlyph.width,
                                 sourceGlyph.height, sourceGlyph.xOffset, sourceGlyph.yOffset,
                                 sourceGlyph.advance});
        x += static_cast<std::uint32_t>(sourceGlyph.width) +
             static_cast<std::uint32_t>(settings.padding);
        rowHeight = std::max(rowHeight, static_cast<std::uint32_t>(sourceGlyph.height));
    }
    result.atlasHeight = static_cast<std::uint16_t>(
        y + rowHeight + static_cast<std::uint32_t>(settings.padding));
    const auto stride = (static_cast<std::size_t>(result.atlasWidth) + 7U) / 8U;
    result.atlasBits.assign(stride * result.atlasHeight, 0U);
    for (auto glyphIndex = std::size_t{0}; glyphIndex < sourceGlyphs.size(); ++glyphIndex) {
        const auto& sourceGlyph = sourceGlyphs[glyphIndex];
        const auto& targetGlyph = result.glyphs[glyphIndex];
        const auto sourceStride = (static_cast<std::size_t>(sourceGlyph.width) + 7U) / 8U;
        for (std::uint16_t row = 0; row < sourceGlyph.height; ++row) {
            for (std::uint16_t column = 0; column < sourceGlyph.width; ++column) {
                const auto sourceByte = sourceGlyph.bitmap[static_cast<std::size_t>(row) * sourceStride +
                                                           column / 8U];
                if ((sourceByte & static_cast<std::uint8_t>(0x80U >> (column % 8U))) != 0U) {
                    setAtlasPixel(result, static_cast<std::uint16_t>(targetGlyph.x + column),
                                  static_cast<std::uint16_t>(targetGlyph.y + row));
                }
            }
        }
    }
    if (!result.valid(settings.limits)) {
        return Result<BitmapFont>::failure(formatError("BDF atlas failed validation"));
    }
    return Result<BitmapFont>::success(std::move(result));
}

Result<std::vector<std::uint8_t>> encodeBitmapFont(const BitmapFont& font) {
    if (!font.valid() || font.glyphs.size() > std::numeric_limits<std::uint16_t>::max() ||
        font.atlasBits.size() > std::numeric_limits<std::uint32_t>::max()) {
        return Result<std::vector<std::uint8_t>>::failure(
            Error(ErrorCode::InvalidArgument, "bitmap font cannot be encoded"));
    }
    std::vector<std::uint8_t> output;
    output.reserve(FontHeaderSize + font.glyphs.size() * GlyphRecordSize + font.atlasBits.size());
    output.insert(output.end(), {'F', 'G', 'L', 'F'});
    appendU16(output, 1U);
    appendU16(output, 1U);
    appendU16(output, font.atlasWidth);
    appendU16(output, font.atlasHeight);
    appendU16(output, static_cast<std::uint16_t>(font.glyphs.size()));
    appendU16(output, static_cast<std::uint16_t>(font.ascent));
    appendU16(output, static_cast<std::uint16_t>(font.descent));
    appendU16(output, 0U);
    appendU32(output, static_cast<std::uint32_t>(font.atlasBits.size()));
    for (const auto& glyph : font.glyphs) {
        appendU32(output, glyph.codepoint);
        appendU16(output, glyph.x);
        appendU16(output, glyph.y);
        appendU16(output, glyph.width);
        appendU16(output, glyph.height);
        appendU16(output, static_cast<std::uint16_t>(glyph.xOffset));
        appendU16(output, static_cast<std::uint16_t>(glyph.yOffset));
        appendU16(output, static_cast<std::uint16_t>(glyph.advance));
        appendU16(output, 0U);
    }
    output.insert(output.end(), font.atlasBits.begin(), font.atlasBits.end());
    return Result<std::vector<std::uint8_t>>::success(std::move(output));
}

Result<BitmapFont> inspectBitmapFont(const std::vector<std::uint8_t>& bytes,
                                     const BitmapFontLimits& limits) {
    if (bytes.size() < FontHeaderSize || bytes[0] != 'F' || bytes[1] != 'G' ||
        bytes[2] != 'L' || bytes[3] != 'F') {
        return Result<BitmapFont>::failure(formatError("font magic is invalid"));
    }
    if (readU16(bytes, 4U) != 1U) {
        return Result<BitmapFont>::failure(
            Error(ErrorCode::UnsupportedVersion, "font version is unsupported"));
    }
    const auto glyphCount = readU16(bytes, 12U);
    const auto bitmapBytes = readU32(bytes, 20U);
    const auto expectedSize = static_cast<std::uint64_t>(FontHeaderSize) +
                              static_cast<std::uint64_t>(glyphCount) * GlyphRecordSize + bitmapBytes;
    if (readU16(bytes, 6U) != 1U || readU16(bytes, 18U) != 0U ||
        expectedSize != bytes.size()) {
        return Result<BitmapFont>::failure(formatError("font header or payload length is invalid"));
    }
    BitmapFont result;
    result.atlasWidth = readU16(bytes, 8U);
    result.atlasHeight = readU16(bytes, 10U);
    result.ascent = static_cast<std::int16_t>(readU16(bytes, 14U));
    result.descent = static_cast<std::int16_t>(readU16(bytes, 16U));
    result.glyphs.reserve(glyphCount);
    auto offset = FontHeaderSize;
    for (std::uint16_t index = 0; index < glyphCount; ++index) {
        BitmapGlyph glyph;
        glyph.codepoint = readU32(bytes, offset);
        glyph.x = readU16(bytes, offset + 4U);
        glyph.y = readU16(bytes, offset + 6U);
        glyph.width = readU16(bytes, offset + 8U);
        glyph.height = readU16(bytes, offset + 10U);
        glyph.xOffset = static_cast<std::int16_t>(readU16(bytes, offset + 12U));
        glyph.yOffset = static_cast<std::int16_t>(readU16(bytes, offset + 14U));
        glyph.advance = static_cast<std::int16_t>(readU16(bytes, offset + 16U));
        if (readU16(bytes, offset + 18U) != 0U) {
            return Result<BitmapFont>::failure(formatError("font glyph reserved field is non-zero"));
        }
        result.glyphs.push_back(glyph);
        offset += GlyphRecordSize;
    }
    result.atlasBits.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end());
    if (result.atlasBits.size() != bitmapBytes || !result.valid(limits)) {
        return Result<BitmapFont>::failure(formatError("font payload failed validation"));
    }
    return Result<BitmapFont>::success(std::move(result));
}

std::string_view BdfFontImporter::id() const noexcept { return "fabgl.font.bdf"; }
std::uint32_t BdfFontImporter::version() const noexcept { return 1U; }
AssetKind BdfFontImporter::kind() const noexcept { return AssetKind::Font; }
std::vector<std::string> BdfFontImporter::extensions() const { return {"bdf"}; }
Result<ImportedAsset> BdfFontImporter::import(const AssetImportRequest& request) const {
    const auto source = std::string_view(
        reinterpret_cast<const char*>(request.sourceBytes.data()), request.sourceBytes.size());
    auto font = importBdfFont(source);
    if (!font) {
        return Result<ImportedAsset>::failure(font.error());
    }
    auto encoded = encodeBitmapFont(font.value());
    if (!encoded) {
        return Result<ImportedAsset>::failure(encoded.error());
    }
    ImportedAsset output;
    output.payload = std::move(encoded.value());
    output.flashBytes = output.payload.size();
    output.internalRamBytes = font.value().atlasBits.size() +
                              font.value().glyphs.size() * sizeof(BitmapGlyph);
    output.estimatedDecodeMicros = static_cast<std::uint32_t>(font.value().glyphs.size());
    return Result<ImportedAsset>::success(std::move(output));
}

} // namespace fabgl::assets
