#include "test_harness.h"

#include <fabgl/assets/asset_database.h>
#include <fabgl/assets/asset_pack.h>
#include <fabgl/assets/audio_importer.h>
#include <fabgl/assets/file_io.h>
#include <fabgl/assets/image_pipeline.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

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
    const auto cost = fabgl::assets::estimateCost(processed.value(), encoded);
    FGL_CHECK(cost.decodedBytes == 32U);
}

FGL_TEST(wav_pipeline_downmixes_resamples_and_encodes) {
    fabgl::assets::AudioImportSettings settings;
    settings.targetSampleRate = 16000U;
    settings.trimSilence = false;
    auto clip = fabgl::assets::importWav(makeWav(), settings);
    FGL_CHECK(clip);
    FGL_CHECK(clip.value().valid());
    FGL_CHECK(clip.value().samples.size() == 160U);
    const auto encoded = fabgl::assets::encodeAudioClip(clip.value());
    FGL_CHECK(encoded.size() == 24U + 320U);
    FGL_CHECK(encoded[0] == 'F' && encoded[3] == 'A');
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
