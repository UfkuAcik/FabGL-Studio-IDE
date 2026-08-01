#include "fabgl/toolchain/toolchain_manager.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main() {
    try {
        const auto manifest = fgl::toolchain::loadManifest(FGL_TOOLCHAIN_MANIFEST_PATH);
        require(manifest.schemaVersion == 1, "schema version was not loaded");
        require(manifest.arduinoCoreVersion == "2.0.11", "Arduino core pin changed unexpectedly");
        require(manifest.fabglCommit == "04f328a10573297dd554f13be7f369cdee0f7a2b",
                "FabGL commit pin changed unexpectedly");
        require(manifest.fqbn.find("PartitionScheme=huge_app") != std::string::npos,
                "Huge APP option is missing");
        require(manifest.fqbn.find("PSRAM=disabled") != std::string::npos,
                "reference PSRAM option is missing");
        require(manifest.compilerWarnings == "default",
                "compiler warnings contract changed unexpectedly");
        require(manifest.compilerCppExtraFlags == "-Wno-error=narrowing",
                "FabGL compatibility flag changed unexpectedly");
        require(manifest.pins.sdMiso == 35 && manifest.pins.sdMosi == 12 &&
                    manifest.pins.sdClock == 14 && manifest.pins.sdChipSelect == 13,
                "board-specific SD pins changed");
        require(!manifest.automaticUpload, "automatic upload must remain disabled");
        require(manifest.artifacts.size() == 3U, "unexpected artifact lock count");
        for (const auto& artifact : manifest.artifacts) {
            require(artifact.sha256.size() == 64U, "artifact SHA-256 is incomplete");
            require(artifact.size > 0U, "artifact size is missing");
            require(!artifact.commit.empty(), "artifact commit is missing");
            require(!artifact.license.empty(), "artifact license is missing");
        }

        fgl::toolchain::DetectionOptions missingOptions;
        missingOptions.repositoryRoot = ".";
        missingOptions.allowPathLookup = false;
        const auto missing = fgl::toolchain::detectToolchain(manifest, missingOptions);
        require(!missing.buildReady(),
                "an unprovisioned managed toolchain must not be build-ready");
        require(!missing.issues.empty(), "missing tools were not diagnosed");

        fgl::toolchain::ToolchainDetection detection;
        detection.repositoryRoot = "C:/repo root & safe";
        detection.arduinoCli = "C:/Program Files/Arduino CLI/arduino-cli.exe";
        detection.coreDirectory = "C:/Arduino15/packages/esp32/hardware/esp32/2.0.11";
        detection.fabglLibrary = "C:/Arduino Libraries/FabGL 1.0.9";
        detection.cliFound = true;
        detection.coreFound = true;
        detection.fabglFound = true;
        detection.releaseLocked = false;

        const std::string sketch = "C:/repo root & safe/project with spaces & echo pwned";
        fgl::toolchain::BuildRequest request;
        request.sketchDirectory = sketch;
        request.buildDirectory = "C:/repo root & safe/out/build";
        request.outputDirectory = "C:/repo root & safe/out/bin";
        request.jobs = 3;
        request.clean = true;
        const auto command = fgl::toolchain::makeCompileCommand(manifest, detection, request);
        require(!fgl::toolchain::containsUploadOperation(command),
                "compile command contains upload");
        const auto sketchArgument =
            std::find(command.arguments.begin(), command.arguments.end(), sketch);
        require(sketchArgument != command.arguments.end(),
                "metacharacter path was not retained as one argv element");
        require(std::count(command.arguments.begin(), command.arguments.end(), manifest.fqbn) == 1,
                "FQBN was split or omitted");
        require(std::count(command.arguments.begin(), command.arguments.end(),
                           "compiler.cpp.extra_flags=-Wno-error=narrowing") == 1,
                "locked FabGL compatibility flag was split or omitted");
        require(fgl::toolchain::renderCommandForDisplay(command).find('"') != std::string::npos,
                "display rendering did not quote unsafe-looking paths");

        std::cout << "toolchain_manager_tests: PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "toolchain_manager_tests: FAIL: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
