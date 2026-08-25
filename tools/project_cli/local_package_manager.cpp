#include "local_package_manager.h"

#include "project_format.h"

#include <fabgl/assets/file_io.h>
#include <fabgl/core/guid.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifndef FABGL_STUDIO_ENGINE_VERSION
#define FABGL_STUDIO_ENGINE_VERSION "0.1.0"
#endif

namespace fabgl::project {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view ManifestFileName = "fabgl.package";
constexpr std::string_view OwnerFileName = ".fabgl-package-owned";
constexpr std::string_view LockFileName = "fabgl-packages.lock";
constexpr std::string_view TrustFileName = ".fabgl-package-trust";
constexpr std::string_view OwnerMarker = "# FabGL Studio owned local package. Schema: 1\n";
constexpr std::string_view LockMarker = "# FabGL Studio package lock. Schema: 1\n";
constexpr std::string_view TrustMarker = "# FabGL Studio project package trust. Schema: 1\n";
constexpr std::uint64_t MaximumManifestBytes = 256ULL * 1024ULL;

struct FileRecord final {
    std::string relativePath;
    std::vector<std::uint8_t> bytes;
};

struct TrustRecord final {
    std::string id;
    SemVersion version;
    std::string digest;
};

struct ProjectContext final {
    fs::path manifestPath;
    fs::path projectRoot;
    fs::path packagesRoot;
    bool packagesExist = false;
};

struct ProjectState final {
    ProjectContext context;
    std::vector<LocalPackageInfo> packages;
    std::vector<TrustRecord> trustRecords;
    std::vector<std::string> loadOrder;
};

std::string pathText(const fs::path& path) {
    const auto value = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

fs::path pathFromUtf8(const std::string& value) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}

Error ioError(std::string message, const fs::path& path, const std::error_code& code = {}) {
    auto error = Error(ErrorCode::IoError, std::move(message)).addContext("path", pathText(path));
    if (code)
        error.addContext("system", code.message());
    return error;
}

bool samePath(const fs::path& lhs, const fs::path& rhs) {
    auto left = pathText(lhs.lexically_normal());
    auto right = pathText(rhs.lexically_normal());
#ifdef _WIN32
    std::transform(left.begin(), left.end(), left.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    std::transform(right.begin(), right.end(), right.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
#endif
    return left == right;
}

bool pathInside(const fs::path& path, const fs::path& root) {
    auto child = pathText(path.lexically_normal());
    auto parent = pathText(root.lexically_normal());
#ifdef _WIN32
    std::transform(child.begin(), child.end(), child.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    std::transform(parent.begin(), parent.end(), parent.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
#endif
    if (child == parent)
        return true;
    if (!parent.empty() && parent.back() != '/')
        parent += '/';
    return child.rfind(parent, 0U) == 0U;
}

bool isReparsePoint(const fs::path& path) {
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U;
#else
    std::error_code code;
    return fs::is_symlink(fs::symlink_status(path, code));
#endif
}

Result<fs::path> absolutePath(const std::string& input, std::string_view description) {
    if (input.empty() || input.find('\0') != std::string::npos) {
        return Result<fs::path>::failure(
            Error(ErrorCode::InvalidArgument, std::string(description) + " path is invalid"));
    }
    std::error_code code;
    auto result = fs::absolute(pathFromUtf8(input), code).lexically_normal();
    if (code) {
        return Result<fs::path>::failure(
            ioError(std::string("unable to resolve ") + std::string(description), result, code));
    }
    return Result<fs::path>::success(std::move(result));
}

Result<void> rejectReparseChain(const fs::path& path, std::string_view description) {
    auto current = path.root_path();
    for (const auto& component : path.relative_path()) {
        current /= component;
        std::error_code code;
        const auto status = fs::symlink_status(current, code);
        if (code) {
            if (code == std::errc::no_such_file_or_directory)
                break;
            return Result<void>::failure(ioError(
                std::string("unable to inspect ") + std::string(description), current, code));
        }
        if (status.type() == fs::file_type::not_found)
            break;
        if (isReparsePoint(current)) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument,
                      std::string(description) +
                          " cannot traverse a symbolic link, junction, or reparse point")
                    .addContext("path", pathText(current)));
        }
    }
    return Result<void>::success();
}

Result<void> requireDirectory(const fs::path& path, std::string_view description) {
    auto safe = rejectReparseChain(path, description);
    if (!safe)
        return safe;
    std::error_code code;
    if (!fs::is_directory(path, code) || code) {
        return Result<void>::failure(
            Error(ErrorCode::NotFound, std::string(description) + " directory was not found")
                .addContext("path", pathText(path)));
    }
    return Result<void>::success();
}

Result<void> requireRegularFile(const fs::path& path, std::string_view description) {
    auto safe = rejectReparseChain(path, description);
    if (!safe)
        return safe;
    std::error_code code;
    if (!fs::is_regular_file(path, code) || code) {
        return Result<void>::failure(
            Error(ErrorCode::NotFound, std::string(description) + " file was not found")
                .addContext("path", pathText(path)));
    }
    return Result<void>::success();
}

bool validRelativePackagePath(const std::string& path, std::size_t maximumDepth) {
    if (path.size() > 240U || path.find(';') != std::string::npos ||
        !assets::isSafeRelativePath(path)) {
        return false;
    }
    return static_cast<std::size_t>(std::count(path.begin(), path.end(), '/')) + 1U <= maximumDepth;
}

std::string lowercaseAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool reservedPackageFile(const std::string& relativePath) {
    const auto slash = relativePath.find_last_of('/');
    const auto base = slash == std::string::npos ? relativePath : relativePath.substr(slash + 1U);
    const auto lowered = lowercaseAscii(base);
    return lowered.rfind(".fabgl-", 0U) == 0U ||
           (slash == std::string::npos && (lowered == lowercaseAscii(std::string(LockFileName)) ||
                                           lowered == lowercaseAscii(std::string(TrustFileName))));
}

bool looksExecutable(const FileRecord& file) {
    auto name = lowercaseAscii(file.relativePath);
    const auto slash = name.find_last_of('/');
    const auto base = slash == std::string::npos ? name : name.substr(slash + 1U);
    const auto dot = base.find_last_of('.');
    const auto extension = dot == std::string::npos ? std::string() : base.substr(dot);
    static const std::set<std::string> executableExtensions = {
        ".a",   ".bat",   ".c",   ".cc",  ".cmd", ".cmake", ".cpp", ".cxx",
        ".dll", ".dylib", ".exe", ".h",   ".hpp", ".js",    ".lib", ".lua",
        ".mjs", ".o",     ".obj", ".ps1", ".py",  ".sh",    ".so",  ".wasm"};
    if (base == "cmakelists.txt" ||
        executableExtensions.find(extension) != executableExtensions.end()) {
        return true;
    }
    if (file.bytes.size() >= 2U && file.bytes[0] == 'M' && file.bytes[1] == 'Z')
        return true;
    if (file.bytes.size() >= 4U && file.bytes[0] == 0x7FU && file.bytes[1] == 'E' &&
        file.bytes[2] == 'L' && file.bytes[3] == 'F') {
        return true;
    }
    if (file.bytes.size() >= 4U && file.bytes[0] == 0x00U && file.bytes[1] == 0x61U &&
        file.bytes[2] == 0x73U && file.bytes[3] == 0x6DU) {
        return true;
    }
    return file.bytes.size() >= 2U && file.bytes[0] == '#' && file.bytes[1] == '!';
}

Result<std::vector<FileRecord>>
readPackageFiles(const fs::path& root, const LocalPackageLimits& limits, bool installedPackage) {
    auto directory = requireDirectory(root, "package");
    if (!directory)
        return Result<std::vector<FileRecord>>::failure(directory.error());

    struct Pending final {
        fs::path directory;
        std::string relative;
        std::size_t depth = 0U;
    };
    std::vector<Pending> pending{{root, {}, 0U}};
    std::vector<FileRecord> files;
    std::set<std::string> portablePaths;
    std::uint64_t totalBytes = 0U;
    while (!pending.empty()) {
        auto current = std::move(pending.back());
        pending.pop_back();
        std::error_code code;
        std::vector<fs::directory_entry> entries;
        for (fs::directory_iterator iterator(current.directory, code), end; iterator != end;
             iterator.increment(code)) {
            if (code)
                break;
            entries.push_back(*iterator);
        }
        if (code) {
            return Result<std::vector<FileRecord>>::failure(
                ioError("unable to enumerate package directory", current.directory, code));
        }
        std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
            return pathText(lhs.path().filename()) < pathText(rhs.path().filename());
        });
        for (const auto& entry : entries) {
            if (isReparsePoint(entry.path())) {
                return Result<std::vector<FileRecord>>::failure(
                    Error(ErrorCode::InvalidArgument,
                          "package cannot contain a symbolic link, junction, or reparse point")
                        .addContext("path", pathText(entry.path())));
            }
            const auto name = pathText(entry.path().filename());
            const auto relative = current.relative.empty() ? name : current.relative + '/' + name;
            if (!validRelativePackagePath(relative, limits.maximumDepth)) {
                return Result<std::vector<FileRecord>>::failure(
                    Error(ErrorCode::InvalidArgument, "package contains an unsafe path")
                        .addContext("path", relative));
            }
            const auto portable = lowercaseAscii(relative);
            if (!portablePaths.insert(portable).second) {
                return Result<std::vector<FileRecord>>::failure(
                    Error(ErrorCode::AlreadyExists,
                          "package contains paths that collide on a case-insensitive filesystem")
                        .addContext("path", relative));
            }
            const auto status = entry.symlink_status(code);
            if (code) {
                return Result<std::vector<FileRecord>>::failure(
                    ioError("unable to inspect package entry", entry.path(), code));
            }
            if (fs::is_directory(status)) {
                pending.push_back({entry.path(), relative, current.depth + 1U});
                continue;
            }
            if (!fs::is_regular_file(status)) {
                return Result<std::vector<FileRecord>>::failure(
                    Error(ErrorCode::InvalidArgument, "package entry is not a normal file")
                        .addContext("path", relative));
            }
            if (installedPackage && relative == OwnerFileName)
                continue;
            if (!installedPackage && reservedPackageFile(relative)) {
                return Result<std::vector<FileRecord>>::failure(
                    Error(ErrorCode::InvalidArgument, "source package uses a reserved file name")
                        .addContext("path", relative));
            }
            if (files.size() >= limits.maximumFiles) {
                return Result<std::vector<FileRecord>>::failure(
                    Error(ErrorCode::CapacityExceeded, "package file-count limit exceeded"));
            }
            const auto fileBytes = entry.file_size(code);
            if (code || fileBytes > limits.maximumFileBytes ||
                fileBytes > limits.maximumTotalBytes ||
                totalBytes > limits.maximumTotalBytes - fileBytes) {
                return Result<std::vector<FileRecord>>::failure(
                    Error(ErrorCode::CapacityExceeded, "package byte limit exceeded")
                        .addContext("path", relative));
            }
            auto bytes = assets::readBinaryFile(pathText(entry.path()));
            if (!bytes)
                return Result<std::vector<FileRecord>>::failure(bytes.error());
            if (bytes.value().size() != fileBytes) {
                return Result<std::vector<FileRecord>>::failure(
                    Error(ErrorCode::IoError, "package file changed while it was being read")
                        .addContext("path", relative));
            }
            totalBytes += fileBytes;
            files.push_back({relative, std::move(bytes.value())});
        }
    }
    std::sort(files.begin(), files.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.relativePath < rhs.relativePath; });
    return Result<std::vector<FileRecord>>::success(std::move(files));
}

class Sha256 final {
  public:
    void update(const std::uint8_t* bytes, std::size_t size) {
        totalBytes_ += static_cast<std::uint64_t>(size);
        while (size != 0U) {
            const auto count = std::min(size, block_.size() - blockSize_);
            std::copy_n(bytes, count, block_.data() + blockSize_);
            blockSize_ += count;
            bytes += count;
            size -= count;
            if (blockSize_ == block_.size()) {
                transform();
                blockSize_ = 0U;
            }
        }
    }

    void update(std::string_view text) {
        update(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
    }

    std::array<std::uint8_t, 32> finish() {
        const auto bitLength = totalBytes_ * 8ULL;
        block_[blockSize_++] = 0x80U;
        if (blockSize_ > 56U) {
            std::fill(block_.begin() + static_cast<std::ptrdiff_t>(blockSize_), block_.end(), 0U);
            transform();
            blockSize_ = 0U;
        }
        std::fill(block_.begin() + static_cast<std::ptrdiff_t>(blockSize_), block_.begin() + 56,
                  0U);
        for (std::size_t index = 0U; index < 8U; ++index)
            block_[63U - index] = static_cast<std::uint8_t>(bitLength >> (index * 8U));
        transform();
        std::array<std::uint8_t, 32> digest{};
        for (std::size_t word = 0U; word < state_.size(); ++word) {
            for (std::size_t byte = 0U; byte < 4U; ++byte) {
                digest[word * 4U + byte] =
                    static_cast<std::uint8_t>(state_[word] >> ((3U - byte) * 8U));
            }
        }
        return digest;
    }

  private:
    static std::uint32_t rotateRight(std::uint32_t value, std::uint32_t count) noexcept {
        return (value >> count) | (value << (32U - count));
    }

    void transform() {
        static constexpr std::array<std::uint32_t, 64> constants = {
            0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
            0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
            0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
            0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
            0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
            0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
            0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
            0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
            0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
            0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
            0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0U; index < 16U; ++index) {
            const auto offset = index * 4U;
            words[index] = (static_cast<std::uint32_t>(block_[offset]) << 24U) |
                           (static_cast<std::uint32_t>(block_[offset + 1U]) << 16U) |
                           (static_cast<std::uint32_t>(block_[offset + 2U]) << 8U) |
                           static_cast<std::uint32_t>(block_[offset + 3U]);
        }
        for (std::size_t index = 16U; index < words.size(); ++index) {
            const auto first = rotateRight(words[index - 15U], 7U) ^
                               rotateRight(words[index - 15U], 18U) ^ (words[index - 15U] >> 3U);
            const auto second = rotateRight(words[index - 2U], 17U) ^
                                rotateRight(words[index - 2U], 19U) ^ (words[index - 2U] >> 10U);
            words[index] = words[index - 16U] + first + words[index - 7U] + second;
        }
        auto a = state_[0];
        auto b = state_[1];
        auto c = state_[2];
        auto d = state_[3];
        auto e = state_[4];
        auto f = state_[5];
        auto g = state_[6];
        auto h = state_[7];
        for (std::size_t index = 0U; index < words.size(); ++index) {
            const auto sum1 = rotateRight(e, 6U) ^ rotateRight(e, 11U) ^ rotateRight(e, 25U);
            const auto choice = (e & f) ^ ((~e) & g);
            const auto temporary1 = h + sum1 + choice + constants[index] + words[index];
            const auto sum0 = rotateRight(a, 2U) ^ rotateRight(a, 13U) ^ rotateRight(a, 22U);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto temporary2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temporary1;
            d = c;
            c = b;
            b = a;
            a = temporary1 + temporary2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    std::array<std::uint8_t, 64> block_{};
    std::size_t blockSize_ = 0U;
    std::uint64_t totalBytes_ = 0U;
};

void hashU64(Sha256& hash, std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index)
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    hash.update(bytes.data(), bytes.size());
}

std::string packageDigest(const std::vector<FileRecord>& files) {
    Sha256 hash;
    hash.update("FabGLStudioPackageContent-v1");
    for (const auto& file : files) {
        hashU64(hash, static_cast<std::uint64_t>(file.relativePath.size()));
        hash.update(file.relativePath);
        hashU64(hash, static_cast<std::uint64_t>(file.bytes.size()));
        if (!file.bytes.empty())
            hash.update(file.bytes.data(), file.bytes.size());
    }
    const auto digest = hash.finish();
    constexpr char digits[] = "0123456789abcdef";
    std::string text;
    text.reserve(digest.size() * 2U);
    for (const auto byte : digest) {
        text += digits[(byte >> 4U) & 0x0FU];
        text += digits[byte & 0x0FU];
    }
    return text;
}

std::vector<std::uint8_t> textBytes(const std::string& text) {
    return {text.begin(), text.end()};
}

Result<std::string> readSmallText(const fs::path& path, std::uint64_t limit,
                                  std::string_view description) {
    auto file = requireRegularFile(path, description);
    if (!file)
        return Result<std::string>::failure(file.error());
    std::error_code code;
    const auto size = fs::file_size(path, code);
    if (code || size > limit) {
        return Result<std::string>::failure(
            Error(ErrorCode::CapacityExceeded, std::string(description) + " is too large")
                .addContext("path", pathText(path)));
    }
    return assets::readTextFile(pathText(path));
}

Result<ProjectContext> loadProject(const std::string& manifestInput, bool createPackages) {
    auto absolute = absolutePath(manifestInput, "project manifest");
    if (!absolute)
        return Result<ProjectContext>::failure(absolute.error());
    auto manifestFile = requireRegularFile(absolute.value(), "project manifest");
    if (!manifestFile)
        return Result<ProjectContext>::failure(manifestFile.error());
    if (absolute.value().extension() != ".fglproject") {
        return Result<ProjectContext>::failure(
            Error(ErrorCode::InvalidArgument, "project manifest must use .fglproject"));
    }
    auto text = readSmallText(absolute.value(), MaximumManifestBytes, "project manifest");
    if (!text)
        return Result<ProjectContext>::failure(text.error());
    auto manifest = parseManifest(text.value());
    if (!manifest || manifest.value().sourceVersion != Manifest::CurrentVersion) {
        return Result<ProjectContext>::failure(
            manifest ? Error(ErrorCode::UnsupportedVersion, "project must use the current format")
                     : manifest.error());
    }
    ProjectContext context;
    context.manifestPath = absolute.value();
    context.projectRoot = absolute.value().parent_path();
    context.packagesRoot = context.projectRoot / "Packages";
    auto projectSafe = requireDirectory(context.projectRoot, "project root");
    if (!projectSafe)
        return Result<ProjectContext>::failure(projectSafe.error());
    std::error_code code;
    context.packagesExist = fs::exists(context.packagesRoot, code);
    if (code)
        return Result<ProjectContext>::failure(
            ioError("unable to inspect Packages", context.packagesRoot, code));
    if (!context.packagesExist && createPackages) {
        if (!fs::create_directories(context.packagesRoot, code) || code) {
            return Result<ProjectContext>::failure(
                ioError("unable to create Packages directory", context.packagesRoot, code));
        }
        context.packagesExist = true;
    }
    if (context.packagesExist) {
        auto packagesSafe = requireDirectory(context.packagesRoot, "Packages");
        if (!packagesSafe)
            return Result<ProjectContext>::failure(packagesSafe.error());
    }
    return Result<ProjectContext>::success(std::move(context));
}

std::string ownerText(const LocalPackageInfo& info) {
    std::ostringstream output;
    output << OwnerMarker << "id=" << info.manifest.stableId() << '\n'
           << "version=" << info.manifest.version.toString() << '\n'
           << "sha256=" << info.contentSha256 << '\n'
           << "files=" << info.fileCount << '\n'
           << "bytes=" << info.totalBytes << '\n';
    return output.str();
}

Result<LocalPackageInfo> inspectOwnedPackage(const fs::path& directory,
                                             const std::string& expectedId,
                                             const LocalPackageLimits& limits) {
    auto files = readPackageFiles(directory, limits, true);
    if (!files)
        return Result<LocalPackageInfo>::failure(files.error());
    const auto manifestEntry =
        std::find_if(files.value().begin(), files.value().end(),
                     [](const auto& file) { return file.relativePath == ManifestFileName; });
    if (manifestEntry == files.value().end() ||
        manifestEntry->bytes.size() > MaximumManifestBytes) {
        return Result<LocalPackageInfo>::failure(
            Error(ErrorCode::InvalidFormat, "installed package has no bounded root manifest")
                .addContext("package", expectedId));
    }
    const std::string manifestText(manifestEntry->bytes.begin(), manifestEntry->bytes.end());
    auto manifest = PackageManifestParser::parse(manifestText);
    if (!manifest || manifest.value().schemaVersion != 2 ||
        manifest.value().stableId() != expectedId ||
        manifest.value().localPath != "Packages/" + expectedId) {
        return Result<LocalPackageInfo>::failure(
            Error(ErrorCode::InvalidFormat, "installed package manifest identity is invalid")
                .addContext("package", expectedId));
    }
    auto canonical = PackageManifestParser::serializeCanonical(manifest.value());
    if (!canonical || canonical.value() != manifestText) {
        return Result<LocalPackageInfo>::failure(
            Error(ErrorCode::InvalidFormat, "installed package manifest is not canonical")
                .addContext("package", expectedId));
    }
    std::set<std::string> filePaths;
    std::uint64_t totalBytes = 0U;
    for (const auto& file : files.value()) {
        filePaths.insert(file.relativePath);
        totalBytes += static_cast<std::uint64_t>(file.bytes.size());
    }
    for (const auto& entryPoint : manifest.value().entryPoints) {
        if (filePaths.find(entryPoint.path) == filePaths.end()) {
            return Result<LocalPackageInfo>::failure(
                Error(ErrorCode::NotFound, "package entry point file is missing")
                    .addContext("package", expectedId)
                    .addContext("entry", entryPoint.path));
        }
    }
    LocalPackageInfo info;
    info.manifest = std::move(manifest.value());
    info.directory = pathText(directory);
    info.contentSha256 = packageDigest(files.value());
    info.fileCount = files.value().size();
    info.totalBytes = totalBytes;
    const auto ownerPath = directory / pathFromUtf8(std::string(OwnerFileName));
    auto owner = readSmallText(ownerPath, 4096U, "package ownership marker");
    if (!owner || owner.value() != ownerText(info)) {
        return Result<LocalPackageInfo>::failure(
            Error(ErrorCode::InvalidState, "package ownership marker does not match content")
                .addContext("package", expectedId));
    }
    return Result<LocalPackageInfo>::success(std::move(info));
}

bool validDigest(std::string_view digest) {
    if (digest.size() != 64U)
        return false;
    for (const auto character : digest) {
        if (std::isdigit(static_cast<unsigned char>(character)) == 0 &&
            (character < 'a' || character > 'f')) {
            return false;
        }
    }
    return true;
}

std::string trustText(std::vector<TrustRecord> records) {
    std::sort(records.begin(), records.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.id < rhs.id; });
    std::ostringstream output;
    output << TrustMarker;
    for (const auto& record : records)
        output << "allow=" << record.id << '@' << record.version.toString() << '#' << record.digest
               << '\n';
    return output.str();
}

Result<std::vector<TrustRecord>> readTrust(const ProjectContext& context) {
    const auto path = context.packagesRoot / pathFromUtf8(std::string(TrustFileName));
    std::error_code code;
    if (!fs::exists(path, code))
        return code ? Result<std::vector<TrustRecord>>::failure(
                          ioError("unable to inspect package trust store", path, code))
                    : Result<std::vector<TrustRecord>>::success({});
    auto source = readSmallText(path, 256ULL * 1024ULL, "package trust store");
    if (!source)
        return Result<std::vector<TrustRecord>>::failure(source.error());
    if (source.value().rfind(TrustMarker, 0U) != 0U) {
        return Result<std::vector<TrustRecord>>::failure(
            Error(ErrorCode::InvalidFormat, "package trust store header is invalid"));
    }
    std::istringstream stream(source.value().substr(TrustMarker.size()));
    std::string line;
    std::vector<TrustRecord> records;
    std::set<std::string> ids;
    while (std::getline(stream, line)) {
        if (line.empty())
            continue;
        if (line.rfind("allow=", 0U) != 0U) {
            return Result<std::vector<TrustRecord>>::failure(
                Error(ErrorCode::InvalidFormat, "package trust record is invalid"));
        }
        const auto value = std::string_view(line).substr(6U);
        const auto at = value.find('@');
        const auto hash = value.find('#', at == std::string_view::npos ? 0U : at + 1U);
        if (at == std::string_view::npos || hash == std::string_view::npos) {
            return Result<std::vector<TrustRecord>>::failure(
                Error(ErrorCode::InvalidFormat, "package trust record is malformed"));
        }
        auto version = SemVersion::parse(value.substr(at + 1U, hash - at - 1U));
        const std::string id(value.substr(0U, at));
        const std::string digest(value.substr(hash + 1U));
        if (!version || id.empty() || !validDigest(digest) || !ids.insert(id).second) {
            return Result<std::vector<TrustRecord>>::failure(
                Error(ErrorCode::InvalidFormat, "package trust record value is invalid"));
        }
        records.push_back({id, version.value(), digest});
    }
    if (trustText(records) != source.value()) {
        return Result<std::vector<TrustRecord>>::failure(
            Error(ErrorCode::InvalidFormat, "package trust store is not canonical"));
    }
    return Result<std::vector<TrustRecord>>::success(std::move(records));
}

std::string lockText(std::vector<LocalPackageInfo> packages,
                     const std::vector<std::string>& loadOrder) {
    std::sort(packages.begin(), packages.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.manifest.stableId() < rhs.manifest.stableId();
    });
    std::ostringstream output;
    output << LockMarker << "engine=" << currentPackageEngineVersion().toString() << '\n';
    for (const auto& package : packages) {
        output << "package=" << package.manifest.stableId() << '@'
               << package.manifest.version.toString() << '#' << package.contentSha256
               << ";files=" << package.fileCount << ";bytes=" << package.totalBytes
               << ";executable=" << (package.manifest.containsExecutableCode ? "true" : "false")
               << ";trusted=" << (package.executableTrusted ? "true" : "false") << '\n';
        auto dependencies = package.manifest.dependencies;
        std::sort(dependencies.begin(), dependencies.end(),
                  [](const auto& lhs, const auto& rhs) { return lhs.name < rhs.name; });
        for (const auto& dependency : dependencies) {
            output << "dependency=" << package.manifest.stableId() << "->" << dependency.name << '@'
                   << dependency.requirement.toString() << '\n';
        }
    }
    for (const auto& id : loadOrder)
        output << "load=" << id << '\n';
    return output.str();
}

Result<ProjectState> readProjectState(const ProjectContext& context,
                                      const LocalPackageLimits& limits, bool enforceLock) {
    ProjectState state;
    state.context = context;
    if (!context.packagesExist)
        return Result<ProjectState>::success(std::move(state));
    auto trust = readTrust(context);
    if (!trust)
        return Result<ProjectState>::failure(trust.error());
    state.trustRecords = std::move(trust.value());

    std::error_code code;
    std::vector<fs::directory_entry> entries;
    for (fs::directory_iterator iterator(context.packagesRoot, code), end; iterator != end;
         iterator.increment(code)) {
        if (code)
            break;
        entries.push_back(*iterator);
    }
    if (code)
        return Result<ProjectState>::failure(
            ioError("unable to enumerate Packages", context.packagesRoot, code));
    std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
        return pathText(lhs.path().filename()) < pathText(rhs.path().filename());
    });
    for (const auto& entry : entries) {
        if (isReparsePoint(entry.path())) {
            return Result<ProjectState>::failure(
                Error(ErrorCode::InvalidArgument, "Packages cannot contain a reparse point")
                    .addContext("path", pathText(entry.path())));
        }
        const auto name = pathText(entry.path().filename());
        const auto status = entry.symlink_status(code);
        if (code)
            return Result<ProjectState>::failure(
                ioError("unable to inspect Packages entry", entry.path(), code));
        if (fs::is_regular_file(status) && (name == LockFileName || name == TrustFileName)) {
            continue;
        }
        if (!fs::is_directory(status) || name.empty() || name.front() == '.') {
            return Result<ProjectState>::failure(
                Error(ErrorCode::InvalidState, "Packages contains an unmanaged entry")
                    .addContext("path", name));
        }
        auto package = inspectOwnedPackage(entry.path(), name, limits);
        if (!package)
            return Result<ProjectState>::failure(package.error());
        state.packages.push_back(std::move(package.value()));
    }

    std::map<std::string, TrustRecord> trustById;
    for (const auto& record : state.trustRecords) {
        if (!trustById.emplace(record.id, record).second) {
            return Result<ProjectState>::failure(
                Error(ErrorCode::InvalidFormat, "duplicate package trust record"));
        }
    }
    std::set<std::string> trustedIds;
    PackageRegistry registry;
    for (auto& package : state.packages) {
        const auto id = std::string(package.manifest.stableId());
        const auto record = trustById.find(id);
        if (record != trustById.end()) {
            if (!package.manifest.containsExecutableCode ||
                record->second.version != package.manifest.version ||
                record->second.digest != package.contentSha256) {
                return Result<ProjectState>::failure(
                    Error(ErrorCode::InvalidState, "package trust record does not match content")
                        .addContext("package", id));
            }
            package.executableTrusted = true;
            trustedIds.insert(id);
            trustById.erase(record);
        }
        auto added = registry.add(package.manifest);
        if (!added)
            return Result<ProjectState>::failure(added.error());
    }
    if (!trustById.empty()) {
        return Result<ProjectState>::failure(
            Error(ErrorCode::InvalidState, "package trust store contains a stale record")
                .addContext("package", trustById.begin()->first));
    }
    auto order = registry.validate(currentPackageEngineVersion(), trustedIds);
    if (!order)
        return Result<ProjectState>::failure(order.error());
    state.loadOrder = std::move(order.value());

    const auto lockPath = context.packagesRoot / pathFromUtf8(std::string(LockFileName));
    const auto expectedLock = lockText(state.packages, state.loadOrder);
    if (fs::exists(lockPath, code)) {
        auto lock = readSmallText(lockPath, 1024ULL * 1024ULL, "package lockfile");
        if (!lock)
            return Result<ProjectState>::failure(lock.error());
        if (lock.value() != expectedLock) {
            return Result<ProjectState>::failure(Error(
                ErrorCode::InvalidState, "package lockfile does not match installed content"));
        }
    } else if (code) {
        return Result<ProjectState>::failure(
            ioError("unable to inspect package lockfile", lockPath, code));
    } else if (enforceLock && !state.packages.empty()) {
        return Result<ProjectState>::failure(
            Error(ErrorCode::NotFound, "package lockfile is missing"));
    }
    return Result<ProjectState>::success(std::move(state));
}

Result<void> writeMetadata(const ProjectContext& context,
                           const std::vector<LocalPackageInfo>& packages,
                           const std::vector<TrustRecord>& trustRecords,
                           const std::vector<std::string>& loadOrder) {
    auto trustWrite = assets::writeBinaryFileAtomic(
        pathText(context.packagesRoot / pathFromUtf8(std::string(TrustFileName))),
        textBytes(trustText(trustRecords)));
    if (!trustWrite)
        return trustWrite;
    return assets::writeBinaryFileAtomic(
        pathText(context.packagesRoot / pathFromUtf8(std::string(LockFileName))),
        textBytes(lockText(packages, loadOrder)));
}

Result<void> validateCombined(const std::vector<LocalPackageInfo>& packages,
                              const std::vector<TrustRecord>& trustRecords,
                              std::vector<std::string>& order) {
    std::set<std::string> trustedIds;
    for (const auto& record : trustRecords)
        trustedIds.insert(record.id);
    PackageRegistry registry;
    for (const auto& package : packages) {
        auto added = registry.add(package.manifest);
        if (!added)
            return added;
    }
    auto validated = registry.validate(currentPackageEngineVersion(), trustedIds);
    if (!validated)
        return Result<void>::failure(validated.error());
    order = std::move(validated.value());
    return Result<void>::success();
}

Result<void> writePackageDirectory(const fs::path& directory, const std::vector<FileRecord>& files,
                                   const LocalPackageInfo& info) {
    std::error_code code;
    if (!fs::create_directory(directory, code) || code)
        return Result<void>::failure(
            ioError("unable to create package staging directory", directory, code));
    auto ownerWrite = assets::writeBinaryFileAtomic(
        pathText(directory / pathFromUtf8(std::string(OwnerFileName))), textBytes(ownerText(info)));
    if (!ownerWrite) {
        // The directory was created by this call and is still empty apart from a failed atomic
        // temporary file, if any. Never recurse into an unmarked directory during cleanup.
        fs::remove(directory, code);
        return ownerWrite;
    }
    for (const auto& file : files) {
        const auto output = directory / pathFromUtf8(file.relativePath);
        if (!pathInside(output, directory)) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "package output escaped staging directory"));
        }
        if (!fs::create_directories(output.parent_path(), code) && code) {
            return Result<void>::failure(
                ioError("unable to create staged package directory", output.parent_path(), code));
        }
        auto written = assets::writeBinaryFileAtomic(pathText(output), file.bytes);
        if (!written)
            return written;
    }
    return Result<void>::success();
}

Result<void> removeKnownTree(const fs::path& path, const fs::path& packagesRoot) {
    if (!pathInside(path, packagesRoot) || samePath(path, packagesRoot) || isReparsePoint(path)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "refusing unsafe package directory removal")
                .addContext("path", pathText(path)));
    }
    std::error_code code;
    fs::remove_all(path, code);
    if (code)
        return Result<void>::failure(ioError("unable to remove package directory", path, code));
    return Result<void>::success();
}

Result<void> removeStagedPackage(const fs::path& path, const fs::path& packagesRoot,
                                 const LocalPackageInfo& expected) {
    auto owner = readSmallText(path / pathFromUtf8(std::string(OwnerFileName)), 4096U,
                               "staged package ownership marker");
    if (!owner || owner.value() != ownerText(expected)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "refusing to remove an unowned staging directory")
                .addContext("path", pathText(path)));
    }
    return removeKnownTree(path, packagesRoot);
}

std::string shortOperationToken() {
    auto token = AssetGuid::generate().toString();
    token.erase(std::remove(token.begin(), token.end(), '-'), token.end());
    // Generated GUIDs are time ordered, so their prefix can be identical for several
    // operations in the same clock tick. Keep the varying tail for short private names.
    return token.substr(token.size() - 12U);
}

void renameDirectoryWithRetry(const fs::path& source, const fs::path& destination,
                              std::error_code& code) {
#ifdef _WIN32
    constexpr auto MaximumAttempts = 24;
    for (auto attempt = 0; attempt < MaximumAttempts; ++attempt) {
        if (GetFileAttributesW(destination.c_str()) != INVALID_FILE_ATTRIBUTES) {
            code = std::error_code(ERROR_ALREADY_EXISTS, std::system_category());
            return;
        }
        if (MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE) {
            code.clear();
            return;
        }
        const auto nativeCode = GetLastError();
        code = std::error_code(static_cast<int>(nativeCode), std::system_category());
        // Directory promotion can briefly race Windows Defender/indexers after the staged
        // files are verified. Retry only transient sharing/access failures; all other errors
        // remain immediate and preserve the original atomic no-replace operation.
        if (nativeCode != ERROR_ACCESS_DENIED && nativeCode != ERROR_SHARING_VIOLATION &&
            nativeCode != ERROR_LOCK_VIOLATION) {
            return;
        }
        const auto delay = static_cast<DWORD>(std::min(250, 25 * (attempt + 1)));
        Sleep(delay);
    }
#else
    fs::rename(source, destination, code);
#endif
}

Result<std::pair<PackageManifest, std::vector<FileRecord>>>
prepareSourcePackage(const fs::path& source, const LocalPackageLimits& limits) {
    auto files = readPackageFiles(source, limits, false);
    if (!files)
        return Result<std::pair<PackageManifest, std::vector<FileRecord>>>::failure(files.error());
    auto manifestEntry =
        std::find_if(files.value().begin(), files.value().end(),
                     [](const auto& file) { return file.relativePath == ManifestFileName; });
    if (manifestEntry == files.value().end() ||
        manifestEntry->bytes.size() > MaximumManifestBytes) {
        return Result<std::pair<PackageManifest, std::vector<FileRecord>>>::failure(
            Error(ErrorCode::InvalidFormat, "source package requires root fabgl.package"));
    }
    const std::string sourceText(manifestEntry->bytes.begin(), manifestEntry->bytes.end());
    auto manifest = PackageManifestParser::parse(sourceText);
    if (!manifest)
        return Result<std::pair<PackageManifest, std::vector<FileRecord>>>::failure(
            manifest.error());
    manifest.value().schemaVersion = 2;
    manifest.value().id = std::string(manifest.value().stableId());
    manifest.value().name = manifest.value().id;
    if (manifest.value().displayName.empty())
        manifest.value().displayName = manifest.value().id;
    if (manifest.value().author.empty())
        manifest.value().author = "Unknown";
    if (manifest.value().spdxLicense.empty())
        manifest.value().spdxLicense = "NOASSERTION";
    manifest.value().localPath = "Packages/" + manifest.value().id;
    manifest.value().trust = PackageTrust::Untrusted;
    for (const auto& file : files.value()) {
        if (looksExecutable(file))
            manifest.value().containsExecutableCode = true;
    }
    auto canonical = PackageManifestParser::serializeCanonical(manifest.value());
    if (!canonical)
        return Result<std::pair<PackageManifest, std::vector<FileRecord>>>::failure(
            canonical.error());
    auto normalizedManifest = PackageManifestParser::parse(canonical.value());
    if (!normalizedManifest) {
        return Result<std::pair<PackageManifest, std::vector<FileRecord>>>::failure(
            Error(ErrorCode::InternalError, "canonical package manifest could not be reparsed"));
    }
    manifest.value() = std::move(normalizedManifest.value());
    manifestEntry->bytes = textBytes(canonical.value());
    std::uint64_t total = 0U;
    std::set<std::string> filePaths;
    for (const auto& file : files.value()) {
        const auto size = static_cast<std::uint64_t>(file.bytes.size());
        if (size > limits.maximumTotalBytes || total > limits.maximumTotalBytes - size) {
            return Result<std::pair<PackageManifest, std::vector<FileRecord>>>::failure(
                Error(ErrorCode::CapacityExceeded, "canonical package manifest exceeds limits"));
        }
        total += size;
        filePaths.insert(file.relativePath);
    }
    if (manifestEntry->bytes.size() > limits.maximumFileBytes || total > limits.maximumTotalBytes) {
        return Result<std::pair<PackageManifest, std::vector<FileRecord>>>::failure(
            Error(ErrorCode::CapacityExceeded, "canonical package manifest exceeds limits"));
    }
    for (const auto& entryPoint : manifest.value().entryPoints) {
        if (filePaths.find(entryPoint.path) == filePaths.end()) {
            return Result<std::pair<PackageManifest, std::vector<FileRecord>>>::failure(
                Error(ErrorCode::NotFound, "package entry point file is missing")
                    .addContext("entry", entryPoint.path));
        }
    }
    return Result<std::pair<PackageManifest, std::vector<FileRecord>>>::success(
        {std::move(manifest.value()), std::move(files.value())});
}

} // namespace

SemVersion currentPackageEngineVersion() {
    auto parsed = SemVersion::parse(FABGL_STUDIO_ENGINE_VERSION);
    return parsed ? parsed.value() : SemVersion{};
}

Result<LocalPackageInfo> installLocalPackage(const std::string& projectManifestPath,
                                             const std::string& sourceDirectory,
                                             const LocalPackageInstallOptions& options) {
    auto context = loadProject(projectManifestPath, true);
    if (!context)
        return Result<LocalPackageInfo>::failure(context.error());
    auto source = absolutePath(sourceDirectory, "package source");
    if (!source)
        return Result<LocalPackageInfo>::failure(source.error());
    auto sourceSafe = requireDirectory(source.value(), "package source");
    if (!sourceSafe)
        return Result<LocalPackageInfo>::failure(sourceSafe.error());
    if (pathInside(source.value(), context.value().projectRoot) ||
        pathInside(context.value().projectRoot, source.value())) {
        return Result<LocalPackageInfo>::failure(
            Error(ErrorCode::InvalidArgument,
                  "package source and project root must be separate directory trees"));
    }
    auto current = readProjectState(context.value(), options.limits, true);
    if (!current)
        return Result<LocalPackageInfo>::failure(current.error());
    auto prepared = prepareSourcePackage(source.value(), options.limits);
    if (!prepared)
        return Result<LocalPackageInfo>::failure(prepared.error());
    const auto id = std::string(prepared.value().first.stableId());
    const auto destination = context.value().packagesRoot / pathFromUtf8(id);
    std::error_code code;
    if (fs::exists(destination, code) || code) {
        return Result<LocalPackageInfo>::failure(
            Error(ErrorCode::AlreadyExists, "package destination already exists")
                .addContext("package", id));
    }
    if (prepared.value().first.containsExecutableCode && !options.allowExecutable) {
        return Result<LocalPackageInfo>::failure(
            Error(ErrorCode::InvalidState,
                  "package contains executable code; pass --allow-executable after review")
                .addContext("package", id));
    }
    LocalPackageInfo candidate;
    candidate.manifest = prepared.value().first;
    candidate.directory = pathText(destination);
    candidate.contentSha256 = packageDigest(prepared.value().second);
    candidate.fileCount = prepared.value().second.size();
    for (const auto& file : prepared.value().second)
        candidate.totalBytes += static_cast<std::uint64_t>(file.bytes.size());
    candidate.executableTrusted =
        candidate.manifest.containsExecutableCode && options.allowExecutable;

    auto combinedPackages = current.value().packages;
    combinedPackages.push_back(candidate);
    auto combinedTrust = current.value().trustRecords;
    if (candidate.executableTrusted) {
        combinedTrust.push_back({id, candidate.manifest.version, candidate.contentSha256});
    }
    std::vector<std::string> order;
    auto combinedValid = validateCombined(combinedPackages, combinedTrust, order);
    if (!combinedValid)
        return Result<LocalPackageInfo>::failure(combinedValid.error());

    fs::path staging;
    for (auto attempt = 0; attempt < 4; ++attempt) {
        // Keep private operation names short. Apart from avoiding noisy paths, this leaves room
        // for the package's own nested filenames below Windows' legacy path boundary while all
        // atomic file I/O itself uses the extended Win32 namespace.
        staging = context.value().packagesRoot / pathFromUtf8(".fgl-s-" + shortOperationToken());
        if (!fs::exists(staging, code) && !code)
            break;
        staging.clear();
        code.clear();
    }
    if (staging.empty()) {
        return Result<LocalPackageInfo>::failure(
            Error(ErrorCode::AlreadyExists, "unable to allocate package staging directory"));
    }
    auto staged = writePackageDirectory(staging, prepared.value().second, candidate);
    if (!staged) {
        static_cast<void>(removeStagedPackage(staging, context.value().packagesRoot, candidate));
        return Result<LocalPackageInfo>::failure(staged.error());
    }
    auto verified = inspectOwnedPackage(staging, id, options.limits);
    if (!verified || verified.value().contentSha256 != candidate.contentSha256) {
        static_cast<void>(removeStagedPackage(staging, context.value().packagesRoot, candidate));
        return Result<LocalPackageInfo>::failure(
            verified ? Error(ErrorCode::InvalidState, "staged package digest changed")
                     : verified.error());
    }
    renameDirectoryWithRetry(staging, destination, code);
    if (code) {
        static_cast<void>(removeStagedPackage(staging, context.value().packagesRoot, candidate));
        return Result<LocalPackageInfo>::failure(
            ioError("unable to promote staged package", destination, code));
    }
    auto metadata = writeMetadata(context.value(), combinedPackages, combinedTrust, order);
    if (!metadata) {
        auto error = metadata.error();
        auto rollbackMetadata =
            writeMetadata(context.value(), current.value().packages, current.value().trustRecords,
                          current.value().loadOrder);
        if (!rollbackMetadata)
            error.addContext("metadataRollback", rollbackMetadata.error().message());
        auto owned = inspectOwnedPackage(destination, id, options.limits);
        if (!owned || owned.value().contentSha256 != candidate.contentSha256) {
            error.addContext("directoryRollback", owned
                                                      ? "installed content changed during rollback"
                                                      : owned.error().message());
        } else {
            auto rollbackDirectory = removeKnownTree(destination, context.value().packagesRoot);
            if (!rollbackDirectory)
                error.addContext("directoryRollback", rollbackDirectory.error().message());
        }
        return Result<LocalPackageInfo>::failure(std::move(error));
    }
    return Result<LocalPackageInfo>::success(std::move(candidate));
}

Result<std::vector<LocalPackageInfo>> listLocalPackages(const std::string& projectManifestPath,
                                                        const LocalPackageLimits& limits) {
    auto context = loadProject(projectManifestPath, false);
    if (!context)
        return Result<std::vector<LocalPackageInfo>>::failure(context.error());
    auto state = readProjectState(context.value(), limits, true);
    if (!state)
        return Result<std::vector<LocalPackageInfo>>::failure(state.error());
    return Result<std::vector<LocalPackageInfo>>::success(std::move(state.value().packages));
}

Result<std::vector<std::string>> validateLocalPackages(const std::string& projectManifestPath,
                                                       const LocalPackageLimits& limits) {
    auto context = loadProject(projectManifestPath, false);
    if (!context)
        return Result<std::vector<std::string>>::failure(context.error());
    auto state = readProjectState(context.value(), limits, true);
    if (!state)
        return Result<std::vector<std::string>>::failure(state.error());
    return Result<std::vector<std::string>>::success(std::move(state.value().loadOrder));
}

Result<void> removeLocalPackage(const std::string& projectManifestPath,
                                const std::string& packageId, const LocalPackageLimits& limits) {
    PackageManifest idProbe;
    idProbe.name = packageId;
    idProbe.localPath = "Packages/" + packageId;
    auto idValid = PackageManifestParser::validateLocalManifest(idProbe);
    if (!idValid)
        return idValid;
    auto context = loadProject(projectManifestPath, false);
    if (!context)
        return Result<void>::failure(context.error());
    auto state = readProjectState(context.value(), limits, true);
    if (!state)
        return Result<void>::failure(state.error());
    const auto found = std::find_if(
        state.value().packages.begin(), state.value().packages.end(),
        [&packageId](const auto& package) { return package.manifest.stableId() == packageId; });
    if (found == state.value().packages.end()) {
        return Result<void>::failure(Error(ErrorCode::NotFound, "package is not installed")
                                         .addContext("package", packageId));
    }
    for (const auto& package : state.value().packages) {
        for (const auto& dependency : package.manifest.dependencies) {
            if (dependency.name == packageId) {
                return Result<void>::failure(
                    Error(ErrorCode::InvalidState, "installed package still depends on package")
                        .addContext("package", packageId)
                        .addContext("dependent", std::string(package.manifest.stableId())));
            }
        }
    }
    auto remainingPackages = state.value().packages;
    remainingPackages.erase(std::remove_if(remainingPackages.begin(), remainingPackages.end(),
                                           [&packageId](const auto& package) {
                                               return package.manifest.stableId() == packageId;
                                           }),
                            remainingPackages.end());
    auto remainingTrust = state.value().trustRecords;
    remainingTrust.erase(
        std::remove_if(remainingTrust.begin(), remainingTrust.end(),
                       [&packageId](const auto& record) { return record.id == packageId; }),
        remainingTrust.end());
    std::vector<std::string> order;
    auto combined = validateCombined(remainingPackages, remainingTrust, order);
    if (!combined)
        return combined;

    const auto destination = context.value().packagesRoot / pathFromUtf8(packageId);
    auto verified = inspectOwnedPackage(destination, packageId, limits);
    if (!verified || verified.value().contentSha256 != found->contentSha256)
        return Result<void>::failure(
            verified ? Error(ErrorCode::InvalidState, "package changed before removal")
                     : verified.error());
    const auto tombstone =
        context.value().packagesRoot / pathFromUtf8(".fgl-r-" + shortOperationToken());
    std::error_code code;
    fs::rename(destination, tombstone, code);
    if (code)
        return Result<void>::failure(ioError("unable to stage package removal", destination, code));
    auto metadata = writeMetadata(context.value(), remainingPackages, remainingTrust, order);
    if (!metadata) {
        auto error = metadata.error();
        std::error_code restoreCode;
        fs::rename(tombstone, destination, restoreCode);
        if (restoreCode)
            error.addContext("directoryRollback", restoreCode.message());
        auto rollbackMetadata = writeMetadata(context.value(), state.value().packages,
                                              state.value().trustRecords, state.value().loadOrder);
        if (!rollbackMetadata)
            error.addContext("metadataRollback", rollbackMetadata.error().message());
        return Result<void>::failure(std::move(error));
    }
    auto removed = removeKnownTree(tombstone, context.value().packagesRoot);
    if (!removed)
        return removed;
    return Result<void>::success();
}

} // namespace fabgl::project
