#pragma once

#include <fabgl/math/types.h>

#include <cstddef>
#include <string>
#include <vector>

namespace fabgl::rendering {

class Framebuffer final {
  public:
    Framebuffer(int width, int height);

    [[nodiscard]] int width() const noexcept {
        return width_;
    }
    [[nodiscard]] int height() const noexcept {
        return height_;
    }
    [[nodiscard]] const std::vector<Color>& pixels() const noexcept {
        return pixels_;
    }
    [[nodiscard]] std::vector<Color>& pixels() noexcept {
        return pixels_;
    }

    void clear(Color color) noexcept;
    void setPixel(int x, int y, Color color) noexcept;
    void blendPixel(int x, int y, Color color) noexcept;
    [[nodiscard]] Color pixel(int x, int y) const noexcept;
    void fillRect(int x, int y, int width, int height, Color color) noexcept;
    void drawLine(int x0, int y0, int x1, int y1, Color color) noexcept;
    void fillTriangle(Vec2 a, Vec2 b, Vec2 c, Color color) noexcept;

    [[nodiscard]] bool savePpm(const std::string& path, std::string& error) const;
    [[nodiscard]] std::uint64_t checksum() const noexcept;

  private:
    [[nodiscard]] bool contains(int x, int y) const noexcept;
    [[nodiscard]] std::size_t index(int x, int y) const noexcept;

    int width_ = 0;
    int height_ = 0;
    std::vector<Color> pixels_;
};

} // namespace fabgl::rendering
