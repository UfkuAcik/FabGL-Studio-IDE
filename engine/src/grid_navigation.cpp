#include "fabgl/navigation/grid_navigation.h"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace fabgl {
namespace {

[[nodiscard]] std::size_t checkedCellCount(std::size_t width, std::size_t height) {
    const auto maximumCoordinate = static_cast<std::size_t>(std::numeric_limits<int>::max());
    if (width > maximumCoordinate || height > maximumCoordinate) {
        throw std::invalid_argument("grid dimensions exceed the GridPosition coordinate range");
    }
    if (height != 0 && width > std::numeric_limits<std::size_t>::max() / height) {
        throw std::length_error("grid cell count overflows size_t");
    }
    return width * height;
}

struct SearchRecord final {
    std::size_t costFromStart = std::numeric_limits<std::size_t>::max();
    std::size_t parent = std::numeric_limits<std::size_t>::max();
    std::size_t discoveryOrder = std::numeric_limits<std::size_t>::max();
    bool open = false;
    bool closed = false;
};

} // namespace

GridNavigation::GridNavigation(std::size_t width, std::size_t height)
    : width_(width), height_(height), cells_(checkedCellCount(width, height)) {}

bool GridNavigation::contains(GridPosition position) const noexcept {
    if (position.x < 0 || position.y < 0) {
        return false;
    }
    return static_cast<std::size_t>(position.x) < width_ &&
           static_cast<std::size_t>(position.y) < height_;
}

bool GridNavigation::isWalkable(GridPosition position) const noexcept {
    return contains(position) && cells_[indexOf(position)].walkable;
}

std::uint16_t GridNavigation::traversalCost(GridPosition position) const noexcept {
    return contains(position) ? cells_[indexOf(position)].cost : 0;
}

Result<void> GridNavigation::setWalkable(GridPosition position, bool walkable) {
    if (!contains(position)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "grid position is outside the navigation grid"));
    }
    cells_[indexOf(position)].walkable = walkable;
    return Result<void>::success();
}

Result<void> GridNavigation::setTraversalCost(GridPosition position, std::uint16_t cost) {
    if (!contains(position)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "grid position is outside the navigation grid"));
    }
    if (cost == 0) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "grid traversal cost cannot be zero"));
    }
    cells_[indexOf(position)].cost = cost;
    return Result<void>::success();
}

void GridNavigation::clearObstacles() noexcept {
    for (auto& cell : cells_) {
        cell.walkable = true;
    }
}

Result<std::vector<GridPosition>> GridNavigation::findPath(GridPosition start,
                                                           GridPosition goal) const {
    if (!contains(start) || !contains(goal)) {
        return Result<std::vector<GridPosition>>::failure(
            Error(ErrorCode::InvalidArgument, "path endpoint is outside the navigation grid"));
    }
    if (!isWalkable(start) || !isWalkable(goal)) {
        return Result<std::vector<GridPosition>>::failure(
            Error(ErrorCode::NotFound, "path endpoint is blocked"));
    }

    const auto startIndex = indexOf(start);
    const auto goalIndex = indexOf(goal);
    std::vector<SearchRecord> records(cells_.size());
    records[startIndex].costFromStart = 0;
    records[startIndex].discoveryOrder = 0;
    records[startIndex].open = true;
    std::size_t nextDiscoveryOrder = 1;

    for (;;) {
        auto current = std::numeric_limits<std::size_t>::max();
        auto bestTotalCost = std::numeric_limits<std::size_t>::max();
        auto bestHeuristic = std::numeric_limits<std::size_t>::max();
        auto bestDiscoveryOrder = std::numeric_limits<std::size_t>::max();

        for (std::size_t index = 0; index < records.size(); ++index) {
            const auto& record = records[index];
            if (!record.open) {
                continue;
            }
            const auto remaining = heuristic(index, goalIndex);
            const auto total =
                record.costFromStart > std::numeric_limits<std::size_t>::max() - remaining
                    ? std::numeric_limits<std::size_t>::max()
                    : record.costFromStart + remaining;
            if (current == std::numeric_limits<std::size_t>::max() || total < bestTotalCost ||
                (total == bestTotalCost && remaining < bestHeuristic) ||
                (total == bestTotalCost && remaining == bestHeuristic &&
                 record.discoveryOrder < bestDiscoveryOrder)) {
                current = index;
                bestTotalCost = total;
                bestHeuristic = remaining;
                bestDiscoveryOrder = record.discoveryOrder;
            }
        }

        if (current == std::numeric_limits<std::size_t>::max()) {
            return Result<std::vector<GridPosition>>::failure(
                Error(ErrorCode::NotFound, "no walkable path connects the endpoints"));
        }
        if (current == goalIndex) {
            break;
        }

        auto& currentRecord = records[current];
        currentRecord.open = false;
        currentRecord.closed = true;

        const auto x = current % width_;
        const auto y = current / width_;
        std::array<std::size_t, 4> neighbors{};
        std::size_t neighborCount = 0;
        if (x + 1U < width_) {
            neighbors[neighborCount++] = current + 1U;
        }
        if (y + 1U < height_) {
            neighbors[neighborCount++] = current + width_;
        }
        if (x > 0) {
            neighbors[neighborCount++] = current - 1U;
        }
        if (y > 0) {
            neighbors[neighborCount++] = current - width_;
        }

        for (std::size_t neighborOffset = 0; neighborOffset < neighborCount; ++neighborOffset) {
            const auto neighbor = neighbors[neighborOffset];
            auto& neighborRecord = records[neighbor];
            if (!cells_[neighbor].walkable || neighborRecord.closed) {
                continue;
            }
            const auto stepCost = static_cast<std::size_t>(cells_[neighbor].cost);
            if (currentRecord.costFromStart > std::numeric_limits<std::size_t>::max() - stepCost) {
                continue;
            }
            const auto candidateCost = currentRecord.costFromStart + stepCost;
            if (!neighborRecord.open || candidateCost < neighborRecord.costFromStart) {
                neighborRecord.costFromStart = candidateCost;
                neighborRecord.parent = current;
                if (!neighborRecord.open) {
                    neighborRecord.discoveryOrder = nextDiscoveryOrder++;
                    neighborRecord.open = true;
                }
            }
        }
    }

    std::vector<GridPosition> reversedPath;
    for (auto current = goalIndex;; current = records[current].parent) {
        reversedPath.push_back(positionOf(current));
        if (current == startIndex) {
            break;
        }
        if (records[current].parent == std::numeric_limits<std::size_t>::max()) {
            return Result<std::vector<GridPosition>>::failure(
                Error(ErrorCode::InternalError, "path reconstruction encountered no parent"));
        }
    }
    std::reverse(reversedPath.begin(), reversedPath.end());
    return Result<std::vector<GridPosition>>::success(std::move(reversedPath));
}

std::size_t GridNavigation::indexOf(GridPosition position) const noexcept {
    return static_cast<std::size_t>(position.y) * width_ + static_cast<std::size_t>(position.x);
}

GridPosition GridNavigation::positionOf(std::size_t index) const noexcept {
    return {
        static_cast<int>(index % width_),
        static_cast<int>(index / width_),
    };
}

std::size_t GridNavigation::heuristic(std::size_t from, std::size_t to) const noexcept {
    const auto fromX = from % width_;
    const auto fromY = from / width_;
    const auto toX = to % width_;
    const auto toY = to / width_;
    const auto deltaX = fromX > toX ? fromX - toX : toX - fromX;
    const auto deltaY = fromY > toY ? fromY - toY : toY - fromY;
    return deltaX + deltaY;
}

} // namespace fabgl
