#include <fabgl/rendering/renderer_2d.h>

#include <algorithm>
#include <cstddef>

namespace fabgl::rendering {

bool Sprite::valid() const noexcept {
    return width > 0 && height > 0 &&
           pixels.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
}

bool Tilemap::valid() const noexcept {
    if (width <= 0 || height <= 0 || tileSize <= 0 || tiles.empty()) {
        return false;
    }
    if (cells.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        return false;
    }
    return std::all_of(tiles.begin(), tiles.end(), [](const Sprite& tile) { return tile.valid(); });
}

void Renderer2D::draw(const SpriteDraw& command) noexcept {
    if (command.sprite == nullptr || !command.sprite->valid() || command.scale <= 0) {
        return;
    }
    ++drawCalls_;
    ++spritesSubmitted_;
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

void Renderer2D::drawTilemap(const Tilemap& map, Vec2 camera, Rect viewport) noexcept {
    if (!map.valid()) {
        return;
    }
    const auto viewportLeft = static_cast<int>(viewport.x);
    const auto viewportTop = static_cast<int>(viewport.y);
    const auto firstColumn = std::max(0, static_cast<int>(camera.x) / map.tileSize);
    const auto firstRow = std::max(0, static_cast<int>(camera.y) / map.tileSize);
    const auto lastColumn =
        std::min(map.width - 1, (static_cast<int>(camera.x) + static_cast<int>(viewport.width) +
                                 map.tileSize - 1) /
                                    map.tileSize);
    const auto lastRow =
        std::min(map.height - 1, (static_cast<int>(camera.y) + static_cast<int>(viewport.height) +
                                  map.tileSize - 1) /
                                     map.tileSize);

    for (auto row = firstRow; row <= lastRow; ++row) {
        for (auto column = firstColumn; column <= lastColumn; ++column) {
            const auto cellIndex =
                static_cast<std::size_t>(row) * static_cast<std::size_t>(map.width) +
                static_cast<std::size_t>(column);
            const auto tileIndex = static_cast<std::size_t>(map.cells[cellIndex]);
            if (tileIndex >= map.tiles.size()) {
                continue;
            }
            const auto destinationX =
                viewportLeft + column * map.tileSize - static_cast<int>(camera.x);
            const auto destinationY = viewportTop + row * map.tileSize - static_cast<int>(camera.y);
            draw({&map.tiles[tileIndex],
                  destinationX,
                  destinationY,
                  1,
                  false,
                  false,
                  {255, 255, 255, 255}});
        }
    }
}

void Renderer2D::resetCounters() noexcept {
    drawCalls_ = 0;
    spritesSubmitted_ = 0;
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
