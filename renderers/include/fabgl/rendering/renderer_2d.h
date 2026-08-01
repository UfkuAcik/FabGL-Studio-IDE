#pragma once

#include <fabgl/math/types.h>
#include <fabgl/rendering/framebuffer.h>

#include <cstdint>
#include <vector>

namespace fabgl::rendering {

struct Sprite final {
    int width = 0;
    int height = 0;
    std::vector<Color> pixels;

    [[nodiscard]] bool valid() const noexcept;
};

struct SpriteDraw final {
    const Sprite* sprite = nullptr;
    int x = 0;
    int y = 0;
    int scale = 1;
    bool flipX = false;
    bool flipY = false;
    Color tint{255, 255, 255, 255};
};

struct Tilemap final {
    int width = 0;
    int height = 0;
    int tileSize = 8;
    std::vector<std::uint16_t> cells;
    std::vector<Sprite> tiles;

    [[nodiscard]] bool valid() const noexcept;
};

class Renderer2D final {
  public:
    explicit Renderer2D(Framebuffer& framebuffer) : framebuffer_(framebuffer) {}

    void draw(const SpriteDraw& command) noexcept;
    void drawTilemap(const Tilemap& map, Vec2 camera, Rect viewport) noexcept;

    [[nodiscard]] std::uint32_t drawCalls() const noexcept {
        return drawCalls_;
    }
    [[nodiscard]] std::uint32_t spritesSubmitted() const noexcept {
        return spritesSubmitted_;
    }
    void resetCounters() noexcept;

  private:
    [[nodiscard]] static Color tint(Color source, Color tintColor) noexcept;

    Framebuffer& framebuffer_;
    std::uint32_t drawCalls_ = 0;
    std::uint32_t spritesSubmitted_ = 0;
};

[[nodiscard]] Sprite makeCheckerSprite(int width, int height, Color first, Color second);

} // namespace fabgl::rendering
