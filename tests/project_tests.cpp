#include "test_harness.h"

#include <fabgl/core/guid.h>
#include <project_format.h>
#include <script_generator.h>

#include <string>

FGL_TEST(project_manifest_round_trips_unicode_and_arguments) {
    fabgl::project::Manifest manifest;
    manifest.projectGuid = fabgl::AssetGuid::fromStableName("project:test").toString();
    manifest.name = "Gökyüzü 🚀";
    manifest.startupScene = "Scenes/Ana Sahne.fglscene";
    manifest.buildProgram = "C:/Program Files/CMake/bin/cmake.exe";
    manifest.buildArguments = {"--build", "out/build/Türkçe Debug"};
    auto encoded = fabgl::project::serializeManifest(manifest);
    FGL_CHECK(encoded);
    FGL_CHECK(encoded.value().find("\"scene\"") == std::string::npos);
    auto decoded = fabgl::project::parseManifest(encoded.value());
    FGL_CHECK(decoded);
    FGL_CHECK(decoded.value().projectGuid == manifest.projectGuid);
    FGL_CHECK(decoded.value().name == manifest.name);
    FGL_CHECK(decoded.value().startupScene == manifest.startupScene);
    FGL_CHECK(decoded.value().buildArguments == manifest.buildArguments);
}

FGL_TEST(project_manifest_accepts_migratable_v0_and_rejects_newer_or_unsafe) {
    const std::string legacy = R"json({
      "kind": "FabGLProject",
      "formatVersion": 0,
      "name": "Legacy",
      "projectRoot": ".",
      "scene": {"entities": []}
    })json";
    auto parsed = fabgl::project::parseManifest(legacy);
    FGL_CHECK(parsed);
    FGL_CHECK(parsed.value().sourceVersion == 0);

    const std::string newer = R"json({
      "kind":"FabGLStudioProject","formatVersion":99,"name":"Future",
      "projectRoot":".","scene":{"entities":[]}
    })json";
    FGL_CHECK(!fabgl::project::parseManifest(newer));

    const std::string unsafe = R"json({
      "kind":"FabGLStudioProject","formatVersion":1,
      "projectGuid":"11111111-1111-4111-8111-111111111111","name":"Unsafe",
      "projectRoot":".","startupScene":"../outside.fglscene","scene":{"entities":[]}
    })json";
    FGL_CHECK(!fabgl::project::parseManifest(unsafe));
}

FGL_TEST(project_manifest_reports_corrupt_json) {
    FGL_CHECK(!fabgl::project::parseManifest("{\"kind\": \"FabGLStudioProject\",]"));
    FGL_CHECK(!fabgl::project::parseManifest(
        "{\"kind\":\"FabGLStudioProject\",\"formatVersion\":1,"
        "\"name\":\"bad\\uD800\",\"projectRoot\":\".\",\"scene\":{}}"));
}

FGL_TEST(gameplay_script_generator_rejects_paths_and_emits_reflected_component) {
    FGL_CHECK(!fabgl::project::generateGameplayScript("../Unsafe"));
    FGL_CHECK(!fabgl::project::generateGameplayScript("9Invalid"));
    auto generated = fabgl::project::generateGameplayScript("PlayerController");
    FGL_CHECK(generated);
    FGL_CHECK(generated.value().headerFileName == "PlayerController.h");
    FGL_CHECK(generated.value().sourceFileName == "PlayerController.cpp");
    FGL_CHECK(generated.value().header.find("ScriptComponent") != std::string::npos);
    FGL_CHECK(generated.value().source.find("game.PlayerController") != std::string::npos);
    FGL_CHECK(generated.value().source.find("scriptProperty") != std::string::npos);
}
