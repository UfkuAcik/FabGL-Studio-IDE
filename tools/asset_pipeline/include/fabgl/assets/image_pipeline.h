#pragma once

#include <fabgl/core/result.h>
#include <fabgl/math/types.h>

#include <cstdint>
#include <string>
#include <vector>

namespace fabgl::assets {

struct Image final {
    int width = 0;
    int height = 0;
    std::vector<Color> pixels;

    [[nodiscard]] bool valid() const noexcept;
};

struct ImageRect final {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct AtlasSprite final {
    std::string name;
    Image image;
    float pivotX = 0.5F;
    float pivotY = 0.5F;
};

struct AtlasRegion final {
    std::string name;
    ImageRect rectangle;
    float pivotX = 0.5F;
    float pivotY = 0.5F;
};

struct SpriteAtlas final {
    Image image;
    std::vector<AtlasRegion> regions;
};

enum class ImageSliceMode : std::uint8_t { None = 0, Grid };
enum class ImageOutputKind : std::uint8_t { Image = 0, SpriteAtlas };
enum class ImageCompression : std::uint8_t { Rle = 0 };
enum class ImageResidency : std::uint8_t { Preload = 0, Stream };

struct ImageImportSettings final {
    int targetWidth = 0;
    int targetHeight = 0;
    int paletteSize = 16;
    std::uint8_t alphaThreshold = 127;
    bool dither = false;
    bool reserveTransparentIndex = true;

    // Project-level authoring stages. A disabled crop keeps the complete source.
    bool cropEnabled = false;
    ImageRect crop;

    // Grid slicing is a deterministic atlas input. It is intentionally invalid
    // with Image output so frames can never be silently discarded.
    ImageSliceMode sliceMode = ImageSliceMode::None;
    int frameWidth = 0;
    int frameHeight = 0;
    int frameMargin = 0;
    int frameSpacing = 0;

    ImageOutputKind outputKind = ImageOutputKind::Image;
    int atlasMaximumWidth = 1024;
    int atlasPadding = 1;
    bool atlasPowerOfTwo = true;
    float pivotX = 0.5F;
    float pivotY = 0.5F;
    float pixelsPerUnit = 100.0F;

    // FGLI/FGLS currently have one deterministic indexed RLE representation.
    // Residency controls the target cost model and runtime loading policy
    // metadata; it does not change source decoding.
    ImageCompression compression = ImageCompression::Rle;
    ImageResidency residency = ImageResidency::Preload;
};

struct IndexedImage final {
    int width = 0;
    int height = 0;
    std::vector<Color> palette;
    std::vector<std::uint8_t> indices;
    std::uint8_t transparentIndex = 255;

    [[nodiscard]] bool valid() const noexcept;
};

struct ImageCost final {
    std::size_t decodedBytes = 0;
    std::size_t paletteBytes = 0;
    std::size_t packedBytes = 0;
    std::size_t estimatedPixelsPerFrame = 0;
};

struct CompiledImageAsset final {
    std::vector<std::uint8_t> payload;
    IndexedImage preview;
    ImageCost cost;
    std::size_t frameCount = 1U;
    bool spriteAtlas = false;
};

struct ThumbnailSettings final {
    int maximumWidth = 128;
    int maximumHeight = 128;
    int paletteSize = 16;
    std::uint8_t alphaThreshold = 127;
    bool dither = false;
};

[[nodiscard]] Result<Image> loadImage(const std::string& utf8Path);
[[nodiscard]] Result<Image> cropImage(const Image& source, ImageRect rectangle);
[[nodiscard]] Result<std::vector<Image>> sliceImageGrid(const Image& source, int frameWidth,
                                                        int frameHeight, int margin = 0,
                                                        int spacing = 0);
[[nodiscard]] Result<SpriteAtlas> buildSpriteAtlas(const std::vector<AtlasSprite>& sprites,
                                                   int maximumWidth = 1024, int padding = 1,
                                                   bool powerOfTwo = true);
[[nodiscard]] Result<std::vector<std::uint8_t>>
encodeSpriteAtlas(const SpriteAtlas& atlas, const ImageImportSettings& settings);
[[nodiscard]] Result<IndexedImage> processImage(const Image& source,
                                                const ImageImportSettings& settings);
// Executes the complete canonical project pipeline: crop, optional grid
// slicing/atlas packing, quantization and indexed RLE encoding. All settings
// are validated against the actual source dimensions before producing bytes.
[[nodiscard]] Result<CompiledImageAsset> compileImageAsset(const Image& source,
                                                           const ImageImportSettings& settings);
[[nodiscard]] std::vector<std::uint8_t> encodeIndexedImage(const IndexedImage& image);
[[nodiscard]] Result<IndexedImage> decodeIndexedImage(const std::vector<std::uint8_t>& bytes);
[[nodiscard]] Result<IndexedImage> createThumbnail(const Image& source,
                                                   const ThumbnailSettings& settings = {});
[[nodiscard]] ImageCost estimateCost(const IndexedImage& image,
                                     const std::vector<std::uint8_t>& encoded) noexcept;

} // namespace fabgl::assets
