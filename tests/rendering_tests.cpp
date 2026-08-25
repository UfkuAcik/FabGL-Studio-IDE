#include "test_harness.h"

#include <demo.h>
#include <fabgl/assets/file_io.h>
#include <fabgl/rendering/framebuffer.h>
#include <fabgl/rendering/lowpoly_renderer.h>
#include <fabgl/rendering/racer_renderer.h>
#include <fabgl/rendering/racer_track.h>
#include <fabgl/rendering/raycast_renderer.h>
#include <fabgl/rendering/renderer_2d.h>

#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::uint64_t renderDemo(fabgl::player::DemoKind kind) {
    fabgl::rendering::Framebuffer framebuffer(320, 180);
    fabgl::player::Demo demo(framebuffer, kind);
    fabgl::player::InputState input;
    input.forward = true;
    input.right = kind == fabgl::player::DemoKind::Racer;
    for (auto frame = 0; frame < 180; ++frame) {
        input.action = (frame % 90) == 4;
        demo.update(1.0F / 60.0F, input);
        demo.render();
    }
    return framebuffer.checksum();
}

fabgl::rendering::RacerTrackAsset completeRacerTrack() {
    fabgl::rendering::RacerTrackAsset track;
    track.guid = fabgl::AssetGuid::fromStableName("tests.racer-track");
    track.name = "Coast \\ Test\nTrack";
    track.segmentLength = 2.5F;
    track.startSegment = 0U;
    track.finishSegment = 7U;
    track.weather = {
        fabgl::rendering::RacerWeatherKind::Rain, 0.5F, 0.7F, -0.2F, {160U, 180U, 210U, 255U}, 42U};
    track.segments.resize(8U);
    for (std::size_t index = 0U; index < track.segments.size(); ++index) {
        track.segments[index].curve = static_cast<float>(index) * 0.01F;
        track.segments[index].hill = index % 2U == 0U ? 0.25F : -0.25F;
        track.segments[index].width = 0.8F + static_cast<float>(index) * 0.02F;
    }
    // Checkpoint order is semantic race order, rather than segment sort order.
    track.checkpoints = {{4U, "Tunnel"}, {0U, "Start/Finish"}};
    track.roadsideObjects = {
        {9U,
         3U,
         1.25F,
         0.8F,
         fabgl::AssetGuid::fromStableName("tests.tree"),
         {30U, 180U, 60U, 255U}},
        {2U,
         1U,
         -1.1F,
         1.2F,
         fabgl::AssetGuid::fromStableName("tests.sign"),
         {240U, 220U, 80U, 255U}},
    };
    track.backgroundLayers = {
        {4U,
         fabgl::AssetGuid::fromStableName("tests.mountains"),
         0.15F,
         2.0F,
         1.5F,
         {90U, 110U, 140U, 255U}},
    };
    track.opponentSpawns = {
        {7U, 2U, 0.25F, 48.0F, 0.8F, fabgl::AssetGuid::fromStableName("tests.car-blue")},
        {3U, 6U, -0.3F, 42.0F, 0.4F, fabgl::AssetGuid::fromStableName("tests.car-red")},
    };
    return track;
}

struct PpmImage final {
    int width = 0;
    int height = 0;
    std::vector<fabgl::Color> pixels;
};

struct ImageDifference final {
    std::uint32_t maximumRed = 0U;
    std::uint32_t maximumGreen = 0U;
    std::uint32_t maximumBlue = 0U;
    std::size_t mismatchedPixels = 0U;
    double mismatchRatio = 1.0;
};

std::string nextPpmToken(const std::vector<std::uint8_t>& bytes, std::size_t& cursor) {
    while (cursor < bytes.size()) {
        if (bytes[cursor] == static_cast<std::uint8_t>('#')) {
            while (cursor < bytes.size() && bytes[cursor] != static_cast<std::uint8_t>('\n'))
                ++cursor;
        } else if (std::isspace(static_cast<unsigned char>(bytes[cursor])) != 0) {
            ++cursor;
        } else {
            break;
        }
    }
    const auto first = cursor;
    while (cursor < bytes.size() &&
           std::isspace(static_cast<unsigned char>(bytes[cursor])) == 0 &&
           bytes[cursor] != static_cast<std::uint8_t>('#')) {
        ++cursor;
    }
    return std::string(reinterpret_cast<const char*>(bytes.data() + first), cursor - first);
}

int ppmInteger(const std::string_view text) {
    int result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    FGL_CHECK(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size());
    return result;
}

PpmImage readPpm(const std::filesystem::path& path) {
    auto source = fabgl::assets::readBinaryFile(path.string());
    FGL_CHECK(source);
    auto& bytes = source.value();
    std::size_t cursor = 0U;
    FGL_CHECK(nextPpmToken(bytes, cursor) == "P6");
    PpmImage result;
    result.width = ppmInteger(nextPpmToken(bytes, cursor));
    result.height = ppmInteger(nextPpmToken(bytes, cursor));
    FGL_CHECK(ppmInteger(nextPpmToken(bytes, cursor)) == 255);
    FGL_CHECK(result.width > 0 && result.height > 0 && cursor < bytes.size());
    FGL_CHECK(std::isspace(static_cast<unsigned char>(bytes[cursor])) != 0);
    ++cursor;
    const auto pixelCount = static_cast<std::size_t>(result.width) *
                            static_cast<std::size_t>(result.height);
    FGL_CHECK(pixelCount <= (std::numeric_limits<std::size_t>::max() / 3U));
    FGL_CHECK(bytes.size() - cursor == pixelCount * 3U);
    result.pixels.reserve(pixelCount);
    for (std::size_t index = 0U; index < pixelCount; ++index) {
        result.pixels.push_back(
            {bytes[cursor + index * 3U], bytes[cursor + index * 3U + 1U],
             bytes[cursor + index * 3U + 2U], 255U});
    }
    return result;
}

ImageDifference compareImages(const fabgl::rendering::Framebuffer& actual,
                              const PpmImage& expected,
                              const std::uint32_t perChannelTolerance) {
    FGL_CHECK(actual.width() == expected.width && actual.height() == expected.height);
    FGL_CHECK(actual.pixels().size() == expected.pixels.size());
    ImageDifference difference;
    for (std::size_t index = 0U; index < actual.pixels().size(); ++index) {
        const auto& lhs = actual.pixels()[index];
        const auto& rhs = expected.pixels[index];
        const auto red = static_cast<std::uint32_t>(std::abs(static_cast<int>(lhs.r) - rhs.r));
        const auto green =
            static_cast<std::uint32_t>(std::abs(static_cast<int>(lhs.g) - rhs.g));
        const auto blue = static_cast<std::uint32_t>(std::abs(static_cast<int>(lhs.b) - rhs.b));
        difference.maximumRed = std::max(difference.maximumRed, red);
        difference.maximumGreen = std::max(difference.maximumGreen, green);
        difference.maximumBlue = std::max(difference.maximumBlue, blue);
        if (red > perChannelTolerance || green > perChannelTolerance ||
            blue > perChannelTolerance) {
            ++difference.mismatchedPixels;
        }
    }
    difference.mismatchRatio = actual.pixels().empty()
                                   ? 0.0
                                   : static_cast<double>(difference.mismatchedPixels) /
                                         static_cast<double>(actual.pixels().size());
    return difference;
}

fabgl::rendering::Framebuffer renderTwoDimensionalReferenceScene() {
    fabgl::rendering::Framebuffer framebuffer(96, 64);
    framebuffer.clear({11U, 18U, 31U, 255U});
    fabgl::rendering::Renderer2D renderer(framebuffer, 32U);
    const auto tile = fabgl::rendering::makeCheckerSprite(
        8, 8, {35U, 94U, 62U, 255U}, {27U, 67U, 49U, 255U});
    fabgl::rendering::Tilemap map;
    map.width = 12;
    map.height = 8;
    map.tileSize = 8;
    map.tiles = {tile};
    map.cells.assign(96U, 0U);
    renderer.drawTilemap(map, {0.0F, 0.0F}, {0.0F, 0.0F, 96.0F, 64.0F});

    const auto actor = fabgl::rendering::makeCheckerSprite(
        10, 10, {245U, 208U, 82U, 255U}, {198U, 66U, 66U, 220U});
    FGL_CHECK(renderer.submit({{&actor, 29, 24, 2, false, false,
                                {255U, 255U, 255U, 255U}, nullptr, {}, 18.0F},
                               1, 4, {1.0F, 1.0F}, false}));
    FGL_CHECK(renderer.submit({{&actor, 62, 14, 1, true, false,
                                {100U, 190U, 255U, 210U}, nullptr, {}, -27.0F},
                               0, 2, {0.5F, 0.5F}, false}));
    renderer.flush({4.0F, 2.0F}, {0.0F, 0.0F, 96.0F, 64.0F});
    return framebuffer;
}

fabgl::rendering::Framebuffer renderRaycastReferenceScene() {
    fabgl::rendering::Framebuffer framebuffer(96, 64);
    fabgl::rendering::RaycastRenderer renderer(framebuffer);
    auto map = fabgl::rendering::makeDemoRaycastMap();
    const fabgl::rendering::RaycastTexture wall{
        2, 2, {{210U, 65U, 55U, 255U}, {58U, 125U, 210U, 255U},
               {224U, 188U, 76U, 255U}, {75U, 176U, 108U, 255U}}};
    map.wallTextures = {wall};
    map.sectorLighting.assign(map.cells.size(), 205U);
    fabgl::rendering::RaycastRenderSettings settings;
    settings.internalWidth = 64;
    settings.floorAndCeiling = true;
    settings.distanceFog = true;
    settings.minimap = true;
    settings.weaponOverlay = true;
    settings.floorTexture = &wall;
    settings.ceilingTexture = &wall;
    const std::vector<fabgl::rendering::Billboard> sprites = {
        {{3.5F, 3.5F}, {245U, 90U, 75U, 230U}, 0.3F, nullptr,
         fabgl::rendering::Billboard::Kind::Enemy},
        {{4.5F, 2.5F}, {85U, 235U, 130U, 210U}, 0.2F, nullptr,
         fabgl::rendering::Billboard::Kind::Item}};
    const auto stats = renderer.render(map, {{2.5F, 2.5F}, {1.0F, 0.35F}, 66.0F, 3.0F},
                                       sprites, settings);
    FGL_CHECK(stats.rays == 64U && stats.floorCeilingPixels > 0U);
    return framebuffer;
}

void checkGoldenImage(const std::string_view name,
                      const fabgl::rendering::Framebuffer& framebuffer) {
    constexpr std::uint32_t PerChannelTolerance = 3U;
    constexpr double MaximumMismatchRatio = 0.01;
    const auto goldenDirectory =
        std::filesystem::path(FGL_TEST_REPOSITORY_ROOT) / "tests" / "golden";
    const auto goldenPath = goldenDirectory / (std::string(name) + ".ppm");
    const auto* update = std::getenv("FGL_UPDATE_RENDER_GOLDENS");
    if (update != nullptr && std::string_view(update) == "1") {
        std::error_code error;
        static_cast<void>(std::filesystem::create_directories(goldenDirectory, error));
        FGL_CHECK(!error);
        std::string saveError;
        FGL_CHECK(framebuffer.savePpm(goldenPath.string(), saveError));
        std::cout << "[GOLDEN UPDATE] " << name << " path=" << goldenPath.string() << '\n';
    }
    const auto expected = readPpm(goldenPath);
    const auto difference = compareImages(framebuffer, expected, PerChannelTolerance);
    std::cout << "[GOLDEN DIFF] " << name << " max_rgb=" << difference.maximumRed << ','
              << difference.maximumGreen << ',' << difference.maximumBlue
              << " mismatched=" << difference.mismatchedPixels << '/' << framebuffer.pixels().size()
              << " ratio=" << difference.mismatchRatio
              << " limit=" << MaximumMismatchRatio << '\n';
    FGL_CHECK(difference.mismatchRatio <= MaximumMismatchRatio);
}

} // namespace

FGL_TEST(framebuffer_clips_and_blends_deterministically) {
    fabgl::rendering::Framebuffer framebuffer(4, 3);
    framebuffer.clear({0, 0, 0, 255});
    framebuffer.fillRect(-2, 1, 5, 3, {100, 50, 25, 255});
    framebuffer.blendPixel(0, 1, {200, 100, 50, 128});
    framebuffer.drawLine(0, 0, 3, 2, {255, 255, 255, 255});
    FGL_CHECK(framebuffer.pixel(-1, 0) == fabgl::Color{});
    FGL_CHECK(framebuffer.pixel(3, 2) == (fabgl::Color{255, 255, 255, 255}));
    FGL_CHECK(framebuffer.checksum() == 1580809287497516863ULL);
}

FGL_TEST(renderer_2d_material_resolves_indexed_transparency_and_tint) {
    fabgl::rendering::Framebuffer framebuffer(2, 1);
    framebuffer.clear({10U, 20U, 30U, 255U});
    fabgl::rendering::Sprite sprite;
    sprite.width = 2;
    sprite.height = 1;
    sprite.indices = {0U, 1U};

    fabgl::Material material;
    material.palette = {{255U, 0U, 0U, 255U}, {200U, 100U, 50U, 255U}};
    material.transparentIndex = 0U;
    material.tint = {128U, 255U, 255U, 255U};
    material.participatesInFog = false;

    fabgl::rendering::Renderer2D renderer(framebuffer);
    renderer.draw({&sprite, 0, 0, 1, false, false, {255U, 255U, 255U, 255U}, &material});
    FGL_CHECK(framebuffer.pixel(0, 0) == (fabgl::Color{10U, 20U, 30U, 255U}));
    FGL_CHECK(framebuffer.pixel(1, 0) == (fabgl::Color{100U, 100U, 50U, 255U}));
}

FGL_TEST(renderer_2d_material_applies_bilinear_flat_blend_and_ordered_dither_pixels) {
    fabgl::rendering::Sprite gradient;
    gradient.width = 2;
    gradient.height = 1;
    gradient.pixels = {{255U, 0U, 0U, 255U}, {0U, 0U, 255U, 255U}};
    fabgl::Material bilinear;
    bilinear.sampling = fabgl::MaterialSamplingMode::Bilinear;
    bilinear.participatesInFog = false;
    fabgl::rendering::Framebuffer scaled(4, 1);
    scaled.clear({0U, 0U, 0U, 255U});
    fabgl::rendering::Renderer2D scaledRenderer(scaled);
    scaledRenderer.draw({&gradient, 0, 0, 2, false, false, {255U, 255U, 255U, 255U}, &bilinear});
    FGL_CHECK(scaled.pixel(0, 0) == (fabgl::Color{255U, 0U, 0U, 255U}));
    FGL_CHECK(scaled.pixel(1, 0) == (fabgl::Color{191U, 0U, 64U, 255U}));
    FGL_CHECK(scaled.pixel(2, 0) == (fabgl::Color{64U, 0U, 191U, 255U}));
    FGL_CHECK(scaled.pixel(3, 0) == (fabgl::Color{0U, 0U, 255U, 255U}));

    fabgl::rendering::Sprite one;
    one.width = 1;
    one.height = 1;
    one.pixels = {{255U, 255U, 255U, 255U}};
    fabgl::Material dithered;
    dithered.colorMode = fabgl::MaterialColorMode::Flat;
    dithered.flatColor = {100U, 100U, 100U, 128U};
    dithered.dither = fabgl::MaterialDitherMode::Ordered2x2;
    dithered.blend = fabgl::MaterialBlendMode::Alpha;
    dithered.participatesInFog = false;
    fabgl::rendering::Framebuffer coverage(2, 2);
    coverage.clear({0U, 0U, 0U, 255U});
    fabgl::rendering::Renderer2D coverageRenderer(coverage);
    coverageRenderer.draw({&one, 0, 0, 2, false, false, {255U, 255U, 255U, 255U}, &dithered});
    FGL_CHECK(coverage.pixel(0, 0) == (fabgl::Color{96U, 96U, 96U, 255U}));
    FGL_CHECK(coverage.pixel(1, 0) == (fabgl::Color{0U, 0U, 0U, 255U}));
    FGL_CHECK(coverage.pixel(0, 1) == (fabgl::Color{0U, 0U, 0U, 255U}));
    FGL_CHECK(coverage.pixel(1, 1) == (fabgl::Color{98U, 98U, 98U, 255U}));
}

FGL_TEST(renderer_2d_layers_camera_parallax_overlay_rotation_and_animation_are_bounded) {
    fabgl::rendering::Sprite red{1, 1, {{255U, 0U, 0U, 255U}}, {}};
    fabgl::rendering::Sprite blue{1, 1, {{0U, 0U, 255U, 255U}}, {}};
    fabgl::rendering::Sprite green{1, 1, {{0U, 255U, 0U, 255U}}, {}};
    fabgl::rendering::Framebuffer framebuffer(8, 8);
    framebuffer.clear({0U, 0U, 0U, 255U});
    fabgl::rendering::Renderer2D renderer(framebuffer, 4U);
    FGL_CHECK(renderer.submit({{&blue, 4, 3}, 0, 0, {0.5F, 1.0F}, false}));
    FGL_CHECK(renderer.submit({{&red, 2, 3}, 1, 0, {0.0F, 1.0F}, false}));
    FGL_CHECK(renderer.submit({{&green, 2, 3}, -99, -99, {1.0F, 1.0F}, true}));
    FGL_CHECK(renderer.submit({{&red, 100, 100}, 0, 0, {1.0F, 1.0F}, false}));
    FGL_CHECK(!renderer.submit({{&red, 0, 0}, 0, 0, {1.0F, 1.0F}, false}));
    renderer.flush({4.0F, 0.0F}, {0.0F, 0.0F, 8.0F, 8.0F});
    FGL_CHECK(framebuffer.pixel(2, 3) == (fabgl::Color{0U, 255U, 0U, 255U}));
    FGL_CHECK(renderer.spritesCulled() == 1U);
    FGL_CHECK(renderer.queuedSprites() == 0U);

    fabgl::rendering::Sprite atlas{2, 1, {{220U, 20U, 20U, 255U}, {20U, 220U, 20U, 255U}}, {}};
    fabgl::rendering::SpriteAnimationClip clip{
        &atlas, {{{0.0F, 0.0F, 1.0F, 1.0F}, 0.1F}, {{1.0F, 0.0F, 1.0F, 1.0F}, 0.1F}}, false};
    fabgl::rendering::SpriteAnimator animator;
    animator.update(clip, 0.11F);
    FGL_CHECK(animator.frameIndex() == 1U);
    auto animated = animator.draw(clip, 4, 4);
    animated.rotationDegrees = 90.0F;
    renderer.draw(animated);
    FGL_CHECK(renderer.drawCalls() >= 4U);
    animator.update(clip, 0.2F);
    FGL_CHECK(animator.finished());
}

FGL_TEST(tilemap_layers_objects_collision_animation_chunks_and_culling_are_real) {
    const fabgl::rendering::Sprite empty{1, 1, {{0U, 0U, 0U, 0U}}, {}};
    const fabgl::rendering::Sprite solid{1, 1, {{240U, 80U, 40U, 255U}}, {}};
    fabgl::rendering::Tilemap map;
    map.width = 4;
    map.height = 2;
    map.tileSize = 1;
    map.chunkSize = 2;
    map.tiles = {empty, solid};
    map.layers = {
        {{1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U},
         fabgl::rendering::TilemapLayerKind::Tiles,
         {1.0F, 1.0F},
         255U,
         true},
        {{0U, 0U, 1U, 0U, 0U, 0U, 0U, 0U},
         fabgl::rendering::TilemapLayerKind::Collision,
         {1.0F, 1.0F},
         255U,
         true},
        {{0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U},
         fabgl::rendering::TilemapLayerKind::Objects,
         {1.0F, 1.0F},
         255U,
         true},
    };
    map.objects = {{7U, 2U, 9U, {1.0F, 0.0F, 1.0F, 1.0F}}};
    map.animations = {{1U, {1U, 0U}, 0.1F}};
    FGL_CHECK(map.valid());
    FGL_CHECK(map.collides(2, 0));
    FGL_CHECK(!map.collides(1, 0));
    FGL_CHECK(map.tileAt(0U, 0, 0, 0.15F) == 0U);
    const auto objects = map.objectsIn({0.0F, 0.0F, 3.0F, 1.0F}, 1U);
    FGL_CHECK(objects.size() == 1U && objects[0]->id == 7U);

    fabgl::rendering::Framebuffer framebuffer(2, 2);
    fabgl::rendering::Renderer2D renderer(framebuffer);
    const auto stats =
        renderer.drawTilemapDetailed(map, {0.0F, 0.0F}, {0.0F, 0.0F, 2.0F, 2.0F}, 0.0F);
    FGL_CHECK(stats.layers == 1U && stats.chunks == 1U && stats.tiles == 4U);
    FGL_CHECK(stats.culledTiles == 4U);
    const auto offscreen =
        renderer.drawTilemapDetailed(map, {100.0F, 100.0F}, {0.0F, 0.0F, 2.0F, 2.0F});
    FGL_CHECK(offscreen.tiles == 0U && offscreen.culledTiles == 8U);
}

FGL_TEST(raycast_renderer_uses_textures_floor_ceiling_lighting_fog_doors_sprites_and_overlays) {
    fabgl::rendering::RaycastTexture wall{2,
                                          2,
                                          {{220U, 40U, 40U, 255U},
                                           {40U, 220U, 40U, 255U},
                                           {40U, 40U, 220U, 255U},
                                           {220U, 220U, 40U, 255U}}};
    fabgl::rendering::RaycastTexture sprite{2,
                                            2,
                                            {{255U, 255U, 255U, 128U},
                                             {255U, 255U, 255U, 128U},
                                             {255U, 255U, 255U, 128U},
                                             {255U, 255U, 255U, 128U}}};
    fabgl::rendering::RaycastMap map;
    map.width = 5;
    map.height = 5;
    map.cells = {1U, 1U, 1U, 1U, 1U, 1U, 0U, 0U, 0U, 1U, 1U, 0U, 1U,
                 0U, 1U, 1U, 0U, 0U, 0U, 1U, 1U, 1U, 1U, 1U, 1U};
    map.wallPalette = {{120U, 120U, 120U, 255U}, {220U, 80U, 80U, 255U}};
    map.wallTextures = {wall, wall};
    map.sectorLighting.assign(25U, 180U);
    map.doors = {{2, 2, 0.0F, true}};
    FGL_CHECK(map.valid());

    fabgl::rendering::Framebuffer framebuffer(80, 50);
    fabgl::rendering::RaycastRenderer renderer(framebuffer);
    fabgl::rendering::RaycastCamera camera;
    camera.position = {2.5F, 3.5F};
    camera.direction = {0.0F, -1.0F};
    camera.pitch = 4.0F;
    fabgl::rendering::RaycastRenderSettings settings;
    settings.internalWidth = 40;
    settings.floorAndCeiling = true;
    settings.distanceFog = true;
    settings.minimap = true;
    settings.weaponOverlay = true;
    settings.fixedPointCoordinates = true;
    settings.floorTexture = &wall;
    settings.ceilingTexture = &wall;
    settings.weaponTexture = &sprite;
    const std::vector<fabgl::rendering::Billboard> billboards = {
        {{2.2F, 3.25F},
         {255U, 40U, 40U, 180U},
         0.2F,
         &sprite,
         fabgl::rendering::Billboard::Kind::Enemy},
        {{2.8F, 3.25F},
         {40U, 255U, 40U, 255U},
         0.2F,
         nullptr,
         fabgl::rendering::Billboard::Kind::Item}};
    const auto stats = renderer.render(map, camera, billboards, settings);
    FGL_CHECK(stats.rays == 40U && stats.lookupEntries == 40U && stats.fixedPointPath);
    FGL_CHECK(stats.texturedWallColumns > 0U && stats.floorCeilingPixels > 0U);
    FGL_CHECK(stats.doorsHit > 0U && stats.secretWallsHit > 0U);
    FGL_CHECK(stats.billboards == 2U && stats.enemies == 1U && stats.items == 1U);
    FGL_CHECK(stats.transparentSpritePixels > 0U);

    map.doors[0].openness = 1.0F;
    const auto openStats = renderer.render(map, camera, {}, settings);
    FGL_CHECK(openStats.doorsHit == 0U && openStats.lookupEntries == 40U);
}

FGL_TEST(lowpoly_material_controls_sidedness_vertex_color_emissive_and_fog) {
    fabgl::rendering::LowPolyMesh mesh;
    mesh.vertices = {{{-1.0F, -1.0F, 0.0F}, {255U, 0U, 0U, 255U}},
                     {{1.0F, -1.0F, 0.0F}, {0U, 255U, 0U, 255U}},
                     {{0.0F, 1.0F, 0.0F}, {0U, 0U, 255U, 255U}}};
    fabgl::Material material;
    material.colorMode = fabgl::MaterialColorMode::Vertex;
    material.lighting = fabgl::MaterialLightingMode::Unlit;
    material.doubleSided = true;
    material.emissive = {255U, 0U, 0U, 255U};
    material.emissiveStrength = 255U;
    mesh.triangles = {{0U, 1U, 2U, {255U, 255U, 255U, 255U}, &material}};

    fabgl::rendering::Framebuffer framebuffer(32, 32);
    framebuffer.clear({0U, 0U, 0U, 255U});
    fabgl::rendering::LowPolyCamera camera;
    camera.fogStart = 0.0F;
    camera.fogEnd = 8.0F;
    fabgl::rendering::LowPolyRenderer renderer(framebuffer);
    const auto stats = renderer.render(mesh, fabgl::Mat4::identity(), camera);
    FGL_CHECK(stats.submitted == 1U);
    FGL_CHECK(stats.culled == 0U);
    FGL_CHECK(stats.drawn == 1U);
    FGL_CHECK(framebuffer.pixel(16, 16) == (fabgl::Color{128U, 43U, 43U, 255U}));
}

FGL_TEST(lowpoly_nearest_texture_sampling_supports_small_atlas_uv_regions) {
    fabgl::rendering::LowPolyMesh mesh;
    mesh.vertices = {{{-1.0F, -1.0F, 0.0F}, {255U, 255U, 255U, 255U}, {0.5F, 0.0F}},
                     {{1.0F, -1.0F, 0.0F}, {255U, 255U, 255U, 255U}, {1.0F, 0.0F}},
                     {{0.0F, 1.0F, 0.0F}, {255U, 255U, 255U, 255U}, {0.75F, 1.0F}}};
    fabgl::Material material;
    material.baseTexture = fabgl::AssetGuid::fromStableName("lowpoly.atlas");
    material.colorMode = fabgl::MaterialColorMode::Texture;
    material.sampling = fabgl::MaterialSamplingMode::Nearest;
    material.lighting = fabgl::MaterialLightingMode::Unlit;
    material.doubleSided = true;
    mesh.triangles = {{0U, 1U, 2U, {255U, 255U, 255U, 255U}}};
    const std::vector<fabgl::Color> atlas = {
        {220U, 30U, 20U, 255U}, {220U, 30U, 20U, 255U},
        {20U, 210U, 70U, 255U}, {20U, 210U, 70U, 255U},
        {220U, 30U, 20U, 255U}, {220U, 30U, 20U, 255U},
        {20U, 210U, 70U, 255U}, {20U, 210U, 70U, 255U},
    };
    const fabgl::rendering::LowPolyMaterialBinding binding{
        &material, {4, 2, atlas.data(), atlas.size()}};
    fabgl::rendering::Framebuffer framebuffer(32, 32);
    framebuffer.clear({0U, 0U, 0U, 255U});
    fabgl::rendering::LowPolyRenderer renderer(framebuffer);
    const auto stats = renderer.render(mesh, fabgl::Mat4::identity(),
                                       fabgl::rendering::LowPolyCamera{}, {}, {}, binding);
    FGL_CHECK(stats.drawn == 1U && stats.texturedTriangles == 1U &&
              stats.texturedPixels > 0U);
    FGL_CHECK(framebuffer.pixel(16, 16) == (fabgl::Color{20U, 210U, 70U, 255U}));

    fabgl::rendering::LowPolyRenderSettings lowQuality;
    lowQuality.quality = fabgl::rendering::LowPolyQuality::Low;
    framebuffer.clear({0U, 0U, 0U, 255U});
    const auto flatFallback = renderer.render(mesh, fabgl::Mat4::identity(),
                                              fabgl::rendering::LowPolyCamera{}, lowQuality, {},
                                              binding);
    FGL_CHECK(flatFallback.drawn == 1U && flatFallback.texturedTriangles == 0U);
}

FGL_TEST(lowpoly_camera_rotation_far_culling_orthographic_billboards_and_quality_are_bounded) {
    fabgl::rendering::LowPolyMesh mesh;
    mesh.vertices = {{{4.0F, -1.0F, -1.0F}}, {{4.0F, 1.0F, -1.0F}}, {{4.0F, 0.0F, 1.0F}}};
    mesh.triangles = {{0U, 1U, 2U, {180U, 120U, 60U, 255U}}};
    fabgl::rendering::Framebuffer framebuffer(64, 48);
    fabgl::rendering::LowPolyRenderer renderer(framebuffer);
    fabgl::rendering::LowPolyCamera camera;
    camera.position = {};
    camera.yawRadians = 1.5707963267948966F;
    camera.farPlane = 10.0F;
    fabgl::rendering::LowPolyRenderSettings settings;
    settings.backfaceCulling = false;
    const std::vector<fabgl::rendering::LowPolyBillboard> billboards = {
        {{3.0F, 0.0F, 0.0F}, {0.5F, 1.0F}, {40U, 220U, 80U, 180U}}};
    const auto perspective =
        renderer.render(mesh, fabgl::Mat4::identity(), camera, settings, billboards);
    FGL_CHECK(perspective.drawn == 2U && perspective.billboards == 1U);

    camera.farPlane = 3.5F;
    const auto farCulled = renderer.render(mesh, fabgl::Mat4::identity(), camera, settings);
    FGL_CHECK(farCulled.drawn == 0U && farCulled.culled == 1U);
    camera.farPlane = 10.0F;
    settings.projection = fabgl::rendering::LowPolyProjection::Orthographic;
    settings.quality = fabgl::rendering::LowPolyQuality::Low;
    settings.maximumTriangles = 0U;
    const auto bounded = renderer.render(mesh, fabgl::Mat4::identity(), camera, settings);
    FGL_CHECK(bounded.capacityRejected == 1U && bounded.submitted == 0U);
}

FGL_TEST(racer_track_v1_round_trips_all_authoring_records_canonically) {
    const auto sourceTrack = completeRacerTrack();
    const auto encoded = fabgl::rendering::serializeRacerTrack(sourceTrack);
    FGL_CHECK(encoded);
    FGL_CHECK(encoded.value().rfind("fgltrack 1\n", 0U) == 0U);
    FGL_CHECK(encoded.value().find("checkpoint 4 \"Tunnel\"\ncheckpoint 0 \"Start/Finish\"") !=
              std::string::npos);
    FGL_CHECK(encoded.value().find("roadside 2 ") < encoded.value().find("roadside 9 "));
    FGL_CHECK(encoded.value().find("opponent 3 ") < encoded.value().find("opponent 7 "));

    const auto decoded = fabgl::rendering::deserializeRacerTrack(encoded.value());
    FGL_CHECK(decoded);
    FGL_CHECK(decoded.value().guid == sourceTrack.guid);
    FGL_CHECK(decoded.value().name == sourceTrack.name);
    FGL_CHECK(decoded.value().segments.size() == 8U);
    FGL_CHECK(decoded.value().checkpoints.size() == 2U);
    FGL_CHECK(decoded.value().checkpoints[0].segment == 4U);
    FGL_CHECK(decoded.value().roadsideObjects[0].id == 2U);
    FGL_CHECK(decoded.value().backgroundLayers[0].sprite == sourceTrack.backgroundLayers[0].sprite);
    FGL_CHECK(decoded.value().opponentSpawns[0].id == 3U);
    const auto canonical = fabgl::rendering::serializeRacerTrack(decoded.value());
    FGL_CHECK(canonical && canonical.value() == encoded.value());
}

FGL_TEST(racer_example_contains_a_canonical_loadable_track_asset) {
    const std::string path =
        std::string(FGL_TEST_REPOSITORY_ROOT) + "/examples/pseudo3d_racer/Tracks/Main.fgltrack";
    const auto source = fabgl::assets::readTextFile(path);
    FGL_CHECK(source);
    const auto decoded = fabgl::rendering::deserializeRacerTrack(source.value());
    FGL_CHECK(decoded);
    FGL_CHECK(decoded.value().segments.size() == 16U);
    FGL_CHECK(decoded.value().checkpoints.size() == 4U);
    FGL_CHECK(decoded.value().roadsideObjects.size() == 4U);
    FGL_CHECK(decoded.value().backgroundLayers.size() == 2U);
    FGL_CHECK(decoded.value().opponentSpawns.size() == 3U);
    const auto canonical = fabgl::rendering::serializeRacerTrack(decoded.value());
    FGL_CHECK(canonical && canonical.value() == source.value());
}

FGL_TEST(racer_track_v1_rejects_corruption_bounds_duplicates_and_non_finite_data) {
    auto track = completeRacerTrack();
    track.roadsideObjects[1].id = track.roadsideObjects[0].id;
    FGL_CHECK(!fabgl::rendering::validateRacerTrack(track));

    track = completeRacerTrack();
    track.segments[0].curve = std::numeric_limits<float>::quiet_NaN();
    FGL_CHECK(!fabgl::rendering::serializeRacerTrack(track));

    const auto encoded = fabgl::rendering::serializeRacerTrack(completeRacerTrack());
    FGL_CHECK(encoded);
    FGL_CHECK(!fabgl::rendering::deserializeRacerTrack(encoded.value() + "unexpected\n"));
    auto future = encoded.value();
    future.replace(0U, std::string("fgltrack 1").size(), "fgltrack 99");
    FGL_CHECK(!fabgl::rendering::deserializeRacerTrack(future));
    fabgl::rendering::RacerTrackFormatLimits limits;
    limits.maximumSegments = 4U;
    FGL_CHECK(!fabgl::rendering::deserializeRacerTrack(encoded.value(), limits));
}

FGL_TEST(racer_renderer_consumes_track_scenery_opponents_layers_weather_and_segment_length) {
    auto track = completeRacerTrack();
    fabgl::rendering::Framebuffer framebuffer(160, 90);
    fabgl::rendering::RacerRenderer renderer(framebuffer);
    fabgl::rendering::RacerCamera camera;
    camera.distance = 0.0F;
    const auto sprite = std::make_shared<fabgl::rendering::Sprite>(
        fabgl::rendering::makeCheckerSprite(4, 4, {255U, 255U, 255U, 255U},
                                            {90U, 140U, 230U, 220U}));
    const fabgl::rendering::RacerSpriteResolver resolver =
        [sprite](fabgl::AssetGuid) { return sprite; };
    const auto stats = renderer.render(track, camera, resolver);
    FGL_CHECK(stats.scanlines == 60U);
    FGL_CHECK(stats.segmentsSampled == 60U);
    FGL_CHECK(stats.roadsideObjectsDrawn == 2U);
    FGL_CHECK(stats.backgroundLayersDrawn == 1U);
    FGL_CHECK(stats.opponentsDrawn == 2U);
    FGL_CHECK(stats.weatherPixelsBlended == 160U * 90U);
    FGL_CHECK(stats.resolvedSpriteAssets == 5U && stats.missingSpriteAssets == 0U);
    const auto firstChecksum = framebuffer.checksum();

    camera.distance = track.segmentLength * 3.0F;
    const auto movedStats = renderer.render(track, camera, resolver);
    FGL_CHECK(movedStats.roadsideObjectsDrawn == 2U);
    FGL_CHECK(framebuffer.checksum() != firstChecksum);
}

FGL_TEST(mode7_ground_plane_rotates_scales_fogs_and_honors_internal_resolution) {
    fabgl::rendering::Mode7Texture texture;
    texture.width = 2;
    texture.height = 2;
    texture.pixels = {{240U, 40U, 40U, 255U},
                      {40U, 240U, 40U, 255U},
                      {40U, 40U, 240U, 255U},
                      {240U, 240U, 40U, 255U}};
    fabgl::rendering::Framebuffer framebuffer(40, 30);
    fabgl::rendering::RacerRenderer renderer(framebuffer);
    fabgl::rendering::Mode7Settings settings;
    settings.horizon = 10;
    settings.internalWidth = 20;
    settings.fogStart = 2.0F;
    settings.fogEnd = 8.0F;
    const auto first = renderer.renderMode7(texture, {}, settings);
    FGL_CHECK(first.scanlines == 20U && first.samples == 400U && first.foggedSamples > 0U);
    const auto checksum = framebuffer.checksum();
    fabgl::rendering::Mode7Camera camera;
    camera.position = {0.5F, 0.25F};
    camera.headingRadians = 0.7F;
    camera.altitude = 1.5F;
    const auto moved = renderer.renderMode7(texture, camera, settings);
    FGL_CHECK(moved.samples == first.samples && framebuffer.checksum() != checksum);
}

FGL_TEST(reference_2d_and_raycast_ppm_images_match_tolerant_goldens) {
    checkGoldenImage("renderer2d-reference", renderTwoDimensionalReferenceScene());
    checkGoldenImage("raycast-reference", renderRaycastReferenceScene());
}

FGL_TEST(reference_renderers_match_golden_checksums) {
    const std::array<std::pair<fabgl::player::DemoKind, std::uint64_t>, 10> references = {{
        {fabgl::player::DemoKind::Empty, 3684892159837637615ULL},
        {fabgl::player::DemoKind::Platformer2D, 10026797063304176869ULL},
        {fabgl::player::DemoKind::TopDown, 8335374742846203431ULL},
        {fabgl::player::DemoKind::RaycastFps, 17882889585750137818ULL},
        {fabgl::player::DemoKind::Racer, 10544174321288367818ULL},
        {fabgl::player::DemoKind::LowPolyExperimental, 12883852193710168326ULL},
        {fabgl::player::DemoKind::UiShowcase, 6744144588786156117ULL},
        {fabgl::player::DemoKind::AudioShowcase, 4061273253253862341ULL},
        {fabgl::player::DemoKind::AnimationShowcase, 13752941473616129720ULL},
        {fabgl::player::DemoKind::AssetStreaming, 15684697036675318470ULL},
    }};
    for (const auto& reference : references) {
        FGL_CHECK(renderDemo(reference.first) == reference.second);
    }
}
