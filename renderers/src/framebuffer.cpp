#include <fabgl/rendering/framebuffer.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace fabgl::rendering {

namespace {

[[nodiscard]] float edge(Vec2 a, Vec2 b, Vec2 point) noexcept {
    return (point.x - a.x) * (b.y - a.y) - (point.y - a.y) * (b.x - a.x);
}

} // namespace

Framebuffer::Framebuffer(int width, int height) : width_(width), height_(height) {
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("framebuffer dimensions must be positive");
    }
    const auto pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (pixelCount > std::vector<Color>().max_size()) {
        throw std::length_error("framebuffer dimensions are too large");
    }
    pixels_.resize(pixelCount);
}

void Framebuffer::clear(Color color) noexcept {
    std::fill(pixels_.begin(), pixels_.end(), color);
}

void Framebuffer::setPixel(int x, int y, Color color) noexcept {
    if (contains(x, y)) {
        pixels_[index(x, y)] = color;
    }
}

void Framebuffer::blendPixel(int x, int y, Color color) noexcept {
    if (!contains(x, y) || color.a == 0U) {
        return;
    }
    if (color.a == 255U) {
        pixels_[index(x, y)] = color;
        return;
    }

    auto& destination = pixels_[index(x, y)];
    const auto alpha = static_cast<std::uint32_t>(color.a);
    const auto inverse = 255U - alpha;
    const auto blend = [alpha, inverse](std::uint8_t source, std::uint8_t target) {
        return static_cast<std::uint8_t>((static_cast<std::uint32_t>(source) * alpha +
                                          static_cast<std::uint32_t>(target) * inverse + 127U) /
                                         255U);
    };
    destination = {blend(color.r, destination.r), blend(color.g, destination.g),
                   blend(color.b, destination.b), 255U};
}

Color Framebuffer::pixel(int x, int y) const noexcept {
    if (!contains(x, y)) {
        return {};
    }
    return pixels_[index(x, y)];
}

void Framebuffer::fillRect(int x, int y, int width, int height, Color color) noexcept {
    const auto left = std::max(0, x);
    const auto top = std::max(0, y);
    const auto right = std::min(width_, x + std::max(0, width));
    const auto bottom = std::min(height_, y + std::max(0, height));
    for (auto row = top; row < bottom; ++row) {
        for (auto column = left; column < right; ++column) {
            setPixel(column, row, color);
        }
    }
}

void Framebuffer::drawLine(int x0, int y0, int x1, int y1, Color color) noexcept {
    const auto dx = std::abs(x1 - x0);
    const auto sx = x0 < x1 ? 1 : -1;
    const auto dy = -std::abs(y1 - y0);
    const auto sy = y0 < y1 ? 1 : -1;
    auto error = dx + dy;
    while (true) {
        setPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const auto twiceError = error * 2;
        if (twiceError >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twiceError <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void Framebuffer::fillTriangle(Vec2 a, Vec2 b, Vec2 c, Color color) noexcept {
    const auto area = edge(a, b, c);
    if (std::fabs(area) < std::numeric_limits<float>::epsilon()) {
        return;
    }
    const auto minimumX = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
    const auto maximumX =
        std::min(width_ - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
    const auto minimumY = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
    const auto maximumY =
        std::min(height_ - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
    const auto positiveArea = area > 0.0F;
    for (auto y = minimumY; y <= maximumY; ++y) {
        for (auto x = minimumX; x <= maximumX; ++x) {
            const Vec2 sample{static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F};
            const auto first = edge(a, b, sample);
            const auto second = edge(b, c, sample);
            const auto third = edge(c, a, sample);
            const auto inside = positiveArea ? first >= 0.0F && second >= 0.0F && third >= 0.0F
                                             : first <= 0.0F && second <= 0.0F && third <= 0.0F;
            if (inside) {
                setPixel(x, y, color);
            }
        }
    }
}

bool Framebuffer::savePpm(const std::string& path, std::string& error) const {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "unable to open output image: " + path;
        return false;
    }
    output << "P6\n" << width_ << ' ' << height_ << "\n255\n";
    for (const auto color : pixels_) {
        const char channels[] = {static_cast<char>(color.r), static_cast<char>(color.g),
                                 static_cast<char>(color.b)};
        output.write(channels, 3);
    }
    if (!output) {
        error = "failed while writing output image: " + path;
        return false;
    }
    return true;
}

std::uint64_t Framebuffer::checksum() const noexcept {
    constexpr std::uint64_t offset = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    auto hash = offset;
    for (const auto color : pixels_) {
        for (const auto channel : {color.r, color.g, color.b, color.a}) {
            hash ^= channel;
            hash *= prime;
        }
    }
    return hash;
}

bool Framebuffer::contains(int x, int y) const noexcept {
    return x >= 0 && y >= 0 && x < width_ && y < height_;
}

std::size_t Framebuffer::index(int x, int y) const noexcept {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
           static_cast<std::size_t>(x);
}

} // namespace fabgl::rendering
