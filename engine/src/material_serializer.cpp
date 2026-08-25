#include "fabgl/serialization/material_serializer.h"

#include <charconv>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace fabgl {

namespace {

constexpr std::size_t MaximumInputBytes = 1024U * 1024U;
constexpr std::size_t MaximumPaletteColors = 256U;

class Reader final {
  public:
    explicit Reader(std::string_view text) : input_(std::string(text)) {
        input_.imbue(std::locale::classic());
    }

    [[nodiscard]] bool next(std::string& line) {
        while (std::getline(input_, line)) {
            ++line_;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            const auto first = line.find_first_not_of(" \t");
            if (first == std::string::npos || line[first] == '#')
                continue;
            return true;
        }
        return false;
    }

    [[nodiscard]] std::size_t line() const noexcept {
        return line_;
    }

  private:
    std::istringstream input_;
    std::size_t line_ = 0U;
};

Error invalid(const Reader& reader, std::string message) {
    return Error(ErrorCode::InvalidFormat, std::move(message))
        .addContext("line", std::to_string(reader.line()));
}

bool finished(std::istringstream& stream) {
    stream >> std::ws;
    return stream.eof();
}

template <typename Integer> std::optional<Integer> integer(std::string_view token) {
    Integer value{};
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value, 10);
    if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size())
        return std::nullopt;
    return value;
}

Result<std::string> field(Reader& reader, std::string_view expected) {
    std::string line;
    if (!reader.next(line))
        return Result<std::string>::failure(invalid(reader, "unexpected end of material"));
    std::istringstream stream(line);
    stream.imbue(std::locale::classic());
    std::string key;
    std::string value;
    if (!(stream >> key >> value) || key != expected || !finished(stream)) {
        return Result<std::string>::failure(
            invalid(reader, "invalid material field").addContext("field", std::string(expected)));
    }
    return Result<std::string>::success(std::move(value));
}

Result<std::string> quotedField(Reader& reader, std::string_view expected) {
    std::string line;
    if (!reader.next(line))
        return Result<std::string>::failure(invalid(reader, "unexpected end of material"));
    std::istringstream stream(line);
    stream.imbue(std::locale::classic());
    std::string key;
    std::string value;
    if (!(stream >> key >> std::quoted(value)) || key != expected || !finished(stream)) {
        return Result<std::string>::failure(invalid(reader, "invalid quoted material field")
                                                .addContext("field", std::string(expected)));
    }
    return Result<std::string>::success(std::move(value));
}

Result<void> marker(Reader& reader, std::string_view expected) {
    std::string line;
    if (!reader.next(line) || line != expected) {
        return Result<void>::failure(
            invalid(reader, "invalid material marker").addContext("marker", std::string(expected)));
    }
    return Result<void>::success();
}

template <typename Guid>
Result<std::optional<Guid>> optionalGuid(std::string_view token, const Reader& reader) {
    if (token == "nil")
        return Result<std::optional<Guid>>::success(std::nullopt);
    auto parsed = Guid::parse(token);
    if (!parsed || parsed.value().isNil())
        return Result<std::optional<Guid>>::failure(invalid(reader, "invalid material GUID"));
    return Result<std::optional<Guid>>::success(parsed.value());
}

Result<Color> colorField(Reader& reader, std::string_view expected) {
    std::string line;
    if (!reader.next(line))
        return Result<Color>::failure(invalid(reader, "unexpected end of material"));
    std::istringstream stream(line);
    stream.imbue(std::locale::classic());
    std::string key;
    std::string red;
    std::string green;
    std::string blue;
    std::string alpha;
    if (!(stream >> key >> red >> green >> blue >> alpha) || key != expected || !finished(stream)) {
        return Result<Color>::failure(invalid(reader, "invalid material color"));
    }
    const auto r = integer<unsigned int>(red);
    const auto g = integer<unsigned int>(green);
    const auto b = integer<unsigned int>(blue);
    const auto a = integer<unsigned int>(alpha);
    if (!r || !g || !b || !a || *r > 255U || *g > 255U || *b > 255U || *a > 255U)
        return Result<Color>::failure(invalid(reader, "material color channel is out of range"));
    return Result<Color>::success(
        Color{static_cast<std::uint8_t>(*r), static_cast<std::uint8_t>(*g),
              static_cast<std::uint8_t>(*b), static_cast<std::uint8_t>(*a)});
}

Result<bool> booleanField(Reader& reader, std::string_view expected) {
    auto value = field(reader, expected);
    if (!value)
        return Result<bool>::failure(value.error());
    if (value.value() != "0" && value.value() != "1")
        return Result<bool>::failure(invalid(reader, "material boolean must be 0 or 1"));
    return Result<bool>::success(value.value() == "1");
}

template <typename Enum>
Result<Enum> enumField(Reader& reader, std::string_view expected,
                       std::initializer_list<std::pair<std::string_view, Enum>> values) {
    auto token = field(reader, expected);
    if (!token)
        return Result<Enum>::failure(token.error());
    for (const auto& [name, value] : values) {
        if (token.value() == name)
            return Result<Enum>::success(value);
    }
    return Result<Enum>::failure(invalid(reader, "unknown material enum value")
                                     .addContext("field", std::string(expected))
                                     .addContext("value", token.value()));
}

void writeColor(std::ostringstream& output, std::string_view key, const Color color) {
    output << key << ' ' << static_cast<unsigned int>(color.r) << ' '
           << static_cast<unsigned int>(color.g) << ' ' << static_cast<unsigned int>(color.b) << ' '
           << static_cast<unsigned int>(color.a) << '\n';
}

const char* colorModeName(const MaterialColorMode value) {
    switch (value) {
    case MaterialColorMode::Texture:
        return "texture";
    case MaterialColorMode::Flat:
        return "flat";
    case MaterialColorMode::Vertex:
        return "vertex";
    }
    return "invalid";
}

const char* ditherName(const MaterialDitherMode value) {
    switch (value) {
    case MaterialDitherMode::None:
        return "none";
    case MaterialDitherMode::Ordered2x2:
        return "ordered2x2";
    case MaterialDitherMode::Ordered4x4:
        return "ordered4x4";
    }
    return "invalid";
}

const char* samplingName(const MaterialSamplingMode value) {
    switch (value) {
    case MaterialSamplingMode::Nearest:
        return "nearest";
    case MaterialSamplingMode::Bilinear:
        return "bilinear";
    }
    return "invalid";
}

const char* lightingName(const MaterialLightingMode value) {
    switch (value) {
    case MaterialLightingMode::Unlit:
        return "unlit";
    case MaterialLightingMode::Flat:
        return "flat";
    case MaterialLightingMode::Vertex:
        return "vertex";
    }
    return "invalid";
}

const char* blendName(const MaterialBlendMode value) {
    switch (value) {
    case MaterialBlendMode::Opaque:
        return "opaque";
    case MaterialBlendMode::Alpha:
        return "alpha";
    case MaterialBlendMode::Additive:
        return "additive";
    case MaterialBlendMode::Multiply:
        return "multiply";
    }
    return "invalid";
}

} // namespace

Result<std::string> MaterialSerializer::serialize(const MaterialAsset& asset) {
    const bool invalidName = asset.name.empty() || asset.name.size() > 4096U ||
                             asset.name.find_first_of("\r\n\0", 0U, 3U) != std::string::npos;
    if (asset.id.isNil() || invalidName || asset.material.palette.size() > MaximumPaletteColors ||
        (asset.material.baseTexture && asset.material.baseTexture->isNil()) ||
        (asset.material.paletteAsset && asset.material.paletteAsset->isNil()) ||
        std::string_view(colorModeName(asset.material.colorMode)) == "invalid" ||
        std::string_view(ditherName(asset.material.dither)) == "invalid" ||
        std::string_view(samplingName(asset.material.sampling)) == "invalid" ||
        std::string_view(lightingName(asset.material.lighting)) == "invalid" ||
        std::string_view(blendName(asset.material.blend)) == "invalid") {
        return Result<std::string>::failure(
            Error(ErrorCode::InvalidArgument, "material asset is invalid"));
    }
    const auto compatibility = static_cast<std::uint32_t>(asset.material.compatibleRenderers);
    const auto allCompatibility = static_cast<std::uint32_t>(RendererCompatibility::All);
    if (compatibility == 0U || (compatibility & ~allCompatibility) != 0U) {
        return Result<std::string>::failure(
            Error(ErrorCode::InvalidArgument, "material renderer compatibility mask is invalid"));
    }

    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "fglmaterial " << CurrentVersion << '\n';
    output << "asset_guid " << asset.id.toString() << '\n';
    output << "name " << std::quoted(asset.name) << '\n';
    output << "base_texture "
           << (asset.material.baseTexture ? asset.material.baseTexture->toString()
                                          : std::string("nil"))
           << '\n';
    output << "palette_asset "
           << (asset.material.paletteAsset ? asset.material.paletteAsset->toString()
                                           : std::string("nil"))
           << '\n';
    output << "transparent_index ";
    if (asset.material.transparentIndex)
        output << static_cast<unsigned int>(*asset.material.transparentIndex);
    else
        output << "nil";
    output << '\n';
    writeColor(output, "tint", asset.material.tint);
    writeColor(output, "flat_color", asset.material.flatColor);
    writeColor(output, "emissive", asset.material.emissive);
    output << "emissive_strength " << static_cast<unsigned int>(asset.material.emissiveStrength)
           << '\n';
    output << "color_mode " << colorModeName(asset.material.colorMode) << '\n';
    output << "dither " << ditherName(asset.material.dither) << '\n';
    output << "sampling " << samplingName(asset.material.sampling) << '\n';
    output << "lighting " << lightingName(asset.material.lighting) << '\n';
    output << "blend " << blendName(asset.material.blend) << '\n';
    output << "fog " << (asset.material.participatesInFog ? 1 : 0) << '\n';
    output << "billboard " << (asset.material.billboard ? 1 : 0) << '\n';
    output << "double_sided " << (asset.material.doubleSided ? 1 : 0) << '\n';
    output << "compatible_renderers " << compatibility << '\n';
    output << "palette_count " << asset.material.palette.size() << '\n';
    for (const auto color : asset.material.palette)
        writeColor(output, "palette_color", color);
    output << "material_end\n";
    return Result<std::string>::success(output.str());
}

Result<MaterialAsset> MaterialSerializer::deserialize(std::string_view text) {
    if (text.size() > MaximumInputBytes) {
        return Result<MaterialAsset>::failure(
            Error(ErrorCode::CapacityExceeded, "material exceeds the input size limit"));
    }
    Reader reader(text);
    std::string header;
    if (!reader.next(header))
        return Result<MaterialAsset>::failure(invalid(reader, "material is empty"));
    std::istringstream headerStream(header);
    headerStream.imbue(std::locale::classic());
    std::string magic;
    std::string versionToken;
    if (!(headerStream >> magic >> versionToken) || magic != "fglmaterial" ||
        !finished(headerStream)) {
        return Result<MaterialAsset>::failure(invalid(reader, "invalid material header"));
    }
    const auto version = integer<int>(versionToken);
    if (!version || *version != CurrentVersion) {
        return Result<MaterialAsset>::failure(
            Error(ErrorCode::UnsupportedVersion, "unsupported material version")
                .addContext("version", versionToken));
    }

    auto idToken = field(reader, "asset_guid");
    if (!idToken)
        return Result<MaterialAsset>::failure(idToken.error());
    auto id = AssetGuid::parse(idToken.value());
    if (!id || id.value().isNil())
        return Result<MaterialAsset>::failure(invalid(reader, "invalid material asset GUID"));
    auto name = quotedField(reader, "name");
    if (!name || name.value().empty()) {
        return Result<MaterialAsset>::failure(name ? invalid(reader, "material name is empty")
                                                   : name.error());
    }

    Material material;
    auto baseTextureToken = field(reader, "base_texture");
    if (!baseTextureToken)
        return Result<MaterialAsset>::failure(baseTextureToken.error());
    auto baseTexture = optionalGuid<AssetGuid>(baseTextureToken.value(), reader);
    if (!baseTexture)
        return Result<MaterialAsset>::failure(baseTexture.error());
    material.baseTexture = baseTexture.value();
    auto paletteAssetToken = field(reader, "palette_asset");
    if (!paletteAssetToken)
        return Result<MaterialAsset>::failure(paletteAssetToken.error());
    auto paletteAsset = optionalGuid<AssetGuid>(paletteAssetToken.value(), reader);
    if (!paletteAsset)
        return Result<MaterialAsset>::failure(paletteAsset.error());
    material.paletteAsset = paletteAsset.value();

    auto transparentToken = field(reader, "transparent_index");
    if (!transparentToken)
        return Result<MaterialAsset>::failure(transparentToken.error());
    if (transparentToken.value() != "nil") {
        const auto value = integer<unsigned int>(transparentToken.value());
        if (!value || *value > 255U)
            return Result<MaterialAsset>::failure(invalid(reader, "transparent index is invalid"));
        material.transparentIndex = static_cast<std::uint8_t>(*value);
    }
    auto tint = colorField(reader, "tint");
    auto flat = colorField(reader, "flat_color");
    auto emissive = colorField(reader, "emissive");
    if (!tint)
        return Result<MaterialAsset>::failure(tint.error());
    if (!flat)
        return Result<MaterialAsset>::failure(flat.error());
    if (!emissive)
        return Result<MaterialAsset>::failure(emissive.error());
    material.tint = tint.value();
    material.flatColor = flat.value();
    material.emissive = emissive.value();

    auto strengthToken = field(reader, "emissive_strength");
    if (!strengthToken)
        return Result<MaterialAsset>::failure(strengthToken.error());
    const auto strength = integer<unsigned int>(strengthToken.value());
    if (!strength || *strength > 255U)
        return Result<MaterialAsset>::failure(invalid(reader, "emissive strength is invalid"));
    material.emissiveStrength = static_cast<std::uint8_t>(*strength);

    auto colorMode = enumField<MaterialColorMode>(reader, "color_mode",
                                                  {{"texture", MaterialColorMode::Texture},
                                                   {"flat", MaterialColorMode::Flat},
                                                   {"vertex", MaterialColorMode::Vertex}});
    auto dither = enumField<MaterialDitherMode>(reader, "dither",
                                                {{"none", MaterialDitherMode::None},
                                                 {"ordered2x2", MaterialDitherMode::Ordered2x2},
                                                 {"ordered4x4", MaterialDitherMode::Ordered4x4}});
    auto sampling = enumField<MaterialSamplingMode>(
        reader, "sampling",
        {{"nearest", MaterialSamplingMode::Nearest}, {"bilinear", MaterialSamplingMode::Bilinear}});
    auto lighting = enumField<MaterialLightingMode>(reader, "lighting",
                                                    {{"unlit", MaterialLightingMode::Unlit},
                                                     {"flat", MaterialLightingMode::Flat},
                                                     {"vertex", MaterialLightingMode::Vertex}});
    auto blend = enumField<MaterialBlendMode>(reader, "blend",
                                              {{"opaque", MaterialBlendMode::Opaque},
                                               {"alpha", MaterialBlendMode::Alpha},
                                               {"additive", MaterialBlendMode::Additive},
                                               {"multiply", MaterialBlendMode::Multiply}});
    if (!colorMode)
        return Result<MaterialAsset>::failure(colorMode.error());
    if (!dither)
        return Result<MaterialAsset>::failure(dither.error());
    if (!sampling)
        return Result<MaterialAsset>::failure(sampling.error());
    if (!lighting)
        return Result<MaterialAsset>::failure(lighting.error());
    if (!blend)
        return Result<MaterialAsset>::failure(blend.error());
    material.colorMode = colorMode.value();
    material.dither = dither.value();
    material.sampling = sampling.value();
    material.lighting = lighting.value();
    material.blend = blend.value();

    auto fog = booleanField(reader, "fog");
    auto billboard = booleanField(reader, "billboard");
    auto doubleSided = booleanField(reader, "double_sided");
    if (!fog)
        return Result<MaterialAsset>::failure(fog.error());
    if (!billboard)
        return Result<MaterialAsset>::failure(billboard.error());
    if (!doubleSided)
        return Result<MaterialAsset>::failure(doubleSided.error());
    material.participatesInFog = fog.value();
    material.billboard = billboard.value();
    material.doubleSided = doubleSided.value();

    auto compatibilityToken = field(reader, "compatible_renderers");
    if (!compatibilityToken)
        return Result<MaterialAsset>::failure(compatibilityToken.error());
    const auto compatibility = integer<std::uint32_t>(compatibilityToken.value());
    const auto allCompatibility = static_cast<std::uint32_t>(RendererCompatibility::All);
    if (!compatibility || *compatibility == 0U || (*compatibility & ~allCompatibility) != 0U) {
        return Result<MaterialAsset>::failure(
            invalid(reader, "material renderer compatibility mask is invalid"));
    }
    material.compatibleRenderers = static_cast<RendererCompatibility>(*compatibility);

    auto paletteCountToken = field(reader, "palette_count");
    if (!paletteCountToken)
        return Result<MaterialAsset>::failure(paletteCountToken.error());
    const auto paletteCount = integer<std::size_t>(paletteCountToken.value());
    if (!paletteCount || *paletteCount > MaximumPaletteColors)
        return Result<MaterialAsset>::failure(invalid(reader, "material palette count is invalid"));
    material.palette.reserve(*paletteCount);
    for (std::size_t index = 0; index < *paletteCount; ++index) {
        auto paletteColor = colorField(reader, "palette_color");
        if (!paletteColor)
            return Result<MaterialAsset>::failure(paletteColor.error());
        material.palette.push_back(paletteColor.value());
    }
    auto end = marker(reader, "material_end");
    if (!end)
        return Result<MaterialAsset>::failure(end.error());
    std::string trailing;
    if (reader.next(trailing))
        return Result<MaterialAsset>::failure(invalid(reader, "trailing material data"));

    return Result<MaterialAsset>::success(
        MaterialAsset{id.value(), std::move(name.value()), std::move(material)});
}

} // namespace fabgl
