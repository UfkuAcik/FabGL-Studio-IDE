#include <fabgl/assets/image_pipeline.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
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

} // namespace

bool Image::valid() const noexcept {
    return width > 0 && height > 0 && width <= 8192 && height <= 8192 &&
           pixels.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
}

bool IndexedImage::valid() const noexcept {
    return width > 0 && height > 0 && !palette.empty() && palette.size() <= 256U &&
           indices.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) &&
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
    if (!image.valid() || image.width > 65535 || image.height > 65535) {
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

ImageCost estimateCost(const IndexedImage& image,
                       const std::vector<std::uint8_t>& encoded) noexcept {
    return {image.indices.size(), image.palette.size() * sizeof(Color), encoded.size(),
            image.indices.size()};
}

} // namespace fabgl::assets
