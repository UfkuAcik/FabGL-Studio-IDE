#include <fabgl/rendering/renderer_2d.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <tuple>

namespace fabgl::rendering {

namespace {

constexpr std::size_t MaximumSpritePixels = 1024U * 1024U;
constexpr std::size_t MaximumAnimationFrames = 1024U;
constexpr std::size_t MaximumTilemapCells = 4U * 1024U * 1024U;
constexpr std::size_t MaximumTilemapLayers = 32U;
constexpr std::size_t MaximumTilemapObjects = 4096U;
constexpr std::size_t MaximumTilemapChunks = 4096U;
constexpr std::size_t MaximumTileAnimations = 256U;
constexpr std::size_t MaximumAnimationTiles = 64U;
constexpr std::size_t MaximumTextBytes = 4096U;

[[nodiscard]] bool finiteRect(const Rect value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.width) &&
           std::isfinite(value.height);
}

[[nodiscard]] bool overlaps(const Rect lhs, const Rect rhs) noexcept {
    return lhs.width > 0.0F && lhs.height > 0.0F && rhs.width > 0.0F && rhs.height > 0.0F &&
           lhs.x < rhs.x + rhs.width && lhs.x + lhs.width > rhs.x && lhs.y < rhs.y + rhs.height &&
           lhs.y + lhs.height > rhs.y;
}

[[nodiscard]] std::size_t boundedArea(const int width, const int height) noexcept {
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        return 0U;
    }
    const auto area = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    return area <= MaximumTilemapCells ? area : 0U;
}

} // namespace

bool Sprite::valid() const noexcept {
    if (width <= 0 || height <= 0) {
        return false;
    }
    const auto expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    return pixels.size() == expected || indices.size() == expected;
}

bool Tilemap::valid() const noexcept {
    const auto area = boundedArea(width, height);
    if (area == 0U || tileSize <= 0 || tileSize > 1024 || tiles.empty() ||
        tiles.size() > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) ||
        layers.size() > MaximumTilemapLayers || objects.size() > MaximumTilemapObjects ||
        chunks.size() > MaximumTilemapChunks || animations.size() > MaximumTileAnimations ||
        chunkSize <= 0 || chunkSize > 256) {
        return false;
    }
    if (layers.empty() ? cells.size() != area
                       : std::any_of(layers.begin(), layers.end(), [area](const auto& layer) {
                             return layer.cells.size() != area ||
                                    !std::isfinite(layer.parallax.x) ||
                                    !std::isfinite(layer.parallax.y) || layer.parallax.x < 0.0F ||
                                    layer.parallax.y < 0.0F;
                         })) {
        return false;
    }
    if (!std::all_of(tiles.begin(), tiles.end(), [](const Sprite& tile) { return tile.valid(); })) {
        return false;
    }
    for (const auto& object : objects) {
        if (!finiteRect(object.bounds) || object.bounds.width < 0.0F ||
            object.bounds.height < 0.0F || (!layers.empty() && object.layer >= layers.size())) {
            return false;
        }
    }
    for (const auto& animation : animations) {
        if (animation.sourceTile >= tiles.size() || animation.frames.empty() ||
            animation.frames.size() > MaximumAnimationTiles ||
            !std::isfinite(animation.frameSeconds) || animation.frameSeconds <= 0.0F ||
            (!animation.frameDurationsSeconds.empty() &&
             (animation.frameDurationsSeconds.size() != animation.frames.size() ||
              !std::all_of(animation.frameDurationsSeconds.begin(),
                           animation.frameDurationsSeconds.end(),
                           [](const auto duration) {
                               return std::isfinite(duration) && duration > 0.0F;
                           }))) ||
            !std::all_of(animation.frames.begin(), animation.frames.end(),
                         [this](const auto tile) { return tile < tiles.size(); })) {
            return false;
        }
    }
    const auto layerCount = layers.empty() ? std::size_t{1U} : layers.size();
    for (const auto& chunk : chunks) {
        if (chunk.layer >= layerCount || chunk.x < 0 || chunk.y < 0 || chunk.width <= 0 ||
            chunk.height <= 0 || chunk.x >= width || chunk.y >= height ||
            chunk.width > width - chunk.x || chunk.height > height - chunk.y)
            return false;
    }
    if (!std::is_sorted(solidTiles.begin(), solidTiles.end()) ||
        std::adjacent_find(solidTiles.begin(), solidTiles.end()) != solidTiles.end() ||
        !std::all_of(solidTiles.begin(), solidTiles.end(),
                     [this](const auto tile) { return tile < tiles.size(); }))
        return false;
    return true;
}

std::uint16_t Tilemap::tileAt(const std::size_t layer, const int x, const int y,
                              const float elapsedSeconds) const noexcept {
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return std::numeric_limits<std::uint16_t>::max();
    }
    const auto offset =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
    if ((!layers.empty() && layer >= layers.size()) || (layers.empty() && layer != 0U)) {
        return std::numeric_limits<std::uint16_t>::max();
    }
    auto tile = layers.empty() ? cells[offset] : layers[layer].cells[offset];
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0F) {
        return tile;
    }
    const auto animation =
        std::find_if(animations.begin(), animations.end(),
                     [tile](const auto& item) { return item.sourceTile == tile; });
    if (animation == animations.end() || animation->frames.empty() ||
        animation->frameSeconds <= 0.0F) {
        return tile;
    }
    auto frame = std::size_t{0U};
    if (animation->frameDurationsSeconds.empty()) {
        frame = static_cast<std::size_t>(elapsedSeconds / animation->frameSeconds) %
                animation->frames.size();
    } else {
        const auto cycle = std::accumulate(animation->frameDurationsSeconds.begin(),
                                           animation->frameDurationsSeconds.end(), 0.0F);
        auto cursor = std::fmod(elapsedSeconds, cycle);
        while (frame + 1U < animation->frameDurationsSeconds.size() &&
               cursor >= animation->frameDurationsSeconds[frame]) {
            cursor -= animation->frameDurationsSeconds[frame++];
        }
    }
    return animation->frames[frame];
}

bool Tilemap::collides(const int x, const int y) const noexcept {
    if (layers.empty()) {
        return false;
    }
    for (std::size_t index = 0U; index < layers.size(); ++index) {
        if (layers[index].kind == TilemapLayerKind::Collision && tileAt(index, x, y) != 0U &&
            tileAt(index, x, y) != std::numeric_limits<std::uint16_t>::max()) {
            return true;
        }
    }
    for (std::size_t index = 0U; index < layers.size(); ++index) {
        if (layers[index].kind != TilemapLayerKind::Tiles)
            continue;
        const auto tile = tileAt(index, x, y);
        if (std::binary_search(solidTiles.begin(), solidTiles.end(), tile))
            return true;
    }
    return false;
}

std::vector<const TilemapObject*> Tilemap::objectsIn(const Rect area,
                                                     const std::size_t maximum) const {
    std::vector<const TilemapObject*> result;
    if (!finiteRect(area) || maximum == 0U) {
        return result;
    }
    result.reserve(std::min(maximum, objects.size()));
    for (const auto& object : objects) {
        if (overlaps(area, object.bounds)) {
            result.push_back(&object);
            if (result.size() >= maximum) {
                break;
            }
        }
    }
    return result;
}

bool SpriteAnimationClip::valid() const noexcept {
    if (atlas == nullptr || !atlas->valid() || frames.empty() ||
        frames.size() > MaximumAnimationFrames) {
        return false;
    }
    return std::all_of(frames.begin(), frames.end(), [this](const auto& frame) {
        return finiteRect(frame.sourceRegion) && frame.durationSeconds > 0.0F &&
               std::isfinite(frame.durationSeconds) && frame.sourceRegion.x >= 0.0F &&
               frame.sourceRegion.y >= 0.0F && frame.sourceRegion.width > 0.0F &&
               frame.sourceRegion.height > 0.0F &&
               frame.sourceRegion.x + frame.sourceRegion.width <=
                   static_cast<float>(atlas->width) &&
               frame.sourceRegion.y + frame.sourceRegion.height <=
                   static_cast<float>(atlas->height);
    });
}

void SpriteAnimator::reset() noexcept {
    frameIndex_ = 0U;
    elapsed_ = 0.0F;
    finished_ = false;
}

void SpriteAnimator::update(const SpriteAnimationClip& clip, const float deltaSeconds) noexcept {
    if (!clip.valid() || finished_ || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F) {
        return;
    }
    if (frameIndex_ >= clip.frames.size()) {
        reset();
    }
    elapsed_ += std::min(deltaSeconds, 60.0F);
    std::size_t transitions = 0U;
    while (elapsed_ >= clip.frames[frameIndex_].durationSeconds &&
           transitions++ <= clip.frames.size()) {
        elapsed_ -= clip.frames[frameIndex_].durationSeconds;
        if (frameIndex_ + 1U < clip.frames.size()) {
            ++frameIndex_;
        } else if (clip.loop) {
            frameIndex_ = 0U;
        } else {
            elapsed_ = 0.0F;
            finished_ = true;
            break;
        }
    }
}

SpriteDraw SpriteAnimator::draw(const SpriteAnimationClip& clip, const int x, const int y,
                                const int scale) const noexcept {
    SpriteDraw result;
    if (!clip.valid()) {
        return result;
    }
    result.sprite = clip.atlas;
    result.x = x;
    result.y = y;
    result.scale = scale;
    result.sourceRegion = clip.frames[std::min(frameIndex_, clip.frames.size() - 1U)].sourceRegion;
    return result;
}

bool BitmapFont::valid() const noexcept {
    if (atlas == nullptr || !atlas->valid() || glyphCount == 0U || glyphCount > 4096U ||
        glyphWidth <= 0 || glyphHeight <= 0 || columns <= 0 || horizontalSpacing < -glyphWidth) {
        return false;
    }
    const auto rows = (glyphCount + static_cast<std::uint32_t>(columns) - 1U) /
                      static_cast<std::uint32_t>(columns);
    return static_cast<std::uint64_t>(columns) * static_cast<std::uint64_t>(glyphWidth) <=
               static_cast<std::uint64_t>(atlas->width) &&
           static_cast<std::uint64_t>(rows) * static_cast<std::uint64_t>(glyphHeight) <=
               static_cast<std::uint64_t>(atlas->height);
}

Rect Renderer2D::sourceRegion(const SpriteDraw& command) noexcept {
    if (command.sprite == nullptr) {
        return {};
    }
    if (!finiteRect(command.sourceRegion) || command.sourceRegion.width <= 0.0F ||
        command.sourceRegion.height <= 0.0F) {
        return {0.0F, 0.0F, static_cast<float>(command.sprite->width),
                static_cast<float>(command.sprite->height)};
    }
    return command.sourceRegion;
}

Rect Renderer2D::destinationBounds(const SpriteDraw& command) noexcept {
    if (command.sprite == nullptr || command.scale <= 0 ||
        !std::isfinite(command.rotationDegrees)) {
        return {};
    }
    const auto region = sourceRegion(command);
    const auto width = region.width * static_cast<float>(command.scale);
    const auto height = region.height * static_cast<float>(command.scale);
    const auto radians = command.rotationDegrees * 0.017453292519943F;
    const auto cosine = std::fabs(std::cos(radians));
    const auto sine = std::fabs(std::sin(radians));
    return {static_cast<float>(command.x), static_cast<float>(command.y),
            std::ceil(width * cosine + height * sine), std::ceil(width * sine + height * cosine)};
}

void Renderer2D::draw(const SpriteDraw& command) noexcept {
    if (command.sprite == nullptr || !command.sprite->valid() || command.scale <= 0 ||
        command.scale > 64 || !std::isfinite(command.rotationDegrees) ||
        (command.material == nullptr && command.sprite->pixels.empty())) {
        return;
    }
    const auto region = sourceRegion(command);
    const auto regionX = static_cast<int>(std::floor(region.x));
    const auto regionY = static_cast<int>(std::floor(region.y));
    const auto sourceWidth = static_cast<int>(std::floor(region.width));
    const auto sourceHeight = static_cast<int>(std::floor(region.height));
    if (regionX < 0 || regionY < 0 || sourceWidth <= 0 || sourceHeight <= 0 ||
        regionX + sourceWidth > command.sprite->width ||
        regionY + sourceHeight > command.sprite->height ||
        static_cast<std::size_t>(sourceWidth) * static_cast<std::size_t>(sourceHeight) >
            MaximumSpritePixels) {
        return;
    }

    const auto normalizedRotation = std::fmod(command.rotationDegrees, 360.0F);
    const auto fullRegion = regionX == 0 && regionY == 0 && sourceWidth == command.sprite->width &&
                            sourceHeight == command.sprite->height;
    if (!fullRegion || std::fabs(normalizedRotation) > 0.0001F) {
        Sprite transformed;
        transformed.width = sourceWidth;
        transformed.height = sourceHeight;
        if (!command.sprite->pixels.empty()) {
            transformed.pixels.resize(static_cast<std::size_t>(sourceWidth) *
                                      static_cast<std::size_t>(sourceHeight));
        }
        if (!command.sprite->indices.empty()) {
            transformed.indices.resize(static_cast<std::size_t>(sourceWidth) *
                                       static_cast<std::size_t>(sourceHeight));
        }
        for (auto y = 0; y < sourceHeight; ++y) {
            for (auto x = 0; x < sourceWidth; ++x) {
                const auto from = static_cast<std::size_t>(regionY + y) *
                                      static_cast<std::size_t>(command.sprite->width) +
                                  static_cast<std::size_t>(regionX + x);
                const auto to =
                    static_cast<std::size_t>(y) * static_cast<std::size_t>(sourceWidth) +
                    static_cast<std::size_t>(x);
                if (!transformed.pixels.empty()) {
                    transformed.pixels[to] = command.sprite->pixels[from];
                }
                if (!transformed.indices.empty()) {
                    transformed.indices[to] = command.sprite->indices[from];
                }
            }
        }

        if (std::fabs(normalizedRotation) > 0.0001F) {
            const auto radians = normalizedRotation * 0.017453292519943F;
            const auto cosine = std::cos(radians);
            const auto sine = std::sin(radians);
            const auto rotatedWidth = std::max(
                1, static_cast<int>(std::ceil(std::fabs(static_cast<float>(sourceWidth) * cosine) +
                                              std::fabs(static_cast<float>(sourceHeight) * sine))));
            const auto rotatedHeight = std::max(
                1,
                static_cast<int>(std::ceil(std::fabs(static_cast<float>(sourceWidth) * sine) +
                                           std::fabs(static_cast<float>(sourceHeight) * cosine))));
            if (static_cast<std::size_t>(rotatedWidth) * static_cast<std::size_t>(rotatedHeight) >
                MaximumSpritePixels) {
                return;
            }
            Sprite rotated;
            rotated.width = rotatedWidth;
            rotated.height = rotatedHeight;
            if (!transformed.pixels.empty()) {
                rotated.pixels.assign(static_cast<std::size_t>(rotatedWidth) *
                                          static_cast<std::size_t>(rotatedHeight),
                                      Color{});
            }
            if (!transformed.indices.empty()) {
                const auto transparent =
                    command.material != nullptr && command.material->transparentIndex.has_value()
                        ? *command.material->transparentIndex
                        : std::numeric_limits<std::uint8_t>::max();
                rotated.indices.assign(static_cast<std::size_t>(rotatedWidth) *
                                           static_cast<std::size_t>(rotatedHeight),
                                       transparent);
            }
            const auto sourceCenterX = static_cast<float>(sourceWidth) * 0.5F;
            const auto sourceCenterY = static_cast<float>(sourceHeight) * 0.5F;
            const auto destinationCenterX = static_cast<float>(rotatedWidth) * 0.5F;
            const auto destinationCenterY = static_cast<float>(rotatedHeight) * 0.5F;
            for (auto y = 0; y < rotatedHeight; ++y) {
                for (auto x = 0; x < rotatedWidth; ++x) {
                    const auto relativeX = static_cast<float>(x) + 0.5F - destinationCenterX;
                    const auto relativeY = static_cast<float>(y) + 0.5F - destinationCenterY;
                    const auto sampledX = static_cast<int>(
                        std::floor(cosine * relativeX + sine * relativeY + sourceCenterX));
                    const auto sampledY = static_cast<int>(
                        std::floor(-sine * relativeX + cosine * relativeY + sourceCenterY));
                    if (sampledX < 0 || sampledY < 0 || sampledX >= sourceWidth ||
                        sampledY >= sourceHeight) {
                        continue;
                    }
                    const auto from =
                        static_cast<std::size_t>(sampledY) * static_cast<std::size_t>(sourceWidth) +
                        static_cast<std::size_t>(sampledX);
                    const auto to =
                        static_cast<std::size_t>(y) * static_cast<std::size_t>(rotatedWidth) +
                        static_cast<std::size_t>(x);
                    if (!rotated.pixels.empty()) {
                        rotated.pixels[to] = transformed.pixels[from];
                    }
                    if (!rotated.indices.empty()) {
                        rotated.indices[to] = transformed.indices[from];
                    }
                }
            }
            transformed = std::move(rotated);
        }

        auto local = command;
        local.sprite = &transformed;
        local.sourceRegion = {};
        local.rotationDegrees = 0.0F;
        draw(local);
        return;
    }
    ++drawCalls_;
    ++spritesSubmitted_;
    if (command.material != nullptr) {
        drawMaterial(command);
        return;
    }
    const auto& sprite = *command.sprite;
    for (auto sourceY = 0; sourceY < sprite.height; ++sourceY) {
        const auto sampledY = command.flipY ? sprite.height - sourceY - 1 : sourceY;
        for (auto sourceX = 0; sourceX < sprite.width; ++sourceX) {
            const auto sampledX = command.flipX ? sprite.width - sourceX - 1 : sourceX;
            const auto offset =
                static_cast<std::size_t>(sampledY) * static_cast<std::size_t>(sprite.width) +
                static_cast<std::size_t>(sampledX);
            const auto color = tint(sprite.pixels[offset], command.tint);
            if (color.a == 0U) {
                continue;
            }
            const auto destinationX = command.x + sourceX * command.scale;
            const auto destinationY = command.y + sourceY * command.scale;
            for (auto scaleY = 0; scaleY < command.scale; ++scaleY) {
                for (auto scaleX = 0; scaleX < command.scale; ++scaleX) {
                    framebuffer_.blendPixel(destinationX + scaleX, destinationY + scaleY, color);
                }
            }
        }
    }
}

void Renderer2D::drawMaterial(const SpriteDraw& command) noexcept {
    const auto& sprite = *command.sprite;
    const auto& material = *command.material;
    const auto indexed = !sprite.indices.empty();
    if (indexed && material.palette.empty()) {
        return;
    }

    const auto combinedTint = tint(command.tint, material.tint);
    const auto fetch = [&](int sourceX, int sourceY) -> Color {
        sourceX = std::clamp(sourceX, 0, sprite.width - 1);
        sourceY = std::clamp(sourceY, 0, sprite.height - 1);
        if (command.flipX) {
            sourceX = sprite.width - sourceX - 1;
        }
        if (command.flipY) {
            sourceY = sprite.height - sourceY - 1;
        }
        const auto offset =
            static_cast<std::size_t>(sourceY) * static_cast<std::size_t>(sprite.width) +
            static_cast<std::size_t>(sourceX);
        if (!indexed) {
            return sprite.pixels[offset];
        }
        const auto paletteIndex = sprite.indices[offset];
        if (material.transparentIndex.has_value() && paletteIndex == *material.transparentIndex) {
            return {0U, 0U, 0U, 0U};
        }
        if (paletteIndex >= material.palette.size()) {
            return {0U, 0U, 0U, 0U};
        }
        return material.palette[paletteIndex];
    };
    const auto interpolate = [](Color a, Color b, float amount) -> Color {
        const auto channel = [amount](std::uint8_t lhs, std::uint8_t rhs) {
            const auto value = static_cast<float>(lhs) +
                               (static_cast<float>(rhs) - static_cast<float>(lhs)) * amount;
            return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
        };
        return {channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b), channel(a.a, b.a)};
    };
    const auto sample = [&](float x, float y) -> Color {
        if (material.colorMode == MaterialColorMode::Flat) {
            return material.flatColor;
        }
        if (material.sampling == MaterialSamplingMode::Nearest) {
            return fetch(static_cast<int>(std::floor(x + 0.5F)),
                         static_cast<int>(std::floor(y + 0.5F)));
        }
        const auto x0 = static_cast<int>(std::floor(x));
        const auto y0 = static_cast<int>(std::floor(y));
        const auto tx = x - static_cast<float>(x0);
        const auto ty = y - static_cast<float>(y0);
        const auto top = interpolate(fetch(x0, y0), fetch(x0 + 1, y0), tx);
        const auto bottom = interpolate(fetch(x0, y0 + 1), fetch(x0 + 1, y0 + 1), tx);
        return interpolate(top, bottom, ty);
    };

    static constexpr std::array<std::uint8_t, 4> Bayer2x2{{0U, 2U, 3U, 1U}};
    static constexpr std::array<std::uint8_t, 16> Bayer4x4{
        {0U, 8U, 2U, 10U, 12U, 4U, 14U, 6U, 3U, 11U, 1U, 9U, 15U, 7U, 13U, 5U}};
    const auto applyDither = [&](Color color, int x, int y) -> Color {
        if (material.dither == MaterialDitherMode::None) {
            return color;
        }
        const auto matrixSize = material.dither == MaterialDitherMode::Ordered2x2 ? 2 : 4;
        const auto levelCount = matrixSize * matrixSize;
        const auto matrixIndex = (y % matrixSize) * matrixSize + (x % matrixSize);
        const auto level = material.dither == MaterialDitherMode::Ordered2x2
                               ? Bayer2x2[static_cast<std::size_t>(matrixIndex)]
                               : Bayer4x4[static_cast<std::size_t>(matrixIndex)];
        const auto threshold = (static_cast<unsigned int>(level) * 256U + 128U) /
                               static_cast<unsigned int>(levelCount);
        if (color.a <= threshold) {
            color.a = 0U;
            return color;
        }
        const auto bias = static_cast<int>(level) * 8 / levelCount - 4;
        const auto adjust = [bias](std::uint8_t channel) {
            return static_cast<std::uint8_t>(std::clamp(static_cast<int>(channel) + bias, 0, 255));
        };
        color.r = adjust(color.r);
        color.g = adjust(color.g);
        color.b = adjust(color.b);
        color.a = 255U;
        return color;
    };
    const auto submit = [&](int x, int y, Color color) {
        if (color.a == 0U) {
            return;
        }
        const auto destination = framebuffer_.pixel(x, y);
        switch (material.blend) {
        case MaterialBlendMode::Opaque:
            framebuffer_.setPixel(x, y, color);
            break;
        case MaterialBlendMode::Alpha:
            framebuffer_.blendPixel(x, y, color);
            break;
        case MaterialBlendMode::Additive: {
            const auto add = [alpha = static_cast<unsigned int>(color.a)](std::uint8_t target,
                                                                          std::uint8_t source) {
                const auto value = static_cast<unsigned int>(target) +
                                   static_cast<unsigned int>(source) * alpha / 255U;
                return static_cast<std::uint8_t>(std::min(255U, value));
            };
            framebuffer_.setPixel(x, y,
                                  {add(destination.r, color.r), add(destination.g, color.g),
                                   add(destination.b, color.b), 255U});
            break;
        }
        case MaterialBlendMode::Multiply: {
            const auto multiply = [alpha = static_cast<unsigned int>(color.a)](
                                      std::uint8_t target, std::uint8_t source) {
                const auto factor = 255U - alpha + static_cast<unsigned int>(source) * alpha / 255U;
                return static_cast<std::uint8_t>(static_cast<unsigned int>(target) * factor / 255U);
            };
            framebuffer_.setPixel(x, y,
                                  {multiply(destination.r, color.r),
                                   multiply(destination.g, color.g),
                                   multiply(destination.b, color.b), 255U});
            break;
        }
        }
    };

    const auto destinationWidth = sprite.width * command.scale;
    const auto destinationHeight = sprite.height * command.scale;
    for (auto destinationY = 0; destinationY < destinationHeight; ++destinationY) {
        const auto sourceY =
            (static_cast<float>(destinationY) + 0.5F) / static_cast<float>(command.scale) - 0.5F;
        for (auto destinationX = 0; destinationX < destinationWidth; ++destinationX) {
            const auto sourceX =
                (static_cast<float>(destinationX) + 0.5F) / static_cast<float>(command.scale) -
                0.5F;
            auto color = tint(sample(sourceX, sourceY), combinedTint);
            if (material.emissiveStrength != 0U) {
                const auto addEmissive = [strength =
                                              static_cast<unsigned int>(material.emissiveStrength)](
                                             std::uint8_t value, std::uint8_t emission) {
                    return static_cast<std::uint8_t>(
                        std::min(255U, static_cast<unsigned int>(value) +
                                           static_cast<unsigned int>(emission) * strength / 255U));
                };
                color.r = addEmissive(color.r, material.emissive.r);
                color.g = addEmissive(color.g, material.emissive.g);
                color.b = addEmissive(color.b, material.emissive.b);
            }
            color = applyDither(color, command.x + destinationX, command.y + destinationY);
            submit(command.x + destinationX, command.y + destinationY, color);
        }
    }
}

bool Renderer2D::submit(const LayeredSpriteDraw& command) noexcept {
    if (queue_.size() >= maximumQueuedSprites_ || command.command.sprite == nullptr ||
        !command.command.sprite->valid() || command.command.scale <= 0 ||
        !std::isfinite(command.parallax.x) || !std::isfinite(command.parallax.y) ||
        command.parallax.x < 0.0F || command.parallax.y < 0.0F) {
        return false;
    }
    queue_.push_back({command, nextSequence_++});
    return true;
}

void Renderer2D::flush(const Vec2 camera, const Rect viewport) noexcept {
    if (!std::isfinite(camera.x) || !std::isfinite(camera.y) || !finiteRect(viewport) ||
        viewport.width <= 0.0F || viewport.height <= 0.0F) {
        spritesCulled_ += static_cast<std::uint32_t>(std::min<std::size_t>(
            queue_.size(), std::numeric_limits<std::uint32_t>::max() - spritesCulled_));
        queue_.clear();
        return;
    }
    std::stable_sort(queue_.begin(), queue_.end(), [](const auto& lhs, const auto& rhs) {
        return std::tuple{lhs.command.uiOverlay ? 1 : 0, lhs.command.sortingLayer,
                          lhs.command.zOrder, lhs.sequence} <
               std::tuple{rhs.command.uiOverlay ? 1 : 0, rhs.command.sortingLayer,
                          rhs.command.zOrder, rhs.sequence};
    });
    for (const auto& queued : queue_) {
        auto command = queued.command.command;
        if (!queued.command.uiOverlay) {
            command.x = static_cast<int>(
                std::lround(static_cast<float>(command.x) - camera.x * queued.command.parallax.x));
            command.y = static_cast<int>(
                std::lround(static_cast<float>(command.y) - camera.y * queued.command.parallax.y));
        }
        if (!overlaps(destinationBounds(command), viewport)) {
            ++spritesCulled_;
            continue;
        }
        draw(command);
    }
    queue_.clear();
}

void Renderer2D::drawText(const BitmapFont& font, const std::string_view text, const int x,
                          const int y, const Color tintColor, const int scale) noexcept {
    if (!font.valid() || text.size() > MaximumTextBytes || scale <= 0 || scale > 16) {
        return;
    }
    auto cursorX = x;
    auto cursorY = y;
    for (const auto byte : text) {
        if (byte == '\n') {
            cursorX = x;
            cursorY += font.glyphHeight * scale;
            continue;
        }
        const auto codepoint = static_cast<std::uint32_t>(static_cast<unsigned char>(byte));
        if (codepoint < font.firstCodepoint || codepoint - font.firstCodepoint >= font.glyphCount) {
            cursorX += (font.glyphWidth + font.horizontalSpacing) * scale;
            continue;
        }
        const auto glyph = codepoint - font.firstCodepoint;
        const auto column = glyph % static_cast<std::uint32_t>(font.columns);
        const auto row = glyph / static_cast<std::uint32_t>(font.columns);
        SpriteDraw command;
        command.sprite = font.atlas;
        command.x = cursorX;
        command.y = cursorY;
        command.scale = scale;
        command.tint = tintColor;
        command.sourceRegion = {
            static_cast<float>(column * static_cast<std::uint32_t>(font.glyphWidth)),
            static_cast<float>(row * static_cast<std::uint32_t>(font.glyphHeight)),
            static_cast<float>(font.glyphWidth), static_cast<float>(font.glyphHeight)};
        draw(command);
        cursorX += (font.glyphWidth + font.horizontalSpacing) * scale;
    }
}

void Renderer2D::drawTilemap(const Tilemap& map, const Vec2 camera, const Rect viewport,
                             const float elapsedSeconds) noexcept {
    static_cast<void>(drawTilemapDetailed(map, camera, viewport, elapsedSeconds));
}

TilemapDrawStats Renderer2D::drawTilemapDetailed(const Tilemap& map, const Vec2 camera,
                                                 const Rect viewport,
                                                 const float elapsedSeconds) noexcept {
    TilemapDrawStats stats;
    if (!map.valid() || !std::isfinite(camera.x) || !std::isfinite(camera.y) ||
        !finiteRect(viewport) || viewport.width <= 0.0F || viewport.height <= 0.0F) {
        return stats;
    }
    const auto viewportLeft = static_cast<int>(viewport.x);
    const auto viewportTop = static_cast<int>(viewport.y);
    const auto layerCount = map.layers.empty() ? 1U : map.layers.size();
    for (std::size_t layerIndex = 0U; layerIndex < layerCount; ++layerIndex) {
        const auto* layer = map.layers.empty() ? nullptr : &map.layers[layerIndex];
        if (layer != nullptr && (!layer->visible || layer->kind != TilemapLayerKind::Tiles)) {
            continue;
        }
        ++stats.layers;
        const auto parallax = layer == nullptr ? Vec2{1.0F, 1.0F} : layer->parallax;
        const auto layerCameraX = camera.x * parallax.x;
        const auto layerCameraY = camera.y * parallax.y;
        const auto rawFirstColumn =
            static_cast<int>(std::floor(layerCameraX / static_cast<float>(map.tileSize)));
        const auto rawFirstRow =
            static_cast<int>(std::floor(layerCameraY / static_cast<float>(map.tileSize)));
        const auto rawLastColumn = static_cast<int>(
            std::floor((layerCameraX + viewport.width - 1.0F) / static_cast<float>(map.tileSize)));
        const auto rawLastRow = static_cast<int>(
            std::floor((layerCameraY + viewport.height - 1.0F) / static_cast<float>(map.tileSize)));
        if (rawLastColumn < 0 || rawLastRow < 0 || rawFirstColumn >= map.width ||
            rawFirstRow >= map.height) {
            stats.culledTiles += static_cast<std::uint32_t>(std::min<std::size_t>(
                boundedArea(map.width, map.height), std::numeric_limits<std::uint32_t>::max()));
            continue;
        }
        const auto firstColumn = std::clamp(rawFirstColumn, 0, map.width - 1);
        const auto firstRow = std::clamp(rawFirstRow, 0, map.height - 1);
        const auto lastColumn = std::clamp(rawLastColumn, 0, map.width - 1);
        const auto lastRow = std::clamp(rawLastRow, 0, map.height - 1);
        const auto visibleColumns = static_cast<std::size_t>(lastColumn - firstColumn + 1);
        const auto visibleRows = static_cast<std::size_t>(lastRow - firstRow + 1);
        const auto visibleCount = visibleColumns * visibleRows;
        stats.culledTiles += static_cast<std::uint32_t>(
            std::min<std::size_t>(boundedArea(map.width, map.height) - visibleCount,
                                  std::numeric_limits<std::uint32_t>::max() - stats.culledTiles));
        const auto firstChunkX = firstColumn / map.chunkSize;
        const auto firstChunkY = firstRow / map.chunkSize;
        const auto lastChunkX = lastColumn / map.chunkSize;
        const auto lastChunkY = lastRow / map.chunkSize;
        stats.chunks += static_cast<std::uint32_t>((lastChunkX - firstChunkX + 1) *
                                                   (lastChunkY - firstChunkY + 1));

        for (auto row = firstRow; row <= lastRow; ++row) {
            for (auto column = firstColumn; column <= lastColumn; ++column) {
                const auto tileIndex =
                    static_cast<std::size_t>(map.tileAt(layerIndex, column, row, elapsedSeconds));
                if (tileIndex >= map.tiles.size()) {
                    continue;
                }
                const auto destinationX = viewportLeft + column * map.tileSize -
                                          static_cast<int>(std::floor(layerCameraX));
                const auto destinationY =
                    viewportTop + row * map.tileSize - static_cast<int>(std::floor(layerCameraY));
                const auto opacity = layer == nullptr ? std::uint8_t{255U} : layer->opacity;
                draw({&map.tiles[tileIndex],
                      destinationX,
                      destinationY,
                      1,
                      false,
                      false,
                      {255U, 255U, 255U, opacity}});
                ++stats.tiles;
            }
        }
    }
    return stats;
}

void Renderer2D::resetCounters() noexcept {
    drawCalls_ = 0;
    spritesSubmitted_ = 0;
    spritesCulled_ = 0;
}

Color Renderer2D::tint(Color source, Color tintColor) noexcept {
    const auto multiply = [](std::uint8_t lhs, std::uint8_t rhs) {
        return static_cast<std::uint8_t>((static_cast<std::uint32_t>(lhs) * rhs + 127U) / 255U);
    };
    return {multiply(source.r, tintColor.r), multiply(source.g, tintColor.g),
            multiply(source.b, tintColor.b), multiply(source.a, tintColor.a)};
}

Sprite makeCheckerSprite(int width, int height, Color first, Color second) {
    Sprite sprite;
    sprite.width = std::max(1, width);
    sprite.height = std::max(1, height);
    sprite.pixels.resize(static_cast<std::size_t>(sprite.width) *
                         static_cast<std::size_t>(sprite.height));
    for (auto y = 0; y < sprite.height; ++y) {
        for (auto x = 0; x < sprite.width; ++x) {
            const auto offset =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(sprite.width) +
                static_cast<std::size_t>(x);
            sprite.pixels[offset] = ((x + y) % 2 == 0) ? first : second;
        }
    }
    return sprite;
}

} // namespace fabgl::rendering
