#include "test_harness.h"

#include <fabgl/assets/file_io.h>
#include <fabgl/project/project_asset_streaming.h>
#include <fabgl/scene/scene.h>
#include <fabgl/serialization/scene_serializer.h>

#include <project_format.h>

#include <cstddef>
#include <iostream>
#include <string>

FGL_TEST(project_asset_streaming_loads_only_scene_dependency_closure_and_reloads_after_eviction) {
    const std::string root = std::string(FGL_TEST_REPOSITORY_ROOT) + "/examples/asset_streaming";
    auto projectSource = fabgl::assets::readTextFile(root + "/AssetStreaming.fglproject");
    auto sceneSource = fabgl::assets::readTextFile(root + "/Scenes/Main.fglscene");
    FGL_CHECK(projectSource && sceneSource);
    auto manifest = fabgl::project::parseManifest(projectSource.value());
    auto scene = fabgl::SceneSerializer::deserialize(sceneSource.value());
    if (!manifest)
        std::cerr << "asset-streaming manifest parse failed: " << manifest.error().message()
                  << '\n';
    if (!scene)
        std::cerr << "asset-streaming scene parse failed: " << scene.error().message() << '\n';
    FGL_CHECK(manifest);
    FGL_CHECK(scene);

    auto roots = fabgl::project::ProjectAssetStreamingRuntime::collectSceneRoots(*scene.value(),
                                                                                 manifest.value());
    FGL_CHECK(roots && roots.value().size() == 2U);
    auto streaming = fabgl::project::ProjectAssetStreamingRuntime::create(root, manifest.value());
    FGL_CHECK(streaming);

    const auto tilemap = fabgl::AssetGuid::parse("50000000-0000-4000-8000-000000000010");
    const auto marker = fabgl::AssetGuid::parse("51000000-0000-4000-8000-000000000010");
    FGL_CHECK(tilemap && marker);
    auto resources = streaming.value().resources();
    FGL_CHECK(resources.tilemap(tilemap.value()) == nullptr);
    FGL_CHECK(resources.sprite(marker.value()) == nullptr);

    FGL_CHECK(streaming.value().loadTransitionBlocking(roots.value()));
    FGL_CHECK(resources.tilemap(tilemap.value()) != nullptr);
    FGL_CHECK(resources.sprite(marker.value()) != nullptr);
    FGL_CHECK(streaming.value().stats().loads == 4U);
    FGL_CHECK(streaming.value().residentAssetCount() == 4U);

    FGL_CHECK(streaming.value().beginTransition({}));
    FGL_CHECK(streaming.value().update());
    FGL_CHECK(streaming.value().evictUnused() == 4U);
    FGL_CHECK(resources.tilemap(tilemap.value()) == nullptr);
    FGL_CHECK(resources.sprite(marker.value()) == nullptr);

    FGL_CHECK(streaming.value().loadTransitionBlocking(roots.value()));
    FGL_CHECK(resources.tilemap(tilemap.value()) != nullptr);
    FGL_CHECK(resources.sprite(marker.value()) != nullptr);
    FGL_CHECK(streaming.value().stats().loads == 8U);
    FGL_CHECK(streaming.value().stats().evictions == 4U);
}

FGL_TEST(project_asset_selected_loader_excludes_unreferenced_manifest_assets) {
    const std::string root = std::string(FGL_TEST_REPOSITORY_ROOT) + "/examples/asset_streaming";
    auto source = fabgl::assets::readTextFile(root + "/AssetStreaming.fglproject");
    FGL_CHECK(source);
    auto manifest = fabgl::project::parseManifest(source.value());
    const auto tilemap = fabgl::AssetGuid::parse("50000000-0000-4000-8000-000000000010");
    FGL_CHECK(manifest && tilemap);
    auto selected = fabgl::project::ProjectAssetLibrary::loadSelected(root, manifest.value(),
                                                                      {tilemap.value()});
    FGL_CHECK(selected);
    FGL_CHECK(selected.value().stats().loadedAssetEntries == 3U);
    FGL_CHECK(selected.value().stats().loadedAssets == 2U);
    FGL_CHECK(selected.value().stats().estimatedResidentBytes > 0U);
    auto resources = selected.value().resources();
    FGL_CHECK(resources.tilemap(tilemap.value()) != nullptr);
    const auto marker = fabgl::AssetGuid::parse("51000000-0000-4000-8000-000000000010");
    FGL_CHECK(marker && resources.sprite(marker.value()) == nullptr);
}

FGL_TEST(project_asset_streaming_discovers_racer_track_sprite_dependencies) {
    const std::string root = std::string(FGL_TEST_REPOSITORY_ROOT) + "/examples/pseudo3d_racer";
    auto source = fabgl::assets::readTextFile(root + "/Racer.fglproject");
    FGL_CHECK(source);
    auto manifest = fabgl::project::parseManifest(source.value());
    const auto track = fabgl::AssetGuid::parse("50000000-0000-4000-8000-000000000005");
    const auto sprite = fabgl::AssetGuid::parse("51000000-0000-4000-8000-000000000001");
    FGL_CHECK(manifest && track && sprite);

    auto dependencies = fabgl::project::ProjectAssetLibrary::directDependencies(
        root, manifest.value(), track.value());
    FGL_CHECK(dependencies && dependencies.value().size() == 1U);
    FGL_CHECK(dependencies.value().front() == sprite.value());
    auto selected =
        fabgl::project::ProjectAssetLibrary::loadSelected(root, manifest.value(), {track.value()});
    FGL_CHECK(selected);
    FGL_CHECK(selected.value().stats().loadedAssetEntries == 2U);
    auto resources = selected.value().resources();
    FGL_CHECK(resources.racerTrack(track.value()) != nullptr);
    FGL_CHECK(resources.sprite(sprite.value()) != nullptr);
}
