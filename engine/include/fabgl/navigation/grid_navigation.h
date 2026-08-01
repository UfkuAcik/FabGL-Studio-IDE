#pragma once

#include "fabgl/core/result.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fabgl {

struct GridPosition final {
    int x = 0;
    int y = 0;

    friend constexpr bool operator==(GridPosition lhs, GridPosition rhs) noexcept {
        return lhs.x == rhs.x && lhs.y == rhs.y;
    }
    friend constexpr bool operator!=(GridPosition lhs, GridPosition rhs) noexcept {
        return !(lhs == rhs);
    }
};

class GridNavigation final {
  public:
    GridNavigation(std::size_t width, std::size_t height);

    [[nodiscard]] std::size_t width() const noexcept {
        return width_;
    }
    [[nodiscard]] std::size_t height() const noexcept {
        return height_;
    }
    [[nodiscard]] bool contains(GridPosition position) const noexcept;
    [[nodiscard]] bool isWalkable(GridPosition position) const noexcept;
    [[nodiscard]] std::uint16_t traversalCost(GridPosition position) const noexcept;

    [[nodiscard]] Result<void> setWalkable(GridPosition position, bool walkable);
    [[nodiscard]] Result<void> setBlocked(GridPosition position, bool blocked) {
        return setWalkable(position, !blocked);
    }
    [[nodiscard]] Result<void> setTraversalCost(GridPosition position, std::uint16_t cost);
    void clearObstacles() noexcept;

    // Paths include both start and goal. Equal-cost choices are deterministic: neighbors are
    // discovered right, down, left, then up, and discovery order resolves remaining ties.
    [[nodiscard]] Result<std::vector<GridPosition>> findPath(GridPosition start,
                                                             GridPosition goal) const;

  private:
    struct Cell final {
        std::uint16_t cost = 1;
        bool walkable = true;
    };

    [[nodiscard]] std::size_t indexOf(GridPosition position) const noexcept;
    [[nodiscard]] GridPosition positionOf(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t heuristic(std::size_t from, std::size_t to) const noexcept;

    std::size_t width_ = 0;
    std::size_t height_ = 0;
    std::vector<Cell> cells_;
};

} // namespace fabgl
