#include "fabgl/ui/ui_layout.h"

#include <algorithm>
#include <cmath>

namespace fabgl {

Result<void> UIModel::validateProperties(const UILayoutProperties& properties) const {
    const auto& minimum = properties.anchors.minimum;
    const auto& maximum = properties.anchors.maximum;
    if (!std::isfinite(minimum.x) || !std::isfinite(minimum.y) || !std::isfinite(maximum.x) ||
        !std::isfinite(maximum.y) || minimum.x < 0.0F || minimum.y < 0.0F || maximum.x > 1.0F ||
        maximum.y > 1.0F || minimum.x > maximum.x || minimum.y > maximum.y ||
        !std::isfinite(properties.minimumOffset.x) || !std::isfinite(properties.minimumOffset.y) ||
        !std::isfinite(properties.maximumOffset.x) || !std::isfinite(properties.maximumOffset.y)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "UI layout properties are invalid"));
    }
    return Result<void>::success();
}

Result<UIElementId> UIModel::addElement(std::optional<UIElementId> parent,
                                        UILayoutProperties properties) {
    auto valid = validateProperties(properties);
    if (!valid)
        return Result<UIElementId>::failure(valid.error());
    UIElement* parentElement = nullptr;
    if (parent) {
        parentElement = find(*parent);
        if (parentElement == nullptr) {
            return Result<UIElementId>::failure(
                Error(ErrorCode::NotFound, "UI parent was not found"));
        }
    }
    const UIElementId id{nextId_++};
    elements_.emplace(id, UIElement{id, parent, {}, properties, {}});
    if (parentElement != nullptr)
        parentElement->children.push_back(id);
    else
        roots_.push_back(id);
    return Result<UIElementId>::success(id);
}

UIElement* UIModel::find(UIElementId id) noexcept {
    const auto iterator = elements_.find(id);
    return iterator == elements_.end() ? nullptr : &iterator->second;
}

const UIElement* UIModel::find(UIElementId id) const noexcept {
    const auto iterator = elements_.find(id);
    return iterator == elements_.end() ? nullptr : &iterator->second;
}

void UIModel::removeRecursive(UIElementId id) {
    auto* element = find(id);
    if (element == nullptr)
        return;
    const auto children = element->children;
    for (const auto child : children)
        removeRecursive(child);
    if (focused_ && *focused_ == id)
        focused_.reset();
    elements_.erase(id);
}

Result<void> UIModel::removeElement(UIElementId id) {
    auto* element = find(id);
    if (element == nullptr) {
        return Result<void>::failure(Error(ErrorCode::NotFound, "UI element was not found"));
    }
    if (element->parent) {
        auto* parent = find(*element->parent);
        if (parent != nullptr) {
            parent->children.erase(
                std::remove(parent->children.begin(), parent->children.end(), id),
                parent->children.end());
        }
    } else {
        roots_.erase(std::remove(roots_.begin(), roots_.end(), id), roots_.end());
    }
    removeRecursive(id);
    return Result<void>::success();
}

Result<void> UIModel::reparent(UIElementId childId, std::optional<UIElementId> parentId) {
    auto* child = find(childId);
    if (child == nullptr)
        return Result<void>::failure(Error(ErrorCode::NotFound, "UI child was not found"));
    UIElement* parent = nullptr;
    if (parentId) {
        parent = find(*parentId);
        if (parent == nullptr)
            return Result<void>::failure(Error(ErrorCode::NotFound, "UI parent was not found"));
        auto cursor = parent;
        while (cursor != nullptr) {
            if (cursor->id == childId) {
                return Result<void>::failure(
                    Error(ErrorCode::CycleDetected, "UI parenting would create a cycle"));
            }
            cursor = cursor->parent ? find(*cursor->parent) : nullptr;
        }
    }
    if (child->parent == parentId)
        return Result<void>::success();
    if (child->parent) {
        auto* previous = find(*child->parent);
        if (previous != nullptr) {
            previous->children.erase(
                std::remove(previous->children.begin(), previous->children.end(), childId),
                previous->children.end());
        }
    } else {
        roots_.erase(std::remove(roots_.begin(), roots_.end(), childId), roots_.end());
    }
    child->parent = parentId;
    if (parent != nullptr)
        parent->children.push_back(childId);
    else
        roots_.push_back(childId);
    return Result<void>::success();
}

Result<void> UIModel::setProperties(UIElementId id, UILayoutProperties properties) {
    auto valid = validateProperties(properties);
    if (!valid)
        return valid;
    auto* element = find(id);
    if (element == nullptr)
        return Result<void>::failure(Error(ErrorCode::NotFound, "UI element was not found"));
    element->properties = properties;
    if (focused_) {
        const auto* focusedElement = find(*focused_);
        if (focusedElement == nullptr || !effectivelyFocusable(*focusedElement))
            focused_.reset();
    }
    return Result<void>::success();
}

Result<void> UIModel::layoutElement(UIElement& element, Rect parentRect) {
    const auto& properties = element.properties;
    const auto left =
        parentRect.x + parentRect.width * properties.anchors.minimum.x + properties.minimumOffset.x;
    const auto top = parentRect.y + parentRect.height * properties.anchors.minimum.y +
                     properties.minimumOffset.y;
    const auto right =
        parentRect.x + parentRect.width * properties.anchors.maximum.x + properties.maximumOffset.x;
    const auto bottom = parentRect.y + parentRect.height * properties.anchors.maximum.y +
                        properties.maximumOffset.y;
    if (right < left || bottom < top) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "UI anchors and offsets produce a negative size")
                .addContext("element", std::to_string(element.id.value)));
    }
    element.computedRect = {left, top, right - left, bottom - top};
    for (const auto childId : element.children) {
        auto* child = find(childId);
        if (child == nullptr) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidState, "UI hierarchy contains a missing child"));
        }
        auto result = layoutElement(*child, element.computedRect);
        if (!result)
            return result;
    }
    return Result<void>::success();
}

Result<void> UIModel::layout(Rect rootRect) {
    if (!std::isfinite(rootRect.x) || !std::isfinite(rootRect.y) ||
        !std::isfinite(rootRect.width) || !std::isfinite(rootRect.height) ||
        rootRect.width < 0.0F || rootRect.height < 0.0F) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "UI root rectangle is invalid"));
    }
    for (const auto id : roots_) {
        auto* root = find(id);
        if (root == nullptr)
            return Result<void>::failure(Error(ErrorCode::InvalidState, "UI root is missing"));
        auto result = layoutElement(*root, rootRect);
        if (!result)
            return result;
    }
    return Result<void>::success();
}

bool UIModel::effectivelyFocusable(const UIElement& element) const noexcept {
    if (!element.properties.focusable || !element.properties.visible || !element.properties.enabled)
        return false;
    auto parent = element.parent;
    while (parent) {
        const auto* ancestor = find(*parent);
        if (ancestor == nullptr || !ancestor->properties.visible || !ancestor->properties.enabled)
            return false;
        parent = ancestor->parent;
    }
    return true;
}

Result<void> UIModel::setFocus(std::optional<UIElementId> id) {
    if (!id) {
        focused_.reset();
        return Result<void>::success();
    }
    const auto* element = find(*id);
    if (element == nullptr)
        return Result<void>::failure(Error(ErrorCode::NotFound, "UI focus target was not found"));
    if (!effectivelyFocusable(*element)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "UI focus target is not focusable"));
    }
    focused_ = id;
    return Result<void>::success();
}

void UIModel::collectFocusable(UIElementId id, std::vector<UIElementId>& output) const {
    const auto* element = find(id);
    if (element == nullptr)
        return;
    if (effectivelyFocusable(*element))
        output.push_back(id);
    for (const auto child : element->children)
        collectFocusable(child, output);
}

std::optional<UIElementId> UIModel::focusNext(bool reverse) {
    std::vector<UIElementId> focusable;
    for (const auto root : roots_)
        collectFocusable(root, focusable);
    if (focusable.empty()) {
        focused_.reset();
        return std::nullopt;
    }
    const auto current =
        focused_ ? std::find(focusable.begin(), focusable.end(), *focused_) : focusable.end();
    if (current == focusable.end()) {
        focused_ = reverse ? focusable.back() : focusable.front();
        return focused_;
    }
    const auto index = static_cast<std::size_t>(std::distance(focusable.begin(), current));
    const auto next = reverse ? (index == 0U ? focusable.size() - 1U : index - 1U)
                              : (index + 1U) % focusable.size();
    focused_ = focusable[next];
    return focused_;
}

} // namespace fabgl
