#include "test_harness.h"

#include <fabgl/assets/asset_database.h>
#include <fabgl/assets/asset_importer.h>
#include <fabgl/assets/asset_pack.h>
#include <fabgl/assets/audio_importer.h>
#include <fabgl/assets/file_io.h>
#include <fabgl/assets/font_importer.h>
#include <fabgl/assets/image_pipeline.h>
#include <fabgl/assets/mesh_importer.h>
#include <fabgl/assets/tilemap_importer.h>

#include <project_format.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

namespace test_filesystem = std::filesystem;

struct TemporaryDirectory final {
    test_filesystem::path path;

    ~TemporaryDirectory() {
        std::error_code ignored;
        test_filesystem::remove_all(path, ignored);
    }
};

void appendU16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    const auto wide = static_cast<std::uint32_t>(value);
    output.push_back(static_cast<std::uint8_t>(wide & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((wide >> 8U) & 0xFFU));
}

void appendU32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    for (unsigned int shift = 0; shift < 32U; shift += 8U) {
        output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

std::vector<std::uint8_t> makeWav() {
    constexpr std::uint32_t sampleRate = 8000;
    constexpr std::uint32_t frames = 80;
    std::vector<std::uint8_t> output;
    output.insert(output.end(), {'R', 'I', 'F', 'F'});
    appendU32(output, 36U + frames * 4U);
    output.insert(output.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    appendU32(output, 16U);
    appendU16(output, 1U);
    appendU16(output, 2U);
    appendU32(output, sampleRate);
    appendU32(output, sampleRate * 4U);
    appendU16(output, 4U);
    appendU16(output, 16U);
    output.insert(output.end(), {'d', 'a', 't', 'a'});
    appendU32(output, frames * 4U);
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        const auto sample =
            static_cast<std::int16_t>(std::sin(static_cast<float>(frame) * 0.21F) * 12000.0F);
        appendU16(output, static_cast<std::uint16_t>(sample));
        appendU16(output, static_cast<std::uint16_t>(sample / 2));
    }
    return output;
}

} // namespace

FGL_TEST(file_io_normalizes_dot_segments_for_absolute_paths) {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory temporary{
        test_filesystem::temp_directory_path() /
        (std::string("fabgl-studio-file-io-tests-") + std::to_string(suffix))};
    const auto nested = temporary.path / "nested";
    std::error_code directoryError;
    FGL_CHECK(test_filesystem::create_directories(nested, directoryError));
    FGL_CHECK(!directoryError);

    const std::vector<std::uint8_t> expected{'F', 'G', 'L', 'P'};
    const auto canonical = (nested / "payload.bin").string();
    FGL_CHECK(fabgl::assets::writeBinaryFileAtomic(canonical, expected));

    const auto dotted =
        (temporary.path / "." / "nested" / ".." / "nested" / "payload.bin").string();
    const auto loaded = fabgl::assets::readBinaryFile(dotted);
    FGL_CHECK(loaded);
    FGL_CHECK(loaded.value() == expected);
}

namespace {

class TestImporter final : public fabgl::assets::IAssetImporter {
  public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return "tests.copy";
    }
    [[nodiscard]] std::uint32_t version() const noexcept override {
        return 3U;
    }
    [[nodiscard]] fabgl::assets::AssetKind kind() const noexcept override {
        return fabgl::assets::AssetKind::Json;
    }
    [[nodiscard]] std::vector<std::string> extensions() const override {
        return {".json", "data"};
    }
    [[nodiscard]] fabgl::Result<fabgl::assets::ImportedAsset>
    import(const fabgl::assets::AssetImportRequest& request) const override {
        fabgl::assets::ImportedAsset result;
        result.payload = request.sourceBytes;
        result.flashBytes = result.payload.size();
        return fabgl::Result<fabgl::assets::ImportedAsset>::success(std::move(result));
    }
};

} // namespace

FGL_TEST(image_pipeline_quantizes_dithers_and_encodes) {
    fabgl::assets::Image image;
    image.width = 16;
    image.height = 8;
    image.pixels.resize(128U);
    for (auto y = 0; y < image.height; ++y) {
        for (auto x = 0; x < image.width; ++x) {
            image.pixels[static_cast<std::size_t>(y * image.width + x)] = {
                static_cast<std::uint8_t>(x * 16), static_cast<std::uint8_t>(y * 30),
                static_cast<std::uint8_t>((x + y) * 10),
                static_cast<std::uint8_t>(x == 0 ? 0 : 255)};
        }
    }
    fabgl::assets::ImageImportSettings settings;
    settings.targetWidth = 8;
    settings.targetHeight = 4;
    settings.paletteSize = 8;
    settings.dither = true;
    auto processed = fabgl::assets::processImage(image, settings);
    FGL_CHECK(processed);
    FGL_CHECK(processed.value().valid());
    FGL_CHECK(processed.value().palette.size() <= 8U);
    FGL_CHECK(processed.value().transparentIndex == 0U);
    const auto encoded = fabgl::assets::encodeIndexedImage(processed.value());
    FGL_CHECK(encoded.size() > 20U);
    FGL_CHECK(encoded[0] == 'F' && encoded[1] == 'G' && encoded[2] == 'L' && encoded[3] == 'I');
    const auto decoded = fabgl::assets::decodeIndexedImage(encoded);
    FGL_CHECK(decoded);
    FGL_CHECK(decoded.value().indices == processed.value().indices);
    FGL_CHECK(fabgl::assets::encodeIndexedImage(decoded.value()) == encoded);
    const auto cost = fabgl::assets::estimateCost(processed.value(), encoded);
    FGL_CHECK(cost.decodedBytes == 32U);
}

FGL_TEST(image_thumbnail_preserves_aspect_ratio_and_rejects_noncanonical_rle) {
    fabgl::assets::Image source;
    source.width = 320;
    source.height = 160;
    source.pixels.resize(static_cast<std::size_t>(source.width * source.height));
    for (auto y = 0; y < source.height; ++y) {
        for (auto x = 0; x < source.width; ++x) {
            source.pixels[static_cast<std::size_t>(y * source.width + x)] = {
                static_cast<std::uint8_t>(x % 256), static_cast<std::uint8_t>(y % 256),
                static_cast<std::uint8_t>((x + y) % 256), 255U};
        }
    }
    fabgl::assets::ThumbnailSettings settings;
    settings.maximumWidth = 64;
    settings.maximumHeight = 64;
    settings.paletteSize = 16;
    const auto thumbnail = fabgl::assets::createThumbnail(source, settings);
    FGL_CHECK(thumbnail && thumbnail.value().width == 64 && thumbnail.value().height == 32);
    const auto encoded = fabgl::assets::encodeIndexedImage(thumbnail.value());
    const auto inspected = fabgl::assets::decodeIndexedImage(encoded);
    FGL_CHECK(inspected && inspected.value().indices == thumbnail.value().indices);
    FGL_CHECK(fabgl::assets::encodeIndexedImage(inspected.value()) == encoded);

    auto corrupt = encoded;
    const auto firstRun = 20U + thumbnail.value().palette.size() * 4U;
    corrupt[firstRun] = 0U;
    FGL_CHECK(!fabgl::assets::decodeIndexedImage(corrupt));
    corrupt = encoded;
    corrupt.push_back(1U);
    FGL_CHECK(!fabgl::assets::decodeIndexedImage(corrupt));
    settings.maximumWidth = 0;
    FGL_CHECK(!fabgl::assets::createThumbnail(source, settings));
}

FGL_TEST(tilemap_csv_json_encode_identically_and_inspect_strictly) {
    constexpr auto csv = "# six cells\n1, 2, 255\n256,65536,4\n";
    constexpr auto json = "{\"tiles\":[1,2,255,256,65536,4],\"height\":2,\"width\":3}";
    const auto csvMap = fabgl::assets::importCsvTilemap(csv);
    const auto jsonMap = fabgl::assets::importJsonTilemap(json);
    FGL_CHECK(csvMap && jsonMap);
    FGL_CHECK(csvMap.value().tiles == jsonMap.value().tiles);
    const auto csvBytes = fabgl::assets::encodeTilemap(csvMap.value());
    const auto jsonBytes = fabgl::assets::encodeTilemap(jsonMap.value());
    FGL_CHECK(csvBytes && jsonBytes && csvBytes.value() == jsonBytes.value());
    FGL_CHECK(csvBytes.value()[4] == 2U);
    const auto inspected = fabgl::assets::inspectTilemap(csvBytes.value());
    FGL_CHECK(inspected && inspected.value().width == 3U && inspected.value().height == 2U);
    const auto reencoded = fabgl::assets::encodeTilemap(inspected.value());
    FGL_CHECK(reencoded && reencoded.value() == csvBytes.value());

    FGL_CHECK(!fabgl::assets::importCsvTilemap("1,2\n3\n"));
    FGL_CHECK(!fabgl::assets::importJsonTilemap("{\"width\":2,\"height\":1,\"tiles\":[1]}"));
    FGL_CHECK(!fabgl::assets::importJsonTilemap("{\"width\":1,\"height\":1,\"tiles\":[[1]]}"));
    fabgl::assets::TilemapLimits tiny;
    tiny.maximumCells = 2U;
    FGL_CHECK(!fabgl::assets::importCsvTilemap("1,2,3\n", tiny));
    auto corrupt = csvBytes.value();
    corrupt[4] = 3U;
    FGL_CHECK(!fabgl::assets::inspectTilemap(corrupt));
    corrupt = csvBytes.value();
    corrupt.pop_back();
    FGL_CHECK(!fabgl::assets::inspectTilemap(corrupt));
    fabgl::assets::TilemapLimits byteLimit;
    byteLimit.maximumEncodedBytes = csvBytes.value().size() - 1U;
    FGL_CHECK(!fabgl::assets::inspectTilemap(csvBytes.value(), byteLimit));
}

FGL_TEST(tilemap_v2_preserves_layers_objects_chunks_animations_and_tileset_references) {
    fabgl::assets::Tilemap tilemap;
    tilemap.guid = fabgl::AssetGuid::fromStableName("tilemap.v2");
    tilemap.width = 3U;
    tilemap.height = 2U;
    tilemap.tileWidth = 8U;
    tilemap.tileHeight = 8U;

    fabgl::assets::TilemapLayer ground;
    ground.name = "Ground";
    ground.cells = {1U, 2U, 0U, 3U, 4U, 1U};
    ground.parallaxX = 0.75F;
    ground.opacity = 220U;
    fabgl::assets::TilemapLayer collision;
    collision.name = "Collision";
    collision.kind = fabgl::assets::TilemapLayerKind::Collision;
    collision.cells = {0U, 2U, 0U, 0U, 0U, 1U};
    fabgl::assets::TilemapLayer objects;
    objects.name = "Objects";
    objects.kind = fabgl::assets::TilemapLayerKind::Objects;
    objects.cells.resize(6U);
    tilemap.layers = {ground, collision, objects};
    tilemap.tiles = ground.cells;

    fabgl::assets::TilemapTilesetReference reference;
    reference.tileset = fabgl::AssetGuid::fromStableName("tileset.v1");
    reference.firstTile = 1U;
    reference.tileCount = 4U;
    tilemap.tilesets.push_back(reference);
    fabgl::assets::TilemapObject object;
    object.id = 7U;
    object.layer = 2U;
    object.type = "Spawn";
    object.bounds = {1.0F, 0.0F, 1.0F, 1.0F};
    object.asset = fabgl::AssetGuid::fromStableName("spawn.prefab");
    tilemap.objects.push_back(object);
    fabgl::assets::TilemapChunk chunk;
    chunk.layer = 0U;
    chunk.x = 0U;
    chunk.y = 0U;
    chunk.width = 2U;
    chunk.height = 1U;
    chunk.cells = {1U, 2U};
    tilemap.chunks.push_back(chunk);
    fabgl::assets::TileAnimation animation;
    animation.outputTile = 1U;
    animation.frames = {{2U, 80U}, {3U, 120U}};
    tilemap.animations.push_back(animation);

    const auto encoded = fabgl::assets::encodeTilemap(tilemap);
    FGL_CHECK(encoded);
    const auto decoded = fabgl::assets::inspectTilemap(encoded.value());
    FGL_CHECK(decoded);
    FGL_CHECK(decoded.value().guid == tilemap.guid);
    FGL_CHECK(decoded.value().layers.size() == 3U);
    FGL_CHECK(decoded.value().layers[1U].kind == fabgl::assets::TilemapLayerKind::Collision);
    FGL_CHECK(decoded.value().objects.size() == 1U &&
              decoded.value().objects.front().asset == object.asset);
    FGL_CHECK(decoded.value().chunks.size() == 1U &&
              decoded.value().chunks.front().cells == chunk.cells);
    FGL_CHECK(decoded.value().animations.front().frames[1U].durationMilliseconds == 120U);
    FGL_CHECK(decoded.value().tilesets.front().tileset == reference.tileset);
    const auto reencoded = fabgl::assets::encodeTilemap(decoded.value());
    FGL_CHECK(reencoded && reencoded.value() == encoded.value());

    auto corrupt = encoded.value();
    corrupt[38U] = 1U;
    FGL_CHECK(!fabgl::assets::inspectTilemap(corrupt));

    const std::vector<std::uint8_t> legacy = {'F', 'G', 'L', 'T', 1U, 0U, 0U, 0U, 2U, 0U, 1U,
                                              0U,  2U,  0U,  0U,  0U, 1U, 0U, 0U, 0U, 7U, 8U};
    const auto migrated = fabgl::assets::inspectTilemap(legacy);
    FGL_CHECK(migrated && migrated.value().tiles == std::vector<std::uint32_t>({7U, 8U}));
    fabgl::assets::TilemapLimits legacyByteLimit;
    legacyByteLimit.maximumEncodedBytes = legacy.size() - 1U;
    FGL_CHECK(!fabgl::assets::inspectTilemap(legacy, legacyByteLimit));
    const auto migratedBytes = fabgl::assets::encodeTilemap(migrated.value());
    FGL_CHECK(migratedBytes && migratedBytes.value()[4U] == 2U);
}

FGL_TEST(tileset_v1_round_trips_guid_metadata_and_collision_table_strictly) {
    fabgl::assets::Tileset tileset;
    tileset.guid = fabgl::AssetGuid::fromStableName("tileset.asset");
    tileset.name = "Dungeon";
    tileset.sourceImage = fabgl::AssetGuid::fromStableName("tileset.image");
    tileset.tileWidth = 8U;
    tileset.tileHeight = 8U;
    tileset.margin = 1U;
    tileset.spacing = 2U;
    tileset.tileCount = 12U;
    tileset.columns = 4U;
    tileset.collisionTiles = {1U, 5U, 9U};
    const auto encoded = fabgl::assets::encodeTileset(tileset);
    FGL_CHECK(encoded);
    const auto decoded = fabgl::assets::inspectTileset(encoded.value());
    FGL_CHECK(decoded && decoded.value().guid == tileset.guid &&
              decoded.value().sourceImage == tileset.sourceImage &&
              decoded.value().collisionTiles == tileset.collisionTiles);
    const auto reencoded = fabgl::assets::encodeTileset(decoded.value());
    FGL_CHECK(reencoded && reencoded.value() == encoded.value());

    auto corrupt = encoded.value();
    corrupt.push_back(0U);
    FGL_CHECK(!fabgl::assets::inspectTileset(corrupt));
    corrupt = encoded.value();
    corrupt[4U] = 2U;
    const auto unsupported = fabgl::assets::inspectTileset(corrupt);
    FGL_CHECK(!unsupported && unsupported.error().code() == fabgl::ErrorCode::UnsupportedVersion);
    tileset.collisionTiles = {5U, 1U};
    FGL_CHECK(!fabgl::assets::encodeTileset(tileset));
    tileset.collisionTiles = {1U};
    tileset.name = std::string("Bad\xC2\x80", 5U);
    FGL_CHECK(!fabgl::assets::encodeTileset(tileset));
}

FGL_TEST(wavefront_obj_triangulates_bounds_and_rejects_invalid_geometry) {
    constexpr auto source = "# quad with referenced UVs and normal\n"
                            "v -1 0 0\n"
                            "v 1 0 0\n"
                            "v 1 2 0\n"
                            "v -1 2 0\n"
                            "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
                            "vn 0 0 1\n"
                            "f -4/1/1 -3/2/1 -2/3/1 -1/4/1\n";
    const auto mesh = fabgl::assets::importWavefrontObj(source);
    FGL_CHECK(mesh && mesh.value().positions.size() == 4U && mesh.value().indices.size() == 6U);
    FGL_CHECK(mesh.value().textureCoordinates.size() == 4U);
    FGL_CHECK(mesh.value().textureCoordinates[0].u == 0.0F &&
              mesh.value().textureCoordinates[0].v == 0.0F);
    FGL_CHECK(mesh.value().textureCoordinates[2].u == 1.0F &&
              mesh.value().textureCoordinates[2].v == 1.0F);
    FGL_CHECK(mesh.value().boundsMinimum.x == -1.0F && mesh.value().boundsMinimum.y == 0.0F);
    FGL_CHECK(mesh.value().boundsMaximum.x == 1.0F && mesh.value().boundsMaximum.y == 2.0F);
    const auto encoded = fabgl::assets::encodeLowPolyMesh(mesh.value());
    FGL_CHECK(encoded && encoded.value().size() == 40U + 48U + 32U + 12U);
    const auto inspected = fabgl::assets::inspectLowPolyMesh(encoded.value());
    FGL_CHECK(inspected && inspected.value().indices == mesh.value().indices);
    FGL_CHECK(inspected.value().textureCoordinates.size() == 4U);
    const auto reencoded = fabgl::assets::encodeLowPolyMesh(inspected.value());
    FGL_CHECK(reencoded && reencoded.value() == encoded.value());

    FGL_CHECK(!fabgl::assets::importWavefrontObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 4\n"));
    FGL_CHECK(!fabgl::assets::importWavefrontObj("v 0 0 0\nv 1 0 0\nv 2 0 0\nf 1 2 3\n"));
    FGL_CHECK(!fabgl::assets::importWavefrontObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nl 1 2\nf 1 2 3\n"));
    fabgl::assets::LowPolyMeshLimits oneTriangle;
    oneTriangle.maximumTriangles = 1U;
    FGL_CHECK(!fabgl::assets::importWavefrontObj(source, oneTriangle));
    auto corrupt = encoded.value();
    corrupt[corrupt.size() - 2U] = 0xFFU;
    corrupt[corrupt.size() - 1U] = 0xFFU;
    FGL_CHECK(!fabgl::assets::inspectLowPolyMesh(corrupt));
    corrupt = encoded.value();
    corrupt.pop_back();
    FGL_CHECK(!fabgl::assets::inspectLowPolyMesh(corrupt));

    constexpr auto splitUvSource = "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
                                   "vt 0 0\nvt 1 0\nvt 0 1\nvt 1 1\n"
                                   "f 1/1 2/2 3/3\nf 1/4 3/3 2/2\n";
    const auto split = fabgl::assets::importWavefrontObj(splitUvSource);
    FGL_CHECK(split && split.value().positions.size() == 4U &&
              split.value().textureCoordinates.size() == 4U);
}

FGL_TEST(bdf_font_builds_sorted_atlas_and_inspects_metrics_strictly) {
    const std::string source = "STARTFONT 2.1\n"
                               "FONT demo\n"
                               "SIZE 8 75 75\n"
                               "FONTBOUNDINGBOX 8 8 0 -1\n"
                               "STARTPROPERTIES 2\nFONT_ASCENT 7\nFONT_DESCENT 1\nENDPROPERTIES\n"
                               "CHARS 2\n"
                               "STARTCHAR B\nENCODING 66\nSWIDTH 500 0\nDWIDTH 8 0\nBBX 8 8 0 -1\n"
                               "BITMAP\n7C\n42\n42\n7C\n42\n42\n7C\n00\nENDCHAR\n"
                               "STARTCHAR A\nENCODING 65\nSWIDTH 500 0\nDWIDTH 8 0\nBBX 8 8 0 -1\n"
                               "BITMAP\n18\n24\n42\n7E\n42\n42\n42\n00\nENDCHAR\n"
                               "ENDFONT\n";
    const auto font = fabgl::assets::importBdfFont(source);
    FGL_CHECK(font && font.value().glyphs.size() == 2U);
    FGL_CHECK(font.value().glyphs[0].codepoint == 65U && font.value().glyphs[1].codepoint == 66U);
    FGL_CHECK(font.value().ascent == 7 && font.value().descent == 1);
    const auto encoded = fabgl::assets::encodeBitmapFont(font.value());
    FGL_CHECK(encoded && encoded.value()[0] == 'F' && encoded.value()[3] == 'F');
    const auto inspected = fabgl::assets::inspectBitmapFont(encoded.value());
    FGL_CHECK(inspected && inspected.value().glyphs[0].advance == 8);
    const auto reencoded = fabgl::assets::encodeBitmapFont(inspected.value());
    FGL_CHECK(reencoded && reencoded.value() == encoded.value());

    auto duplicate = source;
    const auto encoding = duplicate.find("ENCODING 66");
    FGL_CHECK(encoding != std::string::npos);
    duplicate.replace(encoding, std::string("ENCODING 66").size(), "ENCODING 65");
    FGL_CHECK(!fabgl::assets::importBdfFont(duplicate));
    auto malformed = source;
    const auto bitmapRow = malformed.find("7C\n42");
    FGL_CHECK(bitmapRow != std::string::npos);
    malformed.replace(bitmapRow, 2U, "GG");
    FGL_CHECK(!fabgl::assets::importBdfFont(malformed));
    auto corrupt = encoded.value();
    corrupt[24U + 18U] = 1U;
    FGL_CHECK(!fabgl::assets::inspectBitmapFont(corrupt));
    corrupt = encoded.value();
    corrupt.pop_back();
    FGL_CHECK(!fabgl::assets::inspectBitmapFont(corrupt));
}

FGL_TEST(concrete_asset_importers_use_registry_contract_and_versioned_payloads) {
    fabgl::assets::AssetImporterRegistry registry;
    FGL_CHECK(registry.add(std::make_unique<fabgl::assets::CsvTilemapImporter>()));
    FGL_CHECK(registry.add(std::make_unique<fabgl::assets::JsonTilemapImporter>()));
    FGL_CHECK(registry.add(std::make_unique<fabgl::assets::WavefrontObjImporter>()));
    FGL_CHECK(registry.add(std::make_unique<fabgl::assets::BdfFontImporter>()));
    FGL_CHECK(registry.size() == 4U);

    fabgl::assets::AssetImportRequest request;
    request.guid = fabgl::AssetGuid::fromStableName("asset:tilemap-importer");
    request.relativePath = "Maps/level.csv";
    request.sourceBytes = {'1', ',', '2', '\n', '3', ',', '4', '\n'};
    const auto imported = registry.import(request);
    FGL_CHECK(imported && imported.value().kind == fabgl::assets::AssetKind::Tilemap);
    FGL_CHECK(imported.value().payload[0] == 'F' && imported.value().payload[3] == 'T');
    FGL_CHECK(imported.value().cacheKey != 0U);
}

FGL_TEST(image_pipeline_crops_slices_and_builds_bounded_atlas_metadata) {
    fabgl::assets::Image image;
    image.width = 8;
    image.height = 4;
    image.pixels.resize(32U);
    for (auto y = 0; y < image.height; ++y) {
        for (auto x = 0; x < image.width; ++x) {
            image.pixels[static_cast<std::size_t>(y * image.width + x)] = {
                static_cast<std::uint8_t>(x * 20), static_cast<std::uint8_t>(y * 40), 10U, 255U};
        }
    }

    const auto cropped = fabgl::assets::cropImage(image, {2, 1, 3, 2});
    FGL_CHECK(cropped && cropped.value().width == 3 && cropped.value().height == 2);
    FGL_CHECK(cropped.value().pixels.front().r == 40U);
    FGL_CHECK(cropped.value().pixels.front().g == 40U);
    FGL_CHECK(!fabgl::assets::cropImage(image, {-1, 0, 1, 1}));

    const auto frames = fabgl::assets::sliceImageGrid(image, 2, 2);
    FGL_CHECK(frames && frames.value().size() == 8U);
    FGL_CHECK(frames.value().front().width == 2 && frames.value().front().height == 2);

    std::vector<fabgl::assets::AtlasSprite> sprites;
    sprites.push_back({"hero", frames.value()[0], 0.5F, 1.0F});
    sprites.push_back({"enemy", frames.value()[1], 0.5F, 0.5F});
    sprites.push_back({"coin", frames.value()[2], 0.5F, 0.5F});
    const auto atlas = fabgl::assets::buildSpriteAtlas(sprites, 8, 1, true);
    FGL_CHECK(atlas);
    FGL_CHECK(atlas.value().image.valid());
    FGL_CHECK(atlas.value().regions.size() == 3U);
    FGL_CHECK((atlas.value().image.width & (atlas.value().image.width - 1)) == 0);
    FGL_CHECK((atlas.value().image.height & (atlas.value().image.height - 1)) == 0);
    FGL_CHECK(atlas.value().regions[0].name == "hero");
    fabgl::assets::ImageImportSettings atlasSettings;
    atlasSettings.paletteSize = 16;
    const auto encodedAtlas = fabgl::assets::encodeSpriteAtlas(atlas.value(), atlasSettings);
    FGL_CHECK(encodedAtlas && encodedAtlas.value().size() > 16U);
    FGL_CHECK(encodedAtlas.value()[0] == 'F' && encodedAtlas.value()[1] == 'G' &&
              encodedAtlas.value()[2] == 'L' && encodedAtlas.value()[3] == 'S');
    sprites.push_back(sprites.front());
    FGL_CHECK(!fabgl::assets::buildSpriteAtlas(sprites, 8, 1, false));
}

FGL_TEST(canonical_project_image_settings_round_trip_advanced_pipeline_fields_fail_closed) {
    constexpr auto Settings =
        R"json({"targetWidth":0,"targetHeight":0,"paletteSize":32,"alphaThreshold":96,"dither":true,"reserveTransparentIndex":true,"crop":{"x":1,"y":2,"width":8,"height":4},"slice":{"mode":"grid","frameWidth":2,"frameHeight":2,"margin":0,"spacing":0},"atlas":{"enabled":true,"maxWidth":64,"padding":2,"powerOfTwo":false},"pivot":{"x":0.25,"y":0.75},"pixelsPerUnit":16,"compression":"rle","residency":"stream"})json";
    auto settings = fabgl::project::decodeProjectImageImportSettings(Settings);
    FGL_CHECK(settings);
    FGL_CHECK(settings.value().cropEnabled && settings.value().crop.x == 1 &&
              settings.value().crop.height == 4);
    FGL_CHECK(settings.value().sliceMode == fabgl::assets::ImageSliceMode::Grid);
    FGL_CHECK(settings.value().outputKind == fabgl::assets::ImageOutputKind::SpriteAtlas);
    FGL_CHECK(settings.value().atlasMaximumWidth == 64 && settings.value().atlasPadding == 2 &&
              !settings.value().atlasPowerOfTwo);
    FGL_CHECK_NEAR(settings.value().pivotX, 0.25F, 0.0001F);
    FGL_CHECK_NEAR(settings.value().pixelsPerUnit, 16.0F, 0.0001F);
    FGL_CHECK(settings.value().residency == fabgl::assets::ImageResidency::Stream);

    fabgl::project::Manifest manifest;
    manifest.projectGuid = fabgl::AssetGuid::fromStableName("advanced-image-project").toString();
    manifest.name = "Advanced Image";
    fabgl::project::ProjectAssetEntry atlas(
        fabgl::AssetGuid::fromStableName("advanced-image-atlas"), "Assets/Frames.png",
        "sprite.atlas");
    atlas.importSettings = Settings;
    atlas.hasImportMetadata = true;
    manifest.assets.push_back(std::move(atlas));
    auto encoded = fabgl::project::serializeManifest(manifest);
    FGL_CHECK(encoded);
    auto decoded = fabgl::project::parseManifest(encoded.value());
    FGL_CHECK(decoded && decoded.value().assets.size() == 1U);
    auto reencoded = fabgl::project::serializeManifest(decoded.value());
    FGL_CHECK(reencoded && reencoded.value() == encoded.value());

    FGL_CHECK(!fabgl::project::decodeProjectImageImportSettings(
        R"json({"crop":{"x":0,"y":0,"width":1,"height":1,"unknown":1}})json"));
    FGL_CHECK(!fabgl::project::decodeProjectImageImportSettings(
        R"json({"slice":{"mode":"grid","frameWidth":2,"frameHeight":2}})json"));
    FGL_CHECK(!fabgl::project::decodeProjectImageImportSettings(
        R"json({"targetWidth":8,"atlas":{"enabled":true}})json"));
    FGL_CHECK(
        !fabgl::project::decodeProjectImageImportSettings(R"json({"compression":"raw"})json"));

    manifest.assets.front().type = "image";
    FGL_CHECK(!fabgl::project::serializeManifest(manifest));
}

FGL_TEST(canonical_image_compiler_applies_crop_grid_atlas_and_reports_frame_cost) {
    fabgl::assets::Image image;
    image.width = 8;
    image.height = 4;
    image.pixels.resize(32U);
    for (auto y = 0; y < image.height; ++y) {
        for (auto x = 0; x < image.width; ++x) {
            image.pixels[static_cast<std::size_t>(y * image.width + x)] = {
                static_cast<std::uint8_t>(x * 20), static_cast<std::uint8_t>(y * 50), 30U, 255U};
        }
    }

    fabgl::assets::ImageImportSettings imageSettings;
    imageSettings.cropEnabled = true;
    imageSettings.crop = {2, 1, 4, 2};
    imageSettings.targetWidth = 2;
    imageSettings.targetHeight = 2;
    imageSettings.paletteSize = 8;
    auto compiledImage = fabgl::assets::compileImageAsset(image, imageSettings);
    FGL_CHECK(compiledImage && !compiledImage.value().spriteAtlas);
    FGL_CHECK(compiledImage.value().preview.width == 2 &&
              compiledImage.value().preview.height == 2);
    FGL_CHECK(compiledImage.value().payload[0] == 'F' && compiledImage.value().payload[3] == 'I');
    FGL_CHECK(compiledImage.value().cost.estimatedPixelsPerFrame == 4U);

    fabgl::assets::ImageImportSettings atlasSettings;
    atlasSettings.sliceMode = fabgl::assets::ImageSliceMode::Grid;
    atlasSettings.frameWidth = 2;
    atlasSettings.frameHeight = 2;
    atlasSettings.outputKind = fabgl::assets::ImageOutputKind::SpriteAtlas;
    atlasSettings.atlasMaximumWidth = 8;
    atlasSettings.atlasPadding = 1;
    atlasSettings.atlasPowerOfTwo = true;
    atlasSettings.pivotX = 0.25F;
    atlasSettings.pivotY = 1.0F;
    auto compiledAtlas = fabgl::assets::compileImageAsset(image, atlasSettings);
    FGL_CHECK(compiledAtlas && compiledAtlas.value().spriteAtlas);
    FGL_CHECK(compiledAtlas.value().frameCount == 8U);
    FGL_CHECK(compiledAtlas.value().payload[0] == 'F' && compiledAtlas.value().payload[3] == 'S');
    FGL_CHECK(compiledAtlas.value().cost.estimatedPixelsPerFrame == 4U);

    atlasSettings.outputKind = fabgl::assets::ImageOutputKind::Image;
    FGL_CHECK(!fabgl::assets::compileImageAsset(image, atlasSettings));
    atlasSettings = {};
    atlasSettings.crop = {0, 0, 1, 1};
    FGL_CHECK(!fabgl::assets::compileImageAsset(image, atlasSettings));
}

FGL_TEST(wav_pipeline_downmixes_resamples_and_encodes) {
    fabgl::assets::AudioImportSettings settings;
    settings.targetSampleRate = 16000U;
    settings.trimSilence = false;
    settings.streaming = true;
    auto clip = fabgl::assets::importWav(makeWav(), settings);
    FGL_CHECK(clip);
    FGL_CHECK(clip.value().valid());
    FGL_CHECK(clip.value().samples.size() == 160U);
    const auto encoded = fabgl::assets::encodeAudioClip(clip.value());
    FGL_CHECK(encoded.size() == 24U + 320U);
    FGL_CHECK(encoded[0] == 'F' && encoded[3] == 'A');

    const auto decoded = fabgl::assets::decodeAudioClip(encoded);
    FGL_CHECK(decoded && decoded.value().samples == clip.value().samples);
    const auto compressed =
        fabgl::assets::encodeAudioClip(clip.value(), fabgl::assets::AudioEncoding::Delta8);
    FGL_CHECK(compressed.size() < encoded.size());
    const auto inspected = fabgl::assets::inspectAudioClip(compressed);
    FGL_CHECK(inspected);
    FGL_CHECK(inspected.value().streaming);
    FGL_CHECK(inspected.value().sampleCount == clip.value().samples.size());
    FGL_CHECK(inspected.value().encoding == fabgl::assets::AudioEncoding::Delta8);
    const auto decompressed = fabgl::assets::decodeAudioClip(compressed);
    FGL_CHECK(decompressed && decompressed.value().samples.size() == clip.value().samples.size());
    auto maximumError = 0;
    for (std::size_t index = 0; index < clip.value().samples.size(); ++index) {
        maximumError =
            std::max(maximumError, std::abs(static_cast<int>(clip.value().samples[index]) -
                                            static_cast<int>(decompressed.value().samples[index])));
    }
    FGL_CHECK(maximumError <= 128);
    std::array<std::int16_t, 40U> window{};
    const auto windowFrames = fabgl::assets::decodeAudioClipFrames(
        compressed, inspected.value(), 120U, window.data(), window.size());
    FGL_CHECK(windowFrames == window.size());
    for (std::size_t index = 0U; index < window.size(); ++index) {
        FGL_CHECK(window[index] == decompressed.value().samples[120U + index]);
    }
    auto mismatchedInfo = inspected.value();
    ++mismatchedInfo.sampleRate;
    FGL_CHECK(fabgl::assets::decodeAudioClipFrames(compressed, mismatchedInfo, 0U, window.data(),
                                                   window.size()) == 0U);
    auto corrupt = compressed;
    corrupt.pop_back();
    FGL_CHECK(!fabgl::assets::inspectAudioClip(corrupt));
    FGL_CHECK(!fabgl::assets::decodeAudioClip(corrupt));
}

FGL_TEST(asset_pack_is_sorted_checked_and_deterministic) {
    const auto first = fabgl::AssetGuid::fromStableName("asset:first");
    const auto second = fabgl::AssetGuid::fromStableName("asset:second");
    std::vector<fabgl::assets::PackInput> inputs = {
        {second, 2U, fabgl::assets::StorageClass::Sd, {7U, 8U, 9U}},
        {first, 1U, fabgl::assets::StorageClass::Flash, {1U, 2U, 3U, 4U}}};
    auto built = fabgl::assets::buildPack(inputs);
    auto repeated = fabgl::assets::buildPack(inputs);
    FGL_CHECK(built && repeated);
    FGL_CHECK(built.value().bytes == repeated.value().bytes);
    FGL_CHECK(built.value().index[0].guid < built.value().index[1].guid);
    auto inspected = fabgl::assets::inspectPack(built.value().bytes);
    FGL_CHECK(inspected);
    FGL_CHECK(inspected.value().index.size() == 2U);
    auto corrupt = built.value().bytes;
    corrupt.back() ^= 1U;
    FGL_CHECK(!fabgl::assets::inspectPack(corrupt));
}

FGL_TEST(asset_database_preserves_guids_and_orders_dependencies) {
    fabgl::assets::AssetDatabase database;
    const auto texture = fabgl::AssetGuid::fromStableName("asset:texture");
    const auto scene = fabgl::AssetGuid::fromStableName("asset:scene");
    FGL_CHECK(database.add({texture, "Assets/Hero.PNG", "image", 1U, {}}));
    FGL_CHECK(database.add({scene, "Scenes/Main.fglscene", "scene", 2U, {texture}}));
    FGL_CHECK(database.findByPath("assets\\hero.png") != nullptr);
    FGL_CHECK(database.move(texture, "Assets/Characters/Hero.png"));
    FGL_CHECK(database.find(texture)->relativePath == "assets/characters/hero.png");
    const auto order = database.buildOrder();
    FGL_CHECK(order);
    FGL_CHECK(order.value().size() == 2U);
    FGL_CHECK(order.value()[0] == texture);
    FGL_CHECK(!fabgl::assets::isSafeRelativePath("../secret"));
    FGL_CHECK(!fabgl::assets::isSafeRelativePath("C:\\outside"));
    FGL_CHECK(!fabgl::assets::isSafeRelativePath("Assets/file.txt:payload"));
    FGL_CHECK(!fabgl::assets::isSafeRelativePath("Assets/CON.png"));
    FGL_CHECK(!fabgl::assets::isSafeRelativePath("Assets/trailing. "));
    FGL_CHECK(fabgl::assets::isSafeRelativePath("Assets/Türkçe Oyun/hero.png"));
}

FGL_TEST(asset_database_detects_missing_dirty_and_untracked_sources_without_timestamp_state) {
    fabgl::assets::AssetDatabase database;
    const auto texture = fabgl::AssetGuid::fromStableName("asset:sync-texture");
    const auto scene = fabgl::AssetGuid::fromStableName("asset:sync-scene");
    FGL_CHECK(database.add({texture, "Assets/Hero.png", "image", 11U, {}}));
    FGL_CHECK(database.add({scene, "Scenes/Main.fglscene", "scene", 22U, {texture}}));
    FGL_CHECK(database.setImportConfiguration(texture, 2U, 101U));
    FGL_CHECK(database.setImportConfiguration(scene, 1U, 202U));
    FGL_CHECK(database.needsImport(texture));
    FGL_CHECK(database.markImported(texture, 11U, 1001U));
    FGL_CHECK(database.markImported(scene, 22U, 1002U));
    FGL_CHECK(!database.needsImport(texture));

    const auto synchronized =
        database.synchronizeSources({{"Assets/Hero.png", 12U}, {"Assets/New.json", 30U}});
    FGL_CHECK(synchronized);
    FGL_CHECK(synchronized.value().dirty == std::vector<fabgl::AssetGuid>{texture});
    FGL_CHECK(synchronized.value().missing == std::vector<fabgl::AssetGuid>{scene});
    FGL_CHECK(synchronized.value().untrackedPaths == std::vector<std::string>{"assets/new.json"});
    FGL_CHECK(database.needsImport(texture));
    FGL_CHECK(!database.needsImport(scene));
    FGL_CHECK(!database.markImported(texture, 11U, 1003U));

    FGL_CHECK(database.setImportConfiguration(texture, 3U, 101U));
    FGL_CHECK(database.markImported(texture, 12U, 1004U));
    FGL_CHECK(!database.needsImport(texture));
    FGL_CHECK(database.setImportConfiguration(texture, 3U, 102U));
    FGL_CHECK(database.needsImport(texture));
}

FGL_TEST(asset_importer_registry_validates_dispatches_and_keys_incremental_outputs) {
    fabgl::assets::AssetImporterRegistry registry;
    FGL_CHECK(registry.add(std::make_unique<TestImporter>()));
    FGL_CHECK(registry.size() == 1U);
    FGL_CHECK(registry.findById("TESTS.COPY") != nullptr);
    FGL_CHECK(registry.findForPath("Assets/Level.JSON") != nullptr);
    FGL_CHECK(!registry.add(std::make_unique<TestImporter>()));

    fabgl::assets::AssetImportRequest request;
    request.guid = fabgl::AssetGuid::fromStableName("asset:registry-test");
    request.sourcePath = "C:/project/Assets/Level.json";
    request.relativePath = "Assets/Level.json";
    request.sourceBytes = {'{', '}', '\n'};
    request.normalizedSettings = "minify=true";
    request.target = fabgl::assets::AssetTarget::Esp32Flash;

    const auto first = registry.import(request);
    const auto repeated = registry.import(request);
    FGL_CHECK(first && repeated);
    FGL_CHECK(first.value().kind == fabgl::assets::AssetKind::Json);
    FGL_CHECK(first.value().payload == request.sourceBytes);
    FGL_CHECK(first.value().cacheKey == repeated.value().cacheKey);

    request.normalizedSettings = "minify=false";
    const auto changedSettings = registry.import(request);
    FGL_CHECK(changedSettings);
    FGL_CHECK(changedSettings.value().cacheKey != first.value().cacheKey);

    request.relativePath = "../outside.json";
    FGL_CHECK(!registry.import(request));
    request.relativePath = "Assets/unknown.xyz";
    FGL_CHECK(!registry.import(request));
}
