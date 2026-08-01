#include "fabgl/scripting/script_component.h"

#include <utility>

namespace fabgl::scripting {

TypeMetadata makeScriptMetadata(std::string stableName, std::string displayName) {
    TypeMetadata metadata;
    metadata.typeId = ComponentTypeGuid::fromStableName("fabgl.script." + stableName + ".v1");
    metadata.name = std::move(stableName);
    metadata.displayName = std::move(displayName);
    return metadata;
}

ScriptComponent::ScriptComponent(TypeMetadata metadata, ApiVersion requiredApiVersion)
    : metadata_(std::move(metadata)), requiredApiVersion_(requiredApiVersion) {}

} // namespace fabgl::scripting
