#pragma once

#include <fabgl/assets/asset_importer.h>
#include <fabgl/core/result.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace fabgl::assets {

struct BitmapFontLimits final {
    std::uint32_t maximumGlyphs = 1024;
    std::uint16_t maximumGlyphDimension = 64;
    std::uint16_t maximumAtlasWidth = 512;
    std::uint16_t maximumAtlasHeight = 2048;
};

struct BitmapGlyph final {
    std::uint32_t codepoint = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::int16_t xOffset = 0;
    std::int16_t yOffset = 0;
    std::int16_t advance = 0;
};

struct BitmapFont final {
    std::uint16_t atlasWidth = 0;
    std::uint16_t atlasHeight = 0;
    std::int16_t ascent = 0;
    std::int16_t descent = 0;
    std::vector<BitmapGlyph> glyphs;
    std::vector<std::uint8_t> atlasBits;

    [[nodiscard]] bool valid(const BitmapFontLimits& limits = {}) const noexcept;
};

struct BdfImportSettings final {
    std::uint16_t maximumAtlasWidth = 128;
    std::uint16_t padding = 1;
    BitmapFontLimits limits;
};

[[nodiscard]] Result<BitmapFont> importBdfFont(std::string_view source,
                                               const BdfImportSettings& settings = {});
[[nodiscard]] Result<std::vector<std::uint8_t>> encodeBitmapFont(const BitmapFont& font);
[[nodiscard]] Result<BitmapFont> inspectBitmapFont(
    const std::vector<std::uint8_t>& bytes, const BitmapFontLimits& limits = {});

class BdfFontImporter final : public IAssetImporter {
  public:
    [[nodiscard]] std::string_view id() const noexcept override;
    [[nodiscard]] std::uint32_t version() const noexcept override;
    [[nodiscard]] AssetKind kind() const noexcept override;
    [[nodiscard]] std::vector<std::string> extensions() const override;
    [[nodiscard]] Result<ImportedAsset> import(const AssetImportRequest& request) const override;
};

} // namespace fabgl::assets
