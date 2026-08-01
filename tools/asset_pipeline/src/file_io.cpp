#include <fabgl/assets/file_io.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <limits>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace fabgl::assets {

namespace {

constexpr std::uint64_t MaximumFileSize = 1024ULL * 1024ULL * 1024ULL;

#ifdef _WIN32

Result<std::wstring> toWide(const std::string& value) {
    if (value.find('\0') != std::string::npos) {
        return Result<std::wstring>::failure(
            Error(ErrorCode::InvalidArgument, "path contains NUL"));
    }
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) {
        return Result<std::wstring>::failure(
            Error(ErrorCode::InvalidArgument, "path is not valid UTF-8"));
    }
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                        result.data(), count);
    return Result<std::wstring>::success(std::move(result));
}

Result<std::vector<std::uint8_t>> readWindows(const std::wstring& path) {
    const auto handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return Result<std::vector<std::uint8_t>>::failure(
            Error(ErrorCode::IoError, "unable to open file")
                .addContext("win32", std::to_string(GetLastError())));
    }
    LARGE_INTEGER size{};
    if (GetFileSizeEx(handle, &size) == FALSE || size.QuadPart < 0 ||
        static_cast<std::uint64_t>(size.QuadPart) > MaximumFileSize) {
        const auto code = GetLastError();
        CloseHandle(handle);
        return Result<std::vector<std::uint8_t>>::failure(
            Error(ErrorCode::CapacityExceeded, "file size is invalid or exceeds 1 GiB")
                .addContext("win32", std::to_string(code)));
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size.QuadPart));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const auto request = static_cast<DWORD>(
            std::min<std::size_t>(remaining, static_cast<std::size_t>(16U * 1024U * 1024U)));
        DWORD read = 0;
        if (ReadFile(handle, bytes.data() + offset, request, &read, nullptr) == FALSE ||
            read == 0U) {
            const auto code = GetLastError();
            CloseHandle(handle);
            return Result<std::vector<std::uint8_t>>::failure(
                Error(ErrorCode::IoError, "failed while reading file")
                    .addContext("win32", std::to_string(code)));
        }
        offset += static_cast<std::size_t>(read);
    }
    CloseHandle(handle);
    return Result<std::vector<std::uint8_t>>::success(std::move(bytes));
}

#endif

} // namespace

Result<std::vector<std::uint8_t>> readBinaryFile(const std::string& utf8Path) {
#ifdef _WIN32
    auto wide = toWide(utf8Path);
    if (!wide) {
        return Result<std::vector<std::uint8_t>>::failure(wide.error());
    }
    auto result = readWindows(wide.value());
    if (!result) {
        return Result<std::vector<std::uint8_t>>::failure(
            result.error().withContext("path", utf8Path));
    }
    return result;
#else
    std::ifstream stream(utf8Path, std::ios::binary | std::ios::ate);
    if (!stream) {
        return Result<std::vector<std::uint8_t>>::failure(
            Error(ErrorCode::IoError, "unable to open file").addContext("path", utf8Path));
    }
    const auto end = stream.tellg();
    if (end < 0 || static_cast<std::uint64_t>(end) > MaximumFileSize) {
        return Result<std::vector<std::uint8_t>>::failure(
            Error(ErrorCode::CapacityExceeded, "file size is invalid or exceeds 1 GiB")
                .addContext("path", utf8Path));
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    stream.seekg(0);
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
    }
    if (!stream) {
        return Result<std::vector<std::uint8_t>>::failure(
            Error(ErrorCode::IoError, "failed while reading file").addContext("path", utf8Path));
    }
    return Result<std::vector<std::uint8_t>>::success(std::move(bytes));
#endif
}

Result<void> writeBinaryFileAtomic(const std::string& utf8Path,
                                   const std::vector<std::uint8_t>& bytes) {
    const auto temporaryPath = utf8Path + ".part";
#ifdef _WIN32
    auto path = toWide(utf8Path);
    auto temporary = toWide(temporaryPath);
    if (!path) {
        return Result<void>::failure(path.error());
    }
    if (!temporary) {
        return Result<void>::failure(temporary.error());
    }
    const auto handle = CreateFileW(temporary.value().c_str(), GENERIC_WRITE, 0, nullptr,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return Result<void>::failure(Error(ErrorCode::IoError, "unable to create temporary output")
                                         .addContext("path", temporaryPath)
                                         .addContext("win32", std::to_string(GetLastError())));
    }
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto request = static_cast<DWORD>(std::min<std::size_t>(
            bytes.size() - offset, static_cast<std::size_t>(16U * 1024U * 1024U)));
        DWORD written = 0;
        if (WriteFile(handle, bytes.data() + offset, request, &written, nullptr) == FALSE ||
            written != request) {
            const auto code = GetLastError();
            CloseHandle(handle);
            DeleteFileW(temporary.value().c_str());
            return Result<void>::failure(
                Error(ErrorCode::IoError, "failed while writing temporary output")
                    .addContext("win32", std::to_string(code)));
        }
        offset += static_cast<std::size_t>(written);
    }
    if (FlushFileBuffers(handle) == FALSE) {
        const auto code = GetLastError();
        CloseHandle(handle);
        DeleteFileW(temporary.value().c_str());
        return Result<void>::failure(Error(ErrorCode::IoError, "failed to flush temporary output")
                                         .addContext("win32", std::to_string(code)));
    }
    CloseHandle(handle);
    if (MoveFileExW(temporary.value().c_str(), path.value().c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
        const auto code = GetLastError();
        DeleteFileW(temporary.value().c_str());
        return Result<void>::failure(Error(ErrorCode::IoError, "failed to commit atomic output")
                                         .addContext("path", utf8Path)
                                         .addContext("win32", std::to_string(code)));
    }
#else
    {
        std::ofstream stream(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return Result<void>::failure(
                Error(ErrorCode::IoError, "unable to create temporary output")
                    .addContext("path", temporaryPath));
        }
        if (!bytes.empty()) {
            stream.write(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
        }
        stream.flush();
        if (!stream) {
            std::remove(temporaryPath.c_str());
            return Result<void>::failure(
                Error(ErrorCode::IoError, "failed while writing temporary output")
                    .addContext("path", temporaryPath));
        }
    }
    if (std::rename(temporaryPath.c_str(), utf8Path.c_str()) != 0) {
        std::remove(temporaryPath.c_str());
        return Result<void>::failure(Error(ErrorCode::IoError, "failed to commit atomic output")
                                         .addContext("path", utf8Path));
    }
#endif
    return Result<void>::success();
}

Result<std::string> readTextFile(const std::string& utf8Path) {
    auto bytes = readBinaryFile(utf8Path);
    if (!bytes) {
        return Result<std::string>::failure(bytes.error());
    }
    if (bytes.value().empty()) {
        return Result<std::string>::success({});
    }
    return Result<std::string>::success(
        std::string(reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size()));
}

Result<void> createDirectories(const std::string& utf8Path) {
    if (utf8Path.empty() || utf8Path.find('\0') != std::string::npos) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "directory path is empty or invalid"));
    }
#ifdef _WIN32
    auto wide = toWide(utf8Path);
    if (!wide) {
        return Result<void>::failure(wide.error());
    }
    auto path = wide.value();
    std::replace(path.begin(), path.end(), L'/', L'\\');
    std::size_t start = 0;
    if (path.size() >= 3U && path[1] == L':' && path[2] == L'\\') {
        start = 3U;
    } else if (path.size() >= 2U && path[0] == L'\\' && path[1] == L'\\') {
        const auto serverEnd = path.find(L'\\', 2U);
        const auto shareEnd =
            serverEnd == std::wstring::npos ? std::wstring::npos : path.find(L'\\', serverEnd + 1U);
        start = shareEnd == std::wstring::npos ? path.size() : shareEnd + 1U;
    }
    for (std::size_t index = start; index <= path.size(); ++index) {
        if (index != path.size() && path[index] != L'\\') {
            continue;
        }
        if (index == 0U) {
            continue;
        }
        const auto part = path.substr(0U, index);
        if (part.empty() || (part.size() == 2U && part[1] == L':')) {
            continue;
        }
        if (CreateDirectoryW(part.c_str(), nullptr) == FALSE &&
            GetLastError() != ERROR_ALREADY_EXISTS) {
            return Result<void>::failure(Error(ErrorCode::IoError, "unable to create directory")
                                             .addContext("path", utf8Path)
                                             .addContext("win32", std::to_string(GetLastError())));
        }
    }
#else
    std::string path = utf8Path;
    std::replace(path.begin(), path.end(), '\\', '/');
    for (std::size_t index = 1U; index <= path.size(); ++index) {
        if (index != path.size() && path[index] != '/') {
            continue;
        }
        const auto part = path.substr(0U, index);
        if (part.empty()) {
            continue;
        }
        if (::mkdir(part.c_str(), 0755) != 0 && errno != EEXIST) {
            return Result<void>::failure(Error(ErrorCode::IoError, "unable to create directory")
                                             .addContext("path", utf8Path)
                                             .addContext("errno", std::to_string(errno)));
        }
    }
#endif
    return Result<void>::success();
}

bool isSafeRelativePath(const std::string& path) noexcept {
    if (path.empty() || path.front() == '/' || path.front() == '\\' ||
        path.find('\0') != std::string::npos) {
        return false;
    }
    for (const auto character : path) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20U || character == ':' || character == '"' || character == '|' ||
            character == '?' || character == '*') {
            return false;
        }
    }
    const auto safeSegment = [](const std::string& value) {
        if (value.empty() || value == "." || value == ".." || value.back() == '.' ||
            value.back() == ' ') {
            return false;
        }
        auto base = value.substr(0U, value.find('.'));
        std::transform(base.begin(), base.end(), base.begin(), [](const unsigned char character) {
            return static_cast<char>(std::toupper(character));
        });
        if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL" || base == "CLOCK$") {
            return false;
        }
        if (base.size() == 4U && (base.rfind("COM", 0U) == 0U || base.rfind("LPT", 0U) == 0U) &&
            base[3] >= '1' && base[3] <= '9') {
            return false;
        }
        return true;
    };
    std::string segment;
    for (std::size_t index = 0; index <= path.size(); ++index) {
        const auto character = index < path.size() ? path[index] : '/';
        if (character == '/' || character == '\\') {
            if (!safeSegment(segment))
                return false;
            segment.clear();
        } else {
            segment += character;
        }
    }
    return true;
}

} // namespace fabgl::assets
