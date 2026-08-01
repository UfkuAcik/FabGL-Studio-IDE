#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fgl::toolchain {

struct ArtifactLock {
    std::string id;
    std::string version;
    std::string commit;
    std::string url;
    std::string fileName;
    std::uint64_t size{};
    std::string sha256;
    std::string license;
    std::string sourceUrl;
    std::string platform;
    std::string installDirectory;
    bool stripSingleRoot{};
};

struct BoardPins {
    int vgaR1{};
    int vgaR0{};
    int vgaG1{};
    int vgaG0{};
    int vgaB1{};
    int vgaB0{};
    int vgaHSync{};
    int vgaVSync{};
    int keyboardData{};
    int keyboardClock{};
    int mouseData{};
    int mouseClock{};
    int audioDac{};
    int sdMiso{};
    int sdMosi{};
    int sdClock{};
    int sdChipSelect{};
};

struct ToolchainManifest {
    int schemaVersion{};
    std::string profileId;
    std::string displayName;
    std::string fqbn;
    std::string compilerWarnings;
    std::string compilerCppExtraFlags;
    std::string arduinoCorePackage;
    std::string arduinoCoreVersion;
    std::string arduinoCoreCommit;
    std::string fabglVersion;
    std::string fabglDistributionVersion;
    std::string fabglCommit;
    std::string boardManagerPackage;
    bool automaticUpload{};
    BoardPins pins;
    std::vector<ArtifactLock> artifacts;
};

struct DetectionOptions {
    std::string repositoryRoot;
    std::optional<std::string> arduinoCliOverride;
    std::optional<std::string> arduinoConfigOverride;
    std::optional<std::string> arduinoDataOverride;
    std::optional<std::string> fabglLibraryOverride;
    bool allowPathLookup{true};
};

enum class DetectionSource {
    Missing,
    Managed,
    Override,
    SystemPath,
};

struct ToolchainDetection {
    std::string repositoryRoot;
    std::string arduinoCli;
    std::string arduinoConfig;
    std::string arduinoData;
    std::string coreDirectory;
    std::string fabglLibrary;
    DetectionSource cliSource{DetectionSource::Missing};
    DetectionSource fabglSource{DetectionSource::Missing};
    bool cliFound{};
    bool coreFound{};
    bool fabglFound{};
    bool releaseLocked{};
    std::vector<std::string> issues;

    [[nodiscard]] bool buildReady() const noexcept {
        return cliFound && coreFound && fabglFound;
    }
};

struct BuildRequest {
    std::string sketchDirectory;
    std::string buildDirectory;
    std::string outputDirectory;
    unsigned int jobs{1};
    bool clean{};
    bool verbose{};
};

struct ProcessCommand {
    std::string program;
    std::vector<std::string> arguments;
    std::string workingDirectory;
};

[[nodiscard]] ToolchainManifest loadManifest(const std::string& manifestPath);

[[nodiscard]] ToolchainDetection detectToolchain(const ToolchainManifest& manifest,
                                                 const DetectionOptions& options);

[[nodiscard]] ProcessCommand makeCompileCommand(const ToolchainManifest& manifest,
                                                const ToolchainDetection& detection,
                                                const BuildRequest& request);

[[nodiscard]] bool containsUploadOperation(const ProcessCommand& command);

// Human-readable diagnostics only. Execute ProcessCommand using an API that
// accepts program and argv separately; never execute this rendered string.
[[nodiscard]] std::string renderCommandForDisplay(const ProcessCommand& command);

[[nodiscard]] const char* toString(DetectionSource source) noexcept;

} // namespace fgl::toolchain
