#include "utf8_arguments.h"
#include <fabgl/assets/asset_pack.h>
#include <fabgl/assets/audio_importer.h>
#include <fabgl/assets/file_io.h>
#include <fabgl/assets/font_importer.h>
#include <fabgl/assets/image_pipeline.h>
#include <fabgl/assets/mesh_importer.h>
#include <fabgl/assets/tilemap_importer.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

[[nodiscard]] std::string lowerExtension(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos || dot + 1U == path.size() ||
        (slash != std::string::npos && dot < slash)) {
        return {};
    }
    auto extension = path.substr(dot + 1U);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension;
}

void printError(const fabgl::Error& error) {
    std::cerr << "error: " << error.message();
    for (const auto& context : error.context()) {
        std::cerr << " [" << context.key << '=' << context.value << ']';
    }
    std::cerr << '\n';
}

int compileImage(int argc, char** argv) {
    if (argc < 4) {
        throw std::invalid_argument("image requires <input> <output>");
    }
    fabgl::assets::ImageImportSettings settings;
    for (auto index = 4; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--dither") {
            settings.dither = true;
        } else if (argument == "--width" && index + 1 < argc) {
            settings.targetWidth = std::stoi(argv[++index]);
        } else if (argument == "--height" && index + 1 < argc) {
            settings.targetHeight = std::stoi(argv[++index]);
        } else if (argument == "--colors" && index + 1 < argc) {
            settings.paletteSize = std::stoi(argv[++index]);
        } else if (argument == "--alpha" && index + 1 < argc) {
            const auto value = std::stoi(argv[++index]);
            if (value < 0 || value > 255) {
                throw std::invalid_argument("--alpha must be between 0 and 255");
            }
            settings.alphaThreshold = static_cast<std::uint8_t>(value);
        } else {
            throw std::invalid_argument("unknown image option: " + argument);
        }
    }
    auto loaded = fabgl::assets::loadImage(argv[2]);
    if (!loaded) {
        printError(loaded.error());
        return EXIT_FAILURE;
    }
    auto processed = fabgl::assets::processImage(loaded.value(), settings);
    if (!processed) {
        printError(processed.error());
        return EXIT_FAILURE;
    }
    auto encoded = fabgl::assets::encodeIndexedImage(processed.value());
    if (encoded.empty()) {
        std::cerr << "error: indexed image encoding failed\n";
        return EXIT_FAILURE;
    }
    auto written = fabgl::assets::writeBinaryFileAtomic(argv[3], encoded);
    if (!written) {
        printError(written.error());
        return EXIT_FAILURE;
    }
    const auto cost = fabgl::assets::estimateCost(processed.value(), encoded);
    std::cout << "image " << processed.value().width << 'x' << processed.value().height
              << " palette=" << processed.value().palette.size() << " decoded=" << cost.decodedBytes
              << " packed=" << cost.packedBytes << '\n';
    return EXIT_SUCCESS;
}

int compileThumbnail(int argc, char** argv) {
    if (argc < 4) {
        throw std::invalid_argument("thumbnail requires <input-image> <output.fgli>");
    }
    fabgl::assets::ThumbnailSettings settings;
    for (auto index = 4; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--max-width" && index + 1 < argc) {
            settings.maximumWidth = std::stoi(argv[++index]);
        } else if (argument == "--max-height" && index + 1 < argc) {
            settings.maximumHeight = std::stoi(argv[++index]);
        } else if (argument == "--colors" && index + 1 < argc) {
            settings.paletteSize = std::stoi(argv[++index]);
        } else if (argument == "--dither") {
            settings.dither = true;
        } else {
            throw std::invalid_argument("unknown thumbnail option: " + argument);
        }
    }
    auto loaded = fabgl::assets::loadImage(argv[2]);
    if (!loaded) {
        printError(loaded.error());
        return EXIT_FAILURE;
    }
    auto thumbnail = fabgl::assets::createThumbnail(loaded.value(), settings);
    if (!thumbnail) {
        printError(thumbnail.error());
        return EXIT_FAILURE;
    }
    const auto encoded = fabgl::assets::encodeIndexedImage(thumbnail.value());
    if (encoded.empty()) {
        std::cerr << "error: thumbnail encoding failed\n";
        return EXIT_FAILURE;
    }
    auto written = fabgl::assets::writeBinaryFileAtomic(argv[3], encoded);
    if (!written) {
        printError(written.error());
        return EXIT_FAILURE;
    }
    std::cout << "thumbnail " << thumbnail.value().width << 'x' << thumbnail.value().height
              << " palette=" << thumbnail.value().palette.size() << " packed=" << encoded.size()
              << '\n';
    return EXIT_SUCCESS;
}

[[nodiscard]] std::uint32_t parseU32Option(const std::string& text, const std::string& option);
[[nodiscard]] std::uint16_t parseU16Option(const std::string& text, const std::string& option);

int compileTilemap(int argc, char** argv) {
    if (argc < 4) {
        throw std::invalid_argument("tilemap requires <input.csv|input.json> <output.fgltilemap>");
    }
    const auto extension = lowerExtension(argv[2]);
    if (extension != "csv" && extension != "json") {
        throw std::invalid_argument("tilemap supports only .csv and flat .json sources");
    }
    if (lowerExtension(argv[3]) != "fgltilemap")
        throw std::invalid_argument("tilemap output must use the canonical .fgltilemap extension");
    auto source = fabgl::assets::readTextFile(argv[2]);
    if (!source) {
        printError(source.error());
        return EXIT_FAILURE;
    }
    fabgl::Result<fabgl::assets::Tilemap> tilemap =
        extension == "csv" ? fabgl::assets::importCsvTilemap(source.value())
                           : fabgl::assets::importJsonTilemap(source.value());
    if (!tilemap) {
        printError(tilemap.error());
        return EXIT_FAILURE;
    }
    for (auto index = 4; index < argc; ++index) {
        const std::string argument = argv[index];
        const auto requireValue = [&](const std::string& option) -> std::string {
            if (index + 1 >= argc)
                throw std::invalid_argument(option + " requires a value");
            return argv[++index];
        };
        if (argument == "--guid") {
            auto parsed = fabgl::AssetGuid::parse(requireValue(argument));
            if (!parsed)
                throw std::invalid_argument("--guid is not a canonical GUID");
            tilemap.value().guid = parsed.value();
        } else if (argument == "--tile-width") {
            tilemap.value().tileWidth = parseU16Option(requireValue(argument), argument);
        } else if (argument == "--tile-height") {
            tilemap.value().tileHeight = parseU16Option(requireValue(argument), argument);
        } else if (argument == "--tileset") {
            const auto value = requireValue(argument);
            const auto firstColon = value.find(':');
            const auto secondColon = firstColon == std::string::npos
                                         ? std::string::npos
                                         : value.find(':', firstColon + 1U);
            if (firstColon == std::string::npos || secondColon == std::string::npos ||
                value.find(':', secondColon + 1U) != std::string::npos)
                throw std::invalid_argument("--tileset requires GUID:FIRST_TILE:TILE_COUNT");
            auto guid = fabgl::AssetGuid::parse(value.substr(0U, firstColon));
            if (!guid)
                throw std::invalid_argument("--tileset GUID is invalid");
            fabgl::assets::TilemapTilesetReference reference;
            reference.tileset = guid.value();
            reference.firstTile = parseU32Option(
                value.substr(firstColon + 1U, secondColon - firstColon - 1U), argument);
            reference.tileCount = parseU32Option(value.substr(secondColon + 1U), argument);
            tilemap.value().tilesets.push_back(reference);
        } else {
            throw std::invalid_argument("unknown tilemap option: " + argument);
        }
    }
    auto encoded = fabgl::assets::encodeTilemap(tilemap.value());
    if (!encoded) {
        printError(encoded.error());
        return EXIT_FAILURE;
    }
    auto written = fabgl::assets::writeBinaryFileAtomic(argv[3], encoded.value());
    if (!written) {
        printError(written.error());
        return EXIT_FAILURE;
    }
    std::cout << "tilemap " << tilemap.value().width << 'x' << tilemap.value().height
              << " packed=" << encoded.value().size() << '\n';
    return EXIT_SUCCESS;
}

[[nodiscard]] std::uint32_t parseU32Option(const std::string& text, const std::string& option) {
    if (text.empty() || text.front() == '-' || text.front() == '+')
        throw std::invalid_argument(option + " requires an unsigned integer");
    std::size_t consumed = 0U;
    const auto value = std::stoull(text, &consumed, 10);
    if (consumed != text.size() || value > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument(option + " value is out of range");
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::uint16_t parseU16Option(const std::string& text, const std::string& option) {
    const auto value = parseU32Option(text, option);
    if (value > std::numeric_limits<std::uint16_t>::max())
        throw std::invalid_argument(option + " value is out of range");
    return static_cast<std::uint16_t>(value);
}

int compileTileset(int argc, char** argv) {
    if (argc < 3)
        throw std::invalid_argument("tileset requires <output.fgltileset> and metadata options");
    if (lowerExtension(argv[2]) != "fgltileset")
        throw std::invalid_argument("tileset output must use the canonical .fgltileset extension");
    fabgl::assets::Tileset tileset;
    for (auto index = 3; index < argc; ++index) {
        const std::string argument = argv[index];
        const auto requireValue = [&](const std::string& option) -> std::string {
            if (index + 1 >= argc)
                throw std::invalid_argument(option + " requires a value");
            return argv[++index];
        };
        if (argument == "--guid") {
            auto parsed = fabgl::AssetGuid::parse(requireValue(argument));
            if (!parsed)
                throw std::invalid_argument("--guid is not a canonical GUID");
            tileset.guid = parsed.value();
        } else if (argument == "--image") {
            auto parsed = fabgl::AssetGuid::parse(requireValue(argument));
            if (!parsed)
                throw std::invalid_argument("--image is not a canonical GUID");
            tileset.sourceImage = parsed.value();
        } else if (argument == "--name") {
            tileset.name = requireValue(argument);
        } else if (argument == "--tile-width") {
            tileset.tileWidth = parseU16Option(requireValue(argument), argument);
        } else if (argument == "--tile-height") {
            tileset.tileHeight = parseU16Option(requireValue(argument), argument);
        } else if (argument == "--margin") {
            tileset.margin = parseU16Option(requireValue(argument), argument);
        } else if (argument == "--spacing") {
            tileset.spacing = parseU16Option(requireValue(argument), argument);
        } else if (argument == "--count") {
            tileset.tileCount = parseU32Option(requireValue(argument), argument);
        } else if (argument == "--columns") {
            tileset.columns = parseU32Option(requireValue(argument), argument);
        } else if (argument == "--collision") {
            const auto values = requireValue(argument);
            auto begin = std::size_t{0U};
            while (begin <= values.size()) {
                const auto comma = values.find(',', begin);
                const auto token = values.substr(
                    begin, comma == std::string::npos ? values.size() - begin : comma - begin);
                tileset.collisionTiles.push_back(parseU32Option(token, argument));
                if (comma == std::string::npos)
                    break;
                begin = comma + 1U;
            }
            std::sort(tileset.collisionTiles.begin(), tileset.collisionTiles.end());
        } else {
            throw std::invalid_argument("unknown tileset option: " + argument);
        }
    }
    auto encoded = fabgl::assets::encodeTileset(tileset);
    if (!encoded) {
        printError(encoded.error());
        return EXIT_FAILURE;
    }
    auto written = fabgl::assets::writeBinaryFileAtomic(argv[2], encoded.value());
    if (!written) {
        printError(written.error());
        return EXIT_FAILURE;
    }
    std::cout << "tileset " << tileset.name << " tiles=" << tileset.tileCount
              << " packed=" << encoded.value().size() << '\n';
    return EXIT_SUCCESS;
}

int compileMesh(int argc, char** argv) {
    if (argc != 4) {
        throw std::invalid_argument("mesh requires <input.obj> <output.fglm>");
    }
    if (lowerExtension(argv[2]) != "obj") {
        throw std::invalid_argument(
            "mesh supports only Wavefront .obj; glTF/.glb require an external converter");
    }
    auto source = fabgl::assets::readTextFile(argv[2]);
    if (!source) {
        printError(source.error());
        return EXIT_FAILURE;
    }
    auto mesh = fabgl::assets::importWavefrontObj(source.value());
    if (!mesh) {
        printError(mesh.error());
        return EXIT_FAILURE;
    }
    auto encoded = fabgl::assets::encodeLowPolyMesh(mesh.value());
    if (!encoded) {
        printError(encoded.error());
        return EXIT_FAILURE;
    }
    auto written = fabgl::assets::writeBinaryFileAtomic(argv[3], encoded.value());
    if (!written) {
        printError(written.error());
        return EXIT_FAILURE;
    }
    std::cout << "mesh vertices=" << mesh.value().positions.size()
              << " triangles=" << mesh.value().indices.size() / 3U
              << " packed=" << encoded.value().size() << '\n';
    return EXIT_SUCCESS;
}

int compileFont(int argc, char** argv) {
    if (argc < 4) {
        throw std::invalid_argument("font requires <input.bdf> <output.fglf>");
    }
    if (lowerExtension(argv[2]) != "bdf") {
        throw std::invalid_argument(
            "font supports only bitmap .bdf; TTF/OTF rasterization is not built in");
    }
    fabgl::assets::BdfImportSettings settings;
    for (auto index = 4; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--atlas-width" && index + 1 < argc) {
            const auto width = std::stoul(argv[++index]);
            if (width > 65535U) {
                throw std::invalid_argument("--atlas-width is too large");
            }
            settings.maximumAtlasWidth = static_cast<std::uint16_t>(width);
        } else if (argument == "--padding" && index + 1 < argc) {
            const auto padding = std::stoul(argv[++index]);
            if (padding > 65535U) {
                throw std::invalid_argument("--padding is too large");
            }
            settings.padding = static_cast<std::uint16_t>(padding);
        } else {
            throw std::invalid_argument("unknown font option: " + argument);
        }
    }
    auto source = fabgl::assets::readTextFile(argv[2]);
    if (!source) {
        printError(source.error());
        return EXIT_FAILURE;
    }
    auto font = fabgl::assets::importBdfFont(source.value(), settings);
    if (!font) {
        printError(font.error());
        return EXIT_FAILURE;
    }
    auto encoded = fabgl::assets::encodeBitmapFont(font.value());
    if (!encoded) {
        printError(encoded.error());
        return EXIT_FAILURE;
    }
    auto written = fabgl::assets::writeBinaryFileAtomic(argv[3], encoded.value());
    if (!written) {
        printError(written.error());
        return EXIT_FAILURE;
    }
    std::cout << "font glyphs=" << font.value().glyphs.size()
              << " atlas=" << font.value().atlasWidth << 'x' << font.value().atlasHeight
              << " packed=" << encoded.value().size() << '\n';
    return EXIT_SUCCESS;
}

int compileAudio(int argc, char** argv) {
    if (argc < 4) {
        throw std::invalid_argument("audio requires <input.wav> <output>");
    }
    fabgl::assets::AudioImportSettings settings;
    auto encoding = fabgl::assets::AudioEncoding::Pcm16;
    for (auto index = 4; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--rate" && index + 1 < argc) {
            settings.targetSampleRate = static_cast<std::uint32_t>(std::stoul(argv[++index]));
        } else if (argument == "--no-normalize") {
            settings.normalize = false;
        } else if (argument == "--no-trim") {
            settings.trimSilence = false;
        } else if (argument == "--stream") {
            settings.streaming = true;
        } else if (argument == "--compress") {
            encoding = fabgl::assets::AudioEncoding::Delta8;
        } else if (argument == "--loop-start" && index + 1 < argc) {
            settings.loopStart = static_cast<std::uint32_t>(std::stoul(argv[++index]));
        } else if (argument == "--loop-end" && index + 1 < argc) {
            settings.loopEnd = static_cast<std::uint32_t>(std::stoul(argv[++index]));
        } else {
            throw std::invalid_argument("unknown audio option: " + argument);
        }
    }
    auto bytes = fabgl::assets::readBinaryFile(argv[2]);
    if (!bytes) {
        printError(bytes.error());
        return EXIT_FAILURE;
    }
    auto clip = fabgl::assets::importWav(bytes.value(), settings);
    if (!clip) {
        printError(clip.error());
        return EXIT_FAILURE;
    }
    auto encoded = fabgl::assets::encodeAudioClip(clip.value(), encoding);
    auto written = fabgl::assets::writeBinaryFileAtomic(argv[3], encoded);
    if (!written) {
        printError(written.error());
        return EXIT_FAILURE;
    }
    std::cout << "audio samples=" << clip.value().samples.size()
              << " rate=" << clip.value().sampleRate << " packed=" << encoded.size() << '\n';
    return EXIT_SUCCESS;
}

int compileAtlas(int argc, char** argv) {
    if (argc < 4) {
        throw std::invalid_argument("atlas requires <manifest.txt> <output.fgls>");
    }
    auto maximumWidth = 1024;
    auto padding = 1;
    auto powerOfTwo = true;
    fabgl::assets::ImageImportSettings imageSettings;
    for (auto index = 4; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--max-width" && index + 1 < argc) {
            maximumWidth = std::stoi(argv[++index]);
        } else if (argument == "--padding" && index + 1 < argc) {
            padding = std::stoi(argv[++index]);
        } else if (argument == "--colors" && index + 1 < argc) {
            imageSettings.paletteSize = std::stoi(argv[++index]);
        } else if (argument == "--dither") {
            imageSettings.dither = true;
        } else if (argument == "--no-power-of-two") {
            powerOfTwo = false;
        } else {
            throw std::invalid_argument("unknown atlas option: " + argument);
        }
    }

    auto manifest = fabgl::assets::readTextFile(argv[2]);
    if (!manifest) {
        printError(manifest.error());
        return EXIT_FAILURE;
    }
    std::istringstream stream(manifest.value());
    std::vector<fabgl::assets::AtlasSprite> sprites;
    std::string line;
    auto lineNumber = std::size_t{0};
    while (std::getline(stream, line)) {
        ++lineNumber;
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos || line[first] == '#') {
            continue;
        }
        std::istringstream fields(line);
        std::string name;
        std::string path;
        auto pivotX = 0.5F;
        auto pivotY = 0.5F;
        if (!(fields >> std::quoted(name) >> std::quoted(path))) {
            throw std::invalid_argument("invalid atlas manifest line " +
                                        std::to_string(lineNumber));
        }
        if (fields >> pivotX) {
            if (!(fields >> pivotY)) {
                throw std::invalid_argument("atlas pivot requires two values on line " +
                                            std::to_string(lineNumber));
            }
        }
        fields >> std::ws;
        if (!fields.eof()) {
            throw std::invalid_argument("trailing atlas manifest fields on line " +
                                        std::to_string(lineNumber));
        }
        auto image = fabgl::assets::loadImage(path);
        if (!image) {
            printError(image.error());
            return EXIT_FAILURE;
        }
        sprites.push_back({std::move(name), std::move(image.value()), pivotX, pivotY});
    }
    auto atlas = fabgl::assets::buildSpriteAtlas(sprites, maximumWidth, padding, powerOfTwo);
    if (!atlas) {
        printError(atlas.error());
        return EXIT_FAILURE;
    }
    auto encoded = fabgl::assets::encodeSpriteAtlas(atlas.value(), imageSettings);
    if (!encoded) {
        printError(encoded.error());
        return EXIT_FAILURE;
    }
    auto written = fabgl::assets::writeBinaryFileAtomic(argv[3], encoded.value());
    if (!written) {
        printError(written.error());
        return EXIT_FAILURE;
    }
    std::cout << "atlas sprites=" << atlas.value().regions.size()
              << " size=" << atlas.value().image.width << 'x' << atlas.value().image.height
              << " packed=" << encoded.value().size() << '\n';
    return EXIT_SUCCESS;
}

fabgl::assets::StorageClass parseStorage(const std::string& value) {
    if (value == "flash")
        return fabgl::assets::StorageClass::Flash;
    if (value == "ram")
        return fabgl::assets::StorageClass::InternalRam;
    if (value == "psram")
        return fabgl::assets::StorageClass::Psram;
    if (value == "sd")
        return fabgl::assets::StorageClass::Sd;
    throw std::invalid_argument("unknown storage class: " + value);
}

int compilePack(int argc, char** argv) {
    if (argc != 4) {
        throw std::invalid_argument("pack requires <manifest.txt> <output.fglpack>");
    }
    auto manifest = fabgl::assets::readTextFile(argv[2]);
    if (!manifest) {
        printError(manifest.error());
        return EXIT_FAILURE;
    }
    std::istringstream stream(manifest.value());
    std::vector<fabgl::assets::PackInput> inputs;
    std::string line;
    auto lineNumber = std::size_t{0};
    while (std::getline(stream, line)) {
        ++lineNumber;
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos || line[first] == '#') {
            continue;
        }
        std::istringstream fields(line);
        std::string guidText;
        std::uint32_t typeId = 0;
        std::string storageText;
        std::string path;
        if (!(fields >> guidText >> typeId >> storageText >> std::quoted(path))) {
            throw std::invalid_argument("invalid pack manifest line " + std::to_string(lineNumber));
        }
        auto guid = fabgl::AssetGuid::parse(guidText);
        if (!guid) {
            throw std::invalid_argument("invalid GUID on pack manifest line " +
                                        std::to_string(lineNumber));
        }
        auto payload = fabgl::assets::readBinaryFile(path);
        if (!payload) {
            printError(payload.error());
            return EXIT_FAILURE;
        }
        inputs.push_back(
            {guid.value(), typeId, parseStorage(storageText), std::move(payload.value())});
    }
    auto pack = fabgl::assets::buildPack(std::move(inputs));
    if (!pack) {
        printError(pack.error());
        return EXIT_FAILURE;
    }
    auto written = fabgl::assets::writeBinaryFileAtomic(argv[3], pack.value().bytes);
    if (!written) {
        printError(written.error());
        return EXIT_FAILURE;
    }
    std::cout << "pack entries=" << pack.value().index.size()
              << " bytes=" << pack.value().bytes.size()
              << " checksum=" << pack.value().buildChecksum << '\n';
    return EXIT_SUCCESS;
}

int inspectPack(int argc, char** argv) {
    if (argc != 3) {
        throw std::invalid_argument("inspect requires <input.fglpack>");
    }
    auto bytes = fabgl::assets::readBinaryFile(argv[2]);
    if (!bytes) {
        printError(bytes.error());
        return EXIT_FAILURE;
    }
    auto pack = fabgl::assets::inspectPack(bytes.value());
    if (!pack) {
        printError(pack.error());
        return EXIT_FAILURE;
    }
    std::cout << "entries=" << pack.value().index.size() << " bytes=" << pack.value().bytes.size()
              << " checksum=" << pack.value().buildChecksum << '\n';
    for (const auto& entry : pack.value().index) {
        std::cout << entry.guid.toString() << " type=" << entry.typeId
                  << " storage=" << static_cast<int>(entry.storage) << " offset=" << entry.offset
                  << " size=" << entry.size << " checksum=" << entry.checksum << '\n';
    }
    return EXIT_SUCCESS;
}

int inspectAsset(int argc, char** argv) {
    if (argc != 3) {
        throw std::invalid_argument("inspect-asset requires <input>");
    }
    auto bytes = fabgl::assets::readBinaryFile(argv[2]);
    if (!bytes) {
        printError(bytes.error());
        return EXIT_FAILURE;
    }
    if (bytes.value().size() < 4U) {
        std::cerr << "error: asset header is truncated\n";
        return EXIT_FAILURE;
    }
    const std::string magic(bytes.value().begin(), bytes.value().begin() + 4);
    if (magic == "FGLI") {
        auto image = fabgl::assets::decodeIndexedImage(bytes.value());
        if (!image) {
            printError(image.error());
            return EXIT_FAILURE;
        }
        std::cout << "indexed-image " << image.value().width << 'x' << image.value().height
                  << " palette=" << image.value().palette.size() << '\n';
    } else if (magic == "FGLT") {
        auto tilemap = fabgl::assets::inspectTilemap(bytes.value());
        if (!tilemap) {
            printError(tilemap.error());
            return EXIT_FAILURE;
        }
        std::cout << "tilemap " << tilemap.value().width << 'x' << tilemap.value().height
                  << " layers="
                  << (tilemap.value().layers.empty() ? 1U : tilemap.value().layers.size())
                  << " objects=" << tilemap.value().objects.size()
                  << " chunks=" << tilemap.value().chunks.size() << '\n';
    } else if (magic == "FGLX") {
        auto tileset = fabgl::assets::inspectTileset(bytes.value());
        if (!tileset) {
            printError(tileset.error());
            return EXIT_FAILURE;
        }
        std::cout << "tileset name=" << tileset.value().name
                  << " tiles=" << tileset.value().tileCount
                  << " source=" << tileset.value().sourceImage.toString() << '\n';
    } else if (magic == "FGLM") {
        auto mesh = fabgl::assets::inspectLowPolyMesh(bytes.value());
        if (!mesh) {
            printError(mesh.error());
            return EXIT_FAILURE;
        }
        std::cout << "mesh vertices=" << mesh.value().positions.size()
                  << " triangles=" << mesh.value().indices.size() / 3U
                  << " textureCoordinates=" << mesh.value().textureCoordinates.size() << '\n';
    } else if (magic == "FGLF") {
        auto font = fabgl::assets::inspectBitmapFont(bytes.value());
        if (!font) {
            printError(font.error());
            return EXIT_FAILURE;
        }
        std::cout << "font glyphs=" << font.value().glyphs.size()
                  << " atlas=" << font.value().atlasWidth << 'x' << font.value().atlasHeight
                  << '\n';
    } else {
        std::cerr << "error: unsupported asset magic " << magic << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

void printUsage() {
    std::cout << "FabGL Studio asset compiler\n"
                 "  fabgl_asset_compiler image <input> <output> [--width N] [--height N] [--colors "
                 "N] [--dither]\n"
                 "  fabgl_asset_compiler thumbnail <input> <output.fgli> [--max-width N] "
                 "[--max-height N] [--colors N] [--dither]\n"
                 "  fabgl_asset_compiler audio <input.wav> <output> [--rate N] [--stream] "
                 "[--compress] [--loop-start N] [--loop-end N]\n"
                 "  fabgl_asset_compiler atlas <manifest.txt> <output.fgls> [--max-width N] "
                 "[--padding N] [--colors N] [--dither] [--no-power-of-two]\n"
                 "  fabgl_asset_compiler tilemap <input.csv|input.json> <output.fgltilemap> "
                 "[--guid GUID] [--tile-width N] [--tile-height N] "
                 "[--tileset GUID:FIRST:COUNT]\n"
                 "  fabgl_asset_compiler tileset <output.fgltileset> --guid GUID --name NAME "
                 "--image GUID --tile-width N --tile-height N --count N --columns N "
                 "[--margin N] [--spacing N] [--collision ID,ID]\n"
                 "  fabgl_asset_compiler mesh <input.obj> <output.fglm>\n"
                 "  fabgl_asset_compiler font <input.bdf> <output.fglf> [--atlas-width N] "
                 "[--padding N]\n"
                 "  fabgl_asset_compiler pack <manifest.txt> <output.fglpack>\n"
                 "  fabgl_asset_compiler inspect <input.fglpack>\n"
                 "  fabgl_asset_compiler inspect-asset "
                 "<input.fgli|input.fgltilemap|input.fgltileset|input.fglm|input.fglf>\n";
}

} // namespace

int runMain(int argc, char** argv) {
    try {
        if (argc < 2 || std::string(argv[1]) == "--help") {
            printUsage();
            return argc < 2 ? EXIT_FAILURE : EXIT_SUCCESS;
        }
        const std::string command = argv[1];
        if (command == "image")
            return compileImage(argc, argv);
        if (command == "thumbnail")
            return compileThumbnail(argc, argv);
        if (command == "audio")
            return compileAudio(argc, argv);
        if (command == "atlas")
            return compileAtlas(argc, argv);
        if (command == "tilemap")
            return compileTilemap(argc, argv);
        if (command == "tileset")
            return compileTileset(argc, argv);
        if (command == "mesh")
            return compileMesh(argc, argv);
        if (command == "font")
            return compileFont(argc, argv);
        if (command == "pack")
            return compilePack(argc, argv);
        if (command == "inspect")
            return inspectPack(argc, argv);
        if (command == "inspect-asset")
            return inspectAsset(argc, argv);
        throw std::invalid_argument("unknown command: " + command);
    } catch (const std::exception& exception) {
        std::cerr << "fabgl_asset_compiler: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}

int main(int argc, char** argv) {
    try {
        auto arguments = fabgl::tools::utf8Arguments(argc, argv);
        auto pointers = fabgl::tools::mutableArgumentPointers(arguments);
        return runMain(static_cast<int>(pointers.size()), pointers.data());
    } catch (const std::exception& exception) {
        std::cerr << "fabgl_asset_compiler: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
