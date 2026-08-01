#pragma once

#include "fabgl/core/result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace fabgl {

namespace detail {

using GuidBytes = std::array<std::uint8_t, 16>;

[[nodiscard]] GuidBytes generateGuidBytes();
[[nodiscard]] GuidBytes stableGuidBytes(std::string_view stableName);
[[nodiscard]] Result<GuidBytes> parseGuidBytes(std::string_view text);
[[nodiscard]] std::string formatGuidBytes(const GuidBytes& bytes);

} // namespace detail

template <typename Tag> class StrongGuid final {
  public:
    constexpr StrongGuid() = default;
    explicit constexpr StrongGuid(detail::GuidBytes bytes) : bytes_(bytes) {}

    [[nodiscard]] static StrongGuid generate() {
        return StrongGuid(detail::generateGuidBytes());
    }

    [[nodiscard]] static StrongGuid fromStableName(std::string_view stableName) {
        return StrongGuid(detail::stableGuidBytes(stableName));
    }

    [[nodiscard]] static Result<StrongGuid> parse(std::string_view text) {
        auto parsed = detail::parseGuidBytes(text);
        if (!parsed) {
            return Result<StrongGuid>::failure(parsed.error());
        }
        return Result<StrongGuid>::success(StrongGuid(parsed.value()));
    }

    [[nodiscard]] bool isNil() const noexcept {
        for (const auto byte : bytes_) {
            if (byte != 0) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] const detail::GuidBytes& bytes() const noexcept {
        return bytes_;
    }
    [[nodiscard]] std::string toString() const {
        return detail::formatGuidBytes(bytes_);
    }

    friend bool operator==(const StrongGuid& lhs, const StrongGuid& rhs) noexcept {
        return lhs.bytes_ == rhs.bytes_;
    }

    friend bool operator!=(const StrongGuid& lhs, const StrongGuid& rhs) noexcept {
        return !(lhs == rhs);
    }

    friend bool operator<(const StrongGuid& lhs, const StrongGuid& rhs) noexcept {
        return lhs.bytes_ < rhs.bytes_;
    }

  private:
    detail::GuidBytes bytes_{};
};

template <typename Tag> struct StrongGuidHash final {
    std::size_t operator()(const StrongGuid<Tag>& guid) const noexcept {
        std::size_t hash = static_cast<std::size_t>(1469598103934665603ULL);
        for (const auto byte : guid.bytes()) {
            hash ^= static_cast<std::size_t>(byte);
            hash *= static_cast<std::size_t>(1099511628211ULL);
        }
        return hash;
    }
};

struct EntityGuidTag;
struct AssetGuidTag;
struct SceneGuidTag;
struct ComponentTypeGuidTag;

using EntityGuid = StrongGuid<EntityGuidTag>;
using AssetGuid = StrongGuid<AssetGuidTag>;
using SceneGuid = StrongGuid<SceneGuidTag>;
using ComponentTypeGuid = StrongGuid<ComponentTypeGuidTag>;

} // namespace fabgl
