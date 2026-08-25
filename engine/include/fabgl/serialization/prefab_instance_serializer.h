#pragma once

#include "fabgl/core/result.h"
#include "fabgl/prefab/prefab.h"

#include <string>
#include <string_view>

namespace fabgl {

// Canonical scene-link codec. The resulting payload is stored by the internal
// fabgl.PrefabInstanceLink component in a normal Scene v2 component property.
class PrefabInstanceSerializer final {
  public:
    static constexpr int CurrentVersion = 1;
    static constexpr std::size_t MaximumEntities = 4096U;
    static constexpr std::size_t MaximumMappings = 4096U;
    static constexpr std::size_t MaximumRemovedComponents = 4096U;

    [[nodiscard]] static Result<std::string> serialize(const PrefabSceneInstance& instance);
    [[nodiscard]] static Result<PrefabSceneInstance> deserialize(std::string_view text);
};

} // namespace fabgl
