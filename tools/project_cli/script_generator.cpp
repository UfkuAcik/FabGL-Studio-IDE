#include "script_generator.h"

#include <fabgl/assets/file_io.h>

#include <cctype>
#include <cstdint>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <sys/stat.h>
#endif

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
    if (value.rfind("__", 0U) == 0U || (value.size() > 1U && value.front() == '_' &&
                                        std::isupper(static_cast<unsigned char>(value[1U])) != 0)) {
        return false;
    }
    constexpr std::string_view keywords =
        "alignas alignof and and_eq asm atomic_cancel atomic_commit atomic_noexcept auto bitand "
        "bitor bool break case catch char char8_t char16_t char32_t class compl concept const "
        "consteval constexpr constinit const_cast continue co_await co_return co_yield decltype "
        "default delete do double dynamic_cast else enum explicit export extern false final float "
        "for friend goto if inline int long mutable namespace new noexcept not not_eq nullptr "
        "operator or or_eq override private protected public register reinterpret_cast requires "
        "return short signed sizeof static static_assert static_cast struct switch template this "
        "thread_local throw true try typedef typeid typename union unsigned using virtual void "
        "volatile wchar_t while xor xor_eq";
    for (std::size_t offset = 0U; offset < keywords.size();) {
        const auto separator = keywords.find(' ', offset);
        const auto length =
            separator == std::string_view::npos ? keywords.size() - offset : separator - offset;
        if (value == keywords.substr(offset, length))
            return false;
        offset = separator == std::string_view::npos ? keywords.size() : separator + 1U;
    }
    return true;
}

std::vector<std::uint8_t> bytes(const std::string& text) {
    return {text.begin(), text.end()};
}

#ifdef _WIN32
Result<std::wstring> toWidePath(const std::string& path, std::string_view description) {
    if (path.find('\0') != std::string::npos ||
        path.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return Result<std::wstring>::failure(
            Error(ErrorCode::InvalidArgument, std::string(description) + " path is invalid"));
    }
    const auto length = static_cast<int>(path.size());
    const auto count =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), length, nullptr, 0);
    if (count <= 0) {
        return Result<std::wstring>::failure(
            Error(ErrorCode::InvalidArgument, std::string(description) + " path is not UTF-8"));
    }
    std::wstring wide(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), length, wide.data(),
                            count) <= 0) {
        return Result<std::wstring>::failure(
            Error(ErrorCode::InvalidArgument, std::string(description) + " path is not UTF-8"));
    }
    return Result<std::wstring>::success(std::move(wide));
}
#endif

Result<void> validateRealDirectory(const std::string& path, std::string_view description) {
#ifdef _WIN32
    auto wide = toWidePath(path, description);
    if (!wide)
        return Result<void>::failure(wide.error());
    const auto attributes = GetFileAttributesW(wide.value().c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return Result<void>::failure(
            Error(ErrorCode::IoError, std::string(description) + " directory is unavailable")
                .addContext("path", path)
                .addContext("win32", std::to_string(GetLastError())));
    }
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument,
                  std::string(description) + " directory cannot be a reparse point")
                .addContext("path", path));
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, std::string(description) + " is not a directory")
                .addContext("path", path));
    }
#else
    struct stat status {};
    if (::lstat(path.c_str(), &status) != 0) {
        return Result<void>::failure(
            Error(ErrorCode::IoError, std::string(description) + " directory is unavailable")
                .addContext("path", path)
                .addContext("errno", std::to_string(errno)));
    }
    if (S_ISLNK(status.st_mode)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument,
                  std::string(description) + " directory cannot be a symbolic link")
                .addContext("path", path));
    }
    if (!S_ISDIR(status.st_mode)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, std::string(description) + " is not a directory")
                .addContext("path", path));
    }
#endif
    return Result<void>::success();
}

Result<bool> validateManagedFilePath(const std::string& path) {
#ifdef _WIN32
    auto wide = toWidePath(path, "managed CMake file");
    if (!wide)
        return Result<bool>::failure(wide.error());
    const auto attributes = GetFileAttributesW(wide.value().c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const auto code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
            return Result<bool>::success(false);
        return Result<bool>::failure(Error(ErrorCode::IoError, "managed CMake path is unavailable")
                                         .addContext("path", path)
                                         .addContext("win32", std::to_string(code)));
    }
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
        return Result<bool>::failure(
            Error(ErrorCode::InvalidArgument,
                  "managed CMake file cannot be a symbolic link or reparse point")
                .addContext("path", path));
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U) {
        return Result<bool>::failure(
            Error(ErrorCode::InvalidArgument, "managed CMake path is not a file")
                .addContext("path", path));
    }
#else
    struct stat status {};
    if (::lstat(path.c_str(), &status) != 0) {
        if (errno == ENOENT)
            return Result<bool>::success(false);
        return Result<bool>::failure(Error(ErrorCode::IoError, "managed CMake path is unavailable")
                                         .addContext("path", path)
                                         .addContext("errno", std::to_string(errno)));
    }
    if (S_ISLNK(status.st_mode)) {
        return Result<bool>::failure(
            Error(ErrorCode::InvalidArgument, "managed CMake file cannot be a symbolic link")
                .addContext("path", path));
    }
    if (!S_ISREG(status.st_mode)) {
        return Result<bool>::failure(
            Error(ErrorCode::InvalidArgument, "managed CMake path is not a regular file")
                .addContext("path", path));
    }
#endif
    return Result<bool>::success(true);
}

Result<void> writeManagedFile(const std::string& path, std::string_view marker,
                              const std::string& contents, bool preserveCustom) {
    auto validation = validateManagedFilePath(path);
    if (!validation)
        return Result<void>::failure(validation.error());
    auto current = assets::readTextFile(path);
    if (validation.value()) {
        if (!current)
            return Result<void>::failure(current.error());
        if (current.value().rfind(marker, 0U) != 0U) {
            if (preserveCustom)
                return Result<void>::success();
            return Result<void>::failure(
                Error(ErrorCode::AlreadyExists,
                      "refusing to replace a custom gameplay CMake glue file")
                    .addContext("path", path));
        }
        if (current.value() == contents)
            return Result<void>::success();
    }
    return assets::writeBinaryFileAtomic(path, bytes(contents));
}

std::string generateEsp32Module(const std::set<std::string>& classes) {
    std::ostringstream output;
    output << Esp32ScriptModuleMarker << '\n';
    for (const auto& className : classes)
        output << "// class=" << className << '\n';
    output << '\n';
    for (const auto& className : classes)
        output << "#include \"" << className << "Esp32.h\"\n";
    output << "\nnamespace {\n\n"
              "const fabgl_project_scripts::Descriptor kFabGLProjectScripts[]{\n";
    for (const auto& className : classes)
        output << "    fabgl_project_game::" << className << "Esp32Descriptor,\n";
    output << "};\n\n"
              "} // namespace\n\n"
              "FGL_ESP32_SCRIPT_MODULE(kFabGLProjectScripts)\n";
    return output.str();
}

Result<void> updateEsp32Module(const std::string& path, std::string_view className) {
    std::set<std::string> classes;
    auto existing = validateManagedFilePath(path);
    if (!existing)
        return Result<void>::failure(existing.error());
    if (existing.value()) {
        auto text = assets::readTextFile(path);
        if (!text)
            return Result<void>::failure(text.error());
        if (text.value().rfind(Esp32ScriptModuleMarker, 0U) != 0U) {
            return Result<void>::failure(
                Error(ErrorCode::AlreadyExists,
                      "refusing to replace a custom ESP32 gameplay module")
                    .addContext("path", path));
        }
        std::istringstream lines(text.value());
        std::string line;
        while (std::getline(lines, line)) {
            constexpr std::string_view Prefix = "// class=";
            if (line.rfind(Prefix, 0U) != 0U)
                continue;
            const auto existingClass = line.substr(Prefix.size());
            if (!validIdentifier(existingClass) || !classes.insert(existingClass).second) {
                return Result<void>::failure(
                    Error(ErrorCode::InvalidFormat,
                          "managed ESP32 gameplay module class list is invalid")
                        .addContext("path", path));
            }
        }
    }
    classes.emplace(className);
    return writeManagedFile(path, Esp32ScriptModuleMarker, generateEsp32Module(classes), false);
}

} // namespace

Result<GeneratedScript> generateGameplayScript(std::string_view className) {
    if (!validIdentifier(className)) {
        return Result<GeneratedScript>::failure(Error(
            ErrorCode::InvalidArgument,
            "script class must be a non-reserved ASCII C++ identifier of at most 96 characters"));
    }
    const std::string type(className);
    GeneratedScript generated;
    generated.headerFileName = type + ".h";
    generated.sourceFileName = type + ".cpp";
    generated.portableHeaderFileName = type + "Esp32.h";
    generated.portableSourceFileName = type + "Esp32.cpp";

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
    source << "#include \"" << type << ".h\"\n"
           << "#include <fabgl/scripting/script_module.h>\n\n"
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
           << "}\n\n"
           << "FABGL_REGISTER_SCRIPT(" << type << ")\n";
    generated.source = source.str();

    std::ostringstream portableHeader;
    portableHeader
        << "#pragma once\n\n"
           "#include \"ProjectScriptRuntime.h\"\n\n"
           "namespace fabgl_project_game {\n\n"
        << "void " << type
        << "Esp32Start(fabgl_project_runtime::RuntimeProject& project) noexcept;\n"
        << "void " << type
        << "Esp32Update(fabgl_project_runtime::RuntimeProject& project, "
           "float deltaSeconds) noexcept;\n"
        << "extern const fabgl_project_scripts::Descriptor " << type
        << "Esp32Descriptor;\n\n"
           "} // namespace fabgl_project_game\n";
    generated.portableHeader = portableHeader.str();

    std::ostringstream portableSource;
    portableSource << "#include \"" << generated.portableHeaderFileName << "\"\n\n"
                   << "namespace fabgl_project_game {\n\n"
                   << "void " << type
                   << "Esp32Start(fabgl_project_runtime::RuntimeProject& project) noexcept {\n"
                      "    static_cast<void>(project);\n"
                      "}\n\n"
                   << "void " << type
                   << "Esp32Update(fabgl_project_runtime::RuntimeProject& project, "
                      "float deltaSeconds) noexcept {\n"
                      "    static_cast<void>(project);\n"
                      "    static_cast<void>(deltaSeconds);\n"
                      "    // Portable allocation-free ESP32 gameplay update.\n"
                      "}\n\n"
                   << "const fabgl_project_scripts::Descriptor " << type
                   << "Esp32Descriptor{sizeof(fabgl_project_scripts::Descriptor), \"game."
                   << type << "\", &" << type << "Esp32Start, &" << type
                   << "Esp32Update};\n\n"
                      "} // namespace fabgl_project_game\n";
    generated.portableSource = portableSource.str();
    return Result<GeneratedScript>::success(std::move(generated));
}

std::string generateGameplayCMakeGlue() {
    return R"cmake(# FabGL Studio managed gameplay CMake glue. Schema: 1
include_guard(GLOBAL)

function(fabgl_studio_collect_gameplay_scripts output_variable)
    file(GLOB_RECURSE _fabgl_gameplay_entries CONFIGURE_DEPENDS LIST_DIRECTORIES true
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/*")
    list(SORT _fabgl_gameplay_entries)
    foreach(_fabgl_entry IN LISTS _fabgl_gameplay_entries)
        if(IS_SYMLINK "${_fabgl_entry}")
            message(FATAL_ERROR
                "Scripts tree cannot contain a symbolic link: ${_fabgl_entry}")
        endif()
    endforeach()

    file(GLOB_RECURSE _fabgl_gameplay_sources CONFIGURE_DEPENDS LIST_DIRECTORIES false
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/*.cc"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/*.cpp"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/*.cxx")
    list(FILTER _fabgl_gameplay_sources EXCLUDE REGEX "[/\\\\]ESP32[/\\\\]")
    list(SORT _fabgl_gameplay_sources)
    if(NOT _fabgl_gameplay_sources)
        message(FATAL_ERROR
            "No gameplay C++ sources were found under ${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    endif()

    set(_fabgl_script_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    cmake_path(NORMAL_PATH _fabgl_script_root)
    foreach(_fabgl_source IN LISTS _fabgl_gameplay_sources)
        if(IS_SYMLINK "${_fabgl_source}")
            message(FATAL_ERROR "Gameplay source cannot be a symbolic link: ${_fabgl_source}")
        endif()
        file(REAL_PATH "${_fabgl_source}" _fabgl_source_real)
        cmake_path(IS_PREFIX _fabgl_script_root "${_fabgl_source_real}" NORMALIZE
            _fabgl_source_is_contained)
        if(NOT _fabgl_source_is_contained)
            message(FATAL_ERROR "Gameplay source escapes Scripts/: ${_fabgl_source}")
        endif()
    endforeach()
    set(${output_variable} "${_fabgl_gameplay_sources}" PARENT_SCOPE)
endfunction()

function(fabgl_studio_enable_gameplay_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(${target_name} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
            -Wshadow -Wold-style-cast -Woverloaded-virtual -Werror)
    endif()
endfunction()

function(fabgl_studio_add_gameplay_scripts target_name)
    if(NOT TARGET FabGLStudio::Engine)
        message(FATAL_ERROR
            "FabGLStudio::Engine is unavailable; call find_package(FabGLStudio CONFIG REQUIRED)")
    endif()
    fabgl_studio_collect_gameplay_scripts(_fabgl_gameplay_sources)
    add_library(${target_name} STATIC ${_fabgl_gameplay_sources})
    target_compile_features(${target_name} PUBLIC cxx_std_20)
    target_link_libraries(${target_name} PUBLIC FabGLStudio::Engine)
    fabgl_studio_enable_gameplay_warnings(${target_name})
endfunction()
)cmake";
}

std::string generateProjectCMake() {
    return R"cmake(# FabGL Studio managed project CMake. Schema: 1
cmake_minimum_required(VERSION 3.24)

project(FabGLStudioGameplayProject LANGUAGES CXX)

find_package(FabGLStudio CONFIG REQUIRED)
include("${CMAKE_CURRENT_SOURCE_DIR}/Scripts/FabGLStudioScripts.cmake")
fabgl_studio_add_gameplay_scripts(fabgl_gameplay_scripts)
)cmake";
}

Result<void> ensureGameplayBuildFiles(const std::string& projectDirectory) {
    auto projectValidation = validateRealDirectory(projectDirectory, "project");
    if (!projectValidation)
        return projectValidation;
    const auto scriptsDirectory = joinPath(projectDirectory, "Scripts");
    auto scriptsCreated = assets::createDirectories(scriptsDirectory);
    if (!scriptsCreated)
        return scriptsCreated;
    auto scriptsValidation = validateRealDirectory(scriptsDirectory, "Scripts");
    if (!scriptsValidation)
        return scriptsValidation;

    const auto glue = generateGameplayCMakeGlue();
    auto glueWrite = writeManagedFile(joinPath(scriptsDirectory, "FabGLStudioScripts.cmake"),
                                      GameplayCMakeMarker, glue, false);
    if (!glueWrite)
        return glueWrite;
    const auto projectCMake = generateProjectCMake();
    return writeManagedFile(joinPath(projectDirectory, "CMakeLists.txt"), ProjectCMakeMarker,
                            projectCMake, true);
}

Result<void> writeGameplayScript(const std::string& projectDirectory, std::string_view className) {
    auto generated = generateGameplayScript(className);
    if (!generated)
        return Result<void>::failure(generated.error());
    auto buildFiles = ensureGameplayBuildFiles(projectDirectory);
    if (!buildFiles)
        return buildFiles;
    const auto scriptsDirectory = joinPath(projectDirectory, "Scripts");
    auto directory = assets::createDirectories(scriptsDirectory);
    if (!directory)
        return directory;
    const auto headerPath = joinPath(scriptsDirectory, generated.value().headerFileName);
    const auto sourcePath = joinPath(scriptsDirectory, generated.value().sourceFileName);
    const auto portableDirectory = joinPath(scriptsDirectory, "ESP32");
    auto portableDirectoryCreated = assets::createDirectories(portableDirectory);
    if (!portableDirectoryCreated)
        return portableDirectoryCreated;
    auto portableDirectoryValid = validateRealDirectory(portableDirectory, "Scripts/ESP32");
    if (!portableDirectoryValid)
        return portableDirectoryValid;
    const auto portableHeaderPath =
        joinPath(portableDirectory, generated.value().portableHeaderFileName);
    const auto portableSourcePath =
        joinPath(portableDirectory, generated.value().portableSourceFileName);
    if (assets::readBinaryFile(headerPath) || assets::readBinaryFile(sourcePath) ||
        assets::readBinaryFile(portableHeaderPath) ||
        assets::readBinaryFile(portableSourcePath)) {
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
    auto portableHeaderWrite = assets::writeBinaryFileAtomic(
        portableHeaderPath, bytes(generated.value().portableHeader));
    if (!portableHeaderWrite)
        return portableHeaderWrite;
    auto portableSourceWrite = assets::writeBinaryFileAtomic(
        portableSourcePath, bytes(generated.value().portableSource));
    if (!portableSourceWrite)
        return portableSourceWrite;
    return updateEsp32Module(joinPath(portableDirectory, "FabGLStudioEsp32Module.cpp"),
                             className);
}

} // namespace fabgl::project
