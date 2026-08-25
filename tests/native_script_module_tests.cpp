#include <fabgl/project/project_script_modules.h>
#include <fabgl/reflection/reflection.h>
#include <fabgl/scene/builtin_components.h>
#include <fabgl/scene/entity.h>
#include <fabgl/scene/scene.h>

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "expected one gameplay module path\n";
        return EXIT_FAILURE;
    }
    auto modules = fabgl::project::ProjectScriptModules::load({argv[1]});
    if (!modules) {
        std::cerr << modules.error().message() << '\n';
        return EXIT_FAILURE;
    }

    fabgl::ReflectionRegistry registry;
    if (!fabgl::registerBuiltinComponentTypes(registry)) {
        return EXIT_FAILURE;
    }
    fabgl::Scene scene("Native module integration");
    auto entity = scene.createEntity("Scripted");
    if (!entity) {
        return EXIT_FAILURE;
    }
    auto placeholder = fabgl::createBuiltinDataComponent(registry, "ScriptComponent");
    if (!placeholder ||
        !placeholder.value()->set("class", std::string("game.NativeTestScript")) ||
        !entity.value()->addComponent(std::move(placeholder.value()))) {
        return EXIT_FAILURE;
    }
    auto attached = modules.value().attach(scene);
    if (!attached || attached.value() != 1U ||
        modules.value().stats().attachedComponents != 1U) {
        return EXIT_FAILURE;
    }
    if (!scene.start() || !scene.update(0.5F)) {
        return EXIT_FAILURE;
    }
    const auto position = entity.value()->transform().localPosition();
    if (position.x != 30.0F) {
        std::cerr << "native script lifecycle did not mutate its owner\n";
        return EXIT_FAILURE;
    }
    scene.shutdown();
    std::cout << "native gameplay module ABI v1 load/attach/update passed\n";
    return EXIT_SUCCESS;
}
