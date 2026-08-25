#pragma once

#include "project_format.h"

#include <fabgl/input/input_map.h>

namespace fabgl::project {

[[nodiscard]] Result<InputMap> buildInputMap(const Manifest& manifest);

} // namespace fabgl::project
