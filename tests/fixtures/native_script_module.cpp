#include <fabgl/scripting/script_component.h>
#include <fabgl/scripting/script_module.h>
#include <fabgl/scene/entity.h>

namespace {

class NativeTestScript final : public fabgl::scripting::ScriptComponent {
  public:
    NativeTestScript() : ScriptComponent(metadataDefinition()) {}

  private:
    [[nodiscard]] static fabgl::TypeMetadata metadataDefinition() {
        return fabgl::scripting::makeScriptMetadata("game.NativeTestScript",
                                                    "Native Test Script");
    }

    void onUpdate(const float deltaSeconds) override {
        auto position = owner()->transform().localPosition();
        position.x += 60.0F * deltaSeconds;
        owner()->transform().setLocalPosition(position);
    }
};

} // namespace

FABGL_REGISTER_SCRIPT(NativeTestScript)

FGL_SCRIPT_MODULE_EXPORT bool
fabglStudioGetScriptModuleV1(fabgl::scripting::ScriptModuleView* output) noexcept {
    return fabgl::scripting::detail::exportRegisteredScriptModule(output);
}
