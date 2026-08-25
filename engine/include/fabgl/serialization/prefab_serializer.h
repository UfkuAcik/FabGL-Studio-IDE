#pragma once

#include "fabgl/core/result.h"
#include "fabgl/prefab/prefab.h"

#include <string>
#include <string_view>

namespace fabgl {

class PrefabSerializer final {
  public:
    static constexpr int CurrentVersion = 2;

    [[nodiscard]] static Result<std::string> serialize(const PrefabAsset& prefab);
    [[nodiscard]] static Result<PrefabAsset> deserialize(std::string_view text);
};

} // namespace fabgl
