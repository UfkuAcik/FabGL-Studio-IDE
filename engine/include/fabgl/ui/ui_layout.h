#pragma once

#include "fabgl/core/result.h"
#include "fabgl/math/types.h"

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace fabgl {

struct UIElementId final {
    std::uint32_t value = 0;
    [[nodiscard]] bool valid() const noexcept {
        return value != 0U;
    }
    friend bool operator==(UIElementId lhs, UIElementId rhs) noexcept {
        return lhs.value == rhs.value;
    }
    friend bool operator!=(UIElementId lhs, UIElementId rhs) noexcept {
        return !(lhs == rhs);
    }
    friend bool operator<(UIElementId lhs, UIElementId rhs) noexcept {
        return lhs.value < rhs.value;
    }
};

struct UIAnchors final {
    Vec2 minimum{0.0F, 0.0F};
    Vec2 maximum{0.0F, 0.0F};
};

struct UILayoutProperties final {
    UIAnchors anchors;
    Vec2 minimumOffset{};
    Vec2 maximumOffset{100.0F, 30.0F};
    bool visible = true;
    bool enabled = true;
    bool focusable = false;
};

struct UIElement final {
    UIElementId id;
    std::optional<UIElementId> parent;
    std::vector<UIElementId> children;
    UILayoutProperties properties;
    Rect computedRect;
};

class UIModel final {
  public:
    [[nodiscard]] Result<UIElementId> addElement(std::optional<UIElementId> parent,
                                                 UILayoutProperties properties = {});
    [[nodiscard]] Result<void> removeElement(UIElementId id);
    [[nodiscard]] Result<void> reparent(UIElementId child, std::optional<UIElementId> parent);
    [[nodiscard]] Result<void> setProperties(UIElementId id, UILayoutProperties properties);
    [[nodiscard]] UIElement* find(UIElementId id) noexcept;
    [[nodiscard]] const UIElement* find(UIElementId id) const noexcept;

    [[nodiscard]] Result<void> layout(Rect rootRect);
    [[nodiscard]] Result<void> setFocus(std::optional<UIElementId> id);
    [[nodiscard]] std::optional<UIElementId> focusNext(bool reverse = false);
    [[nodiscard]] std::optional<UIElementId> focused() const noexcept {
        return focused_;
    }

  private:
    [[nodiscard]] Result<void> validateProperties(const UILayoutProperties& properties) const;
    [[nodiscard]] bool effectivelyFocusable(const UIElement& element) const noexcept;
    [[nodiscard]] Result<void> layoutElement(UIElement& element, Rect parentRect);
    void collectFocusable(UIElementId id, std::vector<UIElementId>& output) const;
    void removeRecursive(UIElementId id);

    std::map<UIElementId, UIElement> elements_;
    std::vector<UIElementId> roots_;
    std::optional<UIElementId> focused_;
    std::uint32_t nextId_ = 1;
};

} // namespace fabgl
