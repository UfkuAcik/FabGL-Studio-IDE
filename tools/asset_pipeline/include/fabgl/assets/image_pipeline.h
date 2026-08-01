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

struct ImageImportSettings final {
    int targetWidth = 0;
    int targetHeight = 0;
    int paletteSize = 16;
    std::uint8_t alphaThreshold = 127;
    bool dither = false;
    bool reserveTransparentIndex = true;
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

[[nodiscard]] Result<Image> loadImage(const std::string& utf8Path);
[[nodiscard]] Result<IndexedImage> processImage(const Image& source,
                                                const ImageImportSettings& settings);
[[nodiscard]] std::vector<std::uint8_t> encodeIndexedImage(const IndexedImage& image);
[[nodiscard]] ImageCost estimateCost(const IndexedImage& image,
                                     const std::vector<std::uint8_t>& encoded) noexcept;

} // namespace fabgl::assets
