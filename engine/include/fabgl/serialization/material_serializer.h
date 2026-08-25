#pragma once

#include "fabgl/core/guid.h"
#include "fabgl/core/result.h"
#include "fabgl/material/material.h"

#include <string>
#include <string_view>

namespace fabgl {

struct MaterialAsset final {
    AssetGuid id;
    std::string name;
    Material material;
};

class MaterialSerializer final {
  public:
    static constexpr int CurrentVersion = 1;

    [[nodiscard]] static Result<std::string> serialize(const MaterialAsset& asset);
    [[nodiscard]] static Result<MaterialAsset> deserialize(std::string_view text);
};

} // namespace fabgl
