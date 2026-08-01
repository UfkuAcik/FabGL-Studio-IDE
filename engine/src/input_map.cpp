#include "fabgl/input/input_map.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace fabgl {

Result<void> InputMap::defineContext(std::string name, int priority, bool enabled) {
    if (name.empty()) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "input context name cannot be empty"));
    }
    if (contexts_.find(name) != contexts_.end()) {
        return Result<void>::failure(Error(ErrorCode::AlreadyExists, "input context already exists")
                                         .addContext("context", name));
    }
    contexts_.emplace(std::move(name), Context{priority, enabled, {}, {}});
    return Result<void>::success();
}

Result<InputMap::Context*> InputMap::requireContext(std::string_view name) {
    const auto iterator = contexts_.find(std::string(name));
    if (iterator == contexts_.end()) {
        return Result<Context*>::failure(Error(ErrorCode::NotFound, "input context was not found")
                                             .addContext("context", std::string(name)));
    }
    return Result<Context*>::success(&iterator->second);
}

Result<void> InputMap::setContextEnabled(std::string_view context, bool enabled) {
    auto found = requireContext(context);
    if (!found)
        return Result<void>::failure(found.error());
    found.value()->enabled = enabled;
    return Result<void>::success();
}

Result<void> InputMap::setContextPriority(std::string_view context, int priority) {
    auto found = requireContext(context);
    if (!found)
        return Result<void>::failure(found.error());
    found.value()->priority = priority;
    return Result<void>::success();
}

Result<void> InputMap::validateBinding(const InputBinding& binding) const {
    if (binding.control.empty()) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "input control name cannot be empty"));
    }
    if (!std::isfinite(binding.scale) || !std::isfinite(binding.threshold) ||
        binding.threshold < 0.0F || binding.threshold > 1.0F) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "input binding scale/threshold is invalid"));
    }
    return Result<void>::success();
}

Result<void> InputMap::bindAction(std::string_view context, std::string actionName,
                                  InputBinding binding) {
    if (actionName.empty()) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "action name cannot be empty"));
    }
    auto valid = validateBinding(binding);
    if (!valid)
        return valid;
    auto found = requireContext(context);
    if (!found)
        return Result<void>::failure(found.error());
    found.value()->actions[std::move(actionName)].push_back(std::move(binding));
    return Result<void>::success();
}

Result<void> InputMap::bindAxis(std::string_view context, std::string axisName,
                                InputBinding binding) {
    if (axisName.empty()) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "axis name cannot be empty"));
    }
    auto valid = validateBinding(binding);
    if (!valid)
        return valid;
    auto found = requireContext(context);
    if (!found)
        return Result<void>::failure(found.error());
    found.value()->axes[std::move(axisName)].push_back(std::move(binding));
    return Result<void>::success();
}

Result<void> InputMap::rebindAction(std::string_view context, std::string_view actionName,
                                    std::size_t bindingIndex, InputBinding binding) {
    auto valid = validateBinding(binding);
    if (!valid)
        return valid;
    auto found = requireContext(context);
    if (!found)
        return Result<void>::failure(found.error());
    const auto action = found.value()->actions.find(std::string(actionName));
    if (action == found.value()->actions.end() || bindingIndex >= action->second.size()) {
        return Result<void>::failure(Error(ErrorCode::NotFound, "action binding was not found"));
    }
    action->second[bindingIndex] = std::move(binding);
    return Result<void>::success();
}

Result<void> InputMap::rebindAxis(std::string_view context, std::string_view axisName,
                                  std::size_t bindingIndex, InputBinding binding) {
    auto valid = validateBinding(binding);
    if (!valid)
        return valid;
    auto found = requireContext(context);
    if (!found)
        return Result<void>::failure(found.error());
    const auto axis = found.value()->axes.find(std::string(axisName));
    if (axis == found.value()->axes.end() || bindingIndex >= axis->second.size()) {
        return Result<void>::failure(Error(ErrorCode::NotFound, "axis binding was not found"));
    }
    axis->second[bindingIndex] = std::move(binding);
    return Result<void>::success();
}

Result<void> InputMap::clearActionBindings(std::string_view context, std::string_view actionName) {
    auto found = requireContext(context);
    if (!found)
        return Result<void>::failure(found.error());
    if (found.value()->actions.erase(std::string(actionName)) == 0U) {
        return Result<void>::failure(Error(ErrorCode::NotFound, "action was not found"));
    }
    return Result<void>::success();
}

Result<void> InputMap::clearAxisBindings(std::string_view context, std::string_view axisName) {
    auto found = requireContext(context);
    if (!found)
        return Result<void>::failure(found.error());
    if (found.value()->axes.erase(std::string(axisName)) == 0U) {
        return Result<void>::failure(Error(ErrorCode::NotFound, "axis was not found"));
    }
    return Result<void>::success();
}

Result<void> InputMap::setControlValue(std::string control, float value) {
    if (control.empty() || !std::isfinite(value)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "control name/value is invalid"));
    }
    controls_[std::move(control)] = std::clamp(value, -1.0F, 1.0F);
    return Result<void>::success();
}

float InputMap::controlValue(std::string_view control) const {
    const auto iterator = controls_.find(std::string(control));
    return iterator == controls_.end() ? 0.0F : iterator->second;
}

void InputMap::update() {
    std::vector<std::pair<std::string, const Context*>> enabledContexts;
    for (const auto& context : contexts_) {
        if (context.second.enabled)
            enabledContexts.push_back({context.first, &context.second});
    }
    std::sort(enabledContexts.begin(), enabledContexts.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.second->priority != rhs.second->priority) {
            return lhs.second->priority > rhs.second->priority;
        }
        return lhs.first < rhs.first;
    });

    std::map<std::string, bool> currentActions;
    std::map<std::string, float> currentAxes;
    for (const auto& context : enabledContexts) {
        for (const auto& actionBinding : context.second->actions) {
            if (currentActions.find(actionBinding.first) != currentActions.end())
                continue;
            bool held = false;
            for (const auto& binding : actionBinding.second) {
                if (std::fabs(controlValue(binding.control) * binding.scale) >= binding.threshold) {
                    held = true;
                    break;
                }
            }
            currentActions.emplace(actionBinding.first, held);
        }
        for (const auto& axisBinding : context.second->axes) {
            if (currentAxes.find(axisBinding.first) != currentAxes.end())
                continue;
            float value = 0.0F;
            for (const auto& binding : axisBinding.second) {
                value += controlValue(binding.control) * binding.scale;
            }
            currentAxes.emplace(axisBinding.first, std::clamp(value, -1.0F, 1.0F));
        }
    }

    std::set<std::string> actionNames;
    for (const auto& state : actionStates_)
        actionNames.insert(state.first);
    for (const auto& state : currentActions)
        actionNames.insert(state.first);
    std::map<std::string, ActionState> nextStates;
    for (const auto& name : actionNames) {
        const auto previous = actionStates_.find(name);
        const bool wasHeld = previous != actionStates_.end() && previous->second.held;
        const auto current = currentActions.find(name);
        const bool held = current != currentActions.end() && current->second;
        nextStates.emplace(name, ActionState{held, held && !wasHeld, !held && wasHeld});
    }
    actionStates_ = std::move(nextStates);
    axisStates_ = std::move(currentAxes);
}

ActionState InputMap::action(std::string_view name) const {
    const auto iterator = actionStates_.find(std::string(name));
    return iterator == actionStates_.end() ? ActionState{} : iterator->second;
}

float InputMap::axis(std::string_view name) const {
    const auto iterator = axisStates_.find(std::string(name));
    return iterator == axisStates_.end() ? 0.0F : iterator->second;
}

} // namespace fabgl
