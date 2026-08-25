#include "esp32_export.h"

#include "esp32_capabilities.h"
#include "project_format.h"

#include <fabgl/assets/asset_pack.h>
#include <fabgl/assets/audio_importer.h>
#include <fabgl/assets/file_io.h>
#include <fabgl/assets/image_pipeline.h>
#include <fabgl/core/guid.h>
#include <fabgl/rendering/racer_track.h>
#include <fabgl/rendering/raycast_map_asset.h>
#include <fabgl/scene/scene.h>
#include <fabgl/serialization/scene_serializer.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fabgl::project {
namespace {

constexpr std::size_t MaximumAssetCount = 1024U;
constexpr std::size_t MaximumEmbeddedPayloadBytes = 2U * 1024U * 1024U;
constexpr std::size_t MaximumExternalPayloadBytes = 64U * 1024U * 1024U;
constexpr std::size_t MaximumTemplateBytes = 4U * 1024U * 1024U;

enum class NodeType { Missing, RegularFile, Directory, Link, Other };

struct SourceFile final {
    std::string relativePath;
    std::vector<std::uint8_t> bytes;
};

std::string normalizePath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    while (path.size() > 1U && path.back() == '/') {
#ifdef _WIN32
        if (path.size() == 3U && path[1] == ':')
            break;
#endif
        path.pop_back();
    }
    return path;
}

std::string joinPath(const std::string& left, const std::string& right) {
    if (left.empty())
        return normalizePath(right);
    if (right.empty())
        return normalizePath(left);
    return normalizePath(left + (left.back() == '/' || left.back() == '\\' ? "" : "/") + right);
}

std::string parentPath(const std::string& input) {
    const auto path = normalizePath(input);
    const auto separator = path.find_last_of('/');
    if (separator == std::string::npos)
        return ".";
    if (separator == 0U)
        return "/";
#ifdef _WIN32
    if (separator == 2U && path[1] == ':')
        return path.substr(0U, 3U);
#endif
    return path.substr(0U, separator);
}

std::string fileName(const std::string& input) {
    const auto path = normalizePath(input);
    const auto separator = path.find_last_of('/');
    return separator == std::string::npos ? path : path.substr(separator + 1U);
}

Error pathError(ErrorCode code, std::string message, const std::string& path,
                std::string detail = {}) {
    auto error = Error(code, std::move(message)).addContext("path", path);
    if (!detail.empty())
        error.addContext("filesystem", std::move(detail));
    return error;
}

#ifdef _WIN32

Result<std::wstring> toWide(std::string_view value) {
    if (value.find('\0') != std::string_view::npos) {
        return Result<std::wstring>::failure(
            Error(ErrorCode::InvalidArgument, "path contains NUL"));
    }
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return Result<std::wstring>::failure(
            Error(ErrorCode::CapacityExceeded, "path is too long"));
    }
    const auto length = static_cast<int>(value.size());
    const auto count =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), length, nullptr, 0);
    if (count <= 0) {
        return Result<std::wstring>::failure(
            Error(ErrorCode::InvalidArgument, "path is not valid UTF-8")
                .addContext("win32", std::to_string(GetLastError())));
    }
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), length, result.data(),
                            count) <= 0) {
        return Result<std::wstring>::failure(
            Error(ErrorCode::InvalidArgument, "path conversion failed")
                .addContext("win32", std::to_string(GetLastError())));
    }
    return Result<std::wstring>::success(std::move(result));
}

Result<std::string> fromWide(std::wstring_view value) {
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return Result<std::string>::failure(Error(ErrorCode::CapacityExceeded, "path is too long"));
    }
    const auto length = static_cast<int>(value.size());
    const auto count =
        WideCharToMultiByte(CP_UTF8, 0, value.data(), length, nullptr, 0, nullptr, nullptr);
    if (count <= 0) {
        return Result<std::string>::failure(
            Error(ErrorCode::InvalidArgument, "filesystem path is not valid Unicode")
                .addContext("win32", std::to_string(GetLastError())));
    }
    std::string result(static_cast<std::size_t>(count), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, value.data(), length, result.data(), count, nullptr,
                            nullptr) <= 0) {
        return Result<std::string>::failure(
            Error(ErrorCode::InvalidArgument, "filesystem path conversion failed")
                .addContext("win32", std::to_string(GetLastError())));
    }
    return Result<std::string>::success(normalizePath(std::move(result)));
}

Result<std::string> absolutePath(const std::string& input) {
    auto wide = toWide(input);
    if (!wide)
        return Result<std::string>::failure(wide.error());
    const auto required = GetFullPathNameW(wide.value().c_str(), 0, nullptr, nullptr);
    if (required == 0U) {
        return Result<std::string>::failure(pathError(ErrorCode::IoError,
                                                      "unable to resolve absolute path", input,
                                                      std::to_string(GetLastError())));
    }
    std::wstring resolved(static_cast<std::size_t>(required), L'\0');
    const auto written = GetFullPathNameW(wide.value().c_str(), required, resolved.data(), nullptr);
    if (written == 0U || written >= required) {
        return Result<std::string>::failure(pathError(ErrorCode::IoError,
                                                      "unable to resolve absolute path", input,
                                                      std::to_string(GetLastError())));
    }
    resolved.resize(static_cast<std::size_t>(written));
    return fromWide(resolved);
}

Result<NodeType> inspectNode(const std::string& path) {
    auto wide = toWide(path);
    if (!wide)
        return Result<NodeType>::failure(wide.error());
    const auto attributes = GetFileAttributesW(wide.value().c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const auto code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
            return Result<NodeType>::success(NodeType::Missing);
        return Result<NodeType>::failure(pathError(
            ErrorCode::IoError, "unable to inspect filesystem entry", path, std::to_string(code)));
    }
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U)
        return Result<NodeType>::success(NodeType::Link);
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U)
        return Result<NodeType>::success(NodeType::Directory);
    if ((attributes & FILE_ATTRIBUTE_DEVICE) != 0U)
        return Result<NodeType>::success(NodeType::Other);
    return Result<NodeType>::success(NodeType::RegularFile);
}

Result<std::vector<std::string>> listDirectory(const std::string& path) {
    auto wide = toWide(joinPath(path, "*"));
    if (!wide)
        return Result<std::vector<std::string>>::failure(wide.error());
    WIN32_FIND_DATAW data{};
    const auto handle = FindFirstFileW(wide.value().c_str(), &data);
    if (handle == INVALID_HANDLE_VALUE) {
        return Result<std::vector<std::string>>::failure(
            pathError(ErrorCode::IoError, "unable to enumerate directory", path,
                      std::to_string(GetLastError())));
    }
    std::vector<std::string> names;
    for (;;) {
        const std::wstring_view name(data.cFileName);
        if (name != L"." && name != L"..") {
            auto encoded = fromWide(name);
            if (!encoded) {
                FindClose(handle);
                return Result<std::vector<std::string>>::failure(encoded.error());
            }
            names.push_back(std::move(encoded.value()));
        }
        if (FindNextFileW(handle, &data) == FALSE) {
            const auto code = GetLastError();
            FindClose(handle);
            if (code != ERROR_NO_MORE_FILES) {
                return Result<std::vector<std::string>>::failure(
                    pathError(ErrorCode::IoError, "directory enumeration failed", path,
                              std::to_string(code)));
            }
            break;
        }
    }
    return Result<std::vector<std::string>>::success(std::move(names));
}

Result<bool> createDirectoryExclusive(const std::string& path) {
    auto wide = toWide(path);
    if (!wide)
        return Result<bool>::failure(wide.error());
    if (CreateDirectoryW(wide.value().c_str(), nullptr) != FALSE)
        return Result<bool>::success(true);
    const auto code = GetLastError();
    if (code == ERROR_ALREADY_EXISTS)
        return Result<bool>::success(false);
    return Result<bool>::failure(
        pathError(ErrorCode::IoError, "unable to create directory", path, std::to_string(code)));
}

void removeFileNoThrow(const std::string& path) noexcept {
    auto wide = toWide(path);
    if (wide)
        DeleteFileW(wide.value().c_str());
}

void removeDirectoryNoThrow(const std::string& path) noexcept {
    auto wide = toWide(path);
    if (wide)
        RemoveDirectoryW(wide.value().c_str());
}

Result<void> renameDirectoryNoReplace(const std::string& source, const std::string& destination) {
    auto sourceWide = toWide(source);
    auto destinationWide = toWide(destination);
    if (!sourceWide)
        return Result<void>::failure(sourceWide.error());
    if (!destinationWide)
        return Result<void>::failure(destinationWide.error());
    DWORD code = ERROR_SUCCESS;
    for (auto attempt = 0; attempt < 24; ++attempt) {
        if (GetFileAttributesW(destinationWide.value().c_str()) != INVALID_FILE_ATTRIBUTES) {
            return Result<void>::failure(
                Error(ErrorCode::AlreadyExists, "refusing to overwrite an existing export path")
                    .addContext("path", destination));
        }
        if (MoveFileExW(sourceWide.value().c_str(), destinationWide.value().c_str(),
                        MOVEFILE_WRITE_THROUGH) != FALSE) {
            return Result<void>::success();
        }
        code = GetLastError();
        if (code != ERROR_ACCESS_DENIED && code != ERROR_SHARING_VIOLATION &&
            code != ERROR_LOCK_VIOLATION) {
            break;
        }
        Sleep(static_cast<DWORD>(std::min(250, 25 * (attempt + 1))));
    }
    return Result<void>::failure(pathError(ErrorCode::IoError, "unable to commit ESP32 export",
                                           destination, std::to_string(code)));
}

#else

Result<std::string> absolutePath(const std::string& input) {
    char* resolved = ::realpath(input.c_str(), nullptr);
    if (resolved == nullptr) {
        return Result<std::string>::failure(
            pathError(errno == ENOENT ? ErrorCode::NotFound : ErrorCode::IoError,
                      "unable to resolve absolute path", input, std::to_string(errno)));
    }
    std::string result(resolved);
    std::free(resolved);
    return Result<std::string>::success(normalizePath(std::move(result)));
}

Result<NodeType> inspectNode(const std::string& path) {
    struct stat status {};
    if (::lstat(path.c_str(), &status) != 0) {
        if (errno == ENOENT || errno == ENOTDIR)
            return Result<NodeType>::success(NodeType::Missing);
        return Result<NodeType>::failure(pathError(
            ErrorCode::IoError, "unable to inspect filesystem entry", path, std::to_string(errno)));
    }
    if (S_ISLNK(status.st_mode))
        return Result<NodeType>::success(NodeType::Link);
    if (S_ISREG(status.st_mode))
        return Result<NodeType>::success(NodeType::RegularFile);
    if (S_ISDIR(status.st_mode))
        return Result<NodeType>::success(NodeType::Directory);
    return Result<NodeType>::success(NodeType::Other);
}

Result<std::vector<std::string>> listDirectory(const std::string& path) {
    auto* directory = ::opendir(path.c_str());
    if (directory == nullptr) {
        return Result<std::vector<std::string>>::failure(pathError(
            ErrorCode::IoError, "unable to enumerate directory", path, std::to_string(errno)));
    }
    std::vector<std::string> names;
    errno = 0;
    while (const auto* entry = ::readdir(directory)) {
        const std::string name(entry->d_name);
        if (name != "." && name != "..")
            names.push_back(name);
        errno = 0;
    }
    const auto code = errno;
    ::closedir(directory);
    if (code != 0) {
        return Result<std::vector<std::string>>::failure(pathError(
            ErrorCode::IoError, "directory enumeration failed", path, std::to_string(code)));
    }
    return Result<std::vector<std::string>>::success(std::move(names));
}

Result<bool> createDirectoryExclusive(const std::string& path) {
    if (::mkdir(path.c_str(), 0755) == 0)
        return Result<bool>::success(true);
    if (errno == EEXIST)
        return Result<bool>::success(false);
    return Result<bool>::failure(
        pathError(ErrorCode::IoError, "unable to create directory", path, std::to_string(errno)));
}

void removeFileNoThrow(const std::string& path) noexcept {
    ::unlink(path.c_str());
}

void removeDirectoryNoThrow(const std::string& path) noexcept {
    ::rmdir(path.c_str());
}

Result<void> renameDirectoryNoReplace(const std::string& source, const std::string& destination) {
    auto destinationType = inspectNode(destination);
    if (!destinationType)
        return Result<void>::failure(destinationType.error());
    if (destinationType.value() != NodeType::Missing) {
        return Result<void>::failure(
            Error(ErrorCode::AlreadyExists, "refusing to overwrite an existing export path")
                .addContext("path", destination));
    }
    if (::rename(source.c_str(), destination.c_str()) != 0) {
        return Result<void>::failure(pathError(ErrorCode::IoError, "unable to commit ESP32 export",
                                               destination, std::to_string(errno)));
    }
    return Result<void>::success();
}

#endif

void removeTreeNoThrow(const std::string& root) noexcept {
    auto type = inspectNode(root);
    if (!type || type.value() == NodeType::Missing)
        return;
    if (type.value() != NodeType::Directory) {
        removeFileNoThrow(root);
        return;
    }
    auto children = listDirectory(root);
    if (children) {
        for (const auto& child : children.value())
            removeTreeNoThrow(joinPath(root, child));
    }
    removeDirectoryNoThrow(root);
}

class StagingGuard final {
  public:
    explicit StagingGuard(std::string path) : path_(std::move(path)) {}
    ~StagingGuard() {
        if (active_)
            removeTreeNoThrow(path_);
    }

    StagingGuard(const StagingGuard&) = delete;
    StagingGuard& operator=(const StagingGuard&) = delete;

    void release() noexcept {
        active_ = false;
    }

  private:
    std::string path_;
    bool active_ = true;
};

Result<std::string> canonicalNode(const std::string& input, NodeType expected,
                                  std::string_view description) {
    auto absolute = absolutePath(input);
    if (!absolute)
        return Result<std::string>::failure(absolute.error());
    auto type = inspectNode(absolute.value());
    if (!type)
        return Result<std::string>::failure(type.error());
    if (type.value() == NodeType::Missing) {
        return Result<std::string>::failure(pathError(
            ErrorCode::NotFound, std::string(description) + " was not found", absolute.value()));
    }
    if (type.value() == NodeType::Link) {
        return Result<std::string>::failure(
            pathError(ErrorCode::InvalidArgument,
                      std::string(description) + " cannot be a symbolic link", absolute.value()));
    }
    if (type.value() != expected) {
        return Result<std::string>::failure(pathError(
            ErrorCode::InvalidArgument, std::string(description) + " has the wrong filesystem type",
            absolute.value()));
    }
    return absolute;
}

std::string comparablePath(std::string path) {
    path = normalizePath(std::move(path));
#ifdef _WIN32
    std::transform(path.begin(), path.end(), path.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
#endif
    return path;
}

bool isWithin(const std::string& root, const std::string& candidate) {
    const auto comparableRoot = comparablePath(root);
    const auto comparableCandidate = comparablePath(candidate);
    return comparableCandidate == comparableRoot ||
           (comparableCandidate.size() > comparableRoot.size() &&
            comparableCandidate.compare(0U, comparableRoot.size(), comparableRoot) == 0 &&
            comparableCandidate[comparableRoot.size()] == '/');
}

Result<std::string> resolveContainedFile(const std::string& root, const std::string& relativeInput,
                                         std::string_view description) {
    if (!assets::isSafeRelativePath(relativeInput)) {
        return Result<std::string>::failure(
            Error(ErrorCode::InvalidArgument, std::string(description) + " path is unsafe")
                .addContext("path", relativeInput));
    }
    const auto relative = normalizePath(relativeInput);
    std::string current = root;
    std::size_t start = 0;
    while (start < relative.size()) {
        const auto separator = relative.find('/', start);
        const auto segment = relative.substr(
            start, separator == std::string::npos ? std::string::npos : separator - start);
        current = joinPath(current, segment);
        auto type = inspectNode(current);
        if (!type)
            return Result<std::string>::failure(type.error());
        if (type.value() == NodeType::Link) {
            return Result<std::string>::failure(
                Error(ErrorCode::InvalidArgument,
                      std::string(description) + " path cannot contain symbolic links")
                    .addContext("path", relativeInput));
        }
        if (type.value() == NodeType::Missing) {
            return Result<std::string>::failure(pathError(
                ErrorCode::NotFound, std::string(description) + " was not found", current));
        }
        if (separator == std::string::npos)
            break;
        if (type.value() != NodeType::Directory) {
            return Result<std::string>::failure(
                pathError(ErrorCode::InvalidArgument,
                          std::string(description) + " path crosses a non-directory", current));
        }
        start = separator + 1U;
    }
    auto file = canonicalNode(current, NodeType::RegularFile, description);
    if (!file)
        return file;
    if (!isWithin(root, file.value())) {
        return Result<std::string>::failure(
            Error(ErrorCode::InvalidArgument,
                  std::string(description) + " resolves outside the project root")
                .addContext("path", relativeInput));
    }
    return file;
}

Result<void> enumerateFiles(const std::string& root, const std::string& relativeDirectory,
                            std::vector<std::pair<std::string, std::string>>& paths,
                            std::string_view description) {
    const auto directory = relativeDirectory.empty() ? root : joinPath(root, relativeDirectory);
    auto names = listDirectory(directory);
    if (!names)
        return Result<void>::failure(names.error());
    std::sort(names.value().begin(), names.value().end());
    for (const auto& name : names.value()) {
        const auto relative = relativeDirectory.empty() ? name : joinPath(relativeDirectory, name);
        if (!assets::isSafeRelativePath(relative)) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument,
                      std::string(description) + " contains an unsafe relative path")
                    .addContext("path", relative));
        }
        const auto fullPath = joinPath(root, relative);
        auto type = inspectNode(fullPath);
        if (!type)
            return Result<void>::failure(type.error());
        if (type.value() == NodeType::Link) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument,
                      std::string(description) + " cannot contain symbolic links")
                    .addContext("path", fullPath));
        }
        if (type.value() == NodeType::Directory) {
            auto nested = enumerateFiles(root, relative, paths, description);
            if (!nested)
                return nested;
        } else if (type.value() == NodeType::RegularFile) {
            paths.emplace_back(relative, fullPath);
            if (paths.size() > MaximumAssetCount) {
                return Result<void>::failure(
                    Error(ErrorCode::CapacityExceeded,
                          std::string(description) + " contains too many files")
                        .addContext("maximum", std::to_string(MaximumAssetCount)));
            }
        } else {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument,
                      std::string(description) + " contains a non-regular filesystem entry")
                    .addContext("path", fullPath));
        }
    }
    return Result<void>::success();
}

Result<std::vector<SourceFile>> collectRegularFiles(const std::string& rootInput,
                                                    std::string_view description, bool allowMissing,
                                                    std::size_t maximumBytes) {
    auto type = inspectNode(rootInput);
    if (!type)
        return Result<std::vector<SourceFile>>::failure(type.error());
    if (type.value() == NodeType::Missing && allowMissing)
        return Result<std::vector<SourceFile>>::success({});
    if (type.value() == NodeType::Missing) {
        return Result<std::vector<SourceFile>>::failure(pathError(
            ErrorCode::NotFound, std::string(description) + " directory was not found", rootInput));
    }
    if (type.value() == NodeType::Link || type.value() != NodeType::Directory) {
        return Result<std::vector<SourceFile>>::failure(pathError(
            ErrorCode::InvalidArgument,
            std::string(description) + " must be a real directory, not a link", rootInput));
    }
    auto root = canonicalNode(rootInput, NodeType::Directory, description);
    if (!root)
        return Result<std::vector<SourceFile>>::failure(root.error());

    std::vector<std::pair<std::string, std::string>> paths;
    auto enumerated = enumerateFiles(root.value(), {}, paths, description);
    if (!enumerated)
        return Result<std::vector<SourceFile>>::failure(enumerated.error());
    std::sort(paths.begin(), paths.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

    std::vector<SourceFile> files;
    files.reserve(paths.size());
    std::size_t totalBytes = 0;
    for (const auto& [relative, path] : paths) {
        auto bytes = assets::readBinaryFile(path);
        if (!bytes) {
            return Result<std::vector<SourceFile>>::failure(
                bytes.error()
                    .withContext("source", std::string(description))
                    .withContext("relative_path", relative));
        }
        if (bytes.value().size() > maximumBytes - std::min(totalBytes, maximumBytes)) {
            return Result<std::vector<SourceFile>>::failure(
                Error(ErrorCode::CapacityExceeded,
                      std::string(description) + " exceeds the embedded size limit")
                    .addContext("maximum_bytes", std::to_string(maximumBytes)));
        }
        totalBytes += bytes.value().size();
        files.push_back({relative, std::move(bytes.value())});
    }
    return Result<std::vector<SourceFile>>::success(std::move(files));
}

void appendU16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    const auto wide = static_cast<std::uint32_t>(value);
    output.push_back(static_cast<std::uint8_t>(wide & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((wide >> 8U) & 0xFFU));
}

Result<std::vector<std::uint8_t>> wrapAssetPayload(const SourceFile& asset) {
    const auto& embeddedPath = asset.relativePath;
    if (embeddedPath.size() > 65535U) {
        return Result<std::vector<std::uint8_t>>::failure(
            Error(ErrorCode::CapacityExceeded, "asset relative path is too long")
                .addContext("path", embeddedPath));
    }
    std::vector<std::uint8_t> payload;
    payload.reserve(8U + embeddedPath.size() + asset.bytes.size());
    payload.insert(payload.end(), {'F', 'G', 'L', 'A'});
    appendU16(payload, 1U);
    appendU16(payload, static_cast<std::uint16_t>(embeddedPath.size()));
    payload.insert(payload.end(), embeddedPath.begin(), embeddedPath.end());
    payload.insert(payload.end(), asset.bytes.begin(), asset.bytes.end());
    return Result<std::vector<std::uint8_t>>::success(std::move(payload));
}

bool validSketchName(std::string_view name) {
    if (name.empty() || name.size() > 63U)
        return false;
    const auto validInitial = [](unsigned char character) {
        return std::isalnum(character) != 0 || character == '_';
    };
    if (!validInitial(static_cast<unsigned char>(name.front())))
        return false;
    for (const auto character : name) {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte) == 0 && character != '_' && character != '-')
            return false;
    }
    return true;
}

std::string lowercaseAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string cppStringLiteral(std::string_view value) {
    std::ostringstream output;
    output << '"';
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (character == '"' || character == '\\') {
            output << '\\' << character;
        } else if (byte >= 0x20U && byte <= 0x7EU) {
            output << character;
        } else {
            const auto wide = static_cast<unsigned int>(byte);
            output << '\\' << static_cast<char>('0' + ((wide >> 6U) & 0x07U))
                   << static_cast<char>('0' + ((wide >> 3U) & 0x07U))
                   << static_cast<char>('0' + (wide & 0x07U));
        }
    }
    output << '"';
    return output.str();
}

std::string jsonStringLiteral(std::string_view value) {
    constexpr char HexDigits[] = "0123456789abcdef";
    std::ostringstream output;
    output << '"';
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        switch (character) {
        case '"':
            output << "\\\"";
            break;
        case '\\':
            output << "\\\\";
            break;
        case '\b':
            output << "\\b";
            break;
        case '\f':
            output << "\\f";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            if (byte < 0x20U) {
                const auto wide = static_cast<unsigned int>(byte);
                output << "\\u00" << HexDigits[(wide >> 4U) & 0x0FU] << HexDigits[wide & 0x0FU];
            } else {
                output << character;
            }
            break;
        }
    }
    output << '"';
    return output.str();
}

std::string hexadecimal64(std::uint64_t value) {
    std::ostringstream output;
    output << "0x" << std::hex << std::setw(16) << std::setfill('0') << value;
    return output.str();
}

std::string generateExportResult(const Manifest& manifest, std::string_view sketchFileName,
                                 std::size_t entityCount, std::size_t assetCount,
                                 std::size_t portableScriptFileCount, const assets::AssetPack& pack,
                                 std::uint64_t payloadChecksum,
                                 const assets::AssetPack* externalPack,
                                 std::uint64_t externalPayloadChecksum) {
    std::ostringstream output;
    output << "{\n"
           << "  \"schemaVersion\": 1,\n"
           << "  \"kind\": \"FabGLStudioEsp32Export\",\n"
           << "  \"projectName\": " << jsonStringLiteral(manifest.name) << ",\n"
           << "  \"previewDemo\": " << jsonStringLiteral(manifest.previewDemo) << ",\n"
           << "  \"sketchFileName\": " << jsonStringLiteral(sketchFileName) << ",\n"
           << "  \"sceneFormatVersion\": " << SceneSerializer::CurrentVersion << ",\n"
           << "  \"entityCount\": " << entityCount << ",\n"
           << "  \"assetCount\": " << assetCount << ",\n"
           << "  \"portableScriptFileCount\": " << portableScriptFileCount << ",\n"
           << "  \"scriptRuntime\": " << (portableScriptFileCount == 0U ? "false" : "true") << ",\n"
           << "  \"payloadSize\": " << pack.bytes.size() << ",\n"
           << "  \"payloadChecksum\": " << jsonStringLiteral(hexadecimal64(payloadChecksum))
           << ",\n"
           << "  \"packBuildChecksum\": " << jsonStringLiteral(hexadecimal64(pack.buildChecksum))
           << ",\n  \"externalAssetCount\": "
           << (externalPack == nullptr ? 0U : externalPack->index.size())
           << ",\n  \"externalPayloadSize\": "
           << (externalPack == nullptr ? 0U : externalPack->bytes.size())
           << ",\n  \"externalPayloadChecksum\": "
           << jsonStringLiteral(hexadecimal64(externalPayloadChecksum))
           << ",\n  \"externalPackBuildChecksum\": "
           << jsonStringLiteral(hexadecimal64(externalPack == nullptr ? 0U
                                                                       : externalPack->buildChecksum))
           << "\n"
           << "}\n";
    return output.str();
}

std::string generatePayloadHeader(const Manifest& manifest, std::size_t entityCount,
                                  std::size_t assetCount, const assets::AssetPack& pack,
                                  std::uint64_t payloadChecksum,
                                  const assets::AssetPack* externalPack,
                                  std::uint64_t externalPayloadChecksum) {
    std::ostringstream output;
    output << "#pragma once\n\n"
              "#include <Arduino.h>\n\n"
              "namespace fabgl_project_payload {\n\n"
           << "static constexpr char kProjectName[] = " << cppStringLiteral(manifest.name) << ";\n"
           << "static constexpr char kPreviewDemo[] = " << cppStringLiteral(manifest.previewDemo)
           << ";\n"
           << "static constexpr uint32_t kEntityCount = " << entityCount << "U;\n"
           << "static constexpr uint32_t kAssetCount = " << assetCount << "U;\n"
           << "static constexpr uint64_t kPayloadChecksum = 0x" << std::hex << std::setw(16)
           << std::setfill('0') << payloadChecksum << "ULL;\n"
           << "static constexpr uint64_t kPackBuildChecksum = 0x" << std::setw(16)
           << pack.buildChecksum
           << "ULL;\n"
           << "static constexpr uint32_t kExternalAssetCount = " << std::dec
           << (externalPack == nullptr ? 0U : externalPack->index.size()) << "U;\n"
           << "static constexpr uint64_t kExternalPayloadChecksum = 0x" << std::hex
           << std::setw(16) << externalPayloadChecksum << "ULL;\n"
           << "static constexpr uint64_t kExternalPackBuildChecksum = 0x" << std::setw(16)
           << (externalPack == nullptr ? 0U : externalPack->buildChecksum)
           << "ULL;\n\n"
              "static const uint8_t kData[] PROGMEM = {";
    for (std::size_t index = 0; index < pack.bytes.size(); ++index) {
        if (index % 12U == 0U)
            output << "\n    ";
        output << "0x" << std::setw(2) << static_cast<unsigned int>(pack.bytes[index]);
        if (index + 1U != pack.bytes.size())
            output << ", ";
    }
    output << "\n};\n"
              "static constexpr size_t kPayloadSize = sizeof(kData);\n\n"
              "} // namespace fabgl_project_payload\n";
    return output.str();
}

Result<std::string> createStagingDirectory(const std::string& parent, std::string_view sketchName) {
    for (int attempt = 0; attempt < 4; ++attempt) {
        const auto staging =
            joinPath(parent, std::string(".fabgl-export-") + std::string(sketchName) + "-" +
                                 AssetGuid::generate().toString());
        auto created = createDirectoryExclusive(staging);
        if (!created)
            return Result<std::string>::failure(created.error());
        if (created.value())
            return Result<std::string>::success(staging);
    }
    return Result<std::string>::failure(
        Error(ErrorCode::AlreadyExists, "unable to allocate a unique export staging directory"));
}

Result<void> writeStagedFile(const std::string& staging, const SourceFile& file) {
    const auto target = joinPath(staging, file.relativePath);
    auto directories = assets::createDirectories(parentPath(target));
    if (!directories)
        return Result<void>::failure(directories.error());
    auto written = assets::writeBinaryFileAtomic(target, file.bytes);
    if (!written)
        return Result<void>::failure(
            written.error().withContext("relative_path", file.relativePath));
    return Result<void>::success();
}

Result<std::string> prepareOutputPath(const std::string& outputInput, std::string& sketchName) {
    const auto normalized = normalizePath(outputInput);
    sketchName = fileName(normalized);
    if (!validSketchName(sketchName)) {
        return Result<std::string>::failure(
            Error(ErrorCode::InvalidArgument,
                  "output folder must be a valid Arduino sketch name (A-Z, 0-9, _, -; max 63)")
                .addContext("name", sketchName));
    }
    auto parentCreated = assets::createDirectories(parentPath(normalized));
    if (!parentCreated)
        return Result<std::string>::failure(parentCreated.error());
    auto canonicalParent =
        canonicalNode(parentPath(normalized), NodeType::Directory, "export parent directory");
    if (!canonicalParent)
        return Result<std::string>::failure(canonicalParent.error());
    return Result<std::string>::success(joinPath(canonicalParent.value(), sketchName));
}

} // namespace

Result<std::vector<Esp32PortableScriptSource>>
collectEsp32PortableScriptSources(const std::string& projectRootDirectory) {
    auto projectRoot = canonicalNode(projectRootDirectory, NodeType::Directory, "project root");
    if (!projectRoot)
        return Result<std::vector<Esp32PortableScriptSource>>::failure(projectRoot.error());

    auto scriptFiles = collectRegularFiles(joinPath(projectRoot.value(), "Scripts"), "Scripts",
                                           true, MaximumTemplateBytes);
    if (!scriptFiles)
        return Result<std::vector<Esp32PortableScriptSource>>::failure(scriptFiles.error());

    std::vector<Esp32PortableScriptSource> portableScripts;
    bool hasDesktopGameplaySource = false;
    bool hasPortableModuleEntry = false;
    for (auto& script : scriptFiles.value()) {
        const auto path = lowercaseAscii(script.relativePath);
        const auto separator = path.find_last_of('.');
        const auto extension =
            separator == std::string::npos ? std::string() : path.substr(separator);
        const bool cFamily = extension == ".c" || extension == ".cc" || extension == ".cpp" ||
                             extension == ".cxx" || extension == ".h" || extension == ".hh" ||
                             extension == ".hpp" || extension == ".hxx" || extension == ".ino";
        if (!cFamily)
            continue;
        const bool portable = path.rfind("esp32/", 0U) == 0U;
        if (!portable) {
            hasDesktopGameplaySource = true;
            continue;
        }
        if (extension == ".ino" || script.relativePath.size() <= 6U) {
            return Result<std::vector<Esp32PortableScriptSource>>::failure(
                Error(ErrorCode::InvalidArgument,
                      "portable ESP32 scripts must be C/C++ sources or headers, not sketches")
                    .addContext("path", std::string("Scripts/") + script.relativePath));
        }
        const std::string sourceText(script.bytes.begin(), script.bytes.end());
        hasPortableModuleEntry =
            hasPortableModuleEntry ||
            sourceText.find("fabglProjectGetEsp32ScriptsV1") != std::string::npos ||
            sourceText.find("FGL_ESP32_SCRIPT_MODULE") != std::string::npos;
        portableScripts.push_back({std::move(script.relativePath), std::move(script.bytes)});
    }
    if (hasDesktopGameplaySource && portableScripts.empty()) {
        return Result<std::vector<Esp32PortableScriptSource>>::failure(
            Error(ErrorCode::InvalidState,
                  "desktop gameplay scripts require a portable Scripts/ESP32 module for ESP32 "
                  "export"));
    }
    if (!portableScripts.empty() && !hasPortableModuleEntry) {
        return Result<std::vector<Esp32PortableScriptSource>>::failure(
            Error(ErrorCode::InvalidFormat,
                  "portable ESP32 scripts do not export fabglProjectGetEsp32ScriptsV1"));
    }
    return Result<std::vector<Esp32PortableScriptSource>>::success(std::move(portableScripts));
}

Result<Esp32ExportSummary> exportEsp32Project(const std::string& projectManifestPath,
                                              const std::string& firmwareTemplateDirectory,
                                              const std::string& outputSketchDirectory) {
    auto projectFile =
        canonicalNode(projectManifestPath, NodeType::RegularFile, "project manifest");
    if (!projectFile)
        return Result<Esp32ExportSummary>::failure(projectFile.error());
    auto projectRoot =
        canonicalNode(parentPath(projectFile.value()), NodeType::Directory, "project root");
    if (!projectRoot)
        return Result<Esp32ExportSummary>::failure(projectRoot.error());

    auto manifestSource = assets::readTextFile(projectFile.value());
    if (!manifestSource)
        return Result<Esp32ExportSummary>::failure(manifestSource.error());
    auto manifest = parseManifest(manifestSource.value());
    if (!manifest) {
        return Result<Esp32ExportSummary>::failure(
            manifest.error().withContext("path", projectFile.value()));
    }
    const std::string legacyAssetIndex = ".fabglstudio/asset-index-v1.json";
    auto legacyType = inspectNode(joinPath(projectRoot.value(), legacyAssetIndex));
    if (!legacyType)
        return Result<Esp32ExportSummary>::failure(legacyType.error());
    if (legacyType.value() != NodeType::Missing) {
        auto legacyPath =
            resolveContainedFile(projectRoot.value(), legacyAssetIndex, "legacy asset index");
        if (!legacyPath)
            return Result<Esp32ExportSummary>::failure(legacyPath.error());
        auto legacySource = assets::readTextFile(legacyPath.value());
        if (!legacySource)
            return Result<Esp32ExportSummary>::failure(legacySource.error());
        auto migrated = mergeLegacyAssetIndex(legacySource.value(), manifest.value());
        if (!migrated) {
            return Result<Esp32ExportSummary>::failure(
                migrated.error().withContext("path", legacyPath.value()));
        }
    }
    if (manifest.value().sourceVersion != Manifest::CurrentVersion) {
        manifest.value().sourceVersion = Manifest::CurrentVersion;
        manifest.value().projectGuid =
            AssetGuid::fromStableName("legacy-project:" + manifest.value().name).toString();
    }
    auto canonicalManifest = serializeManifest(manifest.value());
    if (!canonicalManifest)
        return Result<Esp32ExportSummary>::failure(canonicalManifest.error());

    auto sceneFile =
        resolveContainedFile(projectRoot.value(), manifest.value().startupScene, "startup scene");
    if (!sceneFile)
        return Result<Esp32ExportSummary>::failure(sceneFile.error());
    auto sceneSource = assets::readTextFile(sceneFile.value());
    if (!sceneSource)
        return Result<Esp32ExportSummary>::failure(sceneSource.error());
    auto scene = SceneSerializer::deserialize(sceneSource.value());
    if (!scene) {
        return Result<Esp32ExportSummary>::failure(
            scene.error().withContext("path", sceneFile.value()));
    }
    auto capabilities = validateEsp32TargetCapabilities(manifest.value(), *scene.value());
    if (!capabilities) {
        return Result<Esp32ExportSummary>::failure(
            capabilities.error().withContext("scene_path", sceneFile.value()));
    }
    auto canonicalScene = SceneSerializer::serialize(*scene.value());
    if (!canonicalScene)
        return Result<Esp32ExportSummary>::failure(canonicalScene.error());

    auto assetFiles = collectRegularFiles(joinPath(projectRoot.value(), "Assets"), "Assets", true,
                                          MaximumExternalPayloadBytes);
    if (!assetFiles)
        return Result<Esp32ExportSummary>::failure(assetFiles.error());
    auto collectedScripts = collectEsp32PortableScriptSources(projectRoot.value());
    if (!collectedScripts)
        return Result<Esp32ExportSummary>::failure(collectedScripts.error());
    std::vector<SourceFile> portableScriptFiles;
    portableScriptFiles.reserve(collectedScripts.value().size());
    for (auto& script : collectedScripts.value()) {
        portableScriptFiles.push_back(
            {std::string("src/ProjectScripts/") + script.relativePath.substr(6U),
             std::move(script.bytes)});
    }

    struct EmbeddedAsset final {
        AssetGuid guid;
        SourceFile file;
        assets::StorageClass storage = assets::StorageClass::Flash;
    };
    std::vector<EmbeddedAsset> embeddedAssets;
    embeddedAssets.reserve(manifest.value().assets.size() + assetFiles.value().size());
    std::set<std::string> embeddedPaths;
    for (const auto& declared : manifest.value().assets) {
        auto source = resolveContainedFile(projectRoot.value(), declared.path, "project asset");
        if (!source)
            return Result<Esp32ExportSummary>::failure(source.error());
        auto bytes = assets::readBinaryFile(source.value());
        if (!bytes)
            return Result<Esp32ExportSummary>::failure(bytes.error());
        const auto normalizedPath = lowercaseAscii(declared.path);
        const auto dot = normalizedPath.find_last_of('.');
        const auto extension =
            dot == std::string::npos ? std::string{} : normalizedPath.substr(dot);
        if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
            extension == ".bmp") {
            auto settings = decodeProjectImageImportSettings(declared.importSettings);
            if (!settings)
                return Result<Esp32ExportSummary>::failure(settings.error());
            auto image = assets::loadImage(source.value());
            if (!image)
                return Result<Esp32ExportSummary>::failure(image.error());
            auto compiled = assets::compileImageAsset(image.value(), settings.value());
            if (!compiled)
                return Result<Esp32ExportSummary>::failure(compiled.error());
            bytes.value() = std::move(compiled.value().payload);
        } else if (extension == ".wav") {
            auto settings = decodeProjectAudioImportSettings(declared.importSettings);
            if (!settings)
                return Result<Esp32ExportSummary>::failure(settings.error());
            auto audio = assets::importWav(bytes.value(), settings.value().settings);
            if (!audio)
                return Result<Esp32ExportSummary>::failure(audio.error());
            bytes.value() = assets::encodeAudioClip(audio.value(), settings.value().encoding);
        }
        if (declared.type == "image") {
            auto image = assets::decodeIndexedImage(bytes.value());
            if (!image) {
                return Result<Esp32ExportSummary>::failure(
                    image.error().withContext("asset", declared.path));
            }
            if (image.value().width > 512 || image.value().height > 512) {
                return Result<Esp32ExportSummary>::failure(
                    Error(ErrorCode::CapacityExceeded,
                          "indexed image exceeds the ESP32 runtime dimension limit")
                        .addContext("asset", declared.path)
                        .addContext("width", std::to_string(image.value().width))
                        .addContext("height", std::to_string(image.value().height))
                        .addContext("maximum", "512"));
            }
        } else if (declared.type == "raycast.map") {
            const std::string text(bytes.value().begin(), bytes.value().end());
            auto map = rendering::deserializeRaycastMapAsset(text);
            if (!map) {
                return Result<Esp32ExportSummary>::failure(
                    map.error().withContext("asset", declared.path));
            }
            if (map.value().guid != declared.guid) {
                return Result<Esp32ExportSummary>::failure(
                    Error(ErrorCode::TypeMismatch, "raycast map GUID does not match its manifest")
                        .addContext("asset", declared.path)
                        .addContext("declared_guid", declared.guid.toString())
                        .addContext("content_guid", map.value().guid.toString()));
            }
            if (map.value().map.width > 32 || map.value().map.height > 32 ||
                map.value().map.wallPalette.size() > 32U) {
                return Result<Esp32ExportSummary>::failure(
                    Error(ErrorCode::CapacityExceeded,
                          "raycast map exceeds the ESP32 runtime map limits")
                        .addContext("asset", declared.path)
                        .addContext("width", std::to_string(map.value().map.width))
                        .addContext("height", std::to_string(map.value().map.height))
                        .addContext("palette_colors",
                                    std::to_string(map.value().map.wallPalette.size()))
                        .addContext("maximum_dimensions", "32x32")
                        .addContext("maximum_palette_colors", "32"));
            }
        } else if (declared.type == "racer.track") {
            const std::string text(bytes.value().begin(), bytes.value().end());
            auto track = rendering::deserializeRacerTrack(text);
            if (!track) {
                return Result<Esp32ExportSummary>::failure(
                    track.error().withContext("asset", declared.path));
            }
            if (track.value().guid != declared.guid) {
                return Result<Esp32ExportSummary>::failure(
                    Error(ErrorCode::TypeMismatch, "racer track GUID does not match its manifest")
                        .addContext("asset", declared.path)
                        .addContext("declared_guid", declared.guid.toString())
                        .addContext("content_guid", track.value().guid.toString()));
            }
            if (track.value().segments.size() > 256U) {
                return Result<Esp32ExportSummary>::failure(
                    Error(ErrorCode::CapacityExceeded,
                          "racer track exceeds the ESP32 runtime segment limit")
                        .addContext("asset", declared.path)
                        .addContext("segments", std::to_string(track.value().segments.size()))
                        .addContext("maximum", "256"));
            }
        }
        if (!embeddedPaths.insert(lowercaseAscii(declared.path)).second) {
            return Result<Esp32ExportSummary>::failure(
                Error(ErrorCode::AlreadyExists, "ESP32 asset paths must be unique")
                    .addContext("path", declared.path));
        }
        const auto storage =
            declared.esp32Target == assets::AssetTarget::Esp32Psram ? assets::StorageClass::Psram
            : declared.esp32Target == assets::AssetTarget::Esp32Sd  ? assets::StorageClass::Sd
                                                                    : assets::StorageClass::Flash;
        embeddedAssets.push_back(
            {declared.guid, {declared.path, std::move(bytes.value())}, storage});
    }
    for (auto& asset : assetFiles.value()) {
        asset.relativePath = std::string("Assets/") + asset.relativePath;
        if (!embeddedPaths.insert(lowercaseAscii(asset.relativePath)).second)
            continue;
        embeddedAssets.push_back(
            {AssetGuid::fromStableName("fabgl.esp32-export.asset:" + manifest.value().projectGuid +
                                       ":" + asset.relativePath),
             std::move(asset), assets::StorageClass::Flash});
    }
    if (embeddedAssets.size() > Esp32MaximumEmbeddedAssets) {
        return Result<Esp32ExportSummary>::failure(
            Error(ErrorCode::CapacityExceeded, "project contains too many ESP32 runtime assets")
                .addContext("maximum", std::to_string(Esp32MaximumEmbeddedAssets)));
    }

    std::vector<assets::PackInput> packInputs;
    std::vector<assets::PackInput> externalPackInputs;
    packInputs.reserve(2U + embeddedAssets.size());
    externalPackInputs.reserve(embeddedAssets.size());
    packInputs.push_back(
        {AssetGuid::fromStableName("fabgl.esp32-export.manifest:" + manifest.value().projectGuid),
         Esp32ManifestPayloadType, assets::StorageClass::Flash,
         std::vector<std::uint8_t>(canonicalManifest.value().begin(),
                                   canonicalManifest.value().end())});
    packInputs.push_back(
        {AssetGuid::fromStableName("fabgl.esp32-export.scene:" + manifest.value().projectGuid +
                                   ":" + manifest.value().startupScene),
         Esp32ScenePayloadType, assets::StorageClass::Flash,
         std::vector<std::uint8_t>(canonicalScene.value().begin(), canonicalScene.value().end())});
    std::size_t sourcePayloadBytes =
        canonicalManifest.value().size() + canonicalScene.value().size();
    for (const auto& asset : embeddedAssets) {
        auto wrapped = wrapAssetPayload(asset.file);
        if (!wrapped)
            return Result<Esp32ExportSummary>::failure(wrapped.error());
        if (asset.storage == assets::StorageClass::Sd) {
            if (wrapped.value().size() >
                MaximumExternalPayloadBytes -
                    std::min<std::size_t>(MaximumExternalPayloadBytes,
                                          std::accumulate(
                                              externalPackInputs.begin(), externalPackInputs.end(),
                                              std::size_t{0U}, [](const std::size_t total,
                                                                 const assets::PackInput& input) {
                                                  return total + input.payload.size();
                                              }))) {
                return Result<Esp32ExportSummary>::failure(
                    Error(ErrorCode::CapacityExceeded,
                          "external ESP32 SD asset payload exceeds its bounded export limit")
                        .addContext("maximum_bytes",
                                    std::to_string(MaximumExternalPayloadBytes)));
            }
            externalPackInputs.push_back(
                {asset.guid, Esp32AssetPayloadType, assets::StorageClass::Sd,
                 std::move(wrapped.value())});
            SourceFile placeholder{asset.file.relativePath, {}};
            auto envelope = wrapAssetPayload(placeholder);
            if (!envelope)
                return Result<Esp32ExportSummary>::failure(envelope.error());
            if (envelope.value().size() >
                MaximumEmbeddedPayloadBytes -
                    std::min(sourcePayloadBytes, MaximumEmbeddedPayloadBytes)) {
                return Result<Esp32ExportSummary>::failure(
                    Error(ErrorCode::CapacityExceeded,
                          "project SD placeholders exceed the ESP32 embed limit")
                        .addContext("maximum_bytes",
                                    std::to_string(MaximumEmbeddedPayloadBytes)));
            }
            sourcePayloadBytes += envelope.value().size();
            packInputs.push_back({asset.guid, Esp32AssetPayloadType, assets::StorageClass::Sd,
                                  std::move(envelope.value())});
            continue;
        }
        if (wrapped.value().size() >
            MaximumEmbeddedPayloadBytes -
                std::min(sourcePayloadBytes, MaximumEmbeddedPayloadBytes)) {
            return Result<Esp32ExportSummary>::failure(
                Error(ErrorCode::CapacityExceeded, "project payload exceeds the ESP32 embed limit")
                    .addContext("maximum_bytes", std::to_string(MaximumEmbeddedPayloadBytes)));
        }
        sourcePayloadBytes += wrapped.value().size();
        packInputs.push_back(
            {asset.guid, Esp32AssetPayloadType, asset.storage, std::move(wrapped.value())});
    }
    auto pack = assets::buildPack(std::move(packInputs));
    if (!pack)
        return Result<Esp32ExportSummary>::failure(pack.error());
    if (pack.value().bytes.size() > MaximumEmbeddedPayloadBytes) {
        return Result<Esp32ExportSummary>::failure(
            Error(ErrorCode::CapacityExceeded, "packed project exceeds the ESP32 embed limit")
                .addContext("maximum_bytes", std::to_string(MaximumEmbeddedPayloadBytes)));
    }
    const auto payloadChecksum =
        assets::checksum64(pack.value().bytes.data(), pack.value().bytes.size());
    assets::AssetPack externalPack;
    const assets::AssetPack* externalPackView = nullptr;
    std::uint64_t externalPayloadChecksum = 0U;
    if (!externalPackInputs.empty()) {
        auto packedExternal = assets::buildPack(std::move(externalPackInputs));
        if (!packedExternal)
            return Result<Esp32ExportSummary>::failure(packedExternal.error());
        if (packedExternal.value().bytes.size() > MaximumExternalPayloadBytes) {
            return Result<Esp32ExportSummary>::failure(
                Error(ErrorCode::CapacityExceeded, "external ESP32 SD pack exceeds its limit")
                    .addContext("maximum_bytes", std::to_string(MaximumExternalPayloadBytes)));
        }
        externalPack = std::move(packedExternal.value());
        externalPayloadChecksum =
            assets::checksum64(externalPack.bytes.data(), externalPack.bytes.size());
        externalPackView = &externalPack;
    }

    std::string sketchName;
    auto output = prepareOutputPath(outputSketchDirectory, sketchName);
    if (!output)
        return Result<Esp32ExportSummary>::failure(output.error());
    auto outputType = inspectNode(output.value());
    if (!outputType)
        return Result<Esp32ExportSummary>::failure(outputType.error());
    if (outputType.value() != NodeType::Missing) {
        return Result<Esp32ExportSummary>::failure(
            Error(ErrorCode::AlreadyExists, "refusing to overwrite an existing export path")
                .addContext("path", output.value()));
    }

    auto templateRoot =
        canonicalNode(firmwareTemplateDirectory, NodeType::Directory, "firmware template");
    if (!templateRoot)
        return Result<Esp32ExportSummary>::failure(templateRoot.error());
    auto templateFiles =
        collectRegularFiles(templateRoot.value(), "firmware template", false, MaximumTemplateBytes);
    if (!templateFiles)
        return Result<Esp32ExportSummary>::failure(templateFiles.error());
    std::optional<SourceFile> sharedLifecycleHeader;
    const auto sharedLifecyclePath = joinPath(
        parentPath(parentPath(templateRoot.value())), "common/LifecycleScheduler.h");
    auto sharedLifecycleType = inspectNode(sharedLifecyclePath);
    if (!sharedLifecycleType)
        return Result<Esp32ExportSummary>::failure(sharedLifecycleType.error());
    if (sharedLifecycleType.value() == NodeType::RegularFile) {
        auto bytes = assets::readBinaryFile(sharedLifecyclePath);
        if (!bytes)
            return Result<Esp32ExportSummary>::failure(bytes.error());
        if (bytes.value().size() > MaximumTemplateBytes) {
            return Result<Esp32ExportSummary>::failure(
                Error(ErrorCode::CapacityExceeded, "shared lifecycle header is too large"));
        }
        sharedLifecycleHeader = SourceFile{"LifecycleScheduler.h", std::move(bytes.value())};
    }

    bool hasBoardProfile = false;
    bool hasFirmwareSketch = false;
    std::set<std::string> stagedNames;
    std::vector<SourceFile> stagedFiles;
    stagedFiles.reserve(templateFiles.value().size() + portableScriptFiles.size() + 5U);
    for (auto& file : templateFiles.value()) {
        const auto lower = lowercaseAscii(file.relativePath);
        if (lower == "projectpayload.h" || lower == "projectpayload.fglpak" ||
            lower == "projectassets.fglpak" ||
            lower == "projectscriptconfig.h" || lower == "exportresult.json") {
            return Result<Esp32ExportSummary>::failure(
                Error(ErrorCode::AlreadyExists,
                      "firmware template uses a reserved generated payload filename")
                    .addContext("path", file.relativePath));
        }
        if (file.relativePath == "BoardProfile.h")
            hasBoardProfile = true;
        if (file.relativePath == "firmware.ino") {
            hasFirmwareSketch = true;
            file.relativePath = sketchName + ".ino";
        }
        const auto stagingKey = lowercaseAscii(file.relativePath);
        if (!stagedNames.insert(stagingKey).second) {
            return Result<Esp32ExportSummary>::failure(
                Error(ErrorCode::AlreadyExists, "firmware template creates duplicate output files")
                    .addContext("path", file.relativePath));
        }
        stagedFiles.push_back(std::move(file));
    }
    if (sharedLifecycleHeader) {
        const auto key = lowercaseAscii(sharedLifecycleHeader->relativePath);
        if (!stagedNames.insert(key).second) {
            return Result<Esp32ExportSummary>::failure(
                Error(ErrorCode::AlreadyExists,
                      "firmware template duplicates the shared lifecycle header"));
        }
        stagedFiles.push_back(std::move(*sharedLifecycleHeader));
    }
    if (!hasBoardProfile || !hasFirmwareSketch) {
        return Result<Esp32ExportSummary>::failure(Error(
            ErrorCode::NotFound, "firmware template must contain BoardProfile.h and firmware.ino"));
    }

    const auto header = generatePayloadHeader(
        manifest.value(), scene.value()->entityCount(), embeddedAssets.size(), pack.value(),
        payloadChecksum, externalPackView, externalPayloadChecksum);
    stagedFiles.push_back(
        SourceFile{"ProjectPayload.h", std::vector<std::uint8_t>(header.begin(), header.end())});
    stagedFiles.push_back(SourceFile{"ProjectPayload.fglpak", pack.value().bytes});
    if (externalPackView != nullptr)
        stagedFiles.push_back(SourceFile{"ProjectAssets.fglpak", externalPack.bytes});
    const auto scriptConfig =
        std::string("#pragma once\n\n#define FABGL_STUDIO_HAS_PROJECT_SCRIPTS ") +
        (portableScriptFiles.empty() ? "0\n" : "1\n") +
        "#define FABGL_STUDIO_PROJECT_SCRIPT_FILE_COUNT " +
        std::to_string(portableScriptFiles.size()) + "U\n";
    stagedFiles.push_back(
        SourceFile{"ProjectScriptConfig.h",
                   std::vector<std::uint8_t>(scriptConfig.begin(), scriptConfig.end())});
    const auto portableScriptFileCount = portableScriptFiles.size();
    if (portableScriptFileCount != 0U) {
        const std::string forwardingHeader =
            "#pragma once\n\n#include \"../../ProjectScriptRuntime.h\"\n";
        stagedFiles.push_back(SourceFile{
            "src/ProjectScripts/ProjectScriptRuntime.h",
            std::vector<std::uint8_t>(forwardingHeader.begin(), forwardingHeader.end())});
        stagedNames.insert("src/projectscripts/projectscriptruntime.h");
    }
    for (auto& script : portableScriptFiles) {
        const auto stagingKey = lowercaseAscii(script.relativePath);
        if (!stagedNames.insert(stagingKey).second) {
            return Result<Esp32ExportSummary>::failure(
                Error(ErrorCode::AlreadyExists,
                      "portable script creates a duplicate staged filename")
                    .addContext("path", script.relativePath));
        }
        stagedFiles.push_back(std::move(script));
    }
    const auto exportResult = generateExportResult(
        manifest.value(), sketchName + ".ino", scene.value()->entityCount(), embeddedAssets.size(),
        portableScriptFileCount, pack.value(), payloadChecksum, externalPackView,
        externalPayloadChecksum);
    stagedFiles.push_back(SourceFile{
        "ExportResult.json", std::vector<std::uint8_t>(exportResult.begin(), exportResult.end())});
    std::sort(stagedFiles.begin(), stagedFiles.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.relativePath < rhs.relativePath; });

    auto staging = createStagingDirectory(parentPath(output.value()), sketchName);
    if (!staging)
        return Result<Esp32ExportSummary>::failure(staging.error());
    StagingGuard stagingGuard(staging.value());
    for (const auto& file : stagedFiles) {
        auto written = writeStagedFile(staging.value(), file);
        if (!written)
            return Result<Esp32ExportSummary>::failure(written.error());
    }
    auto committed = renameDirectoryNoReplace(staging.value(), output.value());
    if (!committed)
        return Result<Esp32ExportSummary>::failure(committed.error());
    stagingGuard.release();

    Esp32ExportSummary summary;
    summary.projectName = manifest.value().name;
    summary.previewDemo = manifest.value().previewDemo;
    summary.sketchFileName = sketchName + ".ino";
    summary.entityCount = scene.value()->entityCount();
    summary.assetCount = embeddedAssets.size();
    summary.portableScriptFileCount = portableScriptFileCount;
    summary.scriptRuntime = portableScriptFileCount != 0U;
    summary.payloadSize = pack.value().bytes.size();
    summary.payloadChecksum = payloadChecksum;
    summary.packBuildChecksum = pack.value().buildChecksum;
    summary.externalAssetCount = externalPackView == nullptr ? 0U : externalPack.index.size();
    summary.externalPayloadSize =
        externalPackView == nullptr ? 0U : externalPack.bytes.size();
    summary.externalPayloadChecksum = externalPayloadChecksum;
    summary.externalPackBuildChecksum =
        externalPackView == nullptr ? 0U : externalPack.buildChecksum;
    return Result<Esp32ExportSummary>::success(std::move(summary));
}

} // namespace fabgl::project
