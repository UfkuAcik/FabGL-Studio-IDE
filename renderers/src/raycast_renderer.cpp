#include <fabgl/rendering/raycast_renderer.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace fabgl::rendering {

bool RaycastMap::valid() const noexcept {
    if (width <= 0 || height <= 0 || wallPalette.empty()) {
        return false;
    }
    return cells.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
}

std::uint8_t RaycastMap::cell(int x, int y) const noexcept {
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return 1U;
    }
    return cells[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)];
}

RaycastStats RaycastRenderer::render(const RaycastMap& map, const RaycastCamera& camera,
                                     const std::vector<Billboard>& billboards) noexcept {
    RaycastStats stats{};
    if (!map.valid()) {
        framebuffer_.clear({64, 0, 64, 255});
        return stats;
    }

    const auto screenWidth = framebuffer_.width();
    const auto screenHeight = framebuffer_.height();
    const auto horizon =
        std::clamp(screenHeight / 2 + static_cast<int>(camera.pitch), 0, screenHeight - 1);
    framebuffer_.fillRect(0, 0, screenWidth, horizon, {35, 42, 58, 255});
    framebuffer_.fillRect(0, horizon, screenWidth, screenHeight - horizon, {40, 34, 28, 255});
    depthBuffer_.assign(static_cast<std::size_t>(screenWidth),
                        std::numeric_limits<float>::infinity());

    const auto planeLength = std::tan(camera.fieldOfViewDegrees * 0.5F * 0.017453292519943F);
    const Vec2 cameraPlane{-camera.direction.y * planeLength, camera.direction.x * planeLength};
    for (auto column = 0; column < screenWidth; ++column) {
        ++stats.rays;
        const auto cameraX =
            2.0F * static_cast<float>(column) / static_cast<float>(screenWidth) - 1.0F;
        const Vec2 rayDirection{camera.direction.x + cameraPlane.x * cameraX,
                                camera.direction.y + cameraPlane.y * cameraX};
        auto mapX = static_cast<int>(camera.position.x);
        auto mapY = static_cast<int>(camera.position.y);
        const auto deltaX = std::fabs(rayDirection.x) < 0.000001F
                                ? std::numeric_limits<float>::infinity()
                                : std::fabs(1.0F / rayDirection.x);
        const auto deltaY = std::fabs(rayDirection.y) < 0.000001F
                                ? std::numeric_limits<float>::infinity()
                                : std::fabs(1.0F / rayDirection.y);
        const auto stepX = rayDirection.x < 0.0F ? -1 : 1;
        const auto stepY = rayDirection.y < 0.0F ? -1 : 1;
        auto sideX = rayDirection.x < 0.0F
                         ? (camera.position.x - static_cast<float>(mapX)) * deltaX
                         : (static_cast<float>(mapX + 1) - camera.position.x) * deltaX;
        auto sideY = rayDirection.y < 0.0F
                         ? (camera.position.y - static_cast<float>(mapY)) * deltaY
                         : (static_cast<float>(mapY + 1) - camera.position.y) * deltaY;
        auto hit = std::uint8_t{0};
        auto hitVerticalSide = false;
        constexpr int maximumSteps = 256;
        for (auto step = 0; step < maximumSteps && hit == 0U; ++step) {
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
        }

        auto distance = hitVerticalSide ? sideY - deltaY : sideX - deltaX;
        distance = std::max(distance, 0.01F);
        depthBuffer_[static_cast<std::size_t>(column)] = distance;
        const auto lineHeight = static_cast<int>(static_cast<float>(screenHeight) / distance);
        const auto drawStart = std::max(0, horizon - lineHeight / 2);
        const auto drawEnd = std::min(screenHeight - 1, horizon + lineHeight / 2);
        auto color = map.wallPalette[static_cast<std::size_t>(hit) % map.wallPalette.size()];
        const auto shade =
            std::clamp(1.0F - distance / 18.0F, 0.22F, 1.0F) * (hitVerticalSide ? 0.72F : 1.0F);
        color.r = static_cast<std::uint8_t>(static_cast<float>(color.r) * shade);
        color.g = static_cast<std::uint8_t>(static_cast<float>(color.g) * shade);
        color.b = static_cast<std::uint8_t>(static_cast<float>(color.b) * shade);
        framebuffer_.fillRect(column, drawStart, 1, drawEnd - drawStart + 1, color);
    }

    struct ProjectedBillboard final {
        float distanceSquared = 0.0F;
        Billboard billboard{};
    };
    std::vector<ProjectedBillboard> sorted;
    sorted.reserve(billboards.size());
    for (const auto& billboard : billboards) {
        const auto dx = billboard.position.x - camera.position.x;
        const auto dy = billboard.position.y - camera.position.y;
        sorted.push_back({dx * dx + dy * dy, billboard});
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.distanceSquared > rhs.distanceSquared;
    });

    const auto inverseDeterminant =
        1.0F / (cameraPlane.x * camera.direction.y - camera.direction.x * cameraPlane.y);
    for (const auto& projected : sorted) {
        const auto relativeX = projected.billboard.position.x - camera.position.x;
        const auto relativeY = projected.billboard.position.y - camera.position.y;
        const auto transformX =
            inverseDeterminant * (camera.direction.y * relativeX - camera.direction.x * relativeY);
        const auto transformY =
            inverseDeterminant * (-cameraPlane.y * relativeX + cameraPlane.x * relativeY);
        if (transformY <= 0.05F) {
            continue;
        }
        ++stats.billboards;
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
                const auto normalizedX =
                    (2.0F * static_cast<float>(x) / static_cast<float>(size)) - 1.0F;
                const auto normalizedY =
                    (2.0F * static_cast<float>(y) / static_cast<float>(size)) - 1.0F;
                if (normalizedX * normalizedX + normalizedY * normalizedY <= 1.0F) {
                    framebuffer_.blendPixel(destinationX, top + y, projected.billboard.color);
                }
            }
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
