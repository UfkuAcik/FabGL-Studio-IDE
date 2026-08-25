#include "fabgl/ui/runtime_widgets.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace fabgl {
namespace {

[[nodiscard]] bool finite(Vec2 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] bool finite(Rect value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.width) && std::isfinite(value.height);
}

[[nodiscard]] bool keyboardFocusable(UIWidgetType type) noexcept {
    return type == UIWidgetType::Button || type == UIWidgetType::Toggle ||
           type == UIWidgetType::Slider || type == UIWidgetType::List;
}

} // namespace

Result<UIElementId> RuntimeUI::addWidget(UIWidgetType type, std::optional<UIElementId> parent,
                                         UILayoutProperties properties) {
    if (keyboardFocusable(type))
        properties.focusable = true;
    auto added = model_.addElement(parent, properties);
    if (!added)
        return added;
    UIWidget widget;
    widget.id = added.value();
    widget.type = type;
    widgets_.emplace(widget.id, std::move(widget));
    return added;
}

void RuntimeUI::collectSubtree(UIElementId id, std::vector<UIElementId>& output) const {
    const auto* element = model_.find(id);
    if (element == nullptr)
        return;
    output.push_back(id);
    for (const auto child : element->children)
        collectSubtree(child, output);
}

Result<void> RuntimeUI::removeWidget(UIElementId id) {
    if (widget(id) == nullptr)
        return Result<void>::failure(Error(ErrorCode::NotFound, "UI widget was not found"));
    std::vector<UIElementId> subtree;
    collectSubtree(id, subtree);
    auto removed = model_.removeElement(id);
    if (!removed)
        return removed;
    for (const auto descendant : subtree)
        widgets_.erase(descendant);
    if (hovered_ && std::find(subtree.begin(), subtree.end(), *hovered_) != subtree.end())
        hovered_.reset();
    if (pressed_ && std::find(subtree.begin(), subtree.end(), *pressed_) != subtree.end())
        pressed_.reset();
    return Result<void>::success();
}

UIWidget* RuntimeUI::widget(UIElementId id) noexcept {
    const auto iterator = widgets_.find(id);
    return iterator == widgets_.end() ? nullptr : &iterator->second;
}

const UIWidget* RuntimeUI::widget(UIElementId id) const noexcept {
    const auto iterator = widgets_.find(id);
    return iterator == widgets_.end() ? nullptr : &iterator->second;
}

Result<void> RuntimeUI::setText(UIElementId id, std::string text) {
    auto* target = widget(id);
    if (target == nullptr)
        return Result<void>::failure(Error(ErrorCode::NotFound, "UI widget was not found"));
    target->text = std::move(text);
    return Result<void>::success();
}

Result<void> RuntimeUI::setRange(UIElementId id, float minimum, float maximum, float step) {
    auto* target = widget(id);
    if (target == nullptr)
        return Result<void>::failure(Error(ErrorCode::NotFound, "UI widget was not found"));
    if (target->type != UIWidgetType::Slider && target->type != UIWidgetType::Progress) {
        return Result<void>::failure(
            Error(ErrorCode::TypeMismatch, "UI widget does not expose a numeric range"));
    }
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || !std::isfinite(step) ||
        maximum <= minimum || step <= 0.0F) {
        return Result<void>::failure(Error(ErrorCode::InvalidArgument, "UI range is invalid"));
    }
    target->minimum = minimum;
    target->maximum = maximum;
    target->step = step;
    target->value = std::clamp(target->value, minimum, maximum);
    return Result<void>::success();
}

Result<void> RuntimeUI::setValue(UIElementId id, float value, bool emitEvent) {
    auto* target = widget(id);
    if (target == nullptr)
        return Result<void>::failure(Error(ErrorCode::NotFound, "UI widget was not found"));
    if (target->type != UIWidgetType::Slider && target->type != UIWidgetType::Progress) {
        return Result<void>::failure(
            Error(ErrorCode::TypeMismatch, "UI widget does not expose a numeric value"));
    }
    if (!std::isfinite(value))
        return Result<void>::failure(Error(ErrorCode::InvalidArgument, "UI value is invalid"));
    const auto clamped = std::clamp(value, target->minimum, target->maximum);
    const bool changed = clamped != target->value;
    target->value = clamped;
    if (changed && emitEvent)
        pushEvent(UIEventType::ValueChanged, id, clamped);
    return Result<void>::success();
}

Result<void> RuntimeUI::setItems(UIElementId id, std::vector<std::string> items) {
    auto* target = widget(id);
    if (target == nullptr)
        return Result<void>::failure(Error(ErrorCode::NotFound, "UI widget was not found"));
    if (target->type != UIWidgetType::List)
        return Result<void>::failure(Error(ErrorCode::TypeMismatch, "UI widget is not a list"));
    target->items = std::move(items);
    if (target->selectedItem && *target->selectedItem >= target->items.size())
        target->selectedItem.reset();
    return Result<void>::success();
}

Result<void> RuntimeUI::setTheme(UITheme theme) {
    if (!std::isfinite(theme.padding) || !std::isfinite(theme.spacing) ||
        !std::isfinite(theme.fontScale) || theme.padding < 0.0F || theme.spacing < 0.0F ||
        theme.fontScale <= 0.0F) {
        return Result<void>::failure(Error(ErrorCode::InvalidArgument, "UI theme is invalid"));
    }
    theme_ = theme;
    return Result<void>::success();
}

Result<void> RuntimeUI::setScale(float scale) {
    if (!std::isfinite(scale) || scale < 0.25F || scale > 8.0F)
        return Result<void>::failure(Error(ErrorCode::InvalidArgument, "UI scale is invalid"));
    scale_ = scale;
    return Result<void>::success();
}

Result<void> RuntimeUI::layoutAnchoredSubtree(UIElementId id, Rect parentRect) {
    auto* element = model_.find(id);
    if (element == nullptr)
        return Result<void>::failure(Error(ErrorCode::InvalidState, "UI child is missing"));
    const auto& properties = element->properties;
    const auto left = parentRect.x + parentRect.width * properties.anchors.minimum.x +
                      properties.minimumOffset.x;
    const auto top = parentRect.y + parentRect.height * properties.anchors.minimum.y +
                     properties.minimumOffset.y;
    const auto right = parentRect.x + parentRect.width * properties.anchors.maximum.x +
                       properties.maximumOffset.x;
    const auto bottom = parentRect.y + parentRect.height * properties.anchors.maximum.y +
                        properties.maximumOffset.y;
    if (right < left || bottom < top)
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "UI layout produced a negative child size"));
    element->computedRect = {left, top, right - left, bottom - top};
    return arrangeSubtree(id);
}

Result<void> RuntimeUI::arrangeSubtree(UIElementId id) {
    auto* element = model_.find(id);
    const auto* target = widget(id);
    if (element == nullptr || target == nullptr)
        return Result<void>::failure(Error(ErrorCode::InvalidState, "UI hierarchy is incomplete"));
    if (element->children.empty())
        return Result<void>::success();

    if (target->type != UIWidgetType::Layout) {
        for (const auto child : element->children) {
            auto result = layoutAnchoredSubtree(child, element->computedRect);
            if (!result)
                return result;
        }
        return Result<void>::success();
    }

    const auto padding = target->layoutPadding < 0.0F ? theme_.padding : target->layoutPadding;
    const auto spacing = target->layoutSpacing < 0.0F ? theme_.spacing : target->layoutSpacing;
    if (!std::isfinite(padding) || !std::isfinite(spacing) || padding < 0.0F || spacing < 0.0F)
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "UI automatic layout spacing is invalid"));
    const auto count = static_cast<float>(element->children.size());
    const auto horizontal = target->layoutDirection == UILayoutDirection::Horizontal;
    const auto primary = horizontal ? element->computedRect.width : element->computedRect.height;
    const auto cross = horizontal ? element->computedRect.height : element->computedRect.width;
    const auto available = primary - 2.0F * padding - spacing * (count - 1.0F);
    const auto crossAvailable = cross - 2.0F * padding;
    if (available < 0.0F || crossAvailable < 0.0F)
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "UI automatic layout has insufficient space"));
    const auto extent = available / count;
    for (std::size_t index = 0; index < element->children.size(); ++index) {
        auto* child = model_.find(element->children[index]);
        if (child == nullptr)
            return Result<void>::failure(Error(ErrorCode::InvalidState, "UI child is missing"));
        const auto offset = padding + static_cast<float>(index) * (extent + spacing);
        child->computedRect = horizontal
                                  ? Rect{element->computedRect.x + offset,
                                         element->computedRect.y + padding, extent, crossAvailable}
                                  : Rect{element->computedRect.x + padding,
                                         element->computedRect.y + offset, crossAvailable, extent};
        auto result = arrangeSubtree(child->id);
        if (!result)
            return result;
    }
    return Result<void>::success();
}

Result<void> RuntimeUI::layout(Rect physicalViewport) {
    if (!finite(physicalViewport) || physicalViewport.width < 0.0F ||
        physicalViewport.height < 0.0F) {
        return Result<void>::failure(Error(ErrorCode::InvalidArgument, "UI viewport is invalid"));
    }
    const Rect logical{physicalViewport.x / scale_, physicalViewport.y / scale_,
                       physicalViewport.width / scale_, physicalViewport.height / scale_};
    auto result = model_.layout(logical);
    if (!result)
        return result;
    for (const auto& [id, target] : widgets_) {
        static_cast<void>(target);
        const auto* element = model_.find(id);
        if (element != nullptr && !element->parent) {
            result = arrangeSubtree(id);
            if (!result)
                return result;
        }
    }
    return Result<void>::success();
}

std::optional<Rect> RuntimeUI::screenRect(UIElementId id) const noexcept {
    const auto* element = model_.find(id);
    if (element == nullptr)
        return std::nullopt;
    const auto& rect = element->computedRect;
    return Rect{rect.x * scale_, rect.y * scale_, rect.width * scale_, rect.height * scale_};
}

bool RuntimeUI::interactive(const UIWidget& target) const noexcept {
    return keyboardFocusable(target.type);
}

bool RuntimeUI::effectivelyInteractive(UIElementId id) const noexcept {
    const auto* target = widget(id);
    const auto* element = model_.find(id);
    if (target == nullptr || element == nullptr || !interactive(*target))
        return false;
    auto cursor = element;
    while (cursor != nullptr) {
        if (!cursor->properties.visible || !cursor->properties.enabled)
            return false;
        cursor = cursor->parent ? model_.find(*cursor->parent) : nullptr;
    }
    return true;
}

std::optional<UIElementId> RuntimeUI::hitTest(Vec2 physicalPosition) const noexcept {
    if (!finite(physicalPosition))
        return std::nullopt;
    const Vec2 logical{physicalPosition.x / scale_, physicalPosition.y / scale_};
    for (auto iterator = widgets_.rbegin(); iterator != widgets_.rend(); ++iterator) {
        const auto id = iterator->first;
        const auto* element = model_.find(id);
        if (element != nullptr && effectivelyInteractive(id) && element->computedRect.contains(logical))
            return id;
    }
    return std::nullopt;
}

void RuntimeUI::pushEvent(UIEventType type, UIElementId target, float value,
                          std::optional<std::size_t> item) {
    events_.push_back({type, target, value, item});
}

Result<void> RuntimeUI::pointerMove(Vec2 physicalPosition) {
    if (!finite(physicalPosition))
        return Result<void>::failure(Error(ErrorCode::InvalidArgument, "UI pointer is invalid"));
    const auto hit = hitTest(physicalPosition);
    if (hit != hovered_) {
        if (hovered_)
            pushEvent(UIEventType::PointerExited, *hovered_);
        hovered_ = hit;
        if (hovered_)
            pushEvent(UIEventType::PointerEntered, *hovered_);
    }
    if (pressed_) {
        const auto* pressedWidget = widget(*pressed_);
        if (pressedWidget != nullptr && pressedWidget->type == UIWidgetType::Slider) {
            return updateSliderFromPointer(
                *pressed_, {physicalPosition.x / scale_, physicalPosition.y / scale_});
        }
    }
    return Result<void>::success();
}

Result<void> RuntimeUI::updateSliderFromPointer(UIElementId id, Vec2 logicalPosition) {
    auto* target = widget(id);
    const auto* element = model_.find(id);
    if (target == nullptr || element == nullptr || target->type != UIWidgetType::Slider)
        return Result<void>::failure(Error(ErrorCode::TypeMismatch, "UI target is not a slider"));
    const auto width = element->computedRect.width;
    const auto normalized = width <= 0.0F
                                ? 0.0F
                                : std::clamp((logicalPosition.x - element->computedRect.x) / width,
                                             0.0F, 1.0F);
    const auto raw = target->minimum + (target->maximum - target->minimum) * normalized;
    const auto steps = std::round((raw - target->minimum) / target->step);
    return setValue(id, target->minimum + steps * target->step, true);
}

Result<void> RuntimeUI::pointerDown(Vec2 physicalPosition) {
    if (!finite(physicalPosition))
        return Result<void>::failure(Error(ErrorCode::InvalidArgument, "UI pointer is invalid"));
    pressed_ = hitTest(physicalPosition);
    if (!pressed_)
        return Result<void>::success();
    const auto previousFocus = model_.focused();
    auto focused = model_.setFocus(pressed_);
    if (!focused)
        return focused;
    if (model_.focused() != previousFocus)
        pushEvent(UIEventType::FocusChanged, *pressed_);

    auto* target = widget(*pressed_);
    if (target != nullptr && target->type == UIWidgetType::Slider) {
        return updateSliderFromPointer(
            *pressed_, {physicalPosition.x / scale_, physicalPosition.y / scale_});
    }
    if (target != nullptr && target->type == UIWidgetType::List && !target->items.empty()) {
        const auto* element = model_.find(*pressed_);
        const auto localY = physicalPosition.y / scale_ - element->computedRect.y;
        const auto itemHeight = element->computedRect.height /
                                static_cast<float>(target->items.size());
        const auto rawIndex = itemHeight <= 0.0F ? std::size_t{0}
                                                : static_cast<std::size_t>(localY / itemHeight);
        const auto selected = std::min(rawIndex, target->items.size() - 1U);
        if (!target->selectedItem || *target->selectedItem != selected) {
            target->selectedItem = selected;
            pushEvent(UIEventType::SelectionChanged, *pressed_,
                      static_cast<float>(selected), selected);
        }
    }
    return Result<void>::success();
}

Result<void> RuntimeUI::activate(UIElementId id, UIKey key) {
    auto* target = widget(id);
    if (target == nullptr || !effectivelyInteractive(id))
        return Result<void>::failure(Error(ErrorCode::InvalidState, "UI widget is not interactive"));
    if (target->type == UIWidgetType::Button) {
        if (key == UIKey::Enter || key == UIKey::Space)
            pushEvent(UIEventType::Clicked, id);
        return Result<void>::success();
    }
    if (target->type == UIWidgetType::Toggle) {
        if (key == UIKey::Enter || key == UIKey::Space) {
            target->checked = !target->checked;
            pushEvent(UIEventType::Toggled, id, target->checked ? 1.0F : 0.0F);
        }
        return Result<void>::success();
    }
    if (target->type == UIWidgetType::Slider) {
        auto value = target->value;
        if (key == UIKey::Left || key == UIKey::Down)
            value -= target->step;
        else if (key == UIKey::Right || key == UIKey::Up)
            value += target->step;
        else if (key == UIKey::Home)
            value = target->minimum;
        else if (key == UIKey::End)
            value = target->maximum;
        return setValue(id, value, true);
    }
    if (target->type == UIWidgetType::List && !target->items.empty()) {
        auto selected = target->selectedItem.value_or(0U);
        if (key == UIKey::Up || key == UIKey::Left)
            selected = selected == 0U ? target->items.size() - 1U : selected - 1U;
        else if (key == UIKey::Down || key == UIKey::Right)
            selected = (selected + 1U) % target->items.size();
        else if (key == UIKey::Home)
            selected = 0U;
        else if (key == UIKey::End)
            selected = target->items.size() - 1U;
        else if (key == UIKey::Enter || key == UIKey::Space) {
            pushEvent(UIEventType::Clicked, id, static_cast<float>(selected), selected);
            return Result<void>::success();
        } else {
            return Result<void>::success();
        }
        if (!target->selectedItem || *target->selectedItem != selected) {
            target->selectedItem = selected;
            pushEvent(UIEventType::SelectionChanged, id, static_cast<float>(selected), selected);
        }
    }
    return Result<void>::success();
}

Result<void> RuntimeUI::pointerUp(Vec2 physicalPosition) {
    if (!finite(physicalPosition))
        return Result<void>::failure(Error(ErrorCode::InvalidArgument, "UI pointer is invalid"));
    const auto wasPressed = pressed_;
    pressed_.reset();
    if (!wasPressed || hitTest(physicalPosition) != wasPressed)
        return Result<void>::success();
    const auto* target = widget(*wasPressed);
    if (target != nullptr && target->type == UIWidgetType::Slider)
        return updateSliderFromPointer(
            *wasPressed, {physicalPosition.x / scale_, physicalPosition.y / scale_});
    return activate(*wasPressed, UIKey::Space);
}

Result<void> RuntimeUI::keyDown(UIKey key) {
    if (key == UIKey::Tab || key == UIKey::ReverseTab) {
        const auto previous = model_.focused();
        const auto focused = model_.focusNext(key == UIKey::ReverseTab);
        if (focused && focused != previous)
            pushEvent(UIEventType::FocusChanged, *focused);
        return Result<void>::success();
    }
    if (!model_.focused())
        return Result<void>::success();
    return activate(*model_.focused(), key);
}

std::vector<UIEvent> RuntimeUI::consumeEvents() {
    auto output = std::move(events_);
    events_.clear();
    return output;
}

} // namespace fabgl
