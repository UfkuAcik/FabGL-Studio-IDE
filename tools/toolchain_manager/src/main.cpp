#include "fabgl/toolchain/toolchain_manager.h"

#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] std::string jsonEscape(const std::string_view value) {
    std::string result;
    result.reserve(value.size() + 8U);
    for (const char character : value) {
        switch (character) {
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            result.push_back(character);
            break;
        }
    }
    return result;
}

struct Arguments {
    std::string operation;
    std::map<std::string, std::string, std::less<>> values;
    bool clean{};
    bool verbose{};
};

[[nodiscard]] Arguments parseArguments(const int argc, char** argv) {
    if (argc < 2) {
        throw std::invalid_argument("an operation is required");
    }
    Arguments result;
    result.operation = argv[1];
    for (int index = 2; index < argc; ++index) {
        const std::string_view option(argv[index]);
        if (option == "--clean") {
            result.clean = true;
            continue;
        }
        if (option == "--verbose") {
            result.verbose = true;
            continue;
        }
        if (option.size() < 2U || option.substr(0, 2) != "--" || index + 1 >= argc) {
            throw std::invalid_argument("expected --name value, --clean, or --verbose");
        }
        result.values[std::string(option.substr(2))] = argv[++index];
    }
    return result;
}

[[nodiscard]] const std::string& required(const Arguments& arguments, const std::string_view name) {
    const auto iterator = arguments.values.find(name);
    if (iterator == arguments.values.end() || iterator->second.empty()) {
        throw std::invalid_argument("missing required option --" + std::string(name));
    }
    return iterator->second;
}

[[nodiscard]] std::optional<std::string> optionalPath(const Arguments& arguments,
                                                      const std::string_view name) {
    const auto iterator = arguments.values.find(name);
    if (iterator == arguments.values.end()) {
        return std::nullopt;
    }
    return iterator->second;
}

[[nodiscard]] unsigned int jobsValue(const Arguments& arguments) {
    const auto iterator = arguments.values.find("jobs");
    if (iterator == arguments.values.end()) {
        return 1U;
    }
    std::size_t consumed{};
    const unsigned long parsed = std::stoul(iterator->second, &consumed, 10);
    if (consumed != iterator->second.size() || parsed == 0UL || parsed > 256UL) {
        throw std::invalid_argument("--jobs must be between 1 and 256");
    }
    return static_cast<unsigned int>(parsed);
}

void printUsage() {
    std::cerr << "Usage:\n"
              << "  fabgl_toolchain_manager inspect --manifest FILE --repo DIR "
                 "[--cli FILE --config FILE --data DIR --fabgl DIR]\n"
              << "  fabgl_toolchain_manager compile-command --manifest FILE --repo DIR "
                 "--sketch DIR --build DIR --output DIR "
                 "[--cli FILE --config FILE --data DIR --fabgl DIR --jobs N --clean "
                 "--verbose]\n"
              << "This utility prints a program/argv model; it never runs upload.\n";
}

} // namespace

int main(const int argc, char** argv) {
    try {
        const Arguments arguments = parseArguments(argc, argv);
        const auto manifest = fgl::toolchain::loadManifest(required(arguments, "manifest"));
        fgl::toolchain::DetectionOptions detectionOptions;
        detectionOptions.repositoryRoot = required(arguments, "repo");
        detectionOptions.arduinoCliOverride = optionalPath(arguments, "cli");
        detectionOptions.arduinoConfigOverride = optionalPath(arguments, "config");
        detectionOptions.arduinoDataOverride = optionalPath(arguments, "data");
        detectionOptions.fabglLibraryOverride = optionalPath(arguments, "fabgl");
        const auto detection = fgl::toolchain::detectToolchain(manifest, detectionOptions);

        if (arguments.operation == "inspect") {
            std::cout << "{\n"
                      << "  \"profile\": \"" << jsonEscape(manifest.profileId) << "\",\n"
                      << "  \"fqbn\": \"" << jsonEscape(manifest.fqbn) << "\",\n"
                      << "  \"buildReady\": " << (detection.buildReady() ? "true" : "false")
                      << ",\n"
                      << "  \"releaseLocked\": " << (detection.releaseLocked ? "true" : "false")
                      << ",\n"
                      << "  \"arduinoCli\": \"" << jsonEscape(detection.arduinoCli) << "\",\n"
                      << "  \"cliSource\": \"" << fgl::toolchain::toString(detection.cliSource)
                      << "\",\n"
                      << "  \"coreDirectory\": \"" << jsonEscape(detection.coreDirectory) << "\",\n"
                      << "  \"fabglLibrary\": \"" << jsonEscape(detection.fabglLibrary) << "\",\n"
                      << "  \"issues\": [";
            for (std::size_t index = 0; index < detection.issues.size(); ++index) {
                std::cout << (index == 0U ? "" : ", ") << "\""
                          << jsonEscape(detection.issues[index]) << "\"";
            }
            std::cout << "]\n}\n";
            return detection.buildReady() ? EXIT_SUCCESS : 2;
        }

        if (arguments.operation == "compile-command") {
            fgl::toolchain::BuildRequest request;
            request.sketchDirectory = required(arguments, "sketch");
            request.buildDirectory = required(arguments, "build");
            request.outputDirectory = required(arguments, "output");
            request.jobs = jobsValue(arguments);
            request.clean = arguments.clean;
            request.verbose = arguments.verbose;
            const auto command = fgl::toolchain::makeCompileCommand(manifest, detection, request);
            std::cout << "{\n  \"program\": \"" << jsonEscape(command.program)
                      << "\",\n  \"workingDirectory\": \"" << jsonEscape(command.workingDirectory)
                      << "\",\n  \"arguments\": [";
            for (std::size_t index = 0; index < command.arguments.size(); ++index) {
                std::cout << (index == 0U ? "" : ", ") << "\""
                          << jsonEscape(command.arguments[index]) << "\"";
            }
            std::cout << "],\n  \"displayOnly\": \""
                      << jsonEscape(fgl::toolchain::renderCommandForDisplay(command))
                      << "\",\n  \"upload\": false\n}\n";
            return EXIT_SUCCESS;
        }

        throw std::invalid_argument("unknown operation: " + arguments.operation);
    } catch (const std::exception& exception) {
        std::cerr << "toolchain-manager: " << exception.what() << '\n';
        printUsage();
        return EXIT_FAILURE;
    }
}
