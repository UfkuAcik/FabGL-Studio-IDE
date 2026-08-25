#include "test_harness.h"

#include <fabgl/assets/file_io.h>
#include <fabgl/assets/image_pipeline.h>
#include <fabgl/assets/mesh_importer.h>
#include <fabgl/assets/tilemap_importer.h>
#include <fabgl/audio/audio_mixer.h>
#include <fabgl/project/project_asset_library.h>
#include <fabgl/project/project_scene_audio.h>
#include <fabgl/reflection/reflection.h>
#include <fabgl/rendering/framebuffer.h>
#include <fabgl/rendering/scene_presenter.h>
#include <fabgl/runtime/scene_runtime.h>
#include <fabgl/scene/builtin_components.h>
#include <fabgl/scene/entity.h>
#include <fabgl/scene/scene.h>
#include <fabgl/serialization/scene_serializer.h>
#include <fabgl/serialization/material_serializer.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <project_format.h>
#include <string>

namespace {

class ScopedAssetDirectory final {
  public:
    ScopedAssetDirectory() {
        path_ = std::filesystem::temp_directory_path() /
                ("fabgl-tile-runtime-" + fabgl::AssetGuid::generate().toString());
        std::filesystem::create_directories(path_ / "Assets");
    }
    ~ScopedAssetDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    [[nodiscard]] std::string path() const {
        return path_.generic_string();
    }

  private:
    std::filesystem::path path_;
};

fabgl::DataComponent* addBuiltin(fabgl::Entity& entity, const fabgl::ReflectionRegistry& registry,
                                 const char* name) {
    auto created = fabgl::createBuiltinDataComponent(registry, name);
    FGL_CHECK(created);
    auto* raw = created.value().get();
    auto added = entity.addComponent(std::move(created.value()));
    FGL_CHECK(added);
    return raw;
}

} // namespace

FGL_TEST(scene_presenter_renders_serialized_components_and_changes_when_scene_data_changes) {
    fabgl::ReflectionRegistry registry;
    FGL_CHECK(fabgl::registerBuiltinComponentTypes(registry));
    fabgl::Scene scene("Data-driven presentation",
                       fabgl::SceneGuid::fromStableName("tests.scene-presenter.scene"));
    auto camera = scene.createEntity(
        "Camera", fabgl::EntityGuid::fromStableName("tests.scene-presenter.camera"));
    auto player = scene.createEntity(
        "Player", fabgl::EntityGuid::fromStableName("tests.scene-presenter.player"));
    FGL_CHECK(camera && player);
    auto* cameraComponent = addBuiltin(*camera.value(), registry, "Camera");
    FGL_CHECK(cameraComponent->set("clearColor", fabgl::Color{3U, 7U, 13U, 255U}));
    auto* spriteComponent = addBuiltin(*player.value(), registry, "SpriteRenderer");
    const auto spriteGuid = fabgl::AssetGuid::fromStableName("tests.scene-presenter.sprite");
    FGL_CHECK(spriteComponent->set("sprite", spriteGuid));
    FGL_CHECK(spriteComponent->set("tint", fabgl::Color{210U, 80U, 40U, 255U}));
    player.value()->transform().setLocalPosition({30.0F, 42.0F, 2.0F});

    auto serialized = fabgl::SceneSerializer::serialize(scene);
    FGL_CHECK(serialized);
    auto loaded = fabgl::SceneSerializer::deserialize(serialized.value());
    FGL_CHECK(loaded);

    auto sprite = std::make_shared<fabgl::rendering::Sprite>(fabgl::rendering::makeCheckerSprite(
        6, 6, {255U, 255U, 255U, 255U}, {120U, 120U, 120U, 255U}));
    fabgl::rendering::ScenePresentationResources resources;
    resources.sprite = [sprite, spriteGuid](const fabgl::AssetGuid requested) {
        return requested == spriteGuid ? std::shared_ptr<const fabgl::rendering::Sprite>(sprite)
                                       : std::shared_ptr<const fabgl::rendering::Sprite>{};
    };
    fabgl::rendering::Framebuffer framebuffer(96, 72);
    fabgl::rendering::ScenePresenter presenter(framebuffer, std::move(resources));
    const auto first = presenter.render(*loaded.value());
    const auto firstChecksum = framebuffer.checksum();
    FGL_CHECK(first.mode == fabgl::rendering::ScenePresentationMode::TwoDimensional);
    FGL_CHECK(first.activeEntities == 2U);
    FGL_CHECK(first.sprites == 1U);
    FGL_CHECK(first.missingAssets == 0U);

    auto* loadedPlayer = loaded.value()->findEntity(player.value()->id());
    FGL_CHECK(loadedPlayer != nullptr);
    loadedPlayer->transform().setLocalPosition({62.0F, 18.0F, 2.0F});
    const auto second = presenter.render(*loaded.value());
    FGL_CHECK(second.sprites == 1U);
    FGL_CHECK(framebuffer.checksum() != firstChecksum);
}

FGL_TEST(scene_presenter_selects_renderer_from_components_not_a_demo_hint) {
    fabgl::ReflectionRegistry registry;
    FGL_CHECK(fabgl::registerBuiltinComponentTypes(registry));
    fabgl::Scene scene("Raycast component scene");
    auto map = scene.createEntity("Map");
    auto camera = scene.createEntity("Camera");
    FGL_CHECK(map && camera);
    auto* mapComponent = addBuiltin(*map.value(), registry, "RaycastMap");
    const auto mapGuid = fabgl::AssetGuid::fromStableName("tests.scene-presenter.raycast-map");
    FGL_CHECK(mapComponent->set("map", mapGuid));
    static_cast<void>(addBuiltin(*camera.value(), registry, "Camera"));
    camera.value()->transform().setLocalPosition({2.5F, 2.5F, 0.0F});

    auto raycastMap =
        std::make_shared<fabgl::rendering::RaycastMap>(fabgl::rendering::makeDemoRaycastMap());
    fabgl::rendering::ScenePresentationResources resources;
    resources.raycastMap = [raycastMap, mapGuid](const fabgl::AssetGuid requested) {
        return requested == mapGuid
                   ? std::shared_ptr<const fabgl::rendering::RaycastMap>(raycastMap)
                   : std::shared_ptr<const fabgl::rendering::RaycastMap>{};
    };
    fabgl::rendering::Framebuffer framebuffer(96, 72);
    fabgl::rendering::ScenePresenter presenter(framebuffer, std::move(resources));
    const auto stats = presenter.render(scene);
    FGL_CHECK(stats.mode == fabgl::rendering::ScenePresentationMode::Raycast);
    FGL_CHECK(stats.rays == 96U);
    FGL_CHECK(stats.missingAssets == 0U);
}

FGL_TEST(project_asset_library_loads_manifest_guid_bound_racer_track) {
    const std::string root = std::string(FGL_TEST_REPOSITORY_ROOT) + "/examples/pseudo3d_racer";
    auto source = fabgl::assets::readTextFile(root + "/Racer.fglproject");
    FGL_CHECK(source);
    auto manifest = fabgl::project::parseManifest(source.value());
    FGL_CHECK(manifest);
    FGL_CHECK(manifest.value().assets.size() == 2U);

    auto library = fabgl::project::ProjectAssetLibrary::load(root, manifest.value());
    FGL_CHECK(library);
    FGL_CHECK(library.value().stats().loadedAssets == 2U);
    FGL_CHECK(library.value().stats().sourceBytes > 0U);
    auto resources = library.value().resources();
    const auto trackGuid = fabgl::AssetGuid::parse("50000000-0000-4000-8000-000000000005");
    const auto spriteGuid = fabgl::AssetGuid::parse("51000000-0000-4000-8000-000000000001");
    FGL_CHECK(trackGuid && spriteGuid);
    auto track = resources.racerTrack(trackGuid.value());
    FGL_CHECK(track != nullptr);
    FGL_CHECK(track->guid == trackGuid.value());
    FGL_CHECK(!track->segments.empty());
    FGL_CHECK(resources.sprite(spriteGuid.value()) != nullptr);
}

FGL_TEST(project_asset_library_resolves_material_texture_and_lowpoly_uvs_end_to_end) {
    ScopedAssetDirectory directory;
    const auto imageGuid = fabgl::AssetGuid::fromStableName("runtime.material.image");
    const auto materialGuid = fabgl::AssetGuid::fromStableName("runtime.material.asset");
    const auto meshGuid = fabgl::AssetGuid::fromStableName("runtime.material.mesh");

    fabgl::assets::IndexedImage image;
    image.width = 2;
    image.height = 2;
    image.palette = {{18U, 204U, 74U, 255U}};
    image.indices = {0U, 0U, 0U, 0U};
    const auto imageBytes = fabgl::assets::encodeIndexedImage(image);
    FGL_CHECK(fabgl::assets::writeBinaryFileAtomic(directory.path() + "/Assets/Texture.fgli",
                                                   imageBytes));

    fabgl::assets::LowPolyMesh sourceMesh;
    sourceMesh.positions = {{-1.0F, -1.0F, 0.0F}, {1.0F, -1.0F, 0.0F},
                            {0.0F, 1.0F, 0.0F}};
    sourceMesh.textureCoordinates = {{0.0F, 0.0F}, {1.0F, 0.0F}, {0.5F, 1.0F}};
    sourceMesh.indices = {0U, 1U, 2U};
    sourceMesh.boundsMinimum = {-1.0F, -1.0F, 0.0F};
    sourceMesh.boundsMaximum = {1.0F, 1.0F, 0.0F};
    const auto meshBytes = fabgl::assets::encodeLowPolyMesh(sourceMesh);
    FGL_CHECK(meshBytes);
    FGL_CHECK(fabgl::assets::writeBinaryFileAtomic(directory.path() + "/Assets/Mesh.fglm",
                                                   meshBytes.value()));

    fabgl::Material material;
    material.baseTexture = imageGuid;
    material.colorMode = fabgl::MaterialColorMode::Texture;
    material.sampling = fabgl::MaterialSamplingMode::Nearest;
    material.lighting = fabgl::MaterialLightingMode::Unlit;
    material.doubleSided = true;
    material.compatibleRenderers = fabgl::RendererCompatibility::LowPoly;
    const auto materialText = fabgl::MaterialSerializer::serialize(
        {materialGuid, "Runtime textured material", material});
    FGL_CHECK(materialText);
    const std::vector<std::uint8_t> materialBytes(materialText.value().begin(),
                                                  materialText.value().end());
    FGL_CHECK(fabgl::assets::writeBinaryFileAtomic(
        directory.path() + "/Assets/Surface.fglmaterial", materialBytes));

    fabgl::project::Manifest manifest;
    manifest.projectGuid = fabgl::AssetGuid::fromStableName("runtime.material.project").toString();
    manifest.name = "Runtime Material";
    manifest.assets = {{meshGuid, "Assets/Mesh.fglm", "mesh"},
                       {materialGuid, "Assets/Surface.fglmaterial", "material"},
                       {imageGuid, "Assets/Texture.fgli", "image"}};
    auto dependencies = fabgl::project::ProjectAssetLibrary::directDependencies(
        directory.path(), manifest, materialGuid);
    FGL_CHECK(dependencies && dependencies.value().size() == 1U &&
              dependencies.value().front() == imageGuid);
    auto library = fabgl::project::ProjectAssetLibrary::load(directory.path(), manifest);
    FGL_CHECK(library && library.value().stats().loadedMaterials == 1U);
    auto resources = library.value().resources();
    FGL_CHECK(resources.material(materialGuid) != nullptr);

    fabgl::ReflectionRegistry registry;
    FGL_CHECK(fabgl::registerBuiltinComponentTypes(registry));
    fabgl::Scene scene("Runtime textured low-poly");
    auto entity = scene.createEntity("Mesh");
    FGL_CHECK(entity);
    auto* renderer = addBuiltin(*entity.value(), registry, "MeshRenderer");
    FGL_CHECK(renderer->set("mesh", meshGuid));
    FGL_CHECK(renderer->set("material", materialGuid));
    fabgl::rendering::Framebuffer framebuffer(32, 32);
    framebuffer.clear({0U, 0U, 0U, 255U});
    fabgl::rendering::ScenePresenter presenter(framebuffer, std::move(resources));
    const auto stats = presenter.render(scene);
    FGL_CHECK(stats.mode == fabgl::rendering::ScenePresentationMode::LowPoly);
    FGL_CHECK(stats.triangles == 1U && stats.missingAssets == 0U);
    FGL_CHECK(framebuffer.pixel(16, 16) == (fabgl::Color{18U, 204U, 74U, 255U}));
}

FGL_TEST(project_asset_library_builds_manifest_bound_animator_and_samples_clip) {
    const std::string root = std::string(FGL_TEST_REPOSITORY_ROOT) + "/examples/animation_showcase";
    auto source = fabgl::assets::readTextFile(root + "/AnimationShowcase.fglproject");
    FGL_CHECK(source);
    auto manifest = fabgl::project::parseManifest(source.value());
    FGL_CHECK(manifest);
    auto library = fabgl::project::ProjectAssetLibrary::load(root, manifest.value());
    FGL_CHECK(library);
    FGL_CHECK(library.value().stats().loadedAnimationClips == 1U);
    FGL_CHECK(library.value().stats().loadedAnimatorControllers == 1U);

    const auto controllerGuid = fabgl::AssetGuid::parse("52000000-0000-4000-8000-000000000009");
    FGL_CHECK(controllerGuid);
    auto animator = library.value().createAnimator(controllerGuid.value());
    FGL_CHECK(animator);
    auto frame = animator.value()->update(0.5F);
    FGL_CHECK(frame);
    FGL_CHECK(frame.value().state == "idle");
    const auto sampled = frame.value().values.find("Transform.localPosition.y");
    FGL_CHECK(sampled != frame.value().values.end());
    FGL_CHECK_NEAR(sampled->second, -6.0F, 0.0001F);
}

FGL_TEST(project_asset_library_resolves_real_tileset_pixels_layers_collision_and_animation) {
    ScopedAssetDirectory directory;
    const auto imageGuid = fabgl::AssetGuid::fromStableName("runtime.tiles.image");
    const auto tilesetGuid = fabgl::AssetGuid::fromStableName("runtime.tiles.tileset");
    const auto tilemapGuid = fabgl::AssetGuid::fromStableName("runtime.tiles.map");

    fabgl::assets::IndexedImage image;
    image.width = 4;
    image.height = 2;
    image.palette = {{0U, 0U, 0U, 0U}, {220U, 30U, 20U, 255U}, {20U, 210U, 60U, 255U}};
    image.indices = {1U, 1U, 2U, 2U, 1U, 1U, 2U, 2U};
    image.transparentIndex = 0U;
    const auto imageBytes = fabgl::assets::encodeIndexedImage(image);
    FGL_CHECK(!imageBytes.empty());
    FGL_CHECK(
        fabgl::assets::writeBinaryFileAtomic(directory.path() + "/Assets/Tiles.fgli", imageBytes));

    fabgl::assets::Tileset tileset;
    tileset.guid = tilesetGuid;
    tileset.name = "Runtime Tiles";
    tileset.sourceImage = imageGuid;
    tileset.tileWidth = 2U;
    tileset.tileHeight = 2U;
    tileset.tileCount = 2U;
    tileset.columns = 2U;
    tileset.collisionTiles = {1U};
    const auto tilesetBytes = fabgl::assets::encodeTileset(tileset);
    FGL_CHECK(tilesetBytes);
    FGL_CHECK(fabgl::assets::writeBinaryFileAtomic(directory.path() + "/Assets/Tiles.fgltileset",
                                                   tilesetBytes.value()));

    fabgl::assets::Tilemap tilemap;
    tilemap.guid = tilemapGuid;
    tilemap.width = 2U;
    tilemap.height = 1U;
    tilemap.tileWidth = 2U;
    tilemap.tileHeight = 2U;
    fabgl::assets::TilemapLayer ground;
    ground.name = "Ground";
    ground.cells = {1U, 2U};
    tilemap.layers.push_back(ground);
    tilemap.tiles = ground.cells;
    fabgl::assets::TilemapTilesetReference reference;
    reference.tileset = tilesetGuid;
    reference.firstTile = 1U;
    reference.tileCount = 2U;
    tilemap.tilesets.push_back(reference);
    fabgl::assets::TilemapObject object;
    object.id = 9U;
    object.type = "Exit";
    object.bounds = {1.0F, 0.0F, 1.0F, 1.0F};
    object.asset = imageGuid;
    tilemap.objects.push_back(object);
    fabgl::assets::TilemapChunk chunk;
    chunk.width = 2U;
    chunk.height = 1U;
    chunk.cells = {1U, 2U};
    tilemap.chunks.push_back(chunk);
    fabgl::assets::TileAnimation animation;
    animation.outputTile = 1U;
    animation.frames = {{1U, 50U}, {2U, 150U}};
    tilemap.animations.push_back(animation);
    const auto tilemapBytes = fabgl::assets::encodeTilemap(tilemap);
    FGL_CHECK(tilemapBytes);
    FGL_CHECK(fabgl::assets::writeBinaryFileAtomic(directory.path() + "/Assets/World.fgltilemap",
                                                   tilemapBytes.value()));

    fabgl::project::Manifest manifest;
    manifest.projectGuid = fabgl::AssetGuid::fromStableName("runtime.tiles.project").toString();
    manifest.name = "Runtime Tiles";
    manifest.assets = {{tilemapGuid, "Assets/World.fgltilemap", "tilemap"},
                       {tilesetGuid, "Assets/Tiles.fgltileset", "tileset"},
                       {imageGuid, "Assets/Tiles.fgli", "image"}};
    auto library = fabgl::project::ProjectAssetLibrary::load(directory.path(), manifest);
    FGL_CHECK(library);
    auto runtimeMap = library.value().resources().tilemap(tilemapGuid);
    FGL_CHECK(runtimeMap && runtimeMap->valid());
    FGL_CHECK(runtimeMap->layers.size() == 1U && runtimeMap->layers.front().cells[1U] == 2U);
    FGL_CHECK(runtimeMap->tiles.size() == 3U);
    const fabgl::Color expectedRed{220U, 30U, 20U, 255U};
    const fabgl::Color expectedGreen{20U, 210U, 60U, 255U};
    FGL_CHECK(runtimeMap->tiles[1U].pixels.front() == expectedRed);
    FGL_CHECK(runtimeMap->tiles[2U].pixels.front() == expectedGreen);
    FGL_CHECK(runtimeMap->objects.size() == 1U && runtimeMap->objects.front().id == 9U &&
              runtimeMap->objects.front().asset == imageGuid);
    FGL_CHECK(runtimeMap->chunks.size() == 1U);
    FGL_CHECK(runtimeMap->animations.front().frameDurationsSeconds[1U] == 0.15F);
    FGL_CHECK(runtimeMap->tileAt(0U, 0, 0, 0.06F) == 2U);
    FGL_CHECK(runtimeMap->collides(1, 0));

    tilemap.objects.front().asset = fabgl::AssetGuid::fromStableName("runtime.tiles.missing");
    const auto invalidTilemapBytes = fabgl::assets::encodeTilemap(tilemap);
    FGL_CHECK(invalidTilemapBytes);
    FGL_CHECK(fabgl::assets::writeBinaryFileAtomic(directory.path() + "/Assets/World.fgltilemap",
                                                   invalidTilemapBytes.value()));
    auto invalidLibrary = fabgl::project::ProjectAssetLibrary::load(directory.path(), manifest);
    FGL_CHECK(!invalidLibrary && invalidLibrary.error().code() == fabgl::ErrorCode::InvalidFormat);
}

FGL_TEST(project_scene_audio_binds_serialized_sources_and_mixes_decoded_pcm) {
    const std::string root = std::string(FGL_TEST_REPOSITORY_ROOT) + "/examples/audio_showcase";
    auto manifestSource = fabgl::assets::readTextFile(root + "/AudioShowcase.fglproject");
    auto sceneSource = fabgl::assets::readTextFile(root + "/Scenes/Main.fglscene");
    FGL_CHECK(manifestSource && sceneSource);
    auto manifest = fabgl::project::parseManifest(manifestSource.value());
    auto scene = fabgl::SceneSerializer::deserialize(sceneSource.value());
    FGL_CHECK(manifest && scene);
    auto library = fabgl::project::ProjectAssetLibrary::load(root, manifest.value());
    FGL_CHECK(library);
    FGL_CHECK(library.value().stats().loadedAudioClips == 1U);
    const auto toneGuid = fabgl::AssetGuid::parse("50000000-0000-4000-8000-000000000008");
    FGL_CHECK(toneGuid);
    const auto tone = library.value().audioClip(toneGuid.value());
    FGL_CHECK(tone && tone->valid());
    FGL_CHECK(tone->streaming && tone->usesStreamingReader());
    FGL_CHECK(tone->samples.empty() && !tone->encodedBytes.empty());
    FGL_CHECK(tone->encodedBytes.size() < tone->frameCount() * sizeof(float));

    fabgl::AudioMixerConfig config;
    config.outputSampleRate = 48'000U;
    config.maximumVoices = 4U;
    config.mixBlockFrames = 800U;
    fabgl::AudioMixer mixer(config);
    fabgl::project::ProjectSceneAudioRuntime runtime(*scene.value(), library.value(), mixer);
    FGL_CHECK(runtime.initialize());
    FGL_CHECK(runtime.stats().listeners == 1U);
    FGL_CHECK(runtime.stats().sources == 2U);
    FGL_CHECK(runtime.stats().voicesStarted == 2U);
    FGL_CHECK(runtime.activeVoiceCount() == 2U);

    std::vector<float> stereo(1600U);
    FGL_CHECK(mixer.mixTo(stereo.data(), 800U));
    FGL_CHECK(mixer.stats().mixedFrames == 800U);
    FGL_CHECK(mixer.stats().streamCacheRefills > 0U);
    FGL_CHECK(mixer.stats().streamedFrames > 0U);
    FGL_CHECK(mixer.stats().streamUnderruns == 0U);
    FGL_CHECK(std::count_if(stereo.begin(), stereo.end(),
                            [](const float sample) { return sample != 0.0F; }) > 0);
    runtime.shutdown();
    FGL_CHECK(runtime.activeVoiceCount() == 0U);
}

FGL_TEST(scene_presenter_draws_runtime_ui_rectangles_text_and_glyphs) {
    fabgl::ReflectionRegistry registry;
    FGL_CHECK(fabgl::registerBuiltinComponentTypes(registry));
    fabgl::Scene scene("Runtime UI presentation");
    auto label = scene.createEntity("ReadyLabel");
    auto emitter = scene.createEntity("Particles");
    FGL_CHECK(label && emitter);
    auto* transform = addBuiltin(*label.value(), registry, "UITransform");
    FGL_CHECK(transform->set("widgetType", std::int64_t{2}));
    FGL_CHECK(transform->set("offsetMinimum", fabgl::Vec2{8.0F, 8.0F}));
    FGL_CHECK(transform->set("offsetMaximum", fabgl::Vec2{88.0F, 28.0F}));
    FGL_CHECK(transform->set("text", std::string("READY")));
    auto* text = addBuiltin(*label.value(), registry, "UIText");
    FGL_CHECK(text->set("text", std::string("READY")));
    FGL_CHECK(text->set("color", fabgl::Color{250U, 220U, 80U, 255U}));
    auto* particles = addBuiltin(*emitter.value(), registry, "ParticleEmitter");
    FGL_CHECK(particles->set("rate", 0.0));
    FGL_CHECK(particles->set("maxParticles", std::uint64_t{8U}));
    FGL_CHECK(particles->set("burstOnStart", std::uint64_t{3U}));
    FGL_CHECK(particles->set("lifetime", 2.0));
    emitter.value()->transform().setLocalPosition({48.0F, 48.0F, 0.0F});

    fabgl::SceneRuntimeConfig config;
    config.uiViewport = {0.0F, 0.0F, 96.0F, 72.0F};
    fabgl::SceneRuntime runtime(scene, config);
    FGL_CHECK(scene.start());
    FGL_CHECK(runtime.initialize());
    fabgl::rendering::Framebuffer framebuffer(96, 72);
    fabgl::rendering::ScenePresenter presenter(framebuffer);
    const auto stats = presenter.render(scene, &runtime);
    FGL_CHECK(stats.uiWidgets == 1U);
    FGL_CHECK(stats.uiGlyphs == 5U);
    FGL_CHECK(stats.particles == 3U);
    FGL_CHECK(stats.drawCalls >= 1U);
    fabgl::rendering::Framebuffer background(96, 72);
    background.clear({18U, 24U, 34U, 255U});
    FGL_CHECK(framebuffer.checksum() != background.checksum());
    runtime.shutdown();
    scene.shutdown();
}
