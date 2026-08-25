#include <fabgl/rendering/racer_renderer.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>

namespace fabgl::rendering {

namespace {

Color interpolateColor(const Color source, const Color target, float amount) noexcept {
    amount = std::clamp(amount, 0.0F, 1.0F);
    const auto channel = [amount](const std::uint8_t from, const std::uint8_t to) {
        return static_cast<std::uint8_t>(
            std::clamp(std::lround(static_cast<float>(from) +
                                   (static_cast<float>(to) - static_cast<float>(from)) * amount),
                       0L, 255L));
    };
    return {channel(source.r, target.r), channel(source.g, target.g), channel(source.b, target.b),
            channel(source.a, target.a)};
}

Color tintColor(const Color source, const Color tint) noexcept {
    return {static_cast<std::uint8_t>(static_cast<std::uint16_t>(source.r) * tint.r / 255U),
            static_cast<std::uint8_t>(static_cast<std::uint16_t>(source.g) * tint.g / 255U),
            static_cast<std::uint8_t>(static_cast<std::uint16_t>(source.b) * tint.b / 255U),
            static_cast<std::uint8_t>(static_cast<std::uint16_t>(source.a) * tint.a / 255U)};
}

void drawSpriteNearest(Framebuffer& framebuffer, const Sprite& sprite, const int x, const int y,
                       const int width, const int height, const Color tint) noexcept {
    if (!sprite.valid() || sprite.pixels.empty() || width <= 0 || height <= 0)
        return;
    for (auto row = 0; row < height; ++row) {
        const auto sourceY = std::clamp(row * sprite.height / height, 0, sprite.height - 1);
        for (auto column = 0; column < width; ++column) {
            const auto sourceX = std::clamp(column * sprite.width / width, 0, sprite.width - 1);
            const auto color =
                tintColor(sprite.pixels[static_cast<std::size_t>(sourceY) *
                                            static_cast<std::size_t>(sprite.width) +
                                        static_cast<std::size_t>(sourceX)],
                          tint);
            if (color.a == 255U)
                framebuffer.setPixel(x + column, y + row, color);
            else if (color.a != 0U)
                framebuffer.blendPixel(x + column, y + row, color);
        }
    }
}

} // namespace

bool Mode7Texture::valid() const noexcept {
    if (width <= 0 || height <= 0 || width > 2048 || height > 2048) {
        return false;
    }
    return pixels.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
}

Color Mode7Texture::sample(const float x, const float y) const noexcept {
    if (!valid() || !std::isfinite(x) || !std::isfinite(y)) {
        return {};
    }
    auto column = static_cast<int>(std::fmod(std::floor(x), static_cast<float>(width)));
    auto row = static_cast<int>(std::fmod(std::floor(y), static_cast<float>(height)));
    if (column < 0) {
        column += width;
    }
    if (row < 0) {
        row += height;
    }
    return pixels[static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
                  static_cast<std::size_t>(column)];
}

RacerStats RacerRenderer::render(const std::vector<RoadSegment>& track,
                                 const RacerCamera& camera) noexcept {
    RacerStats stats{};
    const auto width = framebuffer_.width();
    const auto height = framebuffer_.height();
    const auto horizon = height / 3;
    framebuffer_.fillRect(0, 0, width, horizon, {76, 157, 220, 255});
    framebuffer_.fillRect(0, horizon, width, height - horizon, {44, 124, 52, 255});
    if (track.empty()) {
        return stats;
    }

    auto accumulatedCurve = 0.0F;
    auto accumulatedHill = 0.0F;
    const auto baseSegment =
        static_cast<std::size_t>(std::max(0.0F, camera.distance)) % track.size();
    for (auto screenY = horizon; screenY < height; ++screenY) {
        ++stats.scanlines;
        const auto normalized =
            static_cast<float>(screenY - horizon + 1) / static_cast<float>(height - horizon);
        const auto depth = 1.0F / std::max(0.025F, normalized * normalized);
        const auto segmentOffset = static_cast<std::size_t>(depth * 1.65F);
        const auto& segment = track[(baseSegment + segmentOffset) % track.size()];
        ++stats.segmentsSampled;
        accumulatedCurve += segment.curve * normalized * normalized;
        accumulatedHill += segment.hill * 0.00012F;

        const auto perspectiveWidth =
            static_cast<float>(width) * 0.08F + static_cast<float>(width) * 0.74F * normalized;
        const auto roadHalf = perspectiveWidth * std::clamp(segment.width, 0.45F, 1.25F) * 0.5F;
        const auto center = static_cast<float>(width) * 0.5F +
                            accumulatedCurve * static_cast<float>(width) * 0.02F -
                            camera.lateral * perspectiveWidth * 0.45F;
        const auto stripePeriod = (segmentOffset / 3U) % 2U;
        auto grass = segment.grass;
        if (stripePeriod == 0U) {
            grass.r = static_cast<std::uint8_t>(std::min(255, static_cast<int>(grass.r) + 12));
            grass.g = static_cast<std::uint8_t>(std::min(255, static_cast<int>(grass.g) + 12));
        }
        framebuffer_.fillRect(0, screenY, width, 1, grass);
        const auto left = static_cast<int>(center - roadHalf);
        const auto right = static_cast<int>(center + roadHalf);
        const auto rumbleWidth = std::max(1, static_cast<int>(roadHalf * 0.08F));
        auto rumble = stripePeriod == 0U ? segment.rumble : Color{205, 45, 45, 255};
        framebuffer_.fillRect(left - rumbleWidth, screenY, rumbleWidth, 1, rumble);
        framebuffer_.fillRect(left, screenY, right - left, 1, segment.road);
        framebuffer_.fillRect(right, screenY, rumbleWidth, 1, rumble);

        if ((segmentOffset / 5U) % 2U == 0U) {
            const auto markerWidth = std::max(1, static_cast<int>(roadHalf * 0.025F));
            framebuffer_.fillRect(static_cast<int>(center) - markerWidth / 2, screenY, markerWidth,
                                  1, {235, 225, 160, 255});
        }
    }

    const auto hillShift = static_cast<int>(accumulatedHill);
    if (hillShift != 0) {
        framebuffer_.drawLine(0, horizon + hillShift, width - 1, horizon + hillShift,
                              {212, 232, 240, 255});
    }
    return stats;
}

RacerStats RacerRenderer::render(const RacerTrackAsset& track, const RacerCamera& camera,
                                 const RacerSpriteResolver& sprites) noexcept {
    if (track.segments.empty())
        return render(track.segments, camera);

    auto segmentCamera = camera;
    const auto segmentLength = std::isfinite(track.segmentLength) && track.segmentLength > 0.0F
                                   ? track.segmentLength
                                   : 1.0F;
    segmentCamera.distance = camera.distance / segmentLength;
    auto stats = render(track.segments, segmentCamera);
    const auto width = framebuffer_.width();
    const auto height = framebuffer_.height();
    const auto horizon = height / 3;
    std::map<AssetGuid, std::shared_ptr<const Sprite>> resolvedSprites;
    const auto resolveSprite = [&](const AssetGuid guid) -> const Sprite* {
        const auto known = resolvedSprites.find(guid);
        if (known != resolvedSprites.end())
            return known->second.get();
        auto resolved = sprites && !guid.isNil() ? sprites(guid) : std::shared_ptr<const Sprite>{};
        if (resolved && resolved->valid())
            ++stats.resolvedSpriteAssets;
        else
            ++stats.missingSpriteAssets;
        return resolvedSprites.emplace(guid, std::move(resolved)).first->second.get();
    };

    for (const auto& layer : track.backgroundLayers) {
        if (!std::isfinite(layer.scale) || layer.scale <= 0.0F ||
            !std::isfinite(layer.verticalOffset) || !std::isfinite(layer.parallax)) {
            continue;
        }
        const auto bandHeight =
            std::clamp(static_cast<int>(std::lround(layer.scale * 3.0F)), 1, std::max(1, horizon));
        const auto y =
            std::clamp(horizon - bandHeight - static_cast<int>(std::lround(layer.verticalOffset)),
                       0, std::max(0, horizon - bandHeight));
        const auto shift =
            width > 0
                ? static_cast<int>(std::lround(segmentCamera.distance * layer.parallax)) % width
                : 0;
        const auto split = width > 0 ? (shift + width) % width : 0;
        const auto* sprite = resolveSprite(layer.sprite);
        if (sprite != nullptr && sprite->valid() && !sprite->pixels.empty()) {
            for (auto column = 0; column < width; ++column) {
                const auto wrapped = (column + split) % width;
                const auto sourceX = wrapped * sprite->width / std::max(1, width);
                for (auto row = 0; row < bandHeight; ++row) {
                    const auto sourceY = row * sprite->height / bandHeight;
                    framebuffer_.blendPixel(
                        column, y + row,
                        tintColor(sprite->pixels[static_cast<std::size_t>(sourceY) *
                                                       static_cast<std::size_t>(sprite->width) +
                                                   static_cast<std::size_t>(sourceX)],
                                  layer.tint));
                }
            }
        } else {
            auto alternate = layer.tint;
            alternate.r = static_cast<std::uint8_t>(alternate.r * 4U / 5U);
            alternate.g = static_cast<std::uint8_t>(alternate.g * 4U / 5U);
            alternate.b = static_cast<std::uint8_t>(alternate.b * 4U / 5U);
            framebuffer_.fillRect(0, y, split, bandHeight, alternate);
            framebuffer_.fillRect(split, y, width - split, bandHeight, layer.tint);
        }
        ++stats.backgroundLayersDrawn;
    }

    const auto baseSegment =
        static_cast<std::size_t>(std::max(0.0F, segmentCamera.distance)) % track.segments.size();
    const auto drawDistance = std::min<std::size_t>(track.segments.size(), 96U);
    const auto drawMarker = [&](const std::uint32_t segment, const float lateral, const float scale,
                                const Color color, const AssetGuid spriteGuid) {
        const auto segmentIndex = static_cast<std::size_t>(segment) % track.segments.size();
        const auto relative =
            (segmentIndex + track.segments.size() - baseSegment) % track.segments.size();
        if (relative > drawDistance)
            return false;
        const auto proximity =
            1.0F - static_cast<float>(relative) /
                       static_cast<float>(std::max<std::size_t>(1U, drawDistance));
        const auto roadScale = 0.08F + 0.74F * proximity;
        const auto segmentWidth = std::clamp(track.segments[segmentIndex].width, 0.45F, 1.25F);
        const auto roadHalf = static_cast<float>(width) * roadScale * segmentWidth * 0.5F;
        const auto center =
            static_cast<float>(width) * 0.5F - segmentCamera.lateral * roadHalf * 0.9F;
        const auto x = static_cast<int>(std::lround(center + lateral * roadHalf));
        const auto y =
            horizon + static_cast<int>(std::lround(proximity * proximity *
                                                   static_cast<float>(height - horizon - 1)));
        const auto markerHeight = std::clamp(
            static_cast<int>(std::lround((2.0F + proximity * 14.0F) * std::max(0.1F, scale))), 1,
            std::max(1, height - horizon));
        const auto markerWidth = std::max(1, markerHeight / 2);
        const auto* sprite = resolveSprite(spriteGuid);
        if (sprite != nullptr && sprite->valid() && !sprite->pixels.empty())
            drawSpriteNearest(framebuffer_, *sprite, x - markerWidth / 2, y - markerHeight,
                              markerWidth, markerHeight, color);
        else
            framebuffer_.fillRect(x - markerWidth / 2, y - markerHeight, markerWidth, markerHeight,
                                  color);
        return true;
    };

    for (const auto& object : track.roadsideObjects) {
        if (drawMarker(object.segment, object.lateral, object.scale, object.tint, object.sprite))
            ++stats.roadsideObjectsDrawn;
    }
    for (const auto& opponent : track.opponentSpawns) {
        const auto skill = std::clamp(opponent.skill, 0.0F, 1.0F);
        const Color color{220U, static_cast<std::uint8_t>(55.0F + skill * 120.0F), 55U, 255U};
        if (drawMarker(opponent.segment, opponent.lateral, 1.0F, color, opponent.sprite))
            ++stats.opponentsDrawn;
    }

    if (track.weather.kind != RacerWeatherKind::Clear && std::isfinite(track.weather.intensity) &&
        track.weather.intensity > 0.0F) {
        auto weatherTint = track.weather.tint;
        weatherTint.a = static_cast<std::uint8_t>(
            std::clamp(std::lround(track.weather.intensity * 96.0F), 0L, 192L));
        for (auto y = 0; y < height; ++y) {
            for (auto x = 0; x < width; ++x) {
                framebuffer_.blendPixel(x, y, weatherTint);
                ++stats.weatherPixelsBlended;
            }
        }
    }
    return stats;
}

Mode7Stats RacerRenderer::renderMode7(const Mode7Texture& texture, const Mode7Camera& camera,
                                      const Mode7Settings& settings) noexcept {
    Mode7Stats stats;
    const auto width = framebuffer_.width();
    const auto height = framebuffer_.height();
    if (!texture.valid() || !std::isfinite(camera.position.x) ||
        !std::isfinite(camera.position.y) || !std::isfinite(camera.headingRadians) ||
        !std::isfinite(camera.altitude) || camera.altitude <= 0.0F ||
        !std::isfinite(settings.scale) || settings.scale <= 0.0F ||
        !std::isfinite(settings.maximumDistance) || settings.maximumDistance <= 0.0F ||
        !std::isfinite(settings.fogStart) || !std::isfinite(settings.fogEnd)) {
        framebuffer_.clear({64U, 0U, 64U, 255U});
        return stats;
    }
    const auto horizon =
        settings.horizon < 0 ? height / 3 : std::clamp(settings.horizon, 0, height - 1);
    framebuffer_.fillRect(0, 0, width, horizon, settings.skyColor);
    const auto internalWidth =
        settings.internalWidth <= 0 ? width : std::clamp(settings.internalWidth, 1, width);
    const auto forward = Vec2{std::cos(camera.headingRadians), std::sin(camera.headingRadians)};
    const auto right = Vec2{-forward.y, forward.x};
    const auto fogRange = settings.fogEnd - settings.fogStart;
    for (auto y = horizon; y < height; ++y) {
        ++stats.scanlines;
        const auto row = std::max(1.0F, static_cast<float>(y - horizon + 1));
        const auto distance =
            std::min(settings.maximumDistance,
                     camera.altitude * static_cast<float>(height - horizon) * settings.scale / row);
        for (auto sample = 0; sample < internalWidth; ++sample) {
            const auto normalizedX =
                (static_cast<float>(sample) + 0.5F) / static_cast<float>(internalWidth) - 0.5F;
            const auto lateral = normalizedX * distance * 1.6F;
            const auto world = camera.position + forward * distance + right * lateral;
            auto color = texture.sample(world.x, world.y);
            if (fogRange > 0.0F && distance > settings.fogStart) {
                color = interpolateColor(color, settings.fogColor,
                                         (distance - settings.fogStart) / fogRange);
                ++stats.foggedSamples;
            }
            const auto firstColumn = sample * width / internalWidth;
            const auto onePastColumn =
                std::max(firstColumn + 1, (sample + 1) * width / internalWidth);
            framebuffer_.fillRect(firstColumn, y, onePastColumn - firstColumn, 1, color);
            ++stats.samples;
        }
    }
    return stats;
}

std::vector<RoadSegment> makeDemoTrack() {
    std::vector<RoadSegment> track(240U);
    for (std::size_t index = 0; index < track.size(); ++index) {
        auto& segment = track[index];
        const auto phase = static_cast<float>(index) * 0.065F;
        segment.curve = std::sin(phase) * 0.018F + std::sin(phase * 0.37F) * 0.012F;
        segment.hill = std::sin(phase * 0.55F) * 0.8F;
        segment.width = 0.88F + std::sin(phase * 0.21F) * 0.08F;
        if ((index / 30U) % 2U != 0U) {
            segment.grass = {80, 118, 45, 255};
        }
    }
    return track;
}

} // namespace fabgl::rendering
