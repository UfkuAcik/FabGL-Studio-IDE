#pragma once

#include "fabgl/core/result.h"

#include <memory>
#include <string>
#include <string_view>

namespace fabgl {

class Scene;

class SceneSerializer final {
  public:
    static constexpr int CurrentVersion = 1;

    [[nodiscard]] static Result<std::string> serialize(const Scene& scene);
    [[nodiscard]] static Result<std::unique_ptr<Scene>> deserialize(std::string_view text);
};

} // namespace fabgl
