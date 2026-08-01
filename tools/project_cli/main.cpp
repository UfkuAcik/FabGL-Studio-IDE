#include "project_format.h"
#include "script_generator.h"
#include "utf8_arguments.h"

#include <fabgl/assets/file_io.h>
#include <fabgl/core/guid.h>
#include <fabgl/scene/scene.h>
#include <fabgl/serialization/scene_serializer.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::string joinPath(const std::string& left, const std::string& right) {
    if (left.empty())
        return right;
    const auto separator = left.back() == '/' || left.back() == '\\' ? "" : "/";
    return left + separator + right;
}

std::string parentPath(const std::string& path) {
    const auto separator = path.find_last_of("/\\");
    return separator == std::string::npos ? "." : path.substr(0U, separator);
}

std::string fileNameForProject(std::string name) {
    for (auto& character : name) {
        if (character == '<' || character == '>' || character == ':' || character == '"' ||
            character == '/' || character == '\\' || character == '|' || character == '?' ||
            character == '*') {
            character = '_';
        }
    }
    return name.empty() ? "FabGLProject" : name;
}

void printError(const fabgl::Error& error) {
    std::cerr << "error: " << error.message();
    for (const auto& context : error.context()) {
        std::cerr << " [" << context.key << '=' << context.value << ']';
    }
    std::cerr << '\n';
}

int createProject(const std::string& directory, const std::string& name) {
    for (const auto& child : {std::string(), std::string("Assets"), std::string("Scenes"),
                              std::string("Scripts"), std::string("Packages")}) {
        auto created = fabgl::assets::createDirectories(child.empty() ? directory
                                                                      : joinPath(directory, child));
        if (!created) {
            printError(created.error());
            return EXIT_FAILURE;
        }
    }
    const auto projectPath = joinPath(directory, fileNameForProject(name) + ".fglproject");
    if (fabgl::assets::readBinaryFile(projectPath)) {
        std::cerr << "error: refusing to replace existing project " << projectPath << '\n';
        return EXIT_FAILURE;
    }
    fabgl::Scene scene("Main", fabgl::SceneGuid::generate());
    auto camera = scene.createEntity("Camera");
    if (!camera) {
        printError(camera.error());
        return EXIT_FAILURE;
    }
    auto serializedScene = fabgl::SceneSerializer::serialize(scene);
    if (!serializedScene) {
        printError(serializedScene.error());
        return EXIT_FAILURE;
    }
    const auto sceneText = serializedScene.value();
    const std::vector<std::uint8_t> sceneBytes(sceneText.begin(), sceneText.end());
    auto sceneWritten = fabgl::assets::writeBinaryFileAtomic(
        joinPath(directory, "Scenes/Main.fglscene"), sceneBytes);
    if (!sceneWritten) {
        printError(sceneWritten.error());
        return EXIT_FAILURE;
    }

    fabgl::project::Manifest manifest;
    manifest.projectGuid = fabgl::AssetGuid::generate().toString();
    manifest.name = name;
    auto serialized = fabgl::project::serializeManifest(manifest);
    if (!serialized) {
        printError(serialized.error());
        return EXIT_FAILURE;
    }
    const std::vector<std::uint8_t> projectBytes(serialized.value().begin(),
                                                 serialized.value().end());
    auto written = fabgl::assets::writeBinaryFileAtomic(projectPath, projectBytes);
    if (!written) {
        printError(written.error());
        return EXIT_FAILURE;
    }
    std::cout << "created " << projectPath << '\n';
    return EXIT_SUCCESS;
}

int validateProject(const std::string& path) {
    auto source = fabgl::assets::readTextFile(path);
    if (!source) {
        printError(source.error());
        return EXIT_FAILURE;
    }
    auto manifest = fabgl::project::parseManifest(source.value());
    if (!manifest) {
        printError(manifest.error());
        return EXIT_FAILURE;
    }
    const auto scenePath = joinPath(parentPath(path), manifest.value().startupScene);
    auto sceneSource = fabgl::assets::readTextFile(scenePath);
    if (!sceneSource) {
        printError(sceneSource.error());
        return EXIT_FAILURE;
    }
    auto scene = fabgl::SceneSerializer::deserialize(sceneSource.value());
    if (!scene) {
        printError(scene.error());
        return EXIT_FAILURE;
    }
    std::cout << "valid project name=\"" << manifest.value().name
              << "\" version=" << manifest.value().sourceVersion
              << " entities=" << scene.value()->entityCount() << '\n';
    return EXIT_SUCCESS;
}

int migrateProject(const std::string& input, const std::string& output) {
    auto source = fabgl::assets::readTextFile(input);
    if (!source) {
        printError(source.error());
        return EXIT_FAILURE;
    }
    auto manifest = fabgl::project::parseManifest(source.value());
    if (!manifest) {
        printError(manifest.error());
        return EXIT_FAILURE;
    }
    if (manifest.value().sourceVersion == fabgl::project::Manifest::CurrentVersion) {
        std::cerr << "error: project is already at the current version\n";
        return EXIT_FAILURE;
    }
    manifest.value().sourceVersion = fabgl::project::Manifest::CurrentVersion;
    manifest.value().projectGuid = fabgl::AssetGuid::generate().toString();
    auto encoded = fabgl::project::serializeManifest(manifest.value());
    if (!encoded) {
        printError(encoded.error());
        return EXIT_FAILURE;
    }
    const std::vector<std::uint8_t> bytes(encoded.value().begin(), encoded.value().end());
    auto written = fabgl::assets::writeBinaryFileAtomic(output, bytes);
    if (!written) {
        printError(written.error());
        return EXIT_FAILURE;
    }
    std::cout << "migrated version 0 -> 1: " << output << '\n';
    return EXIT_SUCCESS;
}

void usage() {
    std::cout << "FabGL Studio project CLI\n"
                 "  fabgl_project_cli new <directory> <name>\n"
                 "  fabgl_project_cli validate <project.fglproject>\n"
                 "  fabgl_project_cli migrate <old.fglproject> <new.fglproject>\n"
                 "  fabgl_project_cli new-script <project-directory> <ClassName>\n";
}

} // namespace

int runMain(int argc, char** argv) {
    try {
        if (argc < 2 || std::string(argv[1]) == "--help") {
            usage();
            return argc < 2 ? EXIT_FAILURE : EXIT_SUCCESS;
        }
        const std::string command = argv[1];
        if (command == "new" && argc == 4)
            return createProject(argv[2], argv[3]);
        if (command == "validate" && argc == 3)
            return validateProject(argv[2]);
        if (command == "migrate" && argc == 4)
            return migrateProject(argv[2], argv[3]);
        if (command == "new-script" && argc == 4) {
            auto generated = fabgl::project::writeGameplayScript(argv[2], argv[3]);
            if (!generated) {
                printError(generated.error());
                return EXIT_FAILURE;
            }
            std::cout << "created gameplay script Scripts/" << argv[3] << ".h and .cpp\n";
            return EXIT_SUCCESS;
        }
        throw std::invalid_argument("invalid command or argument count");
    } catch (const std::exception& exception) {
        std::cerr << "fabgl_project_cli: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}

int main(int argc, char** argv) {
    try {
        auto arguments = fabgl::tools::utf8Arguments(argc, argv);
        auto pointers = fabgl::tools::mutableArgumentPointers(arguments);
        return runMain(static_cast<int>(pointers.size()), pointers.data());
    } catch (const std::exception& exception) {
        std::cerr << "fabgl_project_cli: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
