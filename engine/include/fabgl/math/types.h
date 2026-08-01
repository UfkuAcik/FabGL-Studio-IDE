#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace fabgl {

struct Vec2 final {
    float x = 0.0F;
    float y = 0.0F;

    friend constexpr bool operator==(const Vec2& lhs, const Vec2& rhs) noexcept {
        return lhs.x == rhs.x && lhs.y == rhs.y;
    }
    friend constexpr bool operator!=(const Vec2& lhs, const Vec2& rhs) noexcept {
        return !(lhs == rhs);
    }
    friend constexpr Vec2 operator+(Vec2 lhs, Vec2 rhs) noexcept {
        return {lhs.x + rhs.x, lhs.y + rhs.y};
    }
    friend constexpr Vec2 operator-(Vec2 lhs, Vec2 rhs) noexcept {
        return {lhs.x - rhs.x, lhs.y - rhs.y};
    }
    friend constexpr Vec2 operator*(Vec2 value, float scalar) noexcept {
        return {value.x * scalar, value.y * scalar};
    }
};

struct Vec3 final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;

    friend constexpr bool operator==(const Vec3& lhs, const Vec3& rhs) noexcept {
        return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
    }
    friend constexpr bool operator!=(const Vec3& lhs, const Vec3& rhs) noexcept {
        return !(lhs == rhs);
    }
    friend constexpr Vec3 operator+(Vec3 lhs, Vec3 rhs) noexcept {
        return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
    }
    friend constexpr Vec3 operator-(Vec3 lhs, Vec3 rhs) noexcept {
        return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
    }
    friend constexpr Vec3 operator*(Vec3 value, float scalar) noexcept {
        return {value.x * scalar, value.y * scalar, value.z * scalar};
    }
};

struct Rect final {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;

    [[nodiscard]] constexpr bool contains(Vec2 point) const noexcept {
        return point.x >= x && point.y >= y && point.x <= x + width && point.y <= y + height;
    }
    friend constexpr bool operator==(const Rect& lhs, const Rect& rhs) noexcept {
        return lhs.x == rhs.x && lhs.y == rhs.y && lhs.width == rhs.width &&
               lhs.height == rhs.height;
    }
    friend constexpr bool operator!=(const Rect& lhs, const Rect& rhs) noexcept {
        return !(lhs == rhs);
    }
};

struct Color final {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    [[nodiscard]] constexpr std::uint32_t rgba32() const noexcept {
        return (static_cast<std::uint32_t>(r) << 24U) | (static_cast<std::uint32_t>(g) << 16U) |
               (static_cast<std::uint32_t>(b) << 8U) | static_cast<std::uint32_t>(a);
    }
    friend constexpr bool operator==(const Color& lhs, const Color& rhs) noexcept {
        return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
    }
    friend constexpr bool operator!=(const Color& lhs, const Color& rhs) noexcept {
        return !(lhs == rhs);
    }
};

class Fixed final {
  public:
    static constexpr int FractionBits = 16;
    static constexpr std::int32_t Scale = 1 << FractionBits;

    constexpr Fixed() = default;
    explicit constexpr Fixed(std::int32_t integer) : raw_(integer * Scale) {}

    [[nodiscard]] static constexpr Fixed fromRaw(std::int32_t raw) noexcept {
        Fixed value;
        value.raw_ = raw;
        return value;
    }

    [[nodiscard]] static Fixed fromFloat(float value) noexcept {
        const auto scaled = std::round(static_cast<double>(value) * static_cast<double>(Scale));
        const auto clamped =
            std::clamp(scaled, static_cast<double>(std::numeric_limits<std::int32_t>::min()),
                       static_cast<double>(std::numeric_limits<std::int32_t>::max()));
        return fromRaw(static_cast<std::int32_t>(clamped));
    }

    [[nodiscard]] constexpr std::int32_t raw() const noexcept {
        return raw_;
    }
    [[nodiscard]] constexpr float toFloat() const noexcept {
        return static_cast<float>(raw_) / static_cast<float>(Scale);
    }

    friend constexpr bool operator==(Fixed lhs, Fixed rhs) noexcept {
        return lhs.raw_ == rhs.raw_;
    }
    friend constexpr bool operator!=(Fixed lhs, Fixed rhs) noexcept {
        return !(lhs == rhs);
    }
    friend constexpr Fixed operator+(Fixed lhs, Fixed rhs) noexcept {
        return fromRaw(lhs.raw_ + rhs.raw_);
    }
    friend constexpr Fixed operator-(Fixed lhs, Fixed rhs) noexcept {
        return fromRaw(lhs.raw_ - rhs.raw_);
    }
    friend Fixed operator*(Fixed lhs, Fixed rhs) noexcept {
        const auto wide = static_cast<std::int64_t>(lhs.raw_) * static_cast<std::int64_t>(rhs.raw_);
        return fromRaw(static_cast<std::int32_t>(wide >> FractionBits));
    }
    friend Fixed operator/(Fixed lhs, Fixed rhs) {
        if (rhs.raw_ == 0) {
            throw std::domain_error("fixed-point division by zero");
        }
        const auto wide = static_cast<std::int64_t>(lhs.raw_) << FractionBits;
        return fromRaw(static_cast<std::int32_t>(wide / rhs.raw_));
    }

  private:
    std::int32_t raw_ = 0;
};

struct Mat4 final {
    std::array<float, 16> values{};

    [[nodiscard]] static constexpr Mat4 identity() noexcept {
        Mat4 matrix{};
        matrix.values[0] = 1.0F;
        matrix.values[5] = 1.0F;
        matrix.values[10] = 1.0F;
        matrix.values[15] = 1.0F;
        return matrix;
    }

    [[nodiscard]] constexpr float at(std::size_t row, std::size_t column) const noexcept {
        return values[row * 4U + column];
    }
    constexpr float& at(std::size_t row, std::size_t column) noexcept {
        return values[row * 4U + column];
    }

    [[nodiscard]] static Mat4 translation(Vec3 value) noexcept {
        auto matrix = identity();
        matrix.at(0, 3) = value.x;
        matrix.at(1, 3) = value.y;
        matrix.at(2, 3) = value.z;
        return matrix;
    }

    [[nodiscard]] static Mat4 scaling(Vec3 value) noexcept {
        auto matrix = identity();
        matrix.at(0, 0) = value.x;
        matrix.at(1, 1) = value.y;
        matrix.at(2, 2) = value.z;
        return matrix;
    }

    [[nodiscard]] static Mat4 rotationX(float radians) noexcept {
        auto matrix = identity();
        const auto cosine = std::cos(radians);
        const auto sine = std::sin(radians);
        matrix.at(1, 1) = cosine;
        matrix.at(1, 2) = -sine;
        matrix.at(2, 1) = sine;
        matrix.at(2, 2) = cosine;
        return matrix;
    }

    [[nodiscard]] static Mat4 rotationY(float radians) noexcept {
        auto matrix = identity();
        const auto cosine = std::cos(radians);
        const auto sine = std::sin(radians);
        matrix.at(0, 0) = cosine;
        matrix.at(0, 2) = sine;
        matrix.at(2, 0) = -sine;
        matrix.at(2, 2) = cosine;
        return matrix;
    }

    [[nodiscard]] static Mat4 rotationZ(float radians) noexcept {
        auto matrix = identity();
        const auto cosine = std::cos(radians);
        const auto sine = std::sin(radians);
        matrix.at(0, 0) = cosine;
        matrix.at(0, 1) = -sine;
        matrix.at(1, 0) = sine;
        matrix.at(1, 1) = cosine;
        return matrix;
    }

    [[nodiscard]] static Mat4 trs(Vec3 position, Vec3 rotationRadians, Vec3 scale) noexcept;
    [[nodiscard]] Vec3 transformPoint(Vec3 point) const noexcept;
};

inline Mat4 operator*(const Mat4& lhs, const Mat4& rhs) noexcept {
    Mat4 result{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            float value = 0.0F;
            for (std::size_t index = 0; index < 4; ++index) {
                value += lhs.at(row, index) * rhs.at(index, column);
            }
            result.at(row, column) = value;
        }
    }
    return result;
}

inline Mat4 Mat4::trs(Vec3 position, Vec3 rotationRadians, Vec3 scale) noexcept {
    return translation(position) * rotationZ(rotationRadians.z) * rotationY(rotationRadians.y) *
           rotationX(rotationRadians.x) * scaling(scale);
}

inline Vec3 Mat4::transformPoint(Vec3 point) const noexcept {
    return {
        at(0, 0) * point.x + at(0, 1) * point.y + at(0, 2) * point.z + at(0, 3),
        at(1, 0) * point.x + at(1, 1) * point.y + at(1, 2) * point.z + at(1, 3),
        at(2, 0) * point.x + at(2, 1) * point.y + at(2, 2) * point.z + at(2, 3),
    };
}

[[nodiscard]] inline bool nearlyEqual(float lhs, float rhs, float epsilon = 0.0001F) noexcept {
    return std::fabs(lhs - rhs) <= epsilon;
}

[[nodiscard]] inline bool nearlyEqual(Vec3 lhs, Vec3 rhs, float epsilon = 0.0001F) noexcept {
    return nearlyEqual(lhs.x, rhs.x, epsilon) && nearlyEqual(lhs.y, rhs.y, epsilon) &&
           nearlyEqual(lhs.z, rhs.z, epsilon);
}

} // namespace fabgl
