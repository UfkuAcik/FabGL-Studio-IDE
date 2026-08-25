#include <fabgl/assets/asset_importer.h>
#include <fabgl/frameworks/platformer.h>
#include <fabgl/material/material.h>
#include <fabgl/rendering/framebuffer.h>
#include <fabgl/runtime/scene_runtime.h>
#include <fabgl/scene/scene.h>

int main() {
    fabgl::assets::AssetImporterRegistry importers;
    fabgl::Scene scene{"SDK consumer"};
    const auto entity = scene.createEntity("Player");
    if (!entity) {
        return 1;
    }

    fabgl::rendering::Framebuffer framebuffer{16, 16};
    framebuffer.clear(fabgl::Color{0, 0, 0, 255});

    fabgl::frameworks::PlatformerState state{};
    fabgl::frameworks::PlatformerController controller{};
    controller.step(state, {}, 1.0F / 60.0F, {});
    fabgl::SceneRuntime runtime{scene};
    if (!runtime.initialize()) {
        return 2;
    }
    const auto material = fabgl::validateMaterial({}, fabgl::RendererBackend::Renderer2D);
    return framebuffer.width() == 16 && scene.entityCount() == 1 && material.valid() &&
                   importers.size() == 0U
               ? 0
               : 3;
}
