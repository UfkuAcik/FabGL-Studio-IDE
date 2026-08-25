#include <fabgl/assets/image_pipeline.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <unordered_map>
#include <utility>

namespace fabgl::assets {

namespace {

struct Bucket final {
    std::uint32_t key = 0;
    std::uint32_t count = 0;
};

[[nodiscard]] std::uint32_t bucketKey(Color color) noexcept {
    return (static_cast<std::uint32_t>(color.r >> 3U) << 10U) |
           (static_cast<std::uint32_t>(color.g >> 3U) << 5U) |
           static_cast<std::uint32_t>(color.b >> 3U);
}

[[nodiscard]] Color bucketColor(std::uint32_t key) noexcept {
    return {static_cast<std::uint8_t>(((key >> 10U) & 31U) * 255U / 31U),
            static_cast<std::uint8_t>(((key >> 5U) & 31U) * 255U / 31U),
            static_cast<std::uint8_t>((key & 31U) * 255U / 31U), 255U};
}

[[nodiscard]] std::size_t nearestColor(float red, float green, float blue,
                                       const std::vector<Color>& palette,
                                       std::size_t firstOpaque) noexcept {
    auto bestIndex = firstOpaque;
    auto bestDistance = std::numeric_limits<float>::infinity();
    for (auto index = firstOpaque; index < palette.size(); ++index) {
        const auto redDifference = red - static_cast<float>(palette[index].r);
        const auto greenDifference = green - static_cast<float>(palette[index].g);
        const auto blueDifference = blue - static_cast<float>(palette[index].b);
        const auto distance = redDifference * redDifference + greenDifference * greenDifference +
                              blueDifference * blueDifference;
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = index;
        }
    }
    return bestIndex;
}

[[nodiscard]] Image resizeNearest(const Image& source, int width, int height) {
    Image result;
    result.width = width;
    result.height = height;
    result.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (auto y = 0; y < height; ++y) {
        const auto sourceY =
            std::min(source.height - 1,
                     static_cast<int>((static_cast<std::int64_t>(y) * source.height) / height));
        for (auto x = 0; x < width; ++x) {
            const auto sourceX =
                std::min(source.width - 1,
                         static_cast<int>((static_cast<std::int64_t>(x) * source.width) / width));
            result.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                          static_cast<std::size_t>(x)] =
                source.pixels[static_cast<std::size_t>(sourceY) *
                                  static_cast<std::size_t>(source.width) +
                              static_cast<std::size_t>(sourceX)];
        }
    }
    return result;
}

void appendU16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    const auto wide = static_cast<std::uint32_t>(value);
    output.push_back(static_cast<std::uint8_t>(wide & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((wide >> 8U) & 0xFFU));
}

void appendU32(std::vector<std::uint8_t>& output, std::uint32_t value) {
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

} // namespace

bool Image::valid() const noexcept {
    return width > 0 && height > 0 && width <= 8192 && height <= 8192 &&
           pixels.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
}

Result<Image> cropImage(const Image& source, const ImageRect rectangle) {
    if (!source.valid() || rectangle.x < 0 || rectangle.y < 0 || rectangle.width <= 0 ||
        rectangle.height <= 0 || rectangle.x > source.width - rectangle.width ||
        rectangle.y > source.height - rectangle.height) {
        return Result<Image>::failure(
            Error(ErrorCode::InvalidArgument, "image crop rectangle is outside the source"));
    }
    Image result;
    result.width = rectangle.width;
    result.height = rectangle.height;
    result.pixels.resize(static_cast<std::size_t>(result.width) *
                         static_cast<std::size_t>(result.height));
    for (auto y = 0; y < result.height; ++y) {
        const auto sourceOffset =
            static_cast<std::size_t>(rectangle.y + y) * static_cast<std::size_t>(source.width) +
            static_cast<std::size_t>(rectangle.x);
        const auto destinationOffset =
            static_cast<std::size_t>(y) * static_cast<std::size_t>(result.width);
        std::copy_n(source.pixels.begin() + static_cast<std::ptrdiff_t>(sourceOffset), result.width,
                    result.pixels.begin() + static_cast<std::ptrdiff_t>(destinationOffset));
    }
    return Result<Image>::success(std::move(result));
}

Result<std::vector<Image>> sliceImageGrid(const Image& source, const int frameWidth,
                                          const int frameHeight, const int margin,
                                          const int spacing) {
    if (!source.valid() || frameWidth <= 0 || frameHeight <= 0 || margin < 0 || spacing < 0 ||
        margin > source.width || margin > source.height) {
        return Result<std::vector<Image>>::failure(
            Error(ErrorCode::InvalidArgument, "sprite grid settings are invalid"));
    }
    std::vector<Image> frames;
    const auto strideX = frameWidth + spacing;
    const auto strideY = frameHeight + spacing;
    for (auto y = margin; y <= source.height - margin - frameHeight; y += strideY) {
        for (auto x = margin; x <= source.width - margin - frameWidth; x += strideX) {
            auto frame = cropImage(source, {x, y, frameWidth, frameHeight});
            if (!frame) {
                return Result<std::vector<Image>>::failure(frame.error());
            }
            frames.push_back(std::move(frame.value()));
        }
    }
    if (frames.empty()) {
        return Result<std::vector<Image>>::failure(
            Error(ErrorCode::InvalidArgument, "sprite grid does not contain a complete frame"));
    }
    return Result<std::vector<Image>>::success(std::move(frames));
}

Result<SpriteAtlas> buildSpriteAtlas(const std::vector<AtlasSprite>& sprites,
                                     const int maximumWidth, const int padding,
                                     const bool powerOfTwo) {
    if (sprites.empty() || maximumWidth <= 0 || maximumWidth > 4096 || padding < 0 ||
        padding > 64) {
        return Result<SpriteAtlas>::failure(
            Error(ErrorCode::InvalidArgument, "sprite atlas settings are invalid"));
    }
    std::set<std::string> names;
    auto cursorX = padding;
    auto cursorY = padding;
    auto rowHeight = 0;
    auto usedWidth = 0;
    std::vector<AtlasRegion> regions;
    regions.reserve(sprites.size());
    for (const auto& sprite : sprites) {
        if (sprite.name.empty() || !names.insert(sprite.name).second || !sprite.image.valid() ||
            !std::isfinite(sprite.pivotX) || !std::isfinite(sprite.pivotY) ||
            sprite.pivotX < 0.0F || sprite.pivotX > 1.0F || sprite.pivotY < 0.0F ||
            sprite.pivotY > 1.0F || sprite.image.width > maximumWidth - padding * 2) {
            return Result<SpriteAtlas>::failure(
                Error(ErrorCode::InvalidArgument, "sprite atlas input is invalid")
                    .addContext("sprite", sprite.name));
        }
        if (cursorX + sprite.image.width + padding > maximumWidth) {
            cursorX = padding;
            cursorY += rowHeight + padding;
            rowHeight = 0;
        }
        if (cursorY > 4096 - sprite.image.height - padding) {
            return Result<SpriteAtlas>::failure(
                Error(ErrorCode::CapacityExceeded, "sprite atlas exceeds 4096 pixels in height"));
        }
        regions.push_back({sprite.name,
                           {cursorX, cursorY, sprite.image.width, sprite.image.height},
                           sprite.pivotX,
                           sprite.pivotY});
        cursorX += sprite.image.width + padding;
        rowHeight = std::max(rowHeight, sprite.image.height);
        usedWidth = std::max(usedWidth, cursorX);
    }
    auto atlasWidth = std::max(1, usedWidth);
    auto atlasHeight = cursorY + rowHeight + padding;
    const auto nextPowerOfTwo = [](int value) {
        auto result = 1;
        while (result < value) {
            result *= 2;
        }
        return result;
    };
    if (powerOfTwo) {
        atlasWidth = nextPowerOfTwo(atlasWidth);
        atlasHeight = nextPowerOfTwo(atlasHeight);
    }
    if (atlasWidth > 4096 || atlasHeight > 4096) {
        return Result<SpriteAtlas>::failure(
            Error(ErrorCode::CapacityExceeded, "sprite atlas dimensions exceed 4096"));
    }

    SpriteAtlas result;
    result.image.width = atlasWidth;
    result.image.height = atlasHeight;
    result.image.pixels.assign(static_cast<std::size_t>(atlasWidth) *
                                   static_cast<std::size_t>(atlasHeight),
                               Color{0U, 0U, 0U, 0U});
    result.regions = std::move(regions);
    for (std::size_t spriteIndex = 0; spriteIndex < sprites.size(); ++spriteIndex) {
        const auto& sprite = sprites[spriteIndex];
        const auto& rectangle = result.regions[spriteIndex].rectangle;
        for (auto y = 0; y < sprite.image.height; ++y) {
            const auto sourceOffset =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(sprite.image.width);
            const auto destinationOffset = static_cast<std::size_t>(rectangle.y + y) *
                                               static_cast<std::size_t>(result.image.width) +
                                           static_cast<std::size_t>(rectangle.x);
            std::copy_n(sprite.image.pixels.begin() + static_cast<std::ptrdiff_t>(sourceOffset),
                        sprite.image.width,
                        result.image.pixels.begin() +
                            static_cast<std::ptrdiff_t>(destinationOffset));
        }
    }
    return Result<SpriteAtlas>::success(std::move(result));
}

Result<std::vector<std::uint8_t>> encodeSpriteAtlas(const SpriteAtlas& atlas,
                                                    const ImageImportSettings& settings) {
    if (!atlas.image.valid() || atlas.regions.empty() || atlas.regions.size() > 65535U ||
        atlas.image.width > 65535 || atlas.image.height > 65535) {
        return Result<std::vector<std::uint8_t>>::failure(
            Error(ErrorCode::InvalidArgument, "sprite atlas is invalid"));
    }
    auto processed = processImage(atlas.image, settings);
    if (!processed) {
        return Result<std::vector<std::uint8_t>>::failure(processed.error());
    }
    auto imageBytes = encodeIndexedImage(processed.value());
    if (imageBytes.empty() ||
        imageBytes.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return Result<std::vector<std::uint8_t>>::failure(
            Error(ErrorCode::CapacityExceeded, "encoded sprite atlas image is too large"));
    }

    std::vector<std::uint8_t> metadata;
    for (const auto& region : atlas.regions) {
        if (region.name.empty() || region.name.size() > 65535U || region.rectangle.x < 0 ||
            region.rectangle.y < 0 || region.rectangle.width <= 0 || region.rectangle.height <= 0 ||
            region.rectangle.x > 65535 || region.rectangle.y > 65535 ||
            region.rectangle.width > 65535 || region.rectangle.height > 65535 ||
            region.rectangle.x > atlas.image.width - region.rectangle.width ||
            region.rectangle.y > atlas.image.height - region.rectangle.height ||
            !std::isfinite(region.pivotX) || !std::isfinite(region.pivotY) ||
            region.pivotX < 0.0F || region.pivotX > 1.0F || region.pivotY < 0.0F ||
            region.pivotY > 1.0F) {
            return Result<std::vector<std::uint8_t>>::failure(
                Error(ErrorCode::InvalidArgument, "sprite atlas region is invalid")
                    .addContext("sprite", region.name));
        }
        appendU16(metadata, static_cast<std::uint16_t>(region.name.size()));
        metadata.insert(metadata.end(), region.name.begin(), region.name.end());
        appendU16(metadata, static_cast<std::uint16_t>(region.rectangle.x));
        appendU16(metadata, static_cast<std::uint16_t>(region.rectangle.y));
        appendU16(metadata, static_cast<std::uint16_t>(region.rectangle.width));
        appendU16(metadata, static_cast<std::uint16_t>(region.rectangle.height));
        appendU16(metadata, static_cast<std::uint16_t>(std::lround(region.pivotX * 65535.0F)));
        appendU16(metadata, static_cast<std::uint16_t>(std::lround(region.pivotY * 65535.0F)));
    }
    if (metadata.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return Result<std::vector<std::uint8_t>>::failure(
            Error(ErrorCode::CapacityExceeded, "sprite atlas metadata is too large"));
    }

    std::vector<std::uint8_t> output;
    output.reserve(16U + metadata.size() + imageBytes.size());
    output.insert(output.end(), {'F', 'G', 'L', 'S'});
    appendU16(output, 1U);
    appendU16(output, static_cast<std::uint16_t>(atlas.regions.size()));
    appendU32(output, static_cast<std::uint32_t>(metadata.size()));
    appendU32(output, static_cast<std::uint32_t>(imageBytes.size()));
    output.insert(output.end(), metadata.begin(), metadata.end());
    output.insert(output.end(), imageBytes.begin(), imageBytes.end());
    return Result<std::vector<std::uint8_t>>::success(std::move(output));
}

bool IndexedImage::valid() const noexcept {
    return width > 0 && height > 0 && width <= 4096 && height <= 4096 && !palette.empty() &&
           palette.size() <= 256U &&
           indices.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) &&
           (transparentIndex == 255U ||
            static_cast<std::size_t>(transparentIndex) < palette.size()) &&
           (transparentIndex == 255U || palette[transparentIndex].a == 0U) &&
           std::all_of(indices.begin(), indices.end(), [&](std::uint8_t index) {
               return static_cast<std::size_t>(index) < palette.size();
           });
}

Result<IndexedImage> processImage(const Image& source, const ImageImportSettings& settings) {
    if (!source.valid()) {
        return Result<IndexedImage>::failure(
            Error(ErrorCode::InvalidArgument, "source image is invalid"));
    }
    const auto width = settings.targetWidth == 0 ? source.width : settings.targetWidth;
    const auto height = settings.targetHeight == 0 ? source.height : settings.targetHeight;
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096 || settings.paletteSize < 2 ||
        settings.paletteSize > 256) {
        return Result<IndexedImage>::failure(
            Error(ErrorCode::InvalidArgument, "invalid image import settings"));
    }
    const auto image = (width == source.width && height == source.height)
                           ? source
                           : resizeNearest(source, width, height);

    auto hasTransparency = false;
    std::unordered_map<std::uint32_t, std::uint32_t> histogram;
    for (const auto color : image.pixels) {
        if (color.a <= settings.alphaThreshold && settings.reserveTransparentIndex) {
            hasTransparency = true;
            continue;
        }
        ++histogram[bucketKey(color)];
    }
    std::vector<Bucket> buckets;
    buckets.reserve(histogram.size());
    for (const auto& pair : histogram) {
        buckets.push_back({pair.first, pair.second});
    }
    std::sort(buckets.begin(), buckets.end(), [](const Bucket& lhs, const Bucket& rhs) {
        return lhs.count != rhs.count ? lhs.count > rhs.count : lhs.key < rhs.key;
    });

    IndexedImage result;
    result.width = width;
    result.height = height;
    if (hasTransparency) {
        result.transparentIndex = 0U;
        result.palette.push_back({0, 0, 0, 0});
    }
    const auto opaqueSlots = static_cast<std::size_t>(settings.paletteSize) - result.palette.size();
    const auto selected = std::min(opaqueSlots, buckets.size());
    for (std::size_t index = 0; index < selected; ++index) {
        result.palette.push_back(bucketColor(buckets[index].key));
    }
    if (result.palette.size() == (hasTransparency ? 1U : 0U)) {
        result.palette.push_back({0, 0, 0, 255});
    }
    const auto firstOpaque = hasTransparency ? 1U : 0U;
    result.indices.resize(image.pixels.size());

    if (!settings.dither) {
        for (std::size_t index = 0; index < image.pixels.size(); ++index) {
            const auto sourceColor = image.pixels[index];
            if (hasTransparency && sourceColor.a <= settings.alphaThreshold) {
                result.indices[index] = result.transparentIndex;
                continue;
            }
            result.indices[index] = static_cast<std::uint8_t>(
                nearestColor(static_cast<float>(sourceColor.r), static_cast<float>(sourceColor.g),
                             static_cast<float>(sourceColor.b), result.palette, firstOpaque));
        }
        return Result<IndexedImage>::success(std::move(result));
    }

    using ErrorPixel = std::array<float, 3>;
    std::vector<ErrorPixel> current(static_cast<std::size_t>(width) + 2U);
    std::vector<ErrorPixel> next(static_cast<std::size_t>(width) + 2U);
    for (auto y = 0; y < height; ++y) {
        std::fill(next.begin(), next.end(), ErrorPixel{});
        for (auto x = 0; x < width; ++x) {
            const auto pixelIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                    static_cast<std::size_t>(x);
            const auto sourceColor = image.pixels[pixelIndex];
            if (hasTransparency && sourceColor.a <= settings.alphaThreshold) {
                result.indices[pixelIndex] = result.transparentIndex;
                continue;
            }
            const auto errorIndex = static_cast<std::size_t>(x) + 1U;
            const auto red = std::clamp(static_cast<float>(sourceColor.r) + current[errorIndex][0],
                                        0.0F, 255.0F);
            const auto green = std::clamp(
                static_cast<float>(sourceColor.g) + current[errorIndex][1], 0.0F, 255.0F);
            const auto blue = std::clamp(static_cast<float>(sourceColor.b) + current[errorIndex][2],
                                         0.0F, 255.0F);
            const auto paletteIndex = nearestColor(red, green, blue, result.palette, firstOpaque);
            result.indices[pixelIndex] = static_cast<std::uint8_t>(paletteIndex);
            const auto chosen = result.palette[paletteIndex];
            const ErrorPixel quantization{red - static_cast<float>(chosen.r),
                                          green - static_cast<float>(chosen.g),
                                          blue - static_cast<float>(chosen.b)};
            for (std::size_t channel = 0; channel < quantization.size(); ++channel) {
                current[errorIndex + 1U][channel] += quantization[channel] * (7.0F / 16.0F);
                next[errorIndex - 1U][channel] += quantization[channel] * (3.0F / 16.0F);
                next[errorIndex][channel] += quantization[channel] * (5.0F / 16.0F);
                next[errorIndex + 1U][channel] += quantization[channel] * (1.0F / 16.0F);
            }
        }
        current.swap(next);
    }
    return Result<IndexedImage>::success(std::move(result));
}

std::vector<std::uint8_t> encodeIndexedImage(const IndexedImage& image) {
    if (!image.valid()) {
        return {};
    }
    std::vector<std::uint8_t> output;
    output.reserve(20U + image.palette.size() * 4U + image.indices.size());
    output.insert(output.end(), {'F', 'G', 'L', 'I'});
    appendU16(output, 1U);
    appendU16(output, image.transparentIndex == 255U ? 0U : 1U);
    appendU16(output, static_cast<std::uint16_t>(image.width));
    appendU16(output, static_cast<std::uint16_t>(image.height));
    appendU16(output, static_cast<std::uint16_t>(image.palette.size()));
    appendU16(output, image.transparentIndex);
    appendU32(output, static_cast<std::uint32_t>(image.indices.size()));
    for (const auto color : image.palette) {
        output.insert(output.end(), {color.r, color.g, color.b, color.a});
    }
    for (std::size_t index = 0; index < image.indices.size();) {
        auto count = std::size_t{1};
        while (index + count < image.indices.size() && count < 255U &&
               image.indices[index + count] == image.indices[index]) {
            ++count;
        }
        output.push_back(static_cast<std::uint8_t>(count));
        output.push_back(image.indices[index]);
        index += count;
    }
    return output;
}

Result<IndexedImage> decodeIndexedImage(const std::vector<std::uint8_t>& bytes) {
    constexpr auto headerSize = std::size_t{20};
    if (bytes.size() < headerSize || bytes[0] != 'F' || bytes[1] != 'G' || bytes[2] != 'L' ||
        bytes[3] != 'I') {
        return Result<IndexedImage>::failure(
            Error(ErrorCode::InvalidFormat, "indexed image magic is invalid"));
    }
    if (readU16(bytes, 4U) != 1U) {
        return Result<IndexedImage>::failure(
            Error(ErrorCode::UnsupportedVersion, "indexed image version is unsupported"));
    }
    const auto flags = readU16(bytes, 6U);
    const auto width = readU16(bytes, 8U);
    const auto height = readU16(bytes, 10U);
    const auto paletteCount = readU16(bytes, 12U);
    const auto transparentIndex = readU16(bytes, 14U);
    const auto pixelCount = readU32(bytes, 16U);
    const auto expectedPixels = static_cast<std::uint64_t>(width) * height;
    const auto paletteBytes = static_cast<std::uint64_t>(paletteCount) * 4U;
    if ((flags != 0U && flags != 1U) || width == 0U || height == 0U || width > 4096U ||
        height > 4096U || paletteCount == 0U || paletteCount > 256U ||
        pixelCount != expectedPixels || paletteBytes > bytes.size() - headerSize ||
        (flags == 0U && transparentIndex != 255U) ||
        (flags == 1U && transparentIndex >= paletteCount)) {
        return Result<IndexedImage>::failure(
            Error(ErrorCode::InvalidFormat, "indexed image header is invalid"));
    }

    IndexedImage result;
    result.width = width;
    result.height = height;
    result.transparentIndex = static_cast<std::uint8_t>(transparentIndex);
    result.palette.reserve(paletteCount);
    auto offset = headerSize;
    for (std::uint16_t index = 0; index < paletteCount; ++index) {
        result.palette.push_back(
            {bytes[offset], bytes[offset + 1U], bytes[offset + 2U], bytes[offset + 3U]});
        offset += 4U;
    }
    if (flags == 1U && result.palette[result.transparentIndex].a != 0U) {
        return Result<IndexedImage>::failure(
            Error(ErrorCode::InvalidFormat, "indexed image transparent palette entry is opaque"));
    }
    result.indices.reserve(pixelCount);
    auto previousCount = std::uint8_t{0};
    auto previousIndex = std::uint8_t{0};
    while (offset < bytes.size()) {
        if (bytes.size() - offset < 2U) {
            return Result<IndexedImage>::failure(
                Error(ErrorCode::InvalidFormat, "indexed image RLE pair is truncated"));
        }
        const auto count = bytes[offset++];
        const auto paletteIndex = bytes[offset++];
        if (count == 0U || paletteIndex >= paletteCount ||
            static_cast<std::size_t>(count) > pixelCount - result.indices.size() ||
            (previousCount != 0U && previousCount < 255U && previousIndex == paletteIndex)) {
            return Result<IndexedImage>::failure(
                Error(ErrorCode::InvalidFormat, "indexed image RLE stream is invalid"));
        }
        result.indices.insert(result.indices.end(), count, paletteIndex);
        previousCount = count;
        previousIndex = paletteIndex;
    }
    if (result.indices.size() != pixelCount || !result.valid()) {
        return Result<IndexedImage>::failure(
            Error(ErrorCode::InvalidFormat, "indexed image pixel data is incomplete"));
    }
    return Result<IndexedImage>::success(std::move(result));
}

Result<IndexedImage> createThumbnail(const Image& source, const ThumbnailSettings& settings) {
    if (!source.valid() || settings.maximumWidth <= 0 || settings.maximumHeight <= 0 ||
        settings.maximumWidth > 4096 || settings.maximumHeight > 4096 || settings.paletteSize < 2 ||
        settings.paletteSize > 256) {
        return Result<IndexedImage>::failure(
            Error(ErrorCode::InvalidArgument, "thumbnail source or settings are invalid"));
    }
    auto width = source.width;
    auto height = source.height;
    if (width > settings.maximumWidth || height > settings.maximumHeight) {
        const auto widthLimited = static_cast<std::int64_t>(source.width) * settings.maximumHeight >
                                  static_cast<std::int64_t>(source.height) * settings.maximumWidth;
        if (widthLimited) {
            width = settings.maximumWidth;
            height =
                std::max(1, static_cast<int>((static_cast<std::int64_t>(source.height) * width) /
                                             source.width));
        } else {
            height = settings.maximumHeight;
            width =
                std::max(1, static_cast<int>((static_cast<std::int64_t>(source.width) * height) /
                                             source.height));
        }
    }
    ImageImportSettings importSettings;
    importSettings.targetWidth = width;
    importSettings.targetHeight = height;
    importSettings.paletteSize = settings.paletteSize;
    importSettings.alphaThreshold = settings.alphaThreshold;
    importSettings.dither = settings.dither;
    return processImage(source, importSettings);
}

ImageCost estimateCost(const IndexedImage& image,
                       const std::vector<std::uint8_t>& encoded) noexcept {
    return {image.indices.size(), image.palette.size() * sizeof(Color), encoded.size(),
            image.indices.size()};
}

Result<CompiledImageAsset> compileImageAsset(const Image& source,
                                             const ImageImportSettings& settings) {
    if (!source.valid() || !std::isfinite(settings.pivotX) || !std::isfinite(settings.pivotY) ||
        !std::isfinite(settings.pixelsPerUnit) || settings.pivotX < 0.0F ||
        settings.pivotX > 1.0F || settings.pivotY < 0.0F || settings.pivotY > 1.0F ||
        settings.pixelsPerUnit <= 0.0F || settings.pixelsPerUnit > 100000.0F ||
        settings.compression != ImageCompression::Rle ||
        (settings.residency != ImageResidency::Preload &&
         settings.residency != ImageResidency::Stream)) {
        return Result<CompiledImageAsset>::failure(
            Error(ErrorCode::InvalidArgument, "advanced image import settings are invalid"));
    }
    if (!settings.cropEnabled && (settings.crop.x != 0 || settings.crop.y != 0 ||
                                  settings.crop.width != 0 || settings.crop.height != 0)) {
        return Result<CompiledImageAsset>::failure(
            Error(ErrorCode::InvalidArgument, "disabled image crop contains active values"));
    }
    if (settings.sliceMode == ImageSliceMode::None &&
        (settings.frameWidth != 0 || settings.frameHeight != 0 || settings.frameMargin != 0 ||
         settings.frameSpacing != 0)) {
        return Result<CompiledImageAsset>::failure(
            Error(ErrorCode::InvalidArgument, "disabled sprite slicing contains active values"));
    }
    if (settings.sliceMode == ImageSliceMode::Grid &&
        (settings.frameWidth <= 0 || settings.frameHeight <= 0 || settings.frameMargin < 0 ||
         settings.frameSpacing < 0 || settings.outputKind != ImageOutputKind::SpriteAtlas)) {
        return Result<CompiledImageAsset>::failure(
            Error(ErrorCode::InvalidArgument,
                  "grid slicing requires positive frame dimensions and sprite-atlas output"));
    }
    if (settings.sliceMode != ImageSliceMode::None && settings.sliceMode != ImageSliceMode::Grid) {
        return Result<CompiledImageAsset>::failure(
            Error(ErrorCode::InvalidArgument, "sprite slicing mode is unsupported"));
    }
    if (settings.outputKind == ImageOutputKind::Image &&
        (settings.atlasMaximumWidth != 1024 || settings.atlasPadding != 1 ||
         !settings.atlasPowerOfTwo)) {
        return Result<CompiledImageAsset>::failure(
            Error(ErrorCode::InvalidArgument, "disabled atlas contains non-default options"));
    }
    if (settings.outputKind == ImageOutputKind::SpriteAtlas &&
        (settings.targetWidth != 0 || settings.targetHeight != 0 ||
         settings.atlasMaximumWidth <= 0 || settings.atlasMaximumWidth > 4096 ||
         settings.atlasPadding < 0 || settings.atlasPadding > 64)) {
        return Result<CompiledImageAsset>::failure(
            Error(ErrorCode::InvalidArgument,
                  "sprite-atlas output has invalid packing options or whole-image resize"));
    }
    if (settings.outputKind != ImageOutputKind::Image &&
        settings.outputKind != ImageOutputKind::SpriteAtlas) {
        return Result<CompiledImageAsset>::failure(
            Error(ErrorCode::InvalidArgument, "image output kind is unsupported"));
    }

    Image working = source;
    if (settings.cropEnabled) {
        auto cropped = cropImage(source, settings.crop);
        if (!cropped)
            return Result<CompiledImageAsset>::failure(cropped.error());
        working = std::move(cropped.value());
    }

    CompiledImageAsset output;
    if (settings.outputKind == ImageOutputKind::Image) {
        auto processed = processImage(working, settings);
        if (!processed)
            return Result<CompiledImageAsset>::failure(processed.error());
        output.preview = std::move(processed.value());
        output.payload = encodeIndexedImage(output.preview);
        if (output.payload.empty()) {
            return Result<CompiledImageAsset>::failure(
                Error(ErrorCode::InvalidFormat, "image import produced no encoded payload"));
        }
        output.cost = estimateCost(output.preview, output.payload);
        return Result<CompiledImageAsset>::success(std::move(output));
    }

    std::vector<Image> frames;
    if (settings.sliceMode == ImageSliceMode::Grid) {
        auto sliced = sliceImageGrid(working, settings.frameWidth, settings.frameHeight,
                                     settings.frameMargin, settings.frameSpacing);
        if (!sliced)
            return Result<CompiledImageAsset>::failure(sliced.error());
        frames = std::move(sliced.value());
    } else {
        frames.push_back(std::move(working));
    }
    if (frames.size() > 65535U) {
        return Result<CompiledImageAsset>::failure(
            Error(ErrorCode::CapacityExceeded, "sprite grid contains too many frames"));
    }
    std::vector<AtlasSprite> sprites;
    sprites.reserve(frames.size());
    for (std::size_t index = 0U; index < frames.size(); ++index) {
        auto number = std::to_string(index);
        sprites.push_back(
            {"frame_" + std::string(number.size() < 5U ? 5U - number.size() : 0U, '0') + number,
             std::move(frames[index]), settings.pivotX, settings.pivotY});
    }
    auto atlas = buildSpriteAtlas(sprites, settings.atlasMaximumWidth, settings.atlasPadding,
                                  settings.atlasPowerOfTwo);
    if (!atlas)
        return Result<CompiledImageAsset>::failure(atlas.error());
    auto preview = processImage(atlas.value().image, settings);
    if (!preview)
        return Result<CompiledImageAsset>::failure(preview.error());
    auto encoded = encodeSpriteAtlas(atlas.value(), settings);
    if (!encoded)
        return Result<CompiledImageAsset>::failure(encoded.error());
    output.preview = std::move(preview.value());
    output.payload = std::move(encoded.value());
    output.cost = estimateCost(output.preview, output.payload);
    output.cost.estimatedPixelsPerFrame = sprites.front().image.pixels.size();
    output.frameCount = sprites.size();
    output.spriteAtlas = true;
    return Result<CompiledImageAsset>::success(std::move(output));
}

} // namespace fabgl::assets
