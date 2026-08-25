#include "esp32_export.h"
#include "local_package_manager.h"
#include "project_format.h"
#include "project_input_map.h"
#include "project_prepare.h"
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
    auto projectDirectory = fabgl::assets::createDirectories(directory);
    if (!projectDirectory) {
        printError(projectDirectory.error());
        return EXIT_FAILURE;
    }
    const auto projectPath = joinPath(directory, fileNameForProject(name) + ".fglproject");
    if (fabgl::assets::readBinaryFile(projectPath)) {
        std::cerr << "error: refusing to replace existing project " << projectPath << '\n';
        return EXIT_FAILURE;
    }
    auto gameplayBuildFiles = fabgl::project::ensureGameplayBuildFiles(directory);
    if (!gameplayBuildFiles) {
        printError(gameplayBuildFiles.error());
        return EXIT_FAILURE;
    }
    for (const auto& child :
         {std::string("Assets"), std::string("Scenes"), std::string("Packages")}) {
        auto created = fabgl::assets::createDirectories(joinPath(directory, child));
        if (!created) {
            printError(created.error());
            return EXIT_FAILURE;
        }
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
    manifest.inputContexts = {{"gameplay",
                               0,
                               true,
                               {{"Jump", {{"Key.Space", 1.0F, 0.5F}}}},
                               {{"MoveX", {{"Key.A", -1.0F, 0.5F}, {"Key.D", 1.0F, 0.5F}}},
                                {"MoveY", {{"Key.S", -1.0F, 0.5F}, {"Key.W", 1.0F, 0.5F}}}}}};
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
    auto input = fabgl::project::buildInputMap(manifest.value());
    if (!input) {
        printError(input.error());
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
              << " entities=" << scene.value()->entityCount()
              << " input_contexts=" << manifest.value().inputContexts.size()
              << " packages=" << manifest.value().packageDependencies.size()
              << " pc_profile=" << manifest.value().targetProfiles.pc
              << " esp32_profile=" << manifest.value().targetProfiles.esp32 << '\n';
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
    const auto sourceVersion = manifest.value().sourceVersion;
    manifest.value().sourceVersion = fabgl::project::Manifest::CurrentVersion;
    if (!fabgl::AssetGuid::parse(manifest.value().projectGuid))
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
    std::cout << "migrated version " << sourceVersion << " -> "
              << fabgl::project::Manifest::CurrentVersion << ": " << output << '\n';
    return EXIT_SUCCESS;
}

int migrateScene(const std::string& input, const std::string& output) {
    auto source = fabgl::assets::readTextFile(input);
    if (!source) {
        printError(source.error());
        return EXIT_FAILURE;
    }
    auto scene = fabgl::SceneSerializer::deserialize(source.value());
    if (!scene) {
        printError(scene.error());
        return EXIT_FAILURE;
    }
    auto encoded = fabgl::SceneSerializer::serialize(*scene.value());
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
    std::cout << "migrated scene to fglscene " << fabgl::SceneSerializer::CurrentVersion << ": "
              << output << '\n';
    return EXIT_SUCCESS;
}

int exportEsp32(const std::string& project, const std::string& firmwareTemplate,
                const std::string& outputSketch) {
    auto exported = fabgl::project::exportEsp32Project(project, firmwareTemplate, outputSketch);
    if (!exported) {
        printError(exported.error());
        return EXIT_FAILURE;
    }
    std::cout << "exported ESP32 sketch " << outputSketch << '/' << exported.value().sketchFileName
              << " project=\"" << exported.value().projectName << "\""
              << " entities=" << exported.value().entityCount
              << " assets=" << exported.value().assetCount
              << " portable_script_files=" << exported.value().portableScriptFileCount
              << " script_runtime=" << (exported.value().scriptRuntime ? "true" : "false")
              << " payload_bytes=" << exported.value().payloadSize
              << " payload_checksum=" << exported.value().payloadChecksum << '\n';
    return EXIT_SUCCESS;
}

int prepareProject(const std::string& project, const std::string& output,
                   const std::string& target) {
    const auto prepareTarget = target == "pc" ? fabgl::project::ProjectPrepareTarget::Pc
                                              : fabgl::project::ProjectPrepareTarget::Esp32;
    auto prepared = fabgl::project::prepareProjectInputs(project, output, prepareTarget);
    if (!prepared) {
        printError(prepared.error());
        return EXIT_FAILURE;
    }
    const auto& result = prepared.value();
    std::cout << "prepared target=" << target << " assets=" << result.assetCount
              << " imported=" << result.importedAssetCount
              << " validated=" << result.validatedAssetCount
              << " visual_graphs=" << result.visualGraphCount
              << " visual_programs=" << result.visualProgramCount
              << " portable_script_files=" << result.portableScriptFileCount
              << " source_bytes=" << result.sourceBytes << " pack_bytes=" << result.packedBytes
              << " pack_checksum=" << result.packChecksum << " pack=\"" << result.packPath
              << "\" prepared_project=\"" << result.preparedProjectPath << "\"\n";
    return EXIT_SUCCESS;
}

int installPackage(const std::string& project, const std::string& source, bool allowExecutable) {
    fabgl::project::LocalPackageInstallOptions options;
    options.allowExecutable = allowExecutable;
    auto installed = fabgl::project::installLocalPackage(project, source, options);
    if (!installed) {
        printError(installed.error());
        return EXIT_FAILURE;
    }
    const auto& package = installed.value();
    std::cout << "installed " << package.manifest.stableId() << '@'
              << package.manifest.version.toString() << " sha256=" << package.contentSha256
              << " files=" << package.fileCount << " bytes=" << package.totalBytes
              << " executable=" << (package.manifest.containsExecutableCode ? "true" : "false")
              << " trusted=" << (package.executableTrusted ? "true" : "false") << '\n';
    return EXIT_SUCCESS;
}

int listPackages(const std::string& project) {
    auto packages = fabgl::project::listLocalPackages(project);
    if (!packages) {
        printError(packages.error());
        return EXIT_FAILURE;
    }
    std::cout << "packages=" << packages.value().size() << '\n';
    for (const auto& package : packages.value()) {
        std::cout << package.manifest.stableId() << '@' << package.manifest.version.toString()
                  << " sha256=" << package.contentSha256 << " files=" << package.fileCount
                  << " bytes=" << package.totalBytes
                  << " executable=" << (package.manifest.containsExecutableCode ? "true" : "false")
                  << " trusted=" << (package.executableTrusted ? "true" : "false") << '\n';
    }
    return EXIT_SUCCESS;
}

int validatePackages(const std::string& project) {
    auto order = fabgl::project::validateLocalPackages(project);
    if (!order) {
        printError(order.error());
        return EXIT_FAILURE;
    }
    std::cout << "valid packages=" << order.value().size() << " load-order=";
    for (std::size_t index = 0U; index < order.value().size(); ++index) {
        if (index != 0U)
            std::cout << ',';
        std::cout << order.value()[index];
    }
    std::cout << '\n';
    return EXIT_SUCCESS;
}

int removePackage(const std::string& project, const std::string& packageId) {
    auto removed = fabgl::project::removeLocalPackage(project, packageId);
    if (!removed) {
        printError(removed.error());
        return EXIT_FAILURE;
    }
    std::cout << "removed " << packageId << '\n';
    return EXIT_SUCCESS;
}

void usage() {
    std::cout << "FabGL Studio project CLI\n"
                 "  fabgl_project_cli new <directory> <name>\n"
                 "  fabgl_project_cli validate <project.fglproject>\n"
                 "  fabgl_project_cli migrate <old.fglproject> <new.fglproject>\n"
                 "  fabgl_project_cli migrate-scene <old.fglscene> <new.fglscene>\n"
                 "  fabgl_project_cli export-esp32 <project.fglproject> "
                 "<firmware-template-dir> <output-sketch-dir>\n"
                 "  fabgl_project_cli prepare <project.fglproject> <output-directory> <pc|esp32>\n"
                 "  fabgl_project_cli new-script <project-directory> <ClassName>\n"
                 "  fabgl_project_cli package install <project.fglproject> <package-directory> "
                 "[--allow-executable]\n"
                 "  fabgl_project_cli package list <project.fglproject>\n"
                 "  fabgl_project_cli package validate <project.fglproject>\n"
                 "  fabgl_project_cli package remove <project.fglproject> <package-id>\n";
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
        if (command == "migrate-scene" && argc == 4)
            return migrateScene(argv[2], argv[3]);
        if (command == "export-esp32" && argc == 5)
            return exportEsp32(argv[2], argv[3], argv[4]);
        if (command == "prepare" && argc == 5 &&
            (std::string(argv[4]) == "pc" || std::string(argv[4]) == "esp32"))
            return prepareProject(argv[2], argv[3], argv[4]);
        if (command == "new-script" && argc == 4) {
            auto generated = fabgl::project::writeGameplayScript(argv[2], argv[3]);
            if (!generated) {
                printError(generated.error());
                return EXIT_FAILURE;
            }
            std::cout << "created gameplay script Scripts/" << argv[3]
                      << ".h and .cpp; refreshed managed CMake glue\n";
            return EXIT_SUCCESS;
        }
        if (command == "package" && argc >= 3) {
            const std::string packageCommand = argv[2];
            if (packageCommand == "install" && (argc == 5 || argc == 6)) {
                if (argc == 6 && std::string(argv[5]) != "--allow-executable")
                    throw std::invalid_argument("unknown package install option");
                return installPackage(argv[3], argv[4], argc == 6);
            }
            if (packageCommand == "list" && argc == 4)
                return listPackages(argv[3]);
            if (packageCommand == "validate" && argc == 4)
                return validatePackages(argv[3]);
            if (packageCommand == "remove" && argc == 5)
                return removePackage(argv[3], argv[4]);
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
