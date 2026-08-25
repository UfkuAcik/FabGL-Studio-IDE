#include "project_input_map.h"

#include <utility>

namespace fabgl::project {

Result<InputMap> buildInputMap(const Manifest& manifest) {
    InputMap map;
    for (const auto& context : manifest.inputContexts) {
        auto defined = map.defineContext(context.name, context.priority, context.enabled);
        if (!defined) {
            return Result<InputMap>::failure(
                defined.error().withContext("projectInputContext", context.name));
        }
        for (const auto& action : context.actions) {
            for (const auto& binding : action.bindings) {
                auto bound =
                    map.bindAction(context.name, action.name,
                                   InputBinding{binding.control, binding.scale, binding.threshold});
                if (!bound) {
                    return Result<InputMap>::failure(
                        bound.error()
                            .withContext("projectInputContext", context.name)
                            .withContext("projectInputAction", action.name));
                }
            }
        }
        for (const auto& axis : context.axes) {
            for (const auto& binding : axis.bindings) {
                auto bound =
                    map.bindAxis(context.name, axis.name,
                                 InputBinding{binding.control, binding.scale, binding.threshold});
                if (!bound) {
                    return Result<InputMap>::failure(
                        bound.error()
                            .withContext("projectInputContext", context.name)
                            .withContext("projectInputAxis", axis.name));
                }
            }
        }
    }
    return Result<InputMap>::success(std::move(map));
}

} // namespace fabgl::project
