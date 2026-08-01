#include "script_generator.h"

#include <fabgl/assets/file_io.h>

#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace fabgl::project {
namespace {

std::string joinPath(const std::string& left, const std::string& right) {
    if (left.empty())
        return right;
    const auto separator = left.back() == '/' || left.back() == '\\' ? "" : "/";
    return left + separator + right;
}

bool validIdentifier(std::string_view value) {
    if (value.empty() || value.size() > 96U)
        return false;
    const auto first = static_cast<unsigned char>(value.front());
    if (std::isalpha(first) == 0 && value.front() != '_')
        return false;
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte) == 0 && character != '_')
            return false;
    }
    return true;
}

std::vector<std::uint8_t> bytes(const std::string& text) {
    return {text.begin(), text.end()};
}

} // namespace

Result<GeneratedScript> generateGameplayScript(std::string_view className) {
    if (!validIdentifier(className)) {
        return Result<GeneratedScript>::failure(
            Error(ErrorCode::InvalidArgument,
                  "script class must be an ASCII C++ identifier of at most 96 characters"));
    }
    const std::string type(className);
    GeneratedScript generated;
    generated.headerFileName = type + ".h";
    generated.sourceFileName = type + ".cpp";

    std::ostringstream header;
    header << "#pragma once\n\n"
           << "#include <fabgl/scripting/script_component.h>\n\n"
           << "class " << type << " final : public fabgl::scripting::ScriptComponent {\n"
           << "public:\n"
           << "    " << type << "();\n\n"
           << "    float speed = 3.5F;\n\n"
           << "private:\n"
           << "    static fabgl::TypeMetadata metadataDefinition();\n"
           << "    void onUpdate(float deltaSeconds) override;\n"
           << "};\n";
    generated.header = header.str();

    std::ostringstream source;
    source << "#include \"" << type << ".h\"\n\n"
           << type << "::" << type << "()\n"
           << "    : ScriptComponent(metadataDefinition()) {}\n\n"
           << "fabgl::TypeMetadata " << type << "::metadataDefinition() {\n"
           << "    auto metadata = fabgl::scripting::makeScriptMetadata(\n"
           << "        \"game." << type << "\", \"" << type << "\");\n"
           << "    metadata.properties.push_back(fabgl::scripting::scriptProperty(\n"
           << "        \"speed\", &" << type << "::speed, 3.5F, \"Movement\"));\n"
           << "    return metadata;\n"
           << "}\n\n"
           << "void " << type << "::onUpdate(float deltaSeconds) {\n"
           << "    static_cast<void>(deltaSeconds);\n"
           << "    // Gameplay update. `owner()` is valid after the component is attached.\n"
           << "}\n";
    generated.source = source.str();
    return Result<GeneratedScript>::success(std::move(generated));
}

Result<void> writeGameplayScript(const std::string& projectDirectory, std::string_view className) {
    auto generated = generateGameplayScript(className);
    if (!generated)
        return Result<void>::failure(generated.error());
    const auto scriptsDirectory = joinPath(projectDirectory, "Scripts");
    auto directory = assets::createDirectories(scriptsDirectory);
    if (!directory)
        return directory;
    const auto headerPath = joinPath(scriptsDirectory, generated.value().headerFileName);
    const auto sourcePath = joinPath(scriptsDirectory, generated.value().sourceFileName);
    if (assets::readBinaryFile(headerPath) || assets::readBinaryFile(sourcePath)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "refusing to replace an existing gameplay script")
                .addContext("class", std::string(className)));
    }
    auto headerWrite = assets::writeBinaryFileAtomic(headerPath, bytes(generated.value().header));
    if (!headerWrite)
        return headerWrite;
    auto sourceWrite = assets::writeBinaryFileAtomic(sourcePath, bytes(generated.value().source));
    if (!sourceWrite)
        return sourceWrite;
    return Result<void>::success();
}

} // namespace fabgl::project
