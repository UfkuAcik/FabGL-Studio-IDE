#pragma once

#include "fabgl/core/guid.h"
#include "fabgl/core/result.h"
#include "fabgl/math/types.h"
#include "fabgl/ui/ui_layout.h"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fabgl {

enum class UIWidgetType {
    Panel,
    Image,
    Text,
    Button,
    Toggle,
    Slider,
    Progress,
    List,
    Layout,
};

enum class UILayoutDirection {
    Horizontal,
    Vertical,
};

enum class UIKey {
    Tab,
    ReverseTab,
    Enter,
    Space,
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
};

enum class UIEventType {
    Clicked,
    Toggled,
    ValueChanged,
    SelectionChanged,
    FocusChanged,
    PointerEntered,
    PointerExited,
};

struct UITheme final {
    Color panel{30, 32, 38, 255};
    Color foreground{235, 238, 245, 255};
    Color accent{65, 145, 255, 255};
    Color disabled{110, 115, 125, 255};
    float padding = 4.0F;
    float spacing = 4.0F;
    float fontScale = 1.0F;
};

struct UIWidget final {
    UIElementId id;
    UIWidgetType type = UIWidgetType::Panel;
    std::string text;
    AssetGuid imageAsset;
    float minimum = 0.0F;
    float maximum = 1.0F;
    float value = 0.0F;
    float step = 0.1F;
    bool checked = false;
    std::vector<std::string> items;
    std::optional<std::size_t> selectedItem;
    UILayoutDirection layoutDirection = UILayoutDirection::Vertical;
    float layoutSpacing = -1.0F;
    float layoutPadding = -1.0F;
};

struct UIEvent final {
    UIEventType type = UIEventType::Clicked;
    UIElementId target;
    float value = 0.0F;
    std::optional<std::size_t> item;
};

// Retained-mode runtime UI model with deterministic hit testing and input.
// Rectangles stored by UIModel are logical pixels; screenRect() exposes the
// scaled physical rectangle used by the host renderer.
class RuntimeUI final {
  public:
    [[nodiscard]] Result<UIElementId> addWidget(UIWidgetType type,
                                                std::optional<UIElementId> parent = std::nullopt,
                                                UILayoutProperties properties = {});
    [[nodiscard]] Result<void> removeWidget(UIElementId id);
    [[nodiscard]] UIWidget* widget(UIElementId id) noexcept;
    [[nodiscard]] const UIWidget* widget(UIElementId id) const noexcept;

    [[nodiscard]] Result<void> setText(UIElementId id, std::string text);
    [[nodiscard]] Result<void> setRange(UIElementId id, float minimum, float maximum,
                                        float step = 0.1F);
    [[nodiscard]] Result<void> setValue(UIElementId id, float value, bool emitEvent = false);
    [[nodiscard]] Result<void> setItems(UIElementId id, std::vector<std::string> items);
    [[nodiscard]] Result<void> setTheme(UITheme theme);
    [[nodiscard]] const UITheme& theme() const noexcept {
        return theme_;
    }
    [[nodiscard]] Result<void> setScale(float scale);
    [[nodiscard]] float scale() const noexcept {
        return scale_;
    }

    [[nodiscard]] Result<void> layout(Rect physicalViewport);
    [[nodiscard]] std::optional<Rect> screenRect(UIElementId id) const noexcept;
    [[nodiscard]] std::optional<UIElementId> hitTest(Vec2 physicalPosition) const noexcept;

    [[nodiscard]] Result<void> pointerMove(Vec2 physicalPosition);
    [[nodiscard]] Result<void> pointerDown(Vec2 physicalPosition);
    [[nodiscard]] Result<void> pointerUp(Vec2 physicalPosition);
    [[nodiscard]] Result<void> keyDown(UIKey key);

    [[nodiscard]] const std::vector<UIEvent>& events() const noexcept {
        return events_;
    }
    [[nodiscard]] std::vector<UIEvent> consumeEvents();
    [[nodiscard]] const UIModel& model() const noexcept {
        return model_;
    }
    [[nodiscard]] UIModel& model() noexcept {
        return model_;
    }

  private:
    [[nodiscard]] bool interactive(const UIWidget& widget) const noexcept;
    [[nodiscard]] bool effectivelyInteractive(UIElementId id) const noexcept;
    [[nodiscard]] Result<void> activate(UIElementId id, UIKey key);
    [[nodiscard]] Result<void> updateSliderFromPointer(UIElementId id,
                                                       Vec2 logicalPosition);
    [[nodiscard]] Result<void> arrangeSubtree(UIElementId id);
    [[nodiscard]] Result<void> layoutAnchoredSubtree(UIElementId id, Rect parentRect);
    void collectSubtree(UIElementId id, std::vector<UIElementId>& output) const;
    void pushEvent(UIEventType type, UIElementId target, float value = 0.0F,
                   std::optional<std::size_t> item = std::nullopt);

    UIModel model_;
    std::map<UIElementId, UIWidget> widgets_;
    UITheme theme_;
    float scale_ = 1.0F;
    std::optional<UIElementId> hovered_;
    std::optional<UIElementId> pressed_;
    std::vector<UIEvent> events_;
};

} // namespace fabgl
