#include "utf8_arguments.h"
#include <fabgl/assets/asset_pack.h>
#include <fabgl/assets/audio_importer.h>
#include <fabgl/assets/file_io.h>
#include <fabgl/assets/image_pipeline.h>

#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

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

int compileAudio(int argc, char** argv) {
    if (argc < 4) {
        throw std::invalid_argument("audio requires <input.wav> <output>");
    }
    fabgl::assets::AudioImportSettings settings;
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
    auto encoded = fabgl::assets::encodeAudioClip(clip.value());
    auto written = fabgl::assets::writeBinaryFileAtomic(argv[3], encoded);
    if (!written) {
        printError(written.error());
        return EXIT_FAILURE;
    }
    std::cout << "audio samples=" << clip.value().samples.size()
              << " rate=" << clip.value().sampleRate << " packed=" << encoded.size() << '\n';
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

void printUsage() {
    std::cout << "FabGL Studio asset compiler\n"
                 "  fabgl_asset_compiler image <input> <output> [--width N] [--height N] [--colors "
                 "N] [--dither]\n"
                 "  fabgl_asset_compiler audio <input.wav> <output> [--rate N] [--stream]\n"
                 "  fabgl_asset_compiler pack <manifest.txt> <output.fglpack>\n"
                 "  fabgl_asset_compiler inspect <input.fglpack>\n";
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
        if (command == "audio")
            return compileAudio(argc, argv);
        if (command == "pack")
            return compilePack(argc, argv);
        if (command == "inspect")
            return inspectPack(argc, argv);
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
