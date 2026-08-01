#pragma once

#include "fabgl/core/result.h"

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace fabgl {

struct InputBinding final {
    std::string control;
    float scale = 1.0F;
    float threshold = 0.5F;
};

struct ActionState final {
    bool held = false;
    bool pressed = false;
    bool released = false;
};

class InputMap final {
  public:
    [[nodiscard]] Result<void> defineContext(std::string name, int priority, bool enabled = true);
    [[nodiscard]] Result<void> setContextEnabled(std::string_view context, bool enabled);
    [[nodiscard]] Result<void> setContextPriority(std::string_view context, int priority);

    [[nodiscard]] Result<void> bindAction(std::string_view context, std::string action,
                                          InputBinding binding);
    [[nodiscard]] Result<void> bindAxis(std::string_view context, std::string axis,
                                        InputBinding binding);
    [[nodiscard]] Result<void> rebindAction(std::string_view context, std::string_view action,
                                            std::size_t bindingIndex, InputBinding binding);
    [[nodiscard]] Result<void> rebindAxis(std::string_view context, std::string_view axis,
                                          std::size_t bindingIndex, InputBinding binding);
    [[nodiscard]] Result<void> clearActionBindings(std::string_view context,
                                                   std::string_view action);
    [[nodiscard]] Result<void> clearAxisBindings(std::string_view context, std::string_view axis);

    [[nodiscard]] Result<void> setControlValue(std::string control, float value);
    void clearControlValues() noexcept {
        controls_.clear();
    }
    void update();

    [[nodiscard]] ActionState action(std::string_view name) const;
    [[nodiscard]] float axis(std::string_view name) const;

  private:
    struct Context final {
        int priority = 0;
        bool enabled = true;
        std::map<std::string, std::vector<InputBinding>> actions;
        std::map<std::string, std::vector<InputBinding>> axes;
    };

    [[nodiscard]] Result<Context*> requireContext(std::string_view name);
    [[nodiscard]] Result<void> validateBinding(const InputBinding& binding) const;
    [[nodiscard]] float controlValue(std::string_view control) const;

    std::map<std::string, Context> contexts_;
    std::map<std::string, float> controls_;
    std::map<std::string, ActionState> actionStates_;
    std::map<std::string, float> axisStates_;
};

} // namespace fabgl
