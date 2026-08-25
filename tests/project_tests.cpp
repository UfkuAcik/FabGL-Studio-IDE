#include "test_harness.h"

#include <ProjectRuntime.h>
#include <esp32_export.h>
#include <fabgl/assets/asset_pack.h>
#include <fabgl/assets/audio_importer.h>
#include <fabgl/assets/file_io.h>
#include <fabgl/assets/tilemap_importer.h>
#include <fabgl/core/guid.h>
#include <fabgl/scene/builtin_components.h>
#include <fabgl/scene/scene.h>
#include <fabgl/serialization/scene_serializer.h>
#include <fabgl/visual/visual_graph.h>
#include <local_package_manager.h>
#include <project_format.h>
#include <project_input_map.h>
#include <project_prepare.h>
#include <script_generator.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

struct RuntimeVectorReader final {
    const std::vector<std::uint8_t>* bytes = nullptr;
    [[nodiscard]] std::size_t size() const noexcept {
        return bytes->size();
    }
    [[nodiscard]] std::uint8_t byte(const std::size_t offset) const noexcept {
        return (*bytes)[offset];
    }
};

std::string joinTestPath(const std::string& left, const std::string& right) {
    return left + (left.empty() || left.back() == '/' || left.back() == '\\' ? "" : "/") + right;
}

void removeTestTree(const std::string& root) noexcept {
#ifdef _WIN32
    WIN32_FIND_DATAA data{};
    const auto handle = FindFirstFileA((joinTestPath(root, "*")).c_str(), &data);
    if (handle != INVALID_HANDLE_VALUE) {
        do {
            const std::string name(data.cFileName);
            if (name == "." || name == "..")
                continue;
            const auto child = joinTestPath(root, name);
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U &&
                (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0U) {
                removeTestTree(child);
            } else {
                DeleteFileA(child.c_str());
            }
        } while (FindNextFileA(handle, &data) != FALSE);
        FindClose(handle);
    }
    RemoveDirectoryA(root.c_str());
#else
    auto* directory = ::opendir(root.c_str());
    if (directory != nullptr) {
        while (const auto* entry = ::readdir(directory)) {
            const std::string name(entry->d_name);
            if (name == "." || name == "..")
                continue;
            const auto child = joinTestPath(root, name);
            struct stat status {};
            if (::lstat(child.c_str(), &status) == 0 && S_ISDIR(status.st_mode) &&
                !S_ISLNK(status.st_mode)) {
                removeTestTree(child);
            } else {
                ::unlink(child.c_str());
            }
        }
        ::closedir(directory);
    }
    ::rmdir(root.c_str());
#endif
}

class ExportTestDirectory final {
  public:
    ExportTestDirectory() : path_("fabgl-export-test-" + fabgl::AssetGuid::generate().toString()) {
        FGL_CHECK(fabgl::assets::createDirectories(path_));
    }

    ~ExportTestDirectory() {
        removeTestTree(path_);
    }

    ExportTestDirectory(const ExportTestDirectory&) = delete;
    ExportTestDirectory& operator=(const ExportTestDirectory&) = delete;

    [[nodiscard]] const std::string& path() const noexcept {
        return path_;
    }

  private:
    std::string path_;
};

void writeTestBytes(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    FGL_CHECK(fabgl::assets::createDirectories(path.substr(0U, path.find_last_of("/\\"))));
    FGL_CHECK(fabgl::assets::writeBinaryFileAtomic(path, bytes));
}

void writeTestText(const std::string& path, const std::string& text) {
    writeTestBytes(path, std::vector<std::uint8_t>(text.begin(), text.end()));
}

void appendProjectU16(std::vector<std::uint8_t>& output, const std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void appendProjectU32(std::vector<std::uint8_t>& output, const std::uint32_t value) {
    for (unsigned int shift = 0U; shift < 32U; shift += 8U)
        output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
}

std::vector<std::uint8_t> makeProjectWav() {
    constexpr std::uint32_t SampleRate = 16'000U;
    constexpr std::uint32_t Frames = 160U;
    std::vector<std::uint8_t> output;
    output.insert(output.end(), {'R', 'I', 'F', 'F'});
    appendProjectU32(output, 36U + Frames * 4U);
    output.insert(output.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    appendProjectU32(output, 16U);
    appendProjectU16(output, 1U);
    appendProjectU16(output, 2U);
    appendProjectU32(output, SampleRate);
    appendProjectU32(output, SampleRate * 4U);
    appendProjectU16(output, 4U);
    appendProjectU16(output, 16U);
    output.insert(output.end(), {'d', 'a', 't', 'a'});
    appendProjectU32(output, Frames * 4U);
    for (std::uint32_t frame = 0U; frame < Frames; ++frame) {
        const auto sample =
            static_cast<std::int16_t>(std::sin(static_cast<float>(frame) * 0.17F) * 10000.0F);
        appendProjectU16(output, static_cast<std::uint16_t>(sample));
        appendProjectU16(output, static_cast<std::uint16_t>(sample / 2));
    }
    return output;
}

#ifdef _WIN32
std::vector<std::uint8_t> makeProjectBmp() {
    constexpr std::uint32_t Width = 8U;
    constexpr std::uint32_t Height = 4U;
    constexpr std::uint32_t RowBytes = 24U;
    constexpr std::uint32_t PixelBytes = RowBytes * Height;
    std::vector<std::uint8_t> output;
    output.insert(output.end(), {'B', 'M'});
    appendProjectU32(output, 54U + PixelBytes);
    appendProjectU32(output, 0U);
    appendProjectU32(output, 54U);
    appendProjectU32(output, 40U);
    appendProjectU32(output, Width);
    appendProjectU32(output, Height);
    appendProjectU16(output, 1U);
    appendProjectU16(output, 24U);
    appendProjectU32(output, 0U);
    appendProjectU32(output, PixelBytes);
    appendProjectU32(output, 2835U);
    appendProjectU32(output, 2835U);
    appendProjectU32(output, 0U);
    appendProjectU32(output, 0U);
    for (std::uint32_t storedY = 0U; storedY < Height; ++storedY) {
        const auto y = Height - storedY - 1U;
        for (std::uint32_t x = 0U; x < Width; ++x) {
            output.push_back(static_cast<std::uint8_t>((x + y) * 18U));
            output.push_back(static_cast<std::uint8_t>(y * 60U));
            output.push_back(static_cast<std::uint8_t>(x * 28U));
        }
    }
    return output;
}
#endif

void removeTestFile(const std::string& path) noexcept {
#ifdef _WIN32
    DeleteFileA(path.c_str());
#else
    ::unlink(path.c_str());
#endif
}

std::vector<std::uint8_t> readTestBytes(const std::string& path) {
    auto bytes = fabgl::assets::readBinaryFile(path);
    FGL_CHECK(bytes);
    return std::move(bytes.value());
}

std::string readTestText(const std::string& path) {
    auto text = fabgl::assets::readTextFile(path);
    FGL_CHECK(text);
    return std::move(text.value());
}

fabgl::project::Manifest writeExportProject(const std::string& projectRoot,
                                            const std::string& sceneText) {
    fabgl::project::Manifest manifest;
    manifest.projectGuid =
        fabgl::AssetGuid::fromStableName("tests.esp32-export.project").toString();
    manifest.name = "Deterministic Export";
    manifest.previewDemo = "platformer";
    auto encoded = fabgl::project::serializeManifest(manifest);
    FGL_CHECK(encoded);
    writeTestText(joinTestPath(projectRoot, "Game.fglproject"), encoded.value());
    writeTestText(joinTestPath(projectRoot, "Scenes/Main.fglscene"), sceneText);
    return manifest;
}

void writeFirmwareTemplate(const std::string& templateRoot) {
    writeTestText(joinTestPath(templateRoot, "BoardProfile.h"),
                  "#pragma once\n#define TEST_BOARD_PROFILE 1\n");
    writeTestText(joinTestPath(templateRoot, "firmware.ino"),
                  "#include \"BoardProfile.h\"\n#include \"ProjectPayload.h\"\n"
                  "void setup() {}\nvoid loop() {}\n");
}

std::vector<std::uint8_t> payloadForType(const fabgl::assets::AssetPack& pack,
                                         std::uint32_t typeId) {
    const auto iterator =
        std::find_if(pack.index.begin(), pack.index.end(),
                     [typeId](const auto& entry) { return entry.typeId == typeId; });
    FGL_CHECK(iterator != pack.index.end());
    return std::vector<std::uint8_t>(
        pack.bytes.begin() +
            static_cast<std::vector<std::uint8_t>::difference_type>(iterator->offset),
        pack.bytes.begin() + static_cast<std::vector<std::uint8_t>::difference_type>(
                                 iterator->offset + iterator->size));
}

} // namespace

FGL_TEST(project_manifest_round_trips_unicode_and_arguments) {
    fabgl::project::Manifest manifest;
    manifest.projectGuid = fabgl::AssetGuid::fromStableName("project:test").toString();
    manifest.name = "Gökyüzü 🚀";
    manifest.startupScene = "Scenes/Ana Sahne.fglscene";
    manifest.previewDemo = "platformer";
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
    FGL_CHECK(decoded.value().previewDemo == manifest.previewDemo);
    FGL_CHECK(decoded.value().buildArguments == manifest.buildArguments);
}

FGL_TEST(project_manifest_v2_round_trips_input_packages_targets_and_builds_runtime_map) {
    fabgl::project::Manifest manifest;
    manifest.projectGuid = fabgl::AssetGuid::fromStableName("project:v2-runtime-map").toString();
    manifest.name = "Project V2";
    manifest.previewDemo = "racer";
    manifest.targetProfiles.pc = "pc.performance";
    manifest.targetProfiles.esp32 = "olimex-esp32-sbc-fabgl-revb";
    const auto trackGuid = fabgl::AssetGuid::fromStableName("project:v2:track");
    const auto skyGuid = fabgl::AssetGuid::fromStableName("project:v2:sky");
    fabgl::project::ProjectAssetEntry track(trackGuid, "Tracks\\Main.fgltrack", "racer.track");
    fabgl::project::ProjectAssetEntry sky(skyGuid, "Assets/Sky.fgli", "image");
    sky.importSettings = R"json({"alphaThreshold":95,"dither":true,"paletteSize":8})json";
    sky.esp32Target = fabgl::assets::AssetTarget::Esp32Psram;
    sky.dependencies = {trackGuid};
    sky.hasImportMetadata = true;
    manifest.assets = {std::move(track), std::move(sky)};

    fabgl::project::InputContextDefinition driving;
    driving.name = "driving";
    driving.priority = 10;
    driving.enabled = true;
    driving.actions = {{"Brake", {{"Key.Space", 1.0F, 0.5F}}}};
    driving.axes = {
        {"Steer", {{"Key.A", -1.0F, 0.25F}, {"Key.D", 1.0F, 0.25F}}},
        {"Throttle", {{"Key.W", 1.5F, 0.2F}}},
    };
    fabgl::project::InputContextDefinition menu;
    menu.name = "menu";
    menu.priority = 100;
    menu.enabled = false;
    menu.actions = {{"Confirm", {{"Key.Enter", 1.0F, 0.5F}}}};
    manifest.inputContexts = {menu, driving};
    const auto version = fabgl::VersionRequirement::parse("^1.2.0");
    FGL_CHECK(version);
    manifest.packageDependencies = {{"racer.content", version.value()}};

    const auto encoded = fabgl::project::serializeManifest(manifest);
    FGL_CHECK(encoded);
    FGL_CHECK(encoded.value().find("\"formatVersion\": 2") != std::string::npos);
    FGL_CHECK(encoded.value().find("\"name\": \"driving\"") <
              encoded.value().find("\"name\": \"menu\""));
    FGL_CHECK(encoded.value().find("\"packages\": [") != std::string::npos);
    FGL_CHECK(encoded.value().find("\"pc\": \"pc.performance\"") != std::string::npos);
    FGL_CHECK(encoded.value().find("Tracks/Main.fgltrack") != std::string::npos);
    FGL_CHECK(encoded.value().find("Tracks\\\\Main.fgltrack") == std::string::npos);
    FGL_CHECK(encoded.value().find("\"esp32Target\": \"psram\"") != std::string::npos);
    FGL_CHECK(encoded.value().find(trackGuid.toString()) != std::string::npos);

    const auto decoded = fabgl::project::parseManifest(encoded.value());
    FGL_CHECK(decoded);
    FGL_CHECK(decoded.value().sourceVersion == 2);
    FGL_CHECK(decoded.value().inputContexts.size() == 2U);
    FGL_CHECK(decoded.value().assets.size() == 2U);
    FGL_CHECK(decoded.value().assets[0].path.find('\\') == std::string::npos);
    const auto skyAsset =
        std::find_if(decoded.value().assets.cbegin(), decoded.value().assets.cend(),
                     [skyGuid](const auto& asset) { return asset.guid == skyGuid; });
    FGL_CHECK(skyAsset != decoded.value().assets.cend());
    FGL_CHECK(skyAsset->hasImportMetadata);
    FGL_CHECK(skyAsset->importSettings ==
              R"json({"alphaThreshold":95,"dither":true,"paletteSize":8})json");
    FGL_CHECK(skyAsset->esp32Target == fabgl::assets::AssetTarget::Esp32Psram);
    FGL_CHECK(skyAsset->dependencies == std::vector<fabgl::AssetGuid>{trackGuid});
    FGL_CHECK(decoded.value().packageDependencies.size() == 1U);
    FGL_CHECK(decoded.value().packageDependencies[0].id == "racer.content");
    FGL_CHECK(decoded.value().packageDependencies[0].version.toString() == "^1.2.0");
    FGL_CHECK(decoded.value().targetProfiles.pc == "pc.performance");
    FGL_CHECK(decoded.value().targetProfiles.esp32 == "olimex-esp32-sbc-fabgl-revb");
    const auto reencoded = fabgl::project::serializeManifest(decoded.value());
    FGL_CHECK(reencoded && reencoded.value() == encoded.value());

    auto input = fabgl::project::buildInputMap(decoded.value());
    FGL_CHECK(input);
    FGL_CHECK(input.value().setControlValue("Key.W", 0.15F));
    FGL_CHECK(input.value().setControlValue("Key.A", 0.2F));
    input.value().update();
    FGL_CHECK_NEAR(input.value().axis("Throttle"), 0.0F, 0.0001F);
    FGL_CHECK_NEAR(input.value().axis("Steer"), 0.0F, 0.0001F);
    FGL_CHECK(input.value().setControlValue("Key.W", 0.5F));
    FGL_CHECK(input.value().setControlValue("Key.A", 0.5F));
    FGL_CHECK(input.value().setControlValue("Key.Space", 1.0F));
    input.value().update();
    FGL_CHECK_NEAR(input.value().axis("Throttle"), 0.75F, 0.0001F);
    FGL_CHECK_NEAR(input.value().axis("Steer"), -0.5F, 0.0001F);
    FGL_CHECK(input.value().action("Brake").pressed);
    FGL_CHECK(!input.value().action("Confirm").held);
}

FGL_TEST(project_manifest_v2_rejects_duplicate_or_invalid_nested_models) {
    fabgl::project::Manifest manifest;
    manifest.projectGuid = fabgl::AssetGuid::fromStableName("project:v2-invalid").toString();
    manifest.name = "Invalid Project Models";
    fabgl::project::InputContextDefinition context;
    context.name = "gameplay";
    context.actions = {{"Jump", {{"Key.Space", 1.0F, 0.5F}}}, {"Jump", {{"Pad.A", 1.0F, 0.5F}}}};
    manifest.inputContexts = {context};
    FGL_CHECK(!fabgl::project::serializeManifest(manifest));

    context.actions.resize(1U);
    context.axes = {{"Move", {{"Axis.LeftX", 1.0F, 1.5F}}}};
    manifest.inputContexts = {context};
    FGL_CHECK(!fabgl::project::serializeManifest(manifest));

    manifest.inputContexts.clear();
    manifest.targetProfiles.esp32 = "../unsafe";
    FGL_CHECK(!fabgl::project::serializeManifest(manifest));

    manifest.targetProfiles.esp32 = "olimex-esp32-sbc-fabgl-revb";
    const auto duplicateGuid = fabgl::AssetGuid::fromStableName("project:duplicate-asset");
    manifest.assets = {{duplicateGuid, "Assets/First.fgli", "image"},
                       {duplicateGuid, "Assets/Second.fgli", "image"}};
    FGL_CHECK(!fabgl::project::serializeManifest(manifest));
    manifest.assets = {
        {fabgl::AssetGuid::fromStableName("project:path-a"), "Assets/Track.fgltrack",
         "racer.track"},
        {fabgl::AssetGuid::fromStableName("project:path-b"), "assets\\TRACK.fgltrack",
         "racer.track"},
    };
    FGL_CHECK(!fabgl::project::serializeManifest(manifest));
    manifest.assets = {{fabgl::AssetGuid::fromStableName("project:unsafe-asset"),
                        "../Outside.fgltrack", "Racer Track"}};
    FGL_CHECK(!fabgl::project::serializeManifest(manifest));

    const std::string unknownNested = R"json({
      "kind":"FabGLStudioProject","formatVersion":2,
      "projectGuid":"11111111-1111-4111-8111-111111111111","name":"Unknown",
      "projectRoot":".","startupScene":"Scenes/Main.fglscene",
      "build":{"program":"cmake","arguments":[]},
      "input":{"contexts":[],"unexpected":true},"packages":[],
      "targetProfiles":{"pc":"pc.default","esp32":"olimex-esp32-sbc-fabgl-revb"}
    })json";
    FGL_CHECK(!fabgl::project::parseManifest(unknownNested));
}

FGL_TEST(project_manifest_migrates_sidecar_only_assets_without_overriding_canonical_metadata) {
    fabgl::project::Manifest manifest;
    manifest.projectGuid =
        fabgl::AssetGuid::fromStableName("project:legacy-asset-index").toString();
    manifest.name = "Legacy Asset Index";
    const auto imageGuid = fabgl::AssetGuid::fromStableName("project:legacy-index:image");
    const auto audioGuid = fabgl::AssetGuid::fromStableName("project:legacy-index:audio");
    fabgl::project::ProjectAssetEntry image(imageGuid, "Assets/Image.png", "image");
    image.importSettings = R"json({"dither":false,"paletteSize":4})json";
    image.esp32Target = fabgl::assets::AssetTarget::Esp32Flash;
    image.hasImportMetadata = true;
    manifest.assets.push_back(image);

    const auto index =
        std::string(R"json({"kind":"fabgl.asset-index","version":1,"assets":[{"guid":")json") +
        imageGuid.toString() +
        R"json(","path":"Assets/Image.png","type":"image","settings":"{\"dither\":true,\"paletteSize\":32}","esp32Target":"psram","dependencies":[],"source":{},"imported":{}},{"guid":")json" +
        audioGuid.toString() +
        R"json(","path":"Assets/Tone.fgla","type":"audio","settings":"{}","esp32Target":"sd","dependencies":[")json" +
        imageGuid.toString() + R"json("],"source":{},"imported":{}}]})json";
    auto merged = fabgl::project::mergeLegacyAssetIndex(index, manifest);
    FGL_CHECK(merged && merged.value());
    FGL_CHECK(manifest.assets.size() == 2U);
    const auto migratedAudio =
        std::find_if(manifest.assets.cbegin(), manifest.assets.cend(),
                     [audioGuid](const auto& asset) { return asset.guid == audioGuid; });
    FGL_CHECK(migratedAudio != manifest.assets.cend());
    FGL_CHECK(migratedAudio->hasImportMetadata);
    FGL_CHECK(migratedAudio->esp32Target == fabgl::assets::AssetTarget::Esp32Sd);
    FGL_CHECK(migratedAudio->dependencies == std::vector<fabgl::AssetGuid>{imageGuid});
    const auto canonicalImage =
        std::find_if(manifest.assets.cbegin(), manifest.assets.cend(),
                     [imageGuid](const auto& asset) { return asset.guid == imageGuid; });
    FGL_CHECK(canonicalImage != manifest.assets.cend());
    FGL_CHECK(canonicalImage->importSettings == image.importSettings);
    FGL_CHECK(canonicalImage->esp32Target == fabgl::assets::AssetTarget::Esp32Flash);
    auto mergedAgain = fabgl::project::mergeLegacyAssetIndex(index, manifest);
    FGL_CHECK(mergedAgain && !mergedAgain.value());
    FGL_CHECK(fabgl::project::serializeManifest(manifest));
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

    const std::string versionOne = R"json({
      "kind":"FabGLStudioProject","formatVersion":1,
      "name":"Legacy One","projectRoot":".",
      "startupScene":"Scenes/Main.fglscene",
      "build":{"program":"cmake","arguments":["--build","out/build/dev"]}
    })json";
    auto migratedModel = fabgl::project::parseManifest(versionOne);
    FGL_CHECK(migratedModel);
    FGL_CHECK(migratedModel.value().sourceVersion == 1);
    FGL_CHECK(fabgl::AssetGuid::parse(migratedModel.value().projectGuid));
    FGL_CHECK(migratedModel.value().inputContexts.empty());
    FGL_CHECK(migratedModel.value().assets.empty());
    FGL_CHECK(migratedModel.value().packageDependencies.empty());
    FGL_CHECK(migratedModel.value().targetProfiles.esp32 == "olimex-esp32-sbc-fabgl-revb");

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
    FGL_CHECK(!fabgl::project::generateGameplayScript("class"));
    FGL_CHECK(!fabgl::project::generateGameplayScript("__Reserved"));
    FGL_CHECK(!fabgl::project::generateGameplayScript("_Reserved"));
    auto generated = fabgl::project::generateGameplayScript("PlayerController");
    FGL_CHECK(generated);
    FGL_CHECK(generated.value().headerFileName == "PlayerController.h");
    FGL_CHECK(generated.value().sourceFileName == "PlayerController.cpp");
    FGL_CHECK(generated.value().header.find("ScriptComponent") != std::string::npos);
    FGL_CHECK(generated.value().source.find("game.PlayerController") != std::string::npos);
    FGL_CHECK(generated.value().source.find("scriptProperty") != std::string::npos);
    FGL_CHECK(generated.value().source.find("FABGL_REGISTER_SCRIPT(PlayerController)") !=
              std::string::npos);
    FGL_CHECK(generated.value().portableHeaderFileName == "PlayerControllerEsp32.h");
    FGL_CHECK(generated.value().portableSourceFileName == "PlayerControllerEsp32.cpp");
    FGL_CHECK(generated.value().portableHeader.find("ProjectScriptRuntime.h") != std::string::npos);
    FGL_CHECK(generated.value().portableSource.find("PlayerControllerEsp32Update") !=
              std::string::npos);
}

FGL_TEST(gameplay_script_build_glue_is_deterministic_and_preserves_custom_project_cmake) {
    const auto glue = fabgl::project::generateGameplayCMakeGlue();
    FGL_CHECK(glue.rfind(fabgl::project::GameplayCMakeMarker, 0U) == 0U);
    FGL_CHECK(glue.find("GLOB_RECURSE") != std::string::npos);
    FGL_CHECK(glue.find("list(SORT _fabgl_gameplay_sources)") != std::string::npos);
    FGL_CHECK(glue.find("No gameplay C++ sources were found") != std::string::npos);
    FGL_CHECK(glue.find("Scripts tree cannot contain a symbolic link") != std::string::npos);
    FGL_CHECK(glue.find("Gameplay source cannot be a symbolic link") != std::string::npos);
    FGL_CHECK(glue.find("FabGLStudio::Engine") != std::string::npos);
    FGL_CHECK(glue.find("ESP32") != std::string::npos);
    FGL_CHECK(glue.find("-Werror") != std::string::npos);
    FGL_CHECK(glue.find("/WX") != std::string::npos);

    const auto projectCMake = fabgl::project::generateProjectCMake();
    FGL_CHECK(projectCMake.rfind(fabgl::project::ProjectCMakeMarker, 0U) == 0U);
    FGL_CHECK(projectCMake.find("find_package(FabGLStudio CONFIG REQUIRED)") != std::string::npos);
    FGL_CHECK(projectCMake.find("FabGLStudioScripts.cmake") != std::string::npos);

    ExportTestDirectory temporary;
    const auto projectRoot = joinTestPath(temporary.path(), "GameplayProject");
    FGL_CHECK(fabgl::assets::createDirectories(projectRoot));
    const std::string customProjectCMake =
        "cmake_minimum_required(VERSION 3.24)\n# user-owned project\n";
    writeTestText(joinTestPath(projectRoot, "CMakeLists.txt"), customProjectCMake);

    FGL_CHECK(fabgl::project::ensureGameplayBuildFiles(projectRoot));
    FGL_CHECK(readTestText(joinTestPath(projectRoot, "CMakeLists.txt")) == customProjectCMake);
    const auto gluePath = joinTestPath(projectRoot, "Scripts/FabGLStudioScripts.cmake");
    FGL_CHECK(readTestText(gluePath) == glue);
    FGL_CHECK(fabgl::project::ensureGameplayBuildFiles(projectRoot));
    FGL_CHECK(readTestText(gluePath) == glue);

    FGL_CHECK(fabgl::project::writeGameplayScript(projectRoot, "PlayerController"));
    FGL_CHECK(readTestText(joinTestPath(projectRoot, "Scripts/PlayerController.cpp"))
                  .find("PlayerController::onUpdate") != std::string::npos);
    FGL_CHECK(readTestText(joinTestPath(projectRoot, "Scripts/ESP32/PlayerControllerEsp32.cpp"))
                  .find("PlayerControllerEsp32Update") != std::string::npos);
    const auto esp32Module = joinTestPath(projectRoot, "Scripts/ESP32/FabGLStudioEsp32Module.cpp");
    FGL_CHECK(readTestText(esp32Module).find("FGL_ESP32_SCRIPT_MODULE") != std::string::npos);
    FGL_CHECK(fabgl::project::writeGameplayScript(projectRoot, "EnemyController"));
    const auto multiModule = readTestText(esp32Module);
    FGL_CHECK(multiModule.find("PlayerControllerEsp32Descriptor") != std::string::npos);
    FGL_CHECK(multiModule.find("EnemyControllerEsp32Descriptor") != std::string::npos);
    FGL_CHECK(!fabgl::project::writeGameplayScript(projectRoot, "PlayerController"));

    const std::string customGlue = "# user-owned glue\n";
    writeTestText(gluePath, customGlue);
    auto refused = fabgl::project::ensureGameplayBuildFiles(projectRoot);
    FGL_CHECK(!refused);
    FGL_CHECK(refused.error().code() == fabgl::ErrorCode::AlreadyExists);
    FGL_CHECK(readTestText(gluePath) == customGlue);
    FGL_CHECK(readTestText(joinTestPath(projectRoot, "CMakeLists.txt")) == customProjectCMake);
}

FGL_TEST(esp32_export_is_deterministic_and_embeds_canonical_project_payload) {
    ExportTestDirectory temporary;
    const auto projectRoot = joinTestPath(temporary.path(), "Project");
    const auto templateRoot = joinTestPath(temporary.path(), "FirmwareTemplate");
    const auto sceneId = fabgl::SceneGuid::fromStableName("tests.esp32-export.scene");
    const auto entityId = fabgl::EntityGuid::fromStableName("tests.esp32-export.entity");
    const std::string legacyScene =
        "fglscene 1\nscene_guid " + sceneId.toString() +
        "\nscene_name \"Legacy Export\"\nentity_begin\nguid " + entityId.toString() +
        "\nname \"Player\"\nactive 1\nparent nil\nposition 1 2 3\nrotation 0 0 0\n"
        "scale 1 1 1\nentity_end\nscene_end\n";
    auto manifest = writeExportProject(projectRoot, legacyScene);
    const auto zetaGuid = fabgl::AssetGuid::fromStableName("tests.esp32-export.zeta");
    const auto alphaGuid = fabgl::AssetGuid::fromStableName("tests.esp32-export.alpha");
    manifest.assets = {{zetaGuid, "Assets/Zeta.bin", "binary"},
                       {alphaGuid, "Assets/Textures/Alpha.bin", "binary"}};
    auto declaredManifest = fabgl::project::serializeManifest(manifest);
    FGL_CHECK(declaredManifest);
    writeTestText(joinTestPath(projectRoot, "Game.fglproject"), declaredManifest.value());
    writeTestBytes(joinTestPath(projectRoot, "Assets/Zeta.bin"), {9U, 8U, 7U});
    writeTestBytes(joinTestPath(projectRoot, "Assets/Textures/Alpha.bin"), {1U, 2U, 3U, 4U});
    writeFirmwareTemplate(templateRoot);

    const auto firstOutput = joinTestPath(temporary.path(), "RunA/ExportSketch");
    const auto secondOutput = joinTestPath(temporary.path(), "RunB/ExportSketch");
    auto first = fabgl::project::exportEsp32Project(joinTestPath(projectRoot, "Game.fglproject"),
                                                    templateRoot, firstOutput);
    auto second = fabgl::project::exportEsp32Project(joinTestPath(projectRoot, "Game.fglproject"),
                                                     templateRoot, secondOutput);
    FGL_CHECK(first && second);
    FGL_CHECK(first.value().projectName == manifest.name);
    FGL_CHECK(first.value().previewDemo == manifest.previewDemo);
    FGL_CHECK(first.value().sketchFileName == "ExportSketch.ino");
    FGL_CHECK(first.value().entityCount == 1U);
    FGL_CHECK(first.value().assetCount == 2U);
    FGL_CHECK(first.value().payloadChecksum == second.value().payloadChecksum);
    FGL_CHECK(first.value().packBuildChecksum == second.value().packBuildChecksum);

    const auto firstPackBytes = readTestBytes(joinTestPath(firstOutput, "ProjectPayload.fglpak"));
    const auto secondPackBytes = readTestBytes(joinTestPath(secondOutput, "ProjectPayload.fglpak"));
    FGL_CHECK(firstPackBytes == secondPackBytes);
    FGL_CHECK(first.value().payloadSize == firstPackBytes.size());
    FGL_CHECK(first.value().payloadChecksum ==
              fabgl::assets::checksum64(firstPackBytes.data(), firstPackBytes.size()));
    RuntimeVectorReader runtimeReader{&firstPackBytes};
    fabgl_project_runtime::RuntimeProject embeddedRuntime;
    fabgl_project_runtime::Failure runtimeFailure;
    if (!fabgl_project_runtime::loadProject(
            runtimeReader, first.value().assetCount, first.value().payloadChecksum,
            first.value().packBuildChecksum, "olimex-esp32-sbc-fabgl-revb", embeddedRuntime,
            runtimeFailure)) {
        throw fabgl::tests::AssertionFailure(
            "exported ESP32 runtime load failed: code=" +
            std::to_string(static_cast<unsigned int>(runtimeFailure.code)) + " offset=" +
            std::to_string(runtimeFailure.offset) + " detail=" + runtimeFailure.detail);
    }
    FGL_CHECK(embeddedRuntime.loaded && embeddedRuntime.scene.entityCount == 1U);
    FGL_CHECK(embeddedRuntime.manifest.assetCount == 2U);
    auto inspected = fabgl::assets::inspectPack(firstPackBytes);
    FGL_CHECK(inspected);
    FGL_CHECK(inspected.value().index.size() == 4U);
    FGL_CHECK(std::count_if(inspected.value().index.begin(), inspected.value().index.end(),
                            [](const auto& entry) {
                                return entry.typeId == fabgl::project::Esp32AssetPayloadType;
                            }) == 2);

    const auto manifestPayload =
        payloadForType(inspected.value(), fabgl::project::Esp32ManifestPayloadType);
    const std::string manifestText(manifestPayload.begin(), manifestPayload.end());
    auto embeddedManifest = fabgl::project::parseManifest(manifestText);
    FGL_CHECK(embeddedManifest);
    FGL_CHECK(embeddedManifest.value().name == manifest.name);
    const auto scenePayload =
        payloadForType(inspected.value(), fabgl::project::Esp32ScenePayloadType);
    const std::string sceneText(scenePayload.begin(), scenePayload.end());
    FGL_CHECK(sceneText.find("fglscene 2") == 0U);
    auto embeddedScene = fabgl::SceneSerializer::deserialize(sceneText);
    FGL_CHECK(embeddedScene);
    FGL_CHECK(embeddedScene.value()->entityCount() == 1U);

    std::set<std::string> embeddedAssetPaths;
    std::set<std::string> embeddedAssetGuids;
    for (const auto& entry : inspected.value().index) {
        if (entry.typeId != fabgl::project::Esp32AssetPayloadType)
            continue;
        embeddedAssetGuids.insert(entry.guid.toString());
        FGL_CHECK(entry.size >= 8U);
        const auto* payload = inspected.value().bytes.data() + entry.offset;
        FGL_CHECK(payload[0] == 'F' && payload[1] == 'G' && payload[2] == 'L' && payload[3] == 'A');
        const auto pathLength =
            static_cast<std::size_t>(payload[6]) | (static_cast<std::size_t>(payload[7]) << 8U);
        FGL_CHECK(pathLength <= entry.size - 8U);
        embeddedAssetPaths.emplace(reinterpret_cast<const char*>(payload + 8U), pathLength);
    }
    FGL_CHECK(embeddedAssetPaths ==
              std::set<std::string>{"Assets/Textures/Alpha.bin", "Assets/Zeta.bin"});
    FGL_CHECK(embeddedAssetGuids ==
              std::set<std::string>{alphaGuid.toString(), zetaGuid.toString()});

    const auto firstHeader = readTestText(joinTestPath(firstOutput, "ProjectPayload.h"));
    const auto secondHeader = readTestText(joinTestPath(secondOutput, "ProjectPayload.h"));
    FGL_CHECK(firstHeader == secondHeader);
    FGL_CHECK(firstHeader.find("kProjectName[] = \"Deterministic Export\"") != std::string::npos);
    FGL_CHECK(firstHeader.find("kPreviewDemo[] = \"platformer\"") != std::string::npos);
    FGL_CHECK(firstHeader.find("kEntityCount = 1U") != std::string::npos);
    FGL_CHECK(firstHeader.find("PROGMEM") != std::string::npos);
    FGL_CHECK(readTestText(joinTestPath(firstOutput, "ExportSketch.ino")) ==
              readTestText(joinTestPath(templateRoot, "firmware.ino")));
    FGL_CHECK(readTestText(joinTestPath(firstOutput, "BoardProfile.h")) ==
              readTestText(joinTestPath(templateRoot, "BoardProfile.h")));
    const auto firstExportResult = readTestText(joinTestPath(firstOutput, "ExportResult.json"));
    const auto secondExportResult = readTestText(joinTestPath(secondOutput, "ExportResult.json"));
    FGL_CHECK(firstExportResult == secondExportResult);
    FGL_CHECK(firstExportResult.find("\"kind\": \"FabGLStudioEsp32Export\"") != std::string::npos);
    FGL_CHECK(firstExportResult.find("\"sceneFormatVersion\": 2") != std::string::npos);
    FGL_CHECK(firstExportResult.find("\"sketchFileName\": \"ExportSketch.ino\"") !=
              std::string::npos);
    FGL_CHECK(firstExportResult.find("\"payloadChecksum\": \"0x") != std::string::npos);
    FGL_CHECK(firstExportResult.find("\"portableScriptFileCount\": 0") != std::string::npos);
    FGL_CHECK(firstExportResult.find("\"scriptRuntime\": false") != std::string::npos);
    FGL_CHECK(readTestText(joinTestPath(firstOutput, "ProjectScriptConfig.h"))
                  .find("FABGL_STUDIO_HAS_PROJECT_SCRIPTS 0") != std::string::npos);

    auto overwrite = fabgl::project::exportEsp32Project(
        joinTestPath(projectRoot, "Game.fglproject"), templateRoot, firstOutput);
    FGL_CHECK(!overwrite);
    FGL_CHECK(overwrite.error().code() == fabgl::ErrorCode::AlreadyExists);
    FGL_CHECK(readTestBytes(joinTestPath(firstOutput, "ProjectPayload.fglpak")) == firstPackBytes);
}

FGL_TEST(project_prepare_and_esp32_export_share_canonical_crop_resize_and_storage) {
#ifdef _WIN32
    ExportTestDirectory temporary;
    const auto projectRoot = joinTestPath(temporary.path(), "ImageProject");
    const auto templateRoot = joinTestPath(temporary.path(), "FirmwareTemplate");
    fabgl::Scene scene("Image Pipeline", fabgl::SceneGuid::fromStableName("image.pipeline.scene"));
    auto sceneText = fabgl::SceneSerializer::serialize(scene);
    FGL_CHECK(sceneText);
    writeTestText(joinTestPath(projectRoot, "Scenes/Main.fglscene"), sceneText.value());
    writeTestBytes(joinTestPath(projectRoot, "Assets/Frames.bmp"), makeProjectBmp());
    writeFirmwareTemplate(templateRoot);

    fabgl::project::Manifest manifest;
    manifest.projectGuid = fabgl::AssetGuid::fromStableName("image.pipeline.project").toString();
    manifest.name = "Canonical Image Pipeline";
    const auto imageGuid = fabgl::AssetGuid::fromStableName("image.pipeline.asset");
    fabgl::project::ProjectAssetEntry image(imageGuid, "Assets/Frames.bmp", "image");
    image.importSettings =
        R"json({"crop":{"x":2,"y":1,"width":4,"height":2},"targetWidth":2,"targetHeight":2,"paletteSize":8,"compression":"rle","residency":"stream"})json";
    image.esp32Target = fabgl::assets::AssetTarget::Esp32Sd;
    image.hasImportMetadata = true;
    manifest.assets.push_back(std::move(image));
    auto manifestText = fabgl::project::serializeManifest(manifest);
    FGL_CHECK(manifestText);
    const auto projectPath = joinTestPath(projectRoot, "Game.fglproject");
    writeTestText(projectPath, manifestText.value());

    auto prepared = fabgl::project::prepareProjectInputs(projectPath,
                                                         joinTestPath(temporary.path(), "Prepared"),
                                                         fabgl::project::ProjectPrepareTarget::Pc);
    FGL_CHECK(prepared && prepared.value().importedAssetCount == 1U);
    auto preparedImage = fabgl::assets::decodeIndexedImage(
        readTestBytes(joinTestPath(temporary.path(), "Prepared/project/Assets/Frames.bmp")));
    FGL_CHECK(preparedImage && preparedImage.value().width == 2 &&
              preparedImage.value().height == 2);

    const auto exportRoot = joinTestPath(temporary.path(), "Export/ImageSketch");
    auto exported = fabgl::project::exportEsp32Project(projectPath, templateRoot, exportRoot);
    FGL_CHECK(exported && exported.value().assetCount == 1U);
    auto pack = fabgl::assets::inspectPack(
        readTestBytes(joinTestPath(exportRoot, "ProjectPayload.fglpak")));
    FGL_CHECK(pack);
    const auto asset = std::find_if(
        pack.value().index.cbegin(), pack.value().index.cend(), [imageGuid](const auto& entry) {
            return entry.guid == imageGuid && entry.typeId == fabgl::project::Esp32AssetPayloadType;
        });
    FGL_CHECK(asset != pack.value().index.cend());
    FGL_CHECK(asset->storage == fabgl::assets::StorageClass::Sd && asset->size > 8U);
    const auto* envelope = pack.value().bytes.data() + asset->offset;
    const auto pathLength =
        static_cast<std::size_t>(envelope[6]) | (static_cast<std::size_t>(envelope[7]) << 8U);
    FGL_CHECK(pathLength <= asset->size - 8U);
    const auto imageOffset = static_cast<std::size_t>(asset->offset) + 8U + pathLength;
    const auto imageSize = static_cast<std::size_t>(asset->size) - 8U - pathLength;
    std::vector<std::uint8_t> encodedImage(
        pack.value().bytes.begin() +
            static_cast<std::vector<std::uint8_t>::difference_type>(imageOffset),
        pack.value().bytes.begin() +
            static_cast<std::vector<std::uint8_t>::difference_type>(imageOffset + imageSize));
    auto exportedImage = fabgl::assets::decodeIndexedImage(encodedImage);
    FGL_CHECK(exportedImage && exportedImage.value().width == 2 &&
              exportedImage.value().height == 2);
#else
    FGL_CHECK(true);
#endif
}

FGL_TEST(esp32_export_rejects_traversal_corruption_and_existing_outputs_without_partial_writes) {
    ExportTestDirectory temporary;
    const auto projectRoot = joinTestPath(temporary.path(), "UnsafeProject");
    const auto templateRoot = joinTestPath(temporary.path(), "FirmwareTemplate");
    writeFirmwareTemplate(templateRoot);
    const std::string unsafeManifest = "{\"kind\":\"FabGLStudioProject\",\"formatVersion\":1,"
                                       "\"projectGuid\":\"11111111-1111-4111-8111-111111111111\","
                                       "\"name\":\"Unsafe\",\"projectRoot\":\".\","
                                       "\"startupScene\":\"../Outside.fglscene\"}";
    writeTestText(joinTestPath(projectRoot, "Game.fglproject"), unsafeManifest);
    writeTestText(joinTestPath(temporary.path(), "Outside.fglscene"), "outside");
    const auto output = joinTestPath(temporary.path(), "Output/SafeSketch");
    auto traversal = fabgl::project::exportEsp32Project(
        joinTestPath(projectRoot, "Game.fglproject"), templateRoot, output);
    FGL_CHECK(!traversal);
    FGL_CHECK(traversal.error().code() == fabgl::ErrorCode::InvalidFormat);

    fabgl::project::Manifest manifest;
    manifest.projectGuid = fabgl::AssetGuid::fromStableName("tests.esp32-export.safety").toString();
    manifest.name = "Safety";
    auto manifestText = fabgl::project::serializeManifest(manifest);
    FGL_CHECK(manifestText);
    writeTestText(joinTestPath(projectRoot, "Game.fglproject"), manifestText.value());
    writeTestText(joinTestPath(projectRoot, "Scenes/Main.fglscene"), "not a scene");
    auto corrupt = fabgl::project::exportEsp32Project(joinTestPath(projectRoot, "Game.fglproject"),
                                                      templateRoot, output);
    FGL_CHECK(!corrupt);
    FGL_CHECK(corrupt.error().code() == fabgl::ErrorCode::InvalidFormat);

    fabgl::Scene validScene("Safe");
    FGL_CHECK(validScene.createEntity("Entity"));
    auto sceneText = fabgl::SceneSerializer::serialize(validScene);
    FGL_CHECK(sceneText);
    writeTestText(joinTestPath(projectRoot, "Scenes/Main.fglscene"), sceneText.value());
    const auto unsupportedScript = joinTestPath(projectRoot, "Scripts/Player.cpp");
    writeTestText(unsupportedScript, "void update() {}\n");
    const auto scriptOutput = joinTestPath(temporary.path(), "ScriptOutput/ScriptSketch");
    auto scriptRejected = fabgl::project::exportEsp32Project(
        joinTestPath(projectRoot, "Game.fglproject"), templateRoot, scriptOutput);
    FGL_CHECK(!scriptRejected);
    FGL_CHECK(scriptRejected.error().code() == fabgl::ErrorCode::InvalidState);
    FGL_CHECK(scriptRejected.error().message().find("require a portable") != std::string::npos);
    FGL_CHECK(!fabgl::assets::readBinaryFile(joinTestPath(scriptOutput, "ProjectPayload.h")));

    const auto portableSource = joinTestPath(projectRoot, "Scripts/ESP32/Portable.cpp");
    writeTestText(portableSource, "#include \"ProjectScriptRuntime.h\"\n"
                                  "static const fabgl_project_scripts::Descriptor scripts[]{};\n"
                                  "FGL_ESP32_SCRIPT_MODULE(scripts)\n");
    const auto portableOutput = joinTestPath(temporary.path(), "Portable/PortableSketch");
    auto portable = fabgl::project::exportEsp32Project(joinTestPath(projectRoot, "Game.fglproject"),
                                                       templateRoot, portableOutput);
    FGL_CHECK(portable && portable.value().scriptRuntime);
    FGL_CHECK(portable.value().portableScriptFileCount == 1U);
    FGL_CHECK(readTestText(joinTestPath(portableOutput, "src/ProjectScripts/Portable.cpp"))
                  .find("FGL_ESP32_SCRIPT_MODULE") != std::string::npos);
    FGL_CHECK(readTestText(joinTestPath(portableOutput, "ProjectScriptConfig.h"))
                  .find("FABGL_STUDIO_HAS_PROJECT_SCRIPTS 1") != std::string::npos);
    FGL_CHECK(readTestText(joinTestPath(portableOutput, "ExportResult.json"))
                  .find("\"scriptRuntime\": true") != std::string::npos);
    removeTestFile(unsupportedScript);
    removeTestFile(portableSource);

    auto recovered = fabgl::project::exportEsp32Project(
        joinTestPath(projectRoot, "Game.fglproject"), templateRoot, output);
    FGL_CHECK(recovered);

    const auto occupied = joinTestPath(temporary.path(), "OccupiedSketch");
    const auto sentinel = joinTestPath(occupied, "keep.txt");
    writeTestText(sentinel, "do-not-overwrite");
    auto overwrite = fabgl::project::exportEsp32Project(
        joinTestPath(projectRoot, "Game.fglproject"), templateRoot, occupied);
    FGL_CHECK(!overwrite);
    FGL_CHECK(overwrite.error().code() == fabgl::ErrorCode::AlreadyExists);
    FGL_CHECK(readTestText(sentinel) == "do-not-overwrite");

    const auto incompleteTemplate = joinTestPath(temporary.path(), "IncompleteTemplate");
    writeTestText(joinTestPath(incompleteTemplate, "BoardProfile.h"), "#pragma once\n");
    auto missingTemplate = fabgl::project::exportEsp32Project(
        joinTestPath(projectRoot, "Game.fglproject"), incompleteTemplate,
        joinTestPath(temporary.path(), "MissingTemplateSketch"));
    FGL_CHECK(!missingTemplate);
    FGL_CHECK(missingTemplate.error().code() == fabgl::ErrorCode::NotFound);
}

FGL_TEST(esp32_export_rejects_unported_scene_components_and_visual_script_assets) {
    ExportTestDirectory temporary;
    const auto projectRoot = joinTestPath(temporary.path(), "CapabilityProject");
    const auto templateRoot = joinTestPath(temporary.path(), "FirmwareTemplate");
    writeFirmwareTemplate(templateRoot);

    fabgl::ReflectionRegistry registry;
    FGL_CHECK(fabgl::registerBuiltinComponentTypes(registry));
    fabgl::Scene scene("Host Only Component",
                       fabgl::SceneGuid::fromStableName("tests.esp32-export.capability.scene"));
    auto entity = scene.createEntity("Physics Entity");
    FGL_CHECK(entity);
    auto collider = fabgl::createBuiltinDataComponent(registry, "Collider2D");
    FGL_CHECK(collider && entity.value()->addComponent(std::move(collider.value())));
    auto sceneText = fabgl::SceneSerializer::serialize(scene);
    FGL_CHECK(sceneText);

    auto manifest = writeExportProject(projectRoot, sceneText.value());
    const auto componentOutput = joinTestPath(temporary.path(), "Component/ComponentSketch");
    auto componentRejected = fabgl::project::exportEsp32Project(
        joinTestPath(projectRoot, "Game.fglproject"), templateRoot, componentOutput);
    FGL_CHECK(!componentRejected);
    FGL_CHECK(componentRejected.error().code() == fabgl::ErrorCode::InvalidState);
    FGL_CHECK(componentRejected.error().message().find("not ported") != std::string::npos);
    FGL_CHECK(
        !fabgl::assets::readBinaryFile(joinTestPath(componentOutput, "ProjectPayload.fglpak")));

    fabgl::Scene supportedScene(
        "Visual Asset", fabgl::SceneGuid::fromStableName("tests.esp32-export.visual.scene"));
    FGL_CHECK(supportedScene.createEntity("Entity"));
    sceneText = fabgl::SceneSerializer::serialize(supportedScene);
    FGL_CHECK(sceneText);
    writeTestText(joinTestPath(projectRoot, "Scenes/Main.fglscene"), sceneText.value());
    const auto visualGuid = fabgl::AssetGuid::fromStableName("tests.esp32-export.visual.asset");
    manifest.assets = {{visualGuid, "Visual/Gameplay.fglvisual", "visual.script"}};
    auto encodedManifest = fabgl::project::serializeManifest(manifest);
    FGL_CHECK(encodedManifest);
    writeTestText(joinTestPath(projectRoot, "Game.fglproject"), encodedManifest.value());

    const auto visualOutput = joinTestPath(temporary.path(), "Visual/VisualSketch");
    auto visualRejected = fabgl::project::exportEsp32Project(
        joinTestPath(projectRoot, "Game.fglproject"), templateRoot, visualOutput);
    FGL_CHECK(!visualRejected);
    FGL_CHECK(visualRejected.error().code() == fabgl::ErrorCode::InvalidState);
    FGL_CHECK(visualRejected.error().message().find("no visual-script VM") != std::string::npos);
    FGL_CHECK(!fabgl::assets::readBinaryFile(joinTestPath(visualOutput, "ProjectPayload.fglpak")));
}

FGL_TEST(local_package_install_list_lock_and_remove_are_deterministic) {
    ExportTestDirectory temporary;
    const auto firstProjectRoot = joinTestPath(temporary.path(), "FirstProject");
    const auto secondProjectRoot = joinTestPath(temporary.path(), "SecondProject");
    const auto packageRoot = joinTestPath(temporary.path(), "DataPackage");
    writeExportProject(firstProjectRoot, {});
    writeExportProject(secondProjectRoot, {});
    writeTestText(joinTestPath(packageRoot, "fabgl.package"),
                  "schema=2\nid=sample.data\ndisplayName=Sample Data\nversion=1.2.0\nengine=*\n"
                  "author=FabGL Tests\nlicense=MIT\npath=ignored/by/installer\nexecutable=false\n");
    writeTestText(joinTestPath(packageRoot, "Assets/value.txt"), "deterministic package data\n");

    const auto firstProject = joinTestPath(firstProjectRoot, "Game.fglproject");
    const auto secondProject = joinTestPath(secondProjectRoot, "Game.fglproject");
    auto first = fabgl::project::installLocalPackage(firstProject, packageRoot);
    auto second = fabgl::project::installLocalPackage(secondProject, packageRoot);
    if (!first) {
        auto details = std::string("first package install failed: ") + first.error().message();
        for (const auto& item : first.error().context())
            details += " [" + item.key + "=" + item.value + "]";
        throw fabgl::tests::AssertionFailure(details);
    }
    if (!second) {
        throw fabgl::tests::AssertionFailure("second package install failed: " +
                                             second.error().message());
    }
    FGL_CHECK(first && second);
    FGL_CHECK(first.value().manifest.stableId() == "sample.data");
    FGL_CHECK(first.value().contentSha256 == second.value().contentSha256);
    FGL_CHECK(first.value().contentSha256 ==
              "07a1f4032e3f30fe008468f776ce4c1f9edaf6f536e62712ca60098c192e19c7");
    FGL_CHECK(first.value().fileCount == 2U);
    FGL_CHECK(!first.value().manifest.containsExecutableCode);
    const auto installedManifest =
        readTestText(joinTestPath(firstProjectRoot, "Packages/sample.data/fabgl.package"));
    FGL_CHECK(installedManifest.rfind("schema=2\nid=sample.data\n", 0U) == 0U);
    FGL_CHECK(installedManifest.find("path=Packages/sample.data\n") != std::string::npos);
    FGL_CHECK(installedManifest.find("trust=") == std::string::npos);
    FGL_CHECK(readTestText(joinTestPath(firstProjectRoot, "Packages/fabgl-packages.lock")) ==
              readTestText(joinTestPath(secondProjectRoot, "Packages/fabgl-packages.lock")));

    auto listed = fabgl::project::listLocalPackages(firstProject);
    FGL_CHECK(listed && listed.value().size() == 1U);
    FGL_CHECK(listed.value()[0].contentSha256 == first.value().contentSha256);
    auto order = fabgl::project::validateLocalPackages(firstProject);
    FGL_CHECK(order && order.value() == std::vector<std::string>{"sample.data"});
    auto duplicate = fabgl::project::installLocalPackage(firstProject, packageRoot);
    FGL_CHECK(!duplicate && duplicate.error().code() == fabgl::ErrorCode::AlreadyExists);

    FGL_CHECK(fabgl::project::removeLocalPackage(firstProject, "sample.data"));
    auto empty = fabgl::project::listLocalPackages(firstProject);
    FGL_CHECK(empty && empty.value().empty());
    FGL_CHECK(!fabgl::assets::readBinaryFile(
        joinTestPath(firstProjectRoot, "Packages/sample.data/fabgl.package")));
    auto missing = fabgl::project::removeLocalPackage(firstProject, "sample.data");
    FGL_CHECK(!missing && missing.error().code() == fabgl::ErrorCode::NotFound);
}

FGL_TEST(local_package_executable_trust_is_explicit_content_bound_and_dependency_safe) {
    ExportTestDirectory temporary;
    const auto projectRoot = joinTestPath(temporary.path(), "TrustProject");
    const auto baseRoot = joinTestPath(temporary.path(), "BasePackage");
    const auto extensionRoot = joinTestPath(temporary.path(), "ExtensionPackage");
    writeExportProject(projectRoot, {});
    writeTestText(joinTestPath(baseRoot, "fabgl.package"),
                  "schema=2\nid=base.core\ndisplayName=Base Core\nversion=1.4.0\nengine=*\n"
                  "author=FabGL Tests\nlicense=MIT\nexecutable=false\n");
    writeTestText(joinTestPath(baseRoot, "Assets/base.txt"), "base\n");
    writeTestText(joinTestPath(extensionRoot, "fabgl.package"),
                  "schema=2\nid=game.extension\ndisplayName=Game Extension\nversion=2.0.0\n"
                  "engine=*\nauthor=FabGL Tests\nlicense=Apache-2.0\ntrust=trusted\n"
                  "executable=false\ndependency=base.core@^1.2.0\n"
                  "entry=runtime-module:src/plugin.cpp\n");
    writeTestText(joinTestPath(extensionRoot, "src/plugin.cpp"), "void package_entry() {}\n");

    const auto project = joinTestPath(projectRoot, "Game.fglproject");
    auto baseInstall = fabgl::project::installLocalPackage(project, baseRoot);
    if (!baseInstall) {
        auto details = std::string("base package install failed: ") + baseInstall.error().message();
        for (const auto& item : baseInstall.error().context())
            details += " [" + item.key + "=" + item.value + "]";
        throw fabgl::tests::AssertionFailure(details);
    }
    const auto lockBefore = readTestText(joinTestPath(projectRoot, "Packages/fabgl-packages.lock"));
    const auto trustBefore =
        readTestText(joinTestPath(projectRoot, "Packages/.fabgl-package-trust"));
    auto selfDeclared = fabgl::project::installLocalPackage(project, extensionRoot);
    FGL_CHECK(!selfDeclared && selfDeclared.error().code() == fabgl::ErrorCode::InvalidState);
    FGL_CHECK(readTestText(joinTestPath(projectRoot, "Packages/fabgl-packages.lock")) ==
              lockBefore);
    FGL_CHECK(readTestText(joinTestPath(projectRoot, "Packages/.fabgl-package-trust")) ==
              trustBefore);
    FGL_CHECK(!fabgl::assets::readBinaryFile(
        joinTestPath(projectRoot, "Packages/game.extension/fabgl.package")));

    fabgl::project::LocalPackageInstallOptions options;
    options.allowExecutable = true;
    auto installed = fabgl::project::installLocalPackage(project, extensionRoot, options);
    FGL_CHECK(installed && installed.value().executableTrusted);
    const auto trust = readTestText(joinTestPath(projectRoot, "Packages/.fabgl-package-trust"));
    FGL_CHECK(trust.find("allow=game.extension@2.0.0#" + installed.value().contentSha256) !=
              std::string::npos);
    FGL_CHECK(readTestText(joinTestPath(projectRoot, "Packages/game.extension/fabgl.package"))
                  .find("trust=") == std::string::npos);
    auto order = fabgl::project::validateLocalPackages(project);
    FGL_CHECK(order && order.value() == std::vector<std::string>({"base.core", "game.extension"}));

    auto baseBlocked = fabgl::project::removeLocalPackage(project, "base.core");
    FGL_CHECK(!baseBlocked && baseBlocked.error().code() == fabgl::ErrorCode::InvalidState);
    FGL_CHECK(fabgl::project::removeLocalPackage(project, "game.extension"));
    FGL_CHECK(fabgl::project::removeLocalPackage(project, "base.core"));
}

FGL_TEST(local_package_lock_uses_canonical_dependency_order_immediately_after_install) {
    ExportTestDirectory temporary;
    const auto projectRoot = joinTestPath(temporary.path(), "OrderProject");
    const auto firstRoot = joinTestPath(temporary.path(), "YPackage");
    const auto secondRoot = joinTestPath(temporary.path(), "ZPackage");
    const auto applicationRoot = joinTestPath(temporary.path(), "ApplicationPackage");
    writeExportProject(projectRoot, {});
    writeTestText(joinTestPath(firstRoot, "fabgl.package"),
                  "schema=2\nid=y.support\ndisplayName=Y Support\nversion=1.0.0\nengine=*\n"
                  "author=FabGL Tests\nlicense=MIT\nexecutable=false\n");
    writeTestText(joinTestPath(secondRoot, "fabgl.package"),
                  "schema=2\nid=z.support\ndisplayName=Z Support\nversion=1.0.0\nengine=*\n"
                  "author=FabGL Tests\nlicense=MIT\nexecutable=false\n");
    writeTestText(joinTestPath(applicationRoot, "fabgl.package"),
                  "schema=2\nid=a.application\ndisplayName=Application\nversion=1.0.0\nengine=*\n"
                  "author=FabGL Tests\nlicense=MIT\nexecutable=false\n"
                  "dependency=z.support@*\ndependency=y.support@*\n");

    const auto project = joinTestPath(projectRoot, "Game.fglproject");
    FGL_CHECK(fabgl::project::installLocalPackage(project, firstRoot));
    FGL_CHECK(fabgl::project::installLocalPackage(project, secondRoot));
    FGL_CHECK(fabgl::project::installLocalPackage(project, applicationRoot));
    auto order = fabgl::project::validateLocalPackages(project);
    FGL_CHECK(order && order.value() ==
                           std::vector<std::string>({"y.support", "z.support", "a.application"}));
    const auto installedManifest =
        readTestText(joinTestPath(projectRoot, "Packages/a.application/fabgl.package"));
    FGL_CHECK(installedManifest.find("dependency=y.support@*\ndependency=z.support@*\n") !=
              std::string::npos);
}

FGL_TEST(local_package_rejects_malicious_manifests_limits_and_tampered_owned_content) {
    ExportTestDirectory temporary;
    const auto projectRoot = joinTestPath(temporary.path(), "SafetyProject");
    const auto project = joinTestPath(projectRoot, "Game.fglproject");
    writeExportProject(projectRoot, {});

    const auto traversalRoot = joinTestPath(temporary.path(), "TraversalPackage");
    writeTestText(joinTestPath(traversalRoot, "fabgl.package"),
                  "schema=2\nid=unsafe.entry\ndisplayName=Unsafe Entry\nversion=1.0.0\nengine=*\n"
                  "author=FabGL Tests\nlicense=MIT\nexecutable=true\n"
                  "entry=runtime-module:../escape.cpp\n");
    auto traversal = fabgl::project::installLocalPackage(project, traversalRoot);
    FGL_CHECK(!traversal && traversal.error().code() == fabgl::ErrorCode::InvalidArgument);

    const auto missingRoot = joinTestPath(temporary.path(), "MissingDependencyPackage");
    writeTestText(joinTestPath(missingRoot, "fabgl.package"),
                  "schema=2\nid=missing.dep\ndisplayName=Missing Dependency\nversion=1.0.0\n"
                  "engine=*\nauthor=FabGL Tests\nlicense=MIT\nexecutable=false\n"
                  "dependency=does.not.exist@*\n");
    auto missing = fabgl::project::installLocalPackage(project, missingRoot);
    FGL_CHECK(!missing && missing.error().code() == fabgl::ErrorCode::NotFound);

    const auto incompatibleRoot = joinTestPath(temporary.path(), "IncompatiblePackage");
    writeTestText(joinTestPath(incompatibleRoot, "fabgl.package"),
                  "schema=2\nid=future.only\ndisplayName=Future Only\nversion=1.0.0\n"
                  "engine=>=99.0.0\nauthor=FabGL Tests\nlicense=MIT\nexecutable=false\n");
    auto incompatible = fabgl::project::installLocalPackage(project, incompatibleRoot);
    FGL_CHECK(!incompatible && incompatible.error().code() == fabgl::ErrorCode::UnsupportedVersion);

    const auto limitedRoot = joinTestPath(temporary.path(), "LimitedPackage");
    writeTestText(joinTestPath(limitedRoot, "fabgl.package"),
                  "schema=2\nid=limited.data\ndisplayName=Limited Data\nversion=1.0.0\n"
                  "engine=*\nauthor=FabGL Tests\nlicense=MIT\nexecutable=false\n");
    writeTestText(joinTestPath(limitedRoot, "Assets/data.txt"), "bounded\n");
    fabgl::project::LocalPackageInstallOptions limits;
    limits.limits.maximumFiles = 1U;
    auto overLimit = fabgl::project::installLocalPackage(project, limitedRoot, limits);
    FGL_CHECK(!overLimit && overLimit.error().code() == fabgl::ErrorCode::CapacityExceeded);

    auto installed = fabgl::project::installLocalPackage(project, limitedRoot);
    FGL_CHECK(installed);
    const auto installedData = joinTestPath(projectRoot, "Packages/limited.data/Assets/data.txt");
    writeTestText(installedData, "tampered\n");
    auto tampered = fabgl::project::listLocalPackages(project);
    FGL_CHECK(!tampered && tampered.error().code() == fabgl::ErrorCode::InvalidState);
    auto removal = fabgl::project::removeLocalPackage(project, "limited.data");
    FGL_CHECK(!removal && removal.error().code() == fabgl::ErrorCode::InvalidState);
    FGL_CHECK(readTestText(installedData) == "tampered\n");
}

FGL_TEST(project_prepare_imports_compiles_packs_and_is_deterministic_per_target) {
    ExportTestDirectory temporary;
    const auto projectRoot = joinTestPath(temporary.path(), "PreparedProject");
    const auto scenePath = joinTestPath(projectRoot, "Scenes/Main.fglscene");
    fabgl::Scene scene("Prepared Scene", fabgl::SceneGuid::fromStableName("prepare.scene"));
    auto sceneText = fabgl::SceneSerializer::serialize(scene);
    FGL_CHECK(sceneText);
    writeTestText(scenePath, sceneText.value());

    fabgl::VisualGraph graph;
    const auto graphGuid = fabgl::AssetGuid::fromStableName("prepare.visual");
    graph.setGuid(graphGuid);
    graph.setName("Prepared Visual");
    auto entry = fabgl::VisualNodeRegistry::builtins().create(
        fabgl::VisualBuiltinNodeType::EventStart, 1U, "Start");
    auto returned = fabgl::VisualNodeRegistry::builtins().create(
        fabgl::VisualBuiltinNodeType::FlowReturn, 2U, "Return");
    auto number = fabgl::VisualNodeRegistry::builtins().create(
        fabgl::VisualBuiltinNodeType::NumberConstant, 3U, "One");
    auto hostFunction = fabgl::VisualNodeRegistry::builtins().create(
        fabgl::VisualBuiltinNodeType::FunctionCall, 4U, "Absolute value");
    FGL_CHECK(entry && returned && number && hostFunction);
    number.value().numberValue = 1.0;
    hostFunction.value().callbackName = "math.abs";
    FGL_CHECK(graph.addNode(std::move(entry.value())));
    FGL_CHECK(graph.addNode(std::move(returned.value())));
    FGL_CHECK(graph.addNode(std::move(number.value())));
    FGL_CHECK(graph.addNode(std::move(hostFunction.value())));
    graph.setEntryNode(1U);
    FGL_CHECK(graph.addEdge({1U, 1U, 4U, 1U}));
    FGL_CHECK(graph.addEdge({3U, 1U, 4U, 2U}));
    FGL_CHECK(graph.addEdge({4U, 3U, 2U, 1U}));
    FGL_CHECK(graph.addEdge({3U, 1U, 2U, 2U}));
    auto graphText = fabgl::serializeVisualGraph(graph);
    FGL_CHECK(graphText);
    writeTestText(joinTestPath(projectRoot, "Visual/Start.fglvisual"), graphText.value());
    writeTestText(joinTestPath(projectRoot, "Assets/Grid.csv"), "1,2,3\n4,5,6\n");
    const std::vector<std::uint8_t> legacyTilemap = {
        'F', 'G', 'L', 'T', 1U, 0U, 0U, 0U, 2U, 0U, 1U, 0U, 2U, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 7U, 8U};
    writeTestBytes(joinTestPath(projectRoot, "Assets/Legacy.fglt"), legacyTilemap);

    fabgl::project::Manifest manifest;
    manifest.projectGuid = fabgl::AssetGuid::fromStableName("prepare.project").toString();
    manifest.name = "Prepared Project";
    const auto tileGuid = fabgl::AssetGuid::fromStableName("prepare.tilemap");
    const auto legacyTileGuid = fabgl::AssetGuid::fromStableName("prepare.tilemap.legacy");
    manifest.assets = {{tileGuid, "Assets/Grid.csv", "tilemap"},
                       {legacyTileGuid, "Assets/Legacy.fglt", "tilemap"},
                       {graphGuid, "Visual/Start.fglvisual", "visual.script"}};
    auto manifestText = fabgl::project::serializeManifest(manifest);
    FGL_CHECK(manifestText);
    const auto projectPath = joinTestPath(projectRoot, "Prepared.fglproject");
    writeTestText(projectPath, manifestText.value());

    const auto firstOutput = joinTestPath(temporary.path(), "FirstPc");
    const auto secondOutput = joinTestPath(temporary.path(), "SecondPc");
    auto first = fabgl::project::prepareProjectInputs(projectPath, firstOutput,
                                                      fabgl::project::ProjectPrepareTarget::Pc);
    auto second = fabgl::project::prepareProjectInputs(projectPath, secondOutput,
                                                       fabgl::project::ProjectPrepareTarget::Pc);
    FGL_CHECK(first && second);
    FGL_CHECK(first.value().assetCount == 3U);
    FGL_CHECK(first.value().importedAssetCount == 2U);
    FGL_CHECK(first.value().validatedAssetCount == 3U);
    FGL_CHECK(first.value().visualGraphCount == 1U);
    FGL_CHECK(first.value().visualProgramCount == 1U);
    FGL_CHECK(first.value().packChecksum == second.value().packChecksum);
    FGL_CHECK(readTestBytes(first.value().packPath) == readTestBytes(second.value().packPath));
    FGL_CHECK(readTestText(first.value().preparedProjectPath) == manifestText.value());

    auto tilemap = fabgl::assets::inspectTilemap(
        readTestBytes(joinTestPath(firstOutput, "project/Assets/Grid.csv")));
    FGL_CHECK(tilemap);
    FGL_CHECK(tilemap.value().width == 3U && tilemap.value().height == 2U);
    const auto migratedLegacyBytes =
        readTestBytes(joinTestPath(firstOutput, "project/Assets/Legacy.fglt"));
    auto migratedLegacy = fabgl::assets::inspectTilemap(migratedLegacyBytes);
    FGL_CHECK(migratedLegacy && migratedLegacyBytes[4U] == 2U &&
              migratedLegacy.value().guid == legacyTileGuid);
    auto pcPack = fabgl::assets::inspectPack(readTestBytes(first.value().packPath));
    FGL_CHECK(pcPack && pcPack.value().index.size() == 3U);
    for (const auto& entryIndex : pcPack.value().index)
        FGL_CHECK(entryIndex.storage == fabgl::assets::StorageClass::InternalRam);

    const auto espOutput = joinTestPath(temporary.path(), "Esp32");
    auto esp = fabgl::project::prepareProjectInputs(projectPath, espOutput,
                                                    fabgl::project::ProjectPrepareTarget::Esp32);
    FGL_CHECK(!esp);
    FGL_CHECK(esp.error().code() == fabgl::ErrorCode::InvalidState);
    FGL_CHECK(esp.error().message().find("not supported") != std::string::npos);

    const auto opaqueGuid = fabgl::AssetGuid::fromStableName("prepare.esp32.opaque");
    writeTestBytes(joinTestPath(projectRoot, "Assets/Opaque.bin"), {1U, 3U, 3U, 7U});
    manifest.assets = {{opaqueGuid, "Assets/Opaque.bin", "binary"}};
    manifestText = fabgl::project::serializeManifest(manifest);
    FGL_CHECK(manifestText);
    writeTestText(projectPath, manifestText.value());
    auto supportedEsp = fabgl::project::prepareProjectInputs(
        projectPath, joinTestPath(temporary.path(), "SupportedEsp32"),
        fabgl::project::ProjectPrepareTarget::Esp32);
    FGL_CHECK(supportedEsp);
    auto espPack = fabgl::assets::inspectPack(readTestBytes(supportedEsp.value().packPath));
    FGL_CHECK(espPack && espPack.value().index.size() == 1U);
    FGL_CHECK(espPack.value().index.front().guid == opaqueGuid);
    FGL_CHECK(espPack.value().index.front().storage == fabgl::assets::StorageClass::Flash);

    FGL_CHECK(fabgl::project::writeGameplayScript(projectRoot, "PortableController"));
    const auto scriptedOutput = joinTestPath(temporary.path(), "ScriptedEsp32");
    auto scripted = fabgl::project::prepareProjectInputs(
        projectPath, scriptedOutput, fabgl::project::ProjectPrepareTarget::Esp32);
    FGL_CHECK(scripted);
    FGL_CHECK(scripted.value().portableScriptFileCount == 3U);
    const auto preparedScriptRoot = joinTestPath(scriptedOutput, "project/Scripts/ESP32");
    FGL_CHECK(readTestText(joinTestPath(preparedScriptRoot, "PortableControllerEsp32.cpp"))
                  .find("PortableControllerEsp32Update") != std::string::npos);
    FGL_CHECK(readTestText(joinTestPath(preparedScriptRoot, "FabGLStudioEsp32Module.cpp"))
                  .find("FGL_ESP32_SCRIPT_MODULE") != std::string::npos);
    FGL_CHECK(!fabgl::assets::readBinaryFile(
        joinTestPath(scriptedOutput, "project/Scripts/PortableController.cpp")));

    const auto templateRoot = joinTestPath(temporary.path(), "PreparedScriptFirmwareTemplate");
    writeFirmwareTemplate(templateRoot);
    const auto sketchOutput = joinTestPath(temporary.path(), "PreparedScriptSketch");
    auto exported = fabgl::project::exportEsp32Project(scripted.value().preparedProjectPath,
                                                       templateRoot, sketchOutput);
    FGL_CHECK(exported && exported.value().scriptRuntime);
    FGL_CHECK(exported.value().portableScriptFileCount == 3U);
    FGL_CHECK(
        readTestText(joinTestPath(sketchOutput, "src/ProjectScripts/PortableControllerEsp32.cpp"))
            .find("PortableControllerEsp32Update") != std::string::npos);
    FGL_CHECK(!fabgl::assets::readBinaryFile(
        joinTestPath(sketchOutput, "src/ProjectScripts/PortableController.cpp")));

    removeTestFile(joinTestPath(projectRoot, "Scripts/ESP32/PortableControllerEsp32.h"));
    removeTestFile(joinTestPath(projectRoot, "Scripts/ESP32/PortableControllerEsp32.cpp"));
    removeTestFile(joinTestPath(projectRoot, "Scripts/ESP32/FabGLStudioEsp32Module.cpp"));
    const auto rejectedOutput = joinTestPath(temporary.path(), "MissingCompanionEsp32");
    auto rejected = fabgl::project::prepareProjectInputs(
        projectPath, rejectedOutput, fabgl::project::ProjectPrepareTarget::Esp32);
    FGL_CHECK(!rejected && rejected.error().code() == fabgl::ErrorCode::InvalidState);
    FGL_CHECK(rejected.error().message().find("require a portable") != std::string::npos);
    FGL_CHECK(!fabgl::assets::readBinaryFile(
        joinTestPath(rejectedOutput, "project/Prepared.fglproject")));
}

FGL_TEST(project_prepare_migrates_legacy_audio_settings_and_rejects_it_for_esp32_runtime) {
    ExportTestDirectory temporary;
    const auto projectRoot = joinTestPath(temporary.path(), "AudioMetadataProject");
    fabgl::Scene scene("Audio Metadata", fabgl::SceneGuid::fromStableName("prepare.audio.scene"));
    auto sceneText = fabgl::SceneSerializer::serialize(scene);
    FGL_CHECK(sceneText);
    writeTestText(joinTestPath(projectRoot, "Scenes/Main.fglscene"), sceneText.value());
    writeTestBytes(joinTestPath(projectRoot, "Assets/Tone.wav"), makeProjectWav());

    fabgl::project::Manifest manifest;
    manifest.projectGuid = fabgl::AssetGuid::fromStableName("prepare.audio.project").toString();
    manifest.name = "Audio Metadata Project";
    const auto audioGuid = fabgl::AssetGuid::fromStableName("prepare.audio.asset");
    fabgl::project::ProjectAssetEntry audio(audioGuid, "Assets/Tone.wav", "audio");
    manifest.assets = {std::move(audio)};
    auto manifestText = fabgl::project::serializeManifest(manifest);
    FGL_CHECK(manifestText);
    const auto projectPath = joinTestPath(projectRoot, "Audio.fglproject");
    writeTestText(projectPath, manifestText.value());
    const auto legacyIndex =
        std::string(R"json({"kind":"fabgl.asset-index","version":1,"assets":[{"guid":")json") +
        audioGuid.toString() +
        R"json(","path":"Assets/Tone.wav","type":"audio","settings":"{\"encoding\":\"pcm16\",\"normalize\":false,\"streaming\":true,\"targetSampleRate\":8000,\"trimSilence\":false}","esp32Target":"sd","dependencies":[],"source":{},"imported":{}}]})json";
    writeTestText(joinTestPath(projectRoot, ".fabglstudio/asset-index-v1.json"), legacyIndex);

    auto prepared =
        fabgl::project::prepareProjectInputs(projectPath, joinTestPath(temporary.path(), "AudioPc"),
                                             fabgl::project::ProjectPrepareTarget::Pc);
    FGL_CHECK(prepared);
    FGL_CHECK(prepared.value().assetCount == 1U);
    FGL_CHECK(prepared.value().importedAssetCount == 1U);
    FGL_CHECK(prepared.value().validatedAssetCount == 1U);

    auto pack = fabgl::assets::inspectPack(readTestBytes(prepared.value().packPath));
    FGL_CHECK(pack && pack.value().index.size() == 1U);
    FGL_CHECK(pack.value().index.front().guid == audioGuid);
    FGL_CHECK(pack.value().index.front().storage == fabgl::assets::StorageClass::InternalRam);
    const auto& index = pack.value().index.front();
    const std::vector<std::uint8_t> encodedAudio(
        pack.value().bytes.begin() +
            static_cast<std::vector<std::uint8_t>::difference_type>(index.offset),
        pack.value().bytes.begin() +
            static_cast<std::vector<std::uint8_t>::difference_type>(index.offset + index.size));
    auto decodedAudio = fabgl::assets::decodeAudioClip(encodedAudio);
    FGL_CHECK(decodedAudio);
    FGL_CHECK(decodedAudio.value().sampleRate == 8000U);
    FGL_CHECK(decodedAudio.value().streaming);
    FGL_CHECK(!decodedAudio.value().samples.empty());

    auto preparedManifest =
        fabgl::project::parseManifest(readTestText(prepared.value().preparedProjectPath));
    FGL_CHECK(preparedManifest && preparedManifest.value().assets.size() == 1U);
    FGL_CHECK(preparedManifest.value().assets.front().hasImportMetadata);
    FGL_CHECK(preparedManifest.value().assets.front().esp32Target ==
              fabgl::assets::AssetTarget::Esp32Sd);

    auto unsupportedEsp32 = fabgl::project::prepareProjectInputs(
        projectPath, joinTestPath(temporary.path(), "AudioEsp32"),
        fabgl::project::ProjectPrepareTarget::Esp32);
    FGL_CHECK(!unsupportedEsp32);
    FGL_CHECK(unsupportedEsp32.error().code() == fabgl::ErrorCode::InvalidState);
    FGL_CHECK(unsupportedEsp32.error().message().find("not supported") != std::string::npos);
}

FGL_TEST(project_prepare_rejects_asset_guid_mismatch_without_claiming_compilation) {
    ExportTestDirectory temporary;
    const auto projectRoot = joinTestPath(temporary.path(), "MismatchedProject");
    fabgl::Scene scene("Mismatch", fabgl::SceneGuid::fromStableName("prepare.mismatch.scene"));
    auto sceneText = fabgl::SceneSerializer::serialize(scene);
    FGL_CHECK(sceneText);
    writeTestText(joinTestPath(projectRoot, "Scenes/Main.fglscene"), sceneText.value());

    fabgl::VisualGraph graph;
    graph.setGuid(fabgl::AssetGuid::fromStableName("prepare.actual.visual"));
    graph.setName("Mismatched Visual");
    auto entry = fabgl::VisualNodeRegistry::builtins().create(
        fabgl::VisualBuiltinNodeType::EventStart, 1U, "Start");
    auto returned = fabgl::VisualNodeRegistry::builtins().create(
        fabgl::VisualBuiltinNodeType::FlowReturn, 2U, "Return");
    auto number = fabgl::VisualNodeRegistry::builtins().create(
        fabgl::VisualBuiltinNodeType::NumberConstant, 3U, "Zero");
    FGL_CHECK(entry && returned && number);
    FGL_CHECK(graph.addNode(std::move(entry.value())));
    FGL_CHECK(graph.addNode(std::move(returned.value())));
    FGL_CHECK(graph.addNode(std::move(number.value())));
    graph.setEntryNode(1U);
    FGL_CHECK(graph.addEdge({1U, 1U, 2U, 1U}));
    FGL_CHECK(graph.addEdge({3U, 1U, 2U, 2U}));
    auto graphText = fabgl::serializeVisualGraph(graph);
    FGL_CHECK(graphText);
    writeTestText(joinTestPath(projectRoot, "Visual/Mismatch.fglvisual"), graphText.value());

    fabgl::project::Manifest manifest;
    manifest.projectGuid = fabgl::AssetGuid::fromStableName("prepare.mismatch.project").toString();
    manifest.name = "Mismatched Project";
    manifest.assets = {{fabgl::AssetGuid::fromStableName("prepare.expected.visual"),
                        "Visual/Mismatch.fglvisual", "visual.script"}};
    auto manifestText = fabgl::project::serializeManifest(manifest);
    FGL_CHECK(manifestText);
    const auto projectPath = joinTestPath(projectRoot, "Mismatch.fglproject");
    writeTestText(projectPath, manifestText.value());
    auto prepared =
        fabgl::project::prepareProjectInputs(projectPath, joinTestPath(temporary.path(), "Output"),
                                             fabgl::project::ProjectPrepareTarget::Pc);
    FGL_CHECK(!prepared);
    FGL_CHECK(prepared.error().code() == fabgl::ErrorCode::InvalidFormat);

    auto overlapping =
        fabgl::project::prepareProjectInputs(projectPath, joinTestPath(projectRoot, "BuildOutput"),
                                             fabgl::project::ProjectPrepareTarget::Pc);
    FGL_CHECK(!overlapping);
    FGL_CHECK(overlapping.error().code() == fabgl::ErrorCode::InvalidArgument);
    FGL_CHECK(!fabgl::assets::readBinaryFile(
        joinTestPath(projectRoot, "BuildOutput/project-assets.fglpack")));
}

FGL_TEST(project_prepare_rejects_unknown_canonical_tilemap_dependencies) {
    ExportTestDirectory temporary;
    const auto projectRoot = joinTestPath(temporary.path(), "UnknownTilemapDependency");
    fabgl::Scene scene("Unknown dependency",
                       fabgl::SceneGuid::fromStableName("prepare.tilemap-reference.scene"));
    auto sceneText = fabgl::SceneSerializer::serialize(scene);
    FGL_CHECK(sceneText);
    writeTestText(joinTestPath(projectRoot, "Scenes/Main.fglscene"), sceneText.value());

    const auto tilemapGuid = fabgl::AssetGuid::fromStableName("prepare.tilemap-reference.map");
    fabgl::assets::Tilemap tilemap;
    tilemap.width = 1U;
    tilemap.height = 1U;
    tilemap.tiles = {0U};
    tilemap.guid = tilemapGuid;
    fabgl::assets::TilemapTilesetReference reference;
    reference.tileset =
        fabgl::AssetGuid::fromStableName("prepare.tilemap-reference.unknown-tileset");
    reference.tileCount = 1U;
    tilemap.tilesets.push_back(reference);
    auto encodedTilemap = fabgl::assets::encodeTilemap(tilemap);
    FGL_CHECK(encodedTilemap);
    writeTestBytes(joinTestPath(projectRoot, "Assets/Map.fgltilemap"), encodedTilemap.value());

    fabgl::project::Manifest manifest;
    manifest.projectGuid =
        fabgl::AssetGuid::fromStableName("prepare.tilemap-reference.project").toString();
    manifest.name = "Unknown Tilemap Dependency";
    manifest.assets = {{tilemapGuid, "Assets/Map.fgltilemap", "tilemap"}};
    auto manifestText = fabgl::project::serializeManifest(manifest);
    FGL_CHECK(manifestText);
    const auto projectPath = joinTestPath(projectRoot, "UnknownDependency.fglproject");
    writeTestText(projectPath, manifestText.value());

    auto prepared =
        fabgl::project::prepareProjectInputs(projectPath, joinTestPath(temporary.path(), "Output"),
                                             fabgl::project::ProjectPrepareTarget::Pc);
    FGL_CHECK(!prepared && prepared.error().code() == fabgl::ErrorCode::InvalidFormat);
}
