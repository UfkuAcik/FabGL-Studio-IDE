#include <fabgl/rendering/raycast_renderer.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <tuple>
#include <utility>

namespace fabgl::rendering {

namespace {

constexpr float DegreesToRadians = 0.017453292519943F;

[[nodiscard]] Color shadeColor(Color color, const float amount) noexcept {
    const auto shade = std::clamp(amount, 0.0F, 1.0F);
    color.r = static_cast<std::uint8_t>(static_cast<float>(color.r) * shade);
    color.g = static_cast<std::uint8_t>(static_cast<float>(color.g) * shade);
    color.b = static_cast<std::uint8_t>(static_cast<float>(color.b) * shade);
    return color;
}

[[nodiscard]] Color interpolate(Color source, const Color target, float amount) noexcept {
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

[[nodiscard]] float quantizeFixed(const float value) noexcept {
    constexpr float scale = 65536.0F;
    return std::round(value * scale) / scale;
}

} // namespace

bool RaycastTexture::valid() const noexcept {
    if (width <= 0 || height <= 0 || width > 1024 || height > 1024) {
        return false;
    }
    return pixels.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
}

Color RaycastTexture::sample(float u, float v) const noexcept {
    if (!valid() || !std::isfinite(u) || !std::isfinite(v)) {
        return {};
    }
    u -= std::floor(u);
    v -= std::floor(v);
    const auto x = std::clamp(static_cast<int>(u * static_cast<float>(width)), 0, width - 1);
    const auto y = std::clamp(static_cast<int>(v * static_cast<float>(height)), 0, height - 1);
    return pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                  static_cast<std::size_t>(x)];
}

bool RaycastMap::valid() const noexcept {
    if (width <= 0 || height <= 0 || width > 256 || height > 256 || wallPalette.empty() ||
        wallPalette.size() > 256U || wallTextures.size() > 256U || doors.size() > 256U) {
        return false;
    }
    const auto area = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (cells.size() != area || (!sectorLighting.empty() && sectorLighting.size() != area) ||
        !std::all_of(wallTextures.begin(), wallTextures.end(),
                     [](const auto& texture) { return texture.valid(); })) {
        return false;
    }
    std::set<std::pair<int, int>> doorCells;
    for (const auto& item : doors) {
        if (item.x <= 0 || item.y <= 0 || item.x >= width - 1 || item.y >= height - 1 ||
            !std::isfinite(item.openness) || item.openness < 0.0F || item.openness > 1.0F ||
            cell(item.x, item.y) == 0U || !doorCells.emplace(item.x, item.y).second) {
            return false;
        }
    }
    return true;
}

std::uint8_t RaycastMap::light(const int x, const int y) const noexcept {
    if (sectorLighting.empty() || x < 0 || y < 0 || x >= width || y >= height) {
        return 255U;
    }
    return sectorLighting[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                          static_cast<std::size_t>(x)];
}

const RaycastDoor* RaycastMap::door(const int x, const int y) const noexcept {
    const auto found = std::find_if(doors.begin(), doors.end(), [x, y](const auto& item) {
        return item.x == x && item.y == y;
    });
    return found == doors.end() ? nullptr : &*found;
}

std::uint8_t RaycastMap::cell(int x, int y) const noexcept {
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return 1U;
    }
    return cells[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)];
}

RaycastStats RaycastRenderer::render(const RaycastMap& map, const RaycastCamera& camera,
                                     const std::vector<Billboard>& billboards,
                                     const RaycastRenderSettings& settings) noexcept {
    RaycastStats stats{};
    if (!map.valid() || !std::isfinite(camera.position.x) || !std::isfinite(camera.position.y) ||
        !std::isfinite(camera.direction.x) || !std::isfinite(camera.direction.y) ||
        !std::isfinite(camera.fieldOfViewDegrees) || !std::isfinite(camera.pitch) ||
        !std::isfinite(settings.ambientLight) || !std::isfinite(settings.fogStart) ||
        !std::isfinite(settings.fogEnd) || !std::isfinite(settings.maximumPitch) ||
        settings.maximumDdaSteps == 0U || settings.maximumBillboards == 0U) {
        framebuffer_.clear({64, 0, 64, 255});
        return stats;
    }

    const auto screenWidth = framebuffer_.width();
    const auto screenHeight = framebuffer_.height();
    auto direction = camera.direction;
    const auto directionLength = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    if (directionLength <= 0.000001F) {
        framebuffer_.clear({64, 0, 64, 255});
        return stats;
    }
    direction = {direction.x / directionLength, direction.y / directionLength};
    auto cameraPosition = camera.position;
    if (settings.fixedPointCoordinates) {
        cameraPosition = {quantizeFixed(cameraPosition.x), quantizeFixed(cameraPosition.y)};
        direction = {quantizeFixed(direction.x), quantizeFixed(direction.y)};
        stats.fixedPointPath = true;
    }
    const auto maximumPitch =
        std::clamp(std::fabs(settings.maximumPitch), 0.0F, static_cast<float>(screenHeight) * 0.5F);
    const auto horizon = std::clamp(
        screenHeight / 2 +
            static_cast<int>(std::lround(std::clamp(camera.pitch, -maximumPitch, maximumPitch))),
        0, screenHeight - 1);
    framebuffer_.fillRect(0, 0, screenWidth, horizon, settings.ceilingColor);
    framebuffer_.fillRect(0, horizon, screenWidth, screenHeight - horizon, settings.floorColor);
    depthBuffer_.assign(static_cast<std::size_t>(screenWidth),
                        std::numeric_limits<float>::infinity());

    const auto fieldOfView = std::clamp(camera.fieldOfViewDegrees, 20.0F, 140.0F);
    const auto planeLength = std::tan(fieldOfView * 0.5F * DegreesToRadians);
    const Vec2 cameraPlane{-direction.y * planeLength, direction.x * planeLength};

    if (settings.floorAndCeiling) {
        const Vec2 leftRay{direction.x - cameraPlane.x, direction.y - cameraPlane.y};
        const Vec2 rightRay{direction.x + cameraPlane.x, direction.y + cameraPlane.y};
        for (auto y = 0; y < screenHeight; ++y) {
            const auto rowOffset = y - horizon;
            if (rowOffset == 0) {
                continue;
            }
            const auto distance = (static_cast<float>(screenHeight) * 0.5F) /
                                  std::fabs(static_cast<float>(rowOffset));
            const auto stepX =
                distance * (rightRay.x - leftRay.x) / static_cast<float>(screenWidth);
            const auto stepY =
                distance * (rightRay.y - leftRay.y) / static_cast<float>(screenWidth);
            auto worldX = cameraPosition.x + distance * leftRay.x;
            auto worldY = cameraPosition.y + distance * leftRay.y;
            const auto* texture = rowOffset > 0 ? settings.floorTexture : settings.ceilingTexture;
            const auto base = rowOffset > 0 ? settings.floorColor : settings.ceilingColor;
            for (auto x = 0; x < screenWidth; ++x) {
                auto color =
                    texture != nullptr && texture->valid() ? texture->sample(worldX, worldY) : base;
                if (settings.distanceFog && settings.fogEnd > settings.fogStart) {
                    color = interpolate(color, settings.fogColor,
                                        (distance - settings.fogStart) /
                                            (settings.fogEnd - settings.fogStart));
                }
                framebuffer_.setPixel(x, y, color);
                worldX += stepX;
                worldY += stepY;
                ++stats.floorCeilingPixels;
            }
        }
    }

    const auto internalWidth = settings.internalWidth <= 0
                                   ? screenWidth
                                   : std::clamp(settings.internalWidth, 1, screenWidth);
    if (lookupWidth_ != internalWidth) {
        cameraXLookup_.resize(static_cast<std::size_t>(internalWidth));
        for (auto ray = 0; ray < internalWidth; ++ray) {
            cameraXLookup_[static_cast<std::size_t>(ray)] =
                2.0F * static_cast<float>(ray) / static_cast<float>(internalWidth) - 1.0F;
        }
        lookupWidth_ = internalWidth;
    }
    stats.lookupEntries = static_cast<std::uint32_t>(cameraXLookup_.size());
    for (auto ray = 0; ray < internalWidth; ++ray) {
        ++stats.rays;
        const auto cameraX = cameraXLookup_[static_cast<std::size_t>(ray)];
        Vec2 rayDirection{direction.x + cameraPlane.x * cameraX,
                          direction.y + cameraPlane.y * cameraX};
        if (settings.fixedPointCoordinates) {
            rayDirection = {quantizeFixed(rayDirection.x), quantizeFixed(rayDirection.y)};
        }
        auto mapX = static_cast<int>(cameraPosition.x);
        auto mapY = static_cast<int>(cameraPosition.y);
        const auto deltaX = std::fabs(rayDirection.x) < 0.000001F
                                ? std::numeric_limits<float>::infinity()
                                : std::fabs(1.0F / rayDirection.x);
        const auto deltaY = std::fabs(rayDirection.y) < 0.000001F
                                ? std::numeric_limits<float>::infinity()
                                : std::fabs(1.0F / rayDirection.y);
        const auto stepX = rayDirection.x < 0.0F ? -1 : 1;
        const auto stepY = rayDirection.y < 0.0F ? -1 : 1;
        auto sideX = rayDirection.x < 0.0F
                         ? (cameraPosition.x - static_cast<float>(mapX)) * deltaX
                         : (static_cast<float>(mapX + 1) - cameraPosition.x) * deltaX;
        auto sideY = rayDirection.y < 0.0F
                         ? (cameraPosition.y - static_cast<float>(mapY)) * deltaY
                         : (static_cast<float>(mapY + 1) - cameraPosition.y) * deltaY;
        auto hit = std::uint8_t{0};
        auto hitVerticalSide = false;
        const RaycastDoor* hitDoor = nullptr;
        float wallCoordinate = 0.0F;
        for (std::uint16_t step = 0U; step < settings.maximumDdaSteps && hit == 0U; ++step) {
            ++stats.ddaSteps;
            if (sideX < sideY) {
                sideX += deltaX;
                mapX += stepX;
                hitVerticalSide = false;
            } else {
                sideY += deltaY;
                mapY += stepY;
                hitVerticalSide = true;
            }
            hit = map.cell(mapX, mapY);
            if (hit == 0U) {
                continue;
            }
            const auto distance = hitVerticalSide ? sideY - deltaY : sideX - deltaX;
            wallCoordinate = hitVerticalSide ? cameraPosition.x + distance * rayDirection.x
                                             : cameraPosition.y + distance * rayDirection.y;
            wallCoordinate -= std::floor(wallCoordinate);
            if (const auto* candidate = map.door(mapX, mapY); candidate != nullptr) {
                // Doors slide along their wall plane. The uncovered portion is traversable by
                // individual rays, so animation affects both view and occlusion deterministically.
                if (candidate->openness >= 1.0F || wallCoordinate < candidate->openness) {
                    hit = 0U;
                    continue;
                }
                hitDoor = candidate;
            }
        }

        if (hit == 0U) {
            continue;
        }

        auto distance = hitVerticalSide ? sideY - deltaY : sideX - deltaX;
        distance = std::max(distance, 0.01F);
        const auto lineHeight = static_cast<int>(static_cast<float>(screenHeight) / distance);
        const auto drawStart = std::max(0, horizon - lineHeight / 2);
        const auto drawEnd = std::min(screenHeight - 1, horizon + lineHeight / 2);
        auto color = map.wallPalette[static_cast<std::size_t>(hit) % map.wallPalette.size()];
        const auto ambient = std::clamp(settings.ambientLight, 0.0F, 1.0F);
        const auto sector = static_cast<float>(map.light(mapX, mapY)) / 255.0F;
        const auto shade = std::clamp(1.0F - distance / 18.0F, ambient, 1.0F) *
                           (ambient + (1.0F - ambient) * sector) * (hitVerticalSide ? 0.72F : 1.0F);
        color = shadeColor(color, shade);
        if (settings.distanceFog && settings.fogEnd > settings.fogStart) {
            color =
                interpolate(color, settings.fogColor,
                            (distance - settings.fogStart) / (settings.fogEnd - settings.fogStart));
        }
        if ((!hitVerticalSide && rayDirection.x > 0.0F) ||
            (hitVerticalSide && rayDirection.y < 0.0F)) {
            wallCoordinate = 1.0F - wallCoordinate;
        }
        const auto textureIndex =
            map.wallTextures.empty() ? 0U : static_cast<std::size_t>(hit) % map.wallTextures.size();
        const auto* texture = map.wallTextures.empty() ? nullptr : &map.wallTextures[textureIndex];
        const auto firstColumn = ray * screenWidth / internalWidth;
        const auto onePastColumn =
            std::max(firstColumn + 1, (ray + 1) * screenWidth / internalWidth);
        for (auto column = firstColumn; column < onePastColumn && column < screenWidth; ++column) {
            depthBuffer_[static_cast<std::size_t>(column)] = distance;
            if (texture == nullptr) {
                framebuffer_.fillRect(column, drawStart, 1, drawEnd - drawStart + 1, color);
                continue;
            }
            ++stats.texturedWallColumns;
            const auto drawnHeight = std::max(1, drawEnd - drawStart + 1);
            for (auto y = drawStart; y <= drawEnd; ++y) {
                const auto v = static_cast<float>(y - drawStart) / static_cast<float>(drawnHeight);
                auto textured = shadeColor(texture->sample(wallCoordinate, v), shade);
                if (settings.distanceFog && settings.fogEnd > settings.fogStart) {
                    textured = interpolate(textured, settings.fogColor,
                                           (distance - settings.fogStart) /
                                               (settings.fogEnd - settings.fogStart));
                }
                framebuffer_.blendPixel(column, y, textured);
            }
        }
        if (hitDoor != nullptr) {
            ++stats.doorsHit;
            if (hitDoor->secret) {
                ++stats.secretWallsHit;
            }
        }
    }

    struct ProjectedBillboard final {
        float distanceSquared = 0.0F;
        Billboard billboard{};
    };
    std::vector<ProjectedBillboard> sorted;
    const auto billboardLimit =
        std::min<std::size_t>(billboards.size(), settings.maximumBillboards);
    sorted.reserve(billboardLimit);
    for (std::size_t index = 0U; index < billboardLimit; ++index) {
        const auto& billboard = billboards[index];
        if (!std::isfinite(billboard.position.x) || !std::isfinite(billboard.position.y) ||
            !std::isfinite(billboard.radius) || billboard.radius <= 0.0F ||
            (billboard.texture != nullptr && !billboard.texture->valid())) {
            continue;
        }
        const auto dx = billboard.position.x - cameraPosition.x;
        const auto dy = billboard.position.y - cameraPosition.y;
        sorted.push_back({dx * dx + dy * dy, billboard});
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.distanceSquared > rhs.distanceSquared;
    });

    const auto inverseDeterminant =
        1.0F / (cameraPlane.x * direction.y - direction.x * cameraPlane.y);
    for (const auto& projected : sorted) {
        const auto relativeX = projected.billboard.position.x - cameraPosition.x;
        const auto relativeY = projected.billboard.position.y - cameraPosition.y;
        const auto transformX =
            inverseDeterminant * (direction.y * relativeX - direction.x * relativeY);
        const auto transformY =
            inverseDeterminant * (-cameraPlane.y * relativeX + cameraPlane.x * relativeY);
        if (transformY <= 0.05F) {
            continue;
        }
        ++stats.billboards;
        if (projected.billboard.kind == Billboard::Kind::Enemy) {
            ++stats.enemies;
        } else if (projected.billboard.kind == Billboard::Kind::Item) {
            ++stats.items;
        }
        const auto screenX = static_cast<int>((static_cast<float>(screenWidth) * 0.5F) *
                                              (1.0F + transformX / transformY));
        const auto size =
            std::max(1, static_cast<int>(static_cast<float>(screenHeight) *
                                         projected.billboard.radius * 2.0F / transformY));
        const auto left = screenX - size / 2;
        const auto top = horizon - size / 2;
        for (auto x = 0; x < size; ++x) {
            const auto destinationX = left + x;
            if (destinationX < 0 || destinationX >= screenWidth ||
                transformY >= depthBuffer_[static_cast<std::size_t>(destinationX)]) {
                continue;
            }
            for (auto y = 0; y < size; ++y) {
                Color spriteColor{};
                if (projected.billboard.texture != nullptr) {
                    spriteColor = projected.billboard.texture->sample(
                        static_cast<float>(x) / static_cast<float>(size),
                        static_cast<float>(y) / static_cast<float>(size));
                    spriteColor =
                        shadeColor(spriteColor, std::clamp(1.0F - transformY / 22.0F, 0.3F, 1.0F));
                } else {
                    const auto normalizedX =
                        (2.0F * static_cast<float>(x) / static_cast<float>(size)) - 1.0F;
                    const auto normalizedY =
                        (2.0F * static_cast<float>(y) / static_cast<float>(size)) - 1.0F;
                    if (normalizedX * normalizedX + normalizedY * normalizedY > 1.0F) {
                        continue;
                    }
                    spriteColor = projected.billboard.color;
                }
                if (spriteColor.a != 0U) {
                    framebuffer_.blendPixel(destinationX, top + y, spriteColor);
                    if (spriteColor.a < 255U) {
                        ++stats.transparentSpritePixels;
                    }
                }
            }
        }
    }

    if (settings.weaponOverlay) {
        const auto weaponWidth =
            settings.weaponTexture != nullptr && settings.weaponTexture->valid()
                ? std::min(screenWidth, settings.weaponTexture->width * 2)
                : std::max(8, screenWidth / 4);
        const auto weaponHeight =
            settings.weaponTexture != nullptr && settings.weaponTexture->valid()
                ? std::min(screenHeight, settings.weaponTexture->height * 2)
                : std::max(8, screenHeight / 3);
        const auto weaponLeft = (screenWidth - weaponWidth) / 2;
        const auto weaponTop = screenHeight - weaponHeight;
        for (auto y = 0; y < weaponHeight; ++y) {
            for (auto x = 0; x < weaponWidth; ++x) {
                auto color = settings.weaponColor;
                if (settings.weaponTexture != nullptr && settings.weaponTexture->valid()) {
                    color = settings.weaponTexture->sample(
                        static_cast<float>(x) / static_cast<float>(weaponWidth),
                        static_cast<float>(y) / static_cast<float>(weaponHeight));
                }
                framebuffer_.blendPixel(weaponLeft + x, weaponTop + y, color);
            }
        }
    }

    if (settings.minimap) {
        const auto shownWidth = std::min(map.width, 64);
        const auto shownHeight = std::min(map.height, 64);
        const auto scale =
            shownWidth * 2 <= screenWidth / 2 && shownHeight * 2 <= screenHeight / 2 ? 2 : 1;
        const auto originX = screenWidth - shownWidth * scale - 2;
        constexpr int originY = 2;
        for (auto y = 0; y < shownHeight; ++y) {
            for (auto x = 0; x < shownWidth; ++x) {
                const auto cell = map.cell(x, y);
                const auto color =
                    cell == 0U
                        ? Color{18U, 18U, 20U, 210U}
                        : map.wallPalette[static_cast<std::size_t>(cell) % map.wallPalette.size()];
                framebuffer_.fillRect(originX + x * scale, originY + y * scale, scale, scale,
                                      color);
            }
        }
        const auto playerX = static_cast<int>(cameraPosition.x);
        const auto playerY = static_cast<int>(cameraPosition.y);
        if (playerX >= 0 && playerY >= 0 && playerX < shownWidth && playerY < shownHeight) {
            framebuffer_.fillRect(originX + playerX * scale, originY + playerY * scale, scale,
                                  scale, {255U, 255U, 255U, 255U});
        }
    }
    return stats;
}

RaycastMap makeDemoRaycastMap() {
    constexpr int width = 16;
    constexpr int height = 16;
    RaycastMap map;
    map.width = width;
    map.height = height;
    map.cells.assign(static_cast<std::size_t>(width * height), 0U);
    map.wallPalette = {{28, 28, 34, 255},
                       {205, 64, 56, 255},
                       {61, 143, 205, 255},
                       {218, 173, 62, 255},
                       {96, 180, 90, 255}};
    for (auto y = 0; y < height; ++y) {
        for (auto x = 0; x < width; ++x) {
            if (x == 0 || y == 0 || x == width - 1 || y == height - 1) {
                map.cells[static_cast<std::size_t>(y * width + x)] = 1U;
            }
        }
    }
    for (auto x = 3; x < 13; ++x) {
        map.cells[static_cast<std::size_t>(6 * width + x)] = (x == 8) ? 0U : 2U;
    }
    for (auto y = 8; y < 14; ++y) {
        map.cells[static_cast<std::size_t>(y * width + 11)] = 3U;
    }
    map.cells[static_cast<std::size_t>(10 * width + 5)] = 4U;
    return map;
}

} // namespace fabgl::rendering
