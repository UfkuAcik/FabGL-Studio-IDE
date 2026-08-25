#include "test_harness.h"

#include "fabgl/serialization/material_serializer.h"

#include <string>

using namespace fabgl;

FGL_TEST(material_file_round_trips_canonically_with_palette_and_references) {
    Material material;
    material.baseTexture = AssetGuid::fromStableName("material.texture");
    material.paletteAsset = AssetGuid::fromStableName("material.palette");
    material.palette = {{1U, 2U, 3U, 255U}, {4U, 5U, 6U, 128U}};
    material.transparentIndex = 7U;
    material.tint = {220U, 210U, 200U, 190U};
    material.flatColor = {10U, 20U, 30U, 255U};
    material.emissive = {40U, 50U, 60U, 255U};
    material.emissiveStrength = 80U;
    material.colorMode = MaterialColorMode::Vertex;
    material.dither = MaterialDitherMode::Ordered4x4;
    material.sampling = MaterialSamplingMode::Bilinear;
    material.lighting = MaterialLightingMode::Vertex;
    material.blend = MaterialBlendMode::Alpha;
    material.participatesInFog = false;
    material.billboard = true;
    material.doubleSided = true;
    material.compatibleRenderers =
        RendererCompatibility::Renderer2D | RendererCompatibility::LowPoly;
    MaterialAsset asset{AssetGuid::fromStableName("material.file"), "Hero Material", material};

    auto encoded = MaterialSerializer::serialize(asset);
    FGL_CHECK(encoded);
    auto decoded = MaterialSerializer::deserialize(encoded.value());
    FGL_CHECK(decoded);
    FGL_CHECK(decoded.value().id == asset.id);
    FGL_CHECK(decoded.value().name == asset.name);
    FGL_CHECK(decoded.value().material.baseTexture == material.baseTexture);
    FGL_CHECK(decoded.value().material.palette == material.palette);
    FGL_CHECK(decoded.value().material.transparentIndex == material.transparentIndex);
    FGL_CHECK(decoded.value().material.tint == material.tint);
    FGL_CHECK(decoded.value().material.colorMode == material.colorMode);
    FGL_CHECK(decoded.value().material.dither == material.dither);
    FGL_CHECK(decoded.value().material.sampling == material.sampling);
    FGL_CHECK(decoded.value().material.lighting == material.lighting);
    FGL_CHECK(decoded.value().material.blend == material.blend);
    FGL_CHECK(decoded.value().material.compatibleRenderers == material.compatibleRenderers);
    auto canonical = MaterialSerializer::serialize(decoded.value());
    FGL_CHECK(canonical && canonical.value() == encoded.value());
}

FGL_TEST(material_file_rejects_bad_versions_masks_ranges_and_trailing_data) {
    const auto id = AssetGuid::fromStableName("material.invalid");
    const std::string prefix = "fglmaterial 1\nasset_guid " + id.toString() +
                               "\nname \"Invalid\"\nbase_texture nil\npalette_asset nil\n";
    const std::string suffix =
        "\ntint 255 255 255 255\nflat_color 255 255 255 255\n"
        "emissive 0 0 0 255\nemissive_strength 0\ncolor_mode texture\ndither none\n"
        "sampling nearest\nlighting unlit\nblend opaque\nfog 1\nbillboard 0\n"
        "double_sided 0\ncompatible_renderers 15\npalette_count 0\nmaterial_end\n";
    auto badIndex = MaterialSerializer::deserialize(prefix + "transparent_index 256" + suffix);
    FGL_CHECK(!badIndex && badIndex.error().code() == ErrorCode::InvalidFormat);

    auto badMask = MaterialSerializer::deserialize(
        prefix + "transparent_index nil" + suffix.substr(0, suffix.find("compatible_renderers")) +
        "compatible_renderers 32\npalette_count 0\nmaterial_end\n");
    FGL_CHECK(!badMask && badMask.error().code() == ErrorCode::InvalidFormat);

    MaterialAsset valid{id, "Valid", {}};
    auto encoded = MaterialSerializer::serialize(valid);
    FGL_CHECK(encoded);
    auto trailing = MaterialSerializer::deserialize(encoded.value() + "unexpected\n");
    FGL_CHECK(!trailing && trailing.error().code() == ErrorCode::InvalidFormat);

    auto unsupported = encoded.value();
    unsupported.replace(0U, std::string("fglmaterial 1").size(), "fglmaterial 9");
    auto version = MaterialSerializer::deserialize(unsupported);
    FGL_CHECK(!version && version.error().code() == ErrorCode::UnsupportedVersion);
}
