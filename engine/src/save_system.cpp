#include "fabgl/save/save_system.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>
#include <type_traits>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fabgl {
namespace {

namespace filesystem = std::filesystem;

constexpr std::string_view SaveFileExtension = ".fglsave";
constexpr std::size_t MaximumFileSaveBytes = 64U * 1024U * 1024U;
constexpr std::size_t MaximumBackupCount = 16U;
constexpr std::string_view StateMagic = "FGLSTAT1";
constexpr std::uint32_t StateFormatVersion = 1U;
constexpr std::size_t MaximumStateBytes = 16U * 1024U * 1024U;
constexpr std::size_t MaximumStateEntries = 16384U;
constexpr std::size_t MaximumEntityStates = 2048U;
constexpr std::size_t MaximumFieldsPerEntity = 256U;
constexpr std::size_t MaximumStateKeyBytes = 128U;
constexpr std::size_t MaximumStateStringBytes = 1024U * 1024U;

enum class SaveValueTag : std::uint8_t {
    Boolean = 1U,
    Integer = 2U,
    Number = 3U,
    String = 4U,
    Vector2 = 5U,
    Vector3 = 6U,
};

bool validStateKey(std::string_view key) noexcept {
    if (key.empty() || key.size() > MaximumStateKeyBytes)
        return false;
    return std::none_of(key.begin(), key.end(), [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return byte == 0U || byte < 0x20U || byte == 0x7FU;
    });
}

Result<void> appendBytes(std::string& output, std::string_view bytes) {
    if (bytes.size() > MaximumStateBytes - output.size()) {
        return Result<void>::failure(
            Error(ErrorCode::CapacityExceeded, "structured save exceeds the byte limit"));
    }
    output.append(bytes);
    return Result<void>::success();
}

Result<void> appendByte(std::string& output, std::uint8_t value) {
    const char byte = static_cast<char>(value);
    return appendBytes(output, std::string_view(&byte, 1U));
}

Result<void> appendU32(std::string& output, std::uint32_t value) {
    char bytes[4]{};
    for (std::size_t index = 0U; index < 4U; ++index) {
        bytes[index] = static_cast<char>((value >> (index * 8U)) & 0xFFU);
    }
    return appendBytes(output, std::string_view(bytes, 4U));
}

Result<void> appendU64(std::string& output, std::uint64_t value) {
    char bytes[8]{};
    for (std::size_t index = 0U; index < 8U; ++index) {
        bytes[index] = static_cast<char>((value >> (index * 8U)) & 0xFFU);
    }
    return appendBytes(output, std::string_view(bytes, 8U));
}

Result<void> appendLengthPrefixed(std::string& output, std::string_view value,
                                  std::size_t maximumBytes) {
    if (value.size() > maximumBytes ||
        value.size() > std::numeric_limits<std::uint32_t>::max()) {
        return Result<void>::failure(
            Error(ErrorCode::CapacityExceeded, "structured save string exceeds its limit"));
    }
    auto length = appendU32(output, static_cast<std::uint32_t>(value.size()));
    if (!length)
        return length;
    return appendBytes(output, value);
}

Result<void> appendSaveValue(std::string& output, const SaveValue& value) {
    return std::visit(
        [&output](const auto& concrete) -> Result<void> {
            using Value = std::decay_t<decltype(concrete)>;
            SaveValueTag tag = SaveValueTag::Boolean;
            if constexpr (std::is_same_v<Value, std::int64_t>)
                tag = SaveValueTag::Integer;
            else if constexpr (std::is_same_v<Value, double>)
                tag = SaveValueTag::Number;
            else if constexpr (std::is_same_v<Value, std::string>)
                tag = SaveValueTag::String;
            else if constexpr (std::is_same_v<Value, Vec2>)
                tag = SaveValueTag::Vector2;
            else if constexpr (std::is_same_v<Value, Vec3>)
                tag = SaveValueTag::Vector3;
            auto tagged = appendByte(output, static_cast<std::uint8_t>(tag));
            if (!tagged)
                return tagged;
            if constexpr (std::is_same_v<Value, bool>) {
                return appendByte(output, concrete ? 1U : 0U);
            } else if constexpr (std::is_same_v<Value, std::int64_t>) {
                return appendU64(output, std::bit_cast<std::uint64_t>(concrete));
            } else if constexpr (std::is_same_v<Value, double>) {
                if (!std::isfinite(concrete)) {
                    return Result<void>::failure(
                        Error(ErrorCode::InvalidArgument,
                              "structured save number must be finite"));
                }
                return appendU64(output, std::bit_cast<std::uint64_t>(concrete));
            } else if constexpr (std::is_same_v<Value, std::string>) {
                return appendLengthPrefixed(output, concrete, MaximumStateStringBytes);
            } else if constexpr (std::is_same_v<Value, Vec2>) {
                if (!std::isfinite(concrete.x) || !std::isfinite(concrete.y)) {
                    return Result<void>::failure(
                        Error(ErrorCode::InvalidArgument,
                              "structured save vector must be finite"));
                }
                auto x = appendU32(output, std::bit_cast<std::uint32_t>(concrete.x));
                return x ? appendU32(output, std::bit_cast<std::uint32_t>(concrete.y)) : x;
            } else {
                if (!std::isfinite(concrete.x) || !std::isfinite(concrete.y) ||
                    !std::isfinite(concrete.z)) {
                    return Result<void>::failure(
                        Error(ErrorCode::InvalidArgument,
                              "structured save vector must be finite"));
                }
                auto x = appendU32(output, std::bit_cast<std::uint32_t>(concrete.x));
                if (!x)
                    return x;
                auto y = appendU32(output, std::bit_cast<std::uint32_t>(concrete.y));
                return y ? appendU32(output, std::bit_cast<std::uint32_t>(concrete.z)) : y;
            }
        },
        value);
}

Result<void> appendStateMap(std::string& output, const SaveStateMap& values,
                            std::size_t maximumEntries, std::size_t& totalEntries) {
    if (values.size() > maximumEntries || values.size() > MaximumStateEntries - totalEntries ||
        values.size() > std::numeric_limits<std::uint32_t>::max()) {
        return Result<void>::failure(
            Error(ErrorCode::CapacityExceeded, "structured save map exceeds its entry limit"));
    }
    auto count = appendU32(output, static_cast<std::uint32_t>(values.size()));
    if (!count)
        return count;
    for (const auto& [key, value] : values) {
        if (!validStateKey(key)) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "structured save key is invalid")
                    .addContext("key", key));
        }
        auto keyResult = appendLengthPrefixed(output, key, MaximumStateKeyBytes);
        if (!keyResult)
            return keyResult;
        auto valueResult = appendSaveValue(output, value);
        if (!valueResult)
            return Result<void>::failure(valueResult.error().withContext("key", key));
    }
    totalEntries += values.size();
    return Result<void>::success();
}

class SaveDocumentReader final {
  public:
    explicit SaveDocumentReader(std::string_view input) : input_(input) {}

    [[nodiscard]] bool readByte(std::uint8_t& value) noexcept {
        if (position_ >= input_.size())
            return false;
        value = static_cast<std::uint8_t>(input_[position_++]);
        return true;
    }

    [[nodiscard]] bool readU32(std::uint32_t& value) noexcept {
        if (input_.size() - position_ < 4U)
            return false;
        value = 0U;
        for (std::size_t index = 0U; index < 4U; ++index) {
            value |= static_cast<std::uint32_t>(
                         static_cast<std::uint8_t>(input_[position_ + index]))
                     << (index * 8U);
        }
        position_ += 4U;
        return true;
    }

    [[nodiscard]] bool readU64(std::uint64_t& value) noexcept {
        if (input_.size() - position_ < 8U)
            return false;
        value = 0U;
        for (std::size_t index = 0U; index < 8U; ++index) {
            value |= static_cast<std::uint64_t>(
                         static_cast<std::uint8_t>(input_[position_ + index]))
                     << (index * 8U);
        }
        position_ += 8U;
        return true;
    }

    [[nodiscard]] bool readString(std::string& value, std::size_t maximumBytes) {
        std::uint32_t length = 0U;
        if (!readU32(length) || length > maximumBytes ||
            static_cast<std::size_t>(length) > input_.size() - position_) {
            return false;
        }
        value.assign(input_.substr(position_, length));
        position_ += length;
        return true;
    }

    [[nodiscard]] bool atEnd() const noexcept {
        return position_ == input_.size();
    }

  private:
    std::string_view input_;
    std::size_t position_ = 0U;
};

bool readSaveValue(SaveDocumentReader& reader, SaveValue& value) {
    std::uint8_t rawTag = 0U;
    if (!reader.readByte(rawTag))
        return false;
    const auto tag = static_cast<SaveValueTag>(rawTag);
    if (tag == SaveValueTag::Boolean) {
        std::uint8_t boolean = 0U;
        if (!reader.readByte(boolean) || boolean > 1U)
            return false;
        value = boolean != 0U;
        return true;
    }
    if (tag == SaveValueTag::Integer) {
        std::uint64_t bits = 0U;
        if (!reader.readU64(bits))
            return false;
        value = std::bit_cast<std::int64_t>(bits);
        return true;
    }
    if (tag == SaveValueTag::Number) {
        std::uint64_t bits = 0U;
        if (!reader.readU64(bits))
            return false;
        const auto number = std::bit_cast<double>(bits);
        if (!std::isfinite(number))
            return false;
        value = number;
        return true;
    }
    if (tag == SaveValueTag::String) {
        std::string text;
        if (!reader.readString(text, MaximumStateStringBytes))
            return false;
        value = std::move(text);
        return true;
    }
    if (tag == SaveValueTag::Vector2 || tag == SaveValueTag::Vector3) {
        std::uint32_t xBits = 0U;
        std::uint32_t yBits = 0U;
        if (!reader.readU32(xBits) || !reader.readU32(yBits))
            return false;
        const auto x = std::bit_cast<float>(xBits);
        const auto y = std::bit_cast<float>(yBits);
        if (!std::isfinite(x) || !std::isfinite(y))
            return false;
        if (tag == SaveValueTag::Vector2) {
            value = Vec2{x, y};
            return true;
        }
        std::uint32_t zBits = 0U;
        if (!reader.readU32(zBits))
            return false;
        const auto z = std::bit_cast<float>(zBits);
        if (!std::isfinite(z))
            return false;
        value = Vec3{x, y, z};
        return true;
    }
    return false;
}

bool readStateMap(SaveDocumentReader& reader, SaveStateMap& values,
                  std::size_t maximumEntries, std::size_t& totalEntries) {
    std::uint32_t count = 0U;
    if (!reader.readU32(count) || count > maximumEntries ||
        count > MaximumStateEntries - totalEntries)
        return false;
    for (std::uint32_t index = 0U; index < count; ++index) {
        std::string key;
        SaveValue value;
        if (!reader.readString(key, MaximumStateKeyBytes) || !validStateKey(key) ||
            !readSaveValue(reader, value) || !values.emplace(std::move(key), std::move(value)).second)
            return false;
    }
    totalEntries += count;
    return true;
}

void checksumByte(std::uint32_t& crc, std::uint8_t byte) noexcept {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
        const auto mask = static_cast<std::uint32_t>(-(static_cast<std::int32_t>(crc & 1U)));
        crc = (crc >> 1U) ^ (0xEDB88320U & mask);
    }
}

enum class IntegerParseStatus {
    Success,
    Invalid,
    OutOfRange,
};

IntegerParseStatus parseInteger(std::string_view text, int base, std::uint64_t& value) noexcept {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
        text.remove_prefix(1U);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
        text.remove_suffix(1U);

    if (text.empty() || text.front() == '+' || text.front() == '-')
        return IntegerParseStatus::Invalid;

    if (base == 16 && text.size() >= 2U && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text.remove_prefix(2U);
        if (text.empty())
            return IntegerParseStatus::Invalid;
    }

    const auto* const end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, value, base);
    if (result.ec == std::errc::result_out_of_range)
        return IntegerParseStatus::OutOfRange;
    if (result.ec != std::errc{} || result.ptr != end)
        return IntegerParseStatus::Invalid;
    return IntegerParseStatus::Success;
}

filesystem::path storagePath(std::string_view directory) {
#if defined(_WIN32)
    if (directory.empty() ||
        directory.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return {};
    const auto byteCount = static_cast<int>(directory.size());
    const auto characterCount =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, directory.data(), byteCount, nullptr, 0);
    if (characterCount <= 0)
        return {};
    std::wstring wide(static_cast<std::size_t>(characterCount), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, directory.data(), byteCount, wide.data(),
                            characterCount) != characterCount) {
        return {};
    }
    return filesystem::path(std::move(wide));
#else
    return filesystem::path(std::string(directory));
#endif
}

filesystem::path slotPath(const filesystem::path& directory, std::string_view slot) {
    return directory / (std::string(slot) + std::string(SaveFileExtension));
}

filesystem::path backupPath(const filesystem::path& livePath, std::size_t generation) {
    auto result = livePath;
    result += ".bak" + std::to_string(generation);
    return result;
}

filesystem::path temporaryPath(const filesystem::path& livePath) {
    auto result = livePath;
    result += ".part";
    return result;
}

Error storageError(std::string message, const filesystem::path& path,
                   const std::error_code& nativeError = {}) {
    auto error =
        Error(ErrorCode::IoError, std::move(message)).addContext("path", path.generic_string());
    if (nativeError)
        error.addContext("native_error", nativeError.message());
    return error;
}

Result<void> rejectSymlink(const filesystem::path& path) {
    std::error_code error;
    const auto status = filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory)
        return Result<void>::success();
    if (error)
        return Result<void>::failure(storageError("save path could not be inspected", path, error));
    if (filesystem::is_symlink(status)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "save storage refuses symbolic links")
                .addContext("path", path.generic_string()));
    }
    return Result<void>::success();
}

Result<std::string> readStorageFile(const filesystem::path& path) {
    auto safe = rejectSymlink(path);
    if (!safe)
        return Result<std::string>::failure(safe.error());

    std::error_code error;
    if (!filesystem::is_regular_file(path, error)) {
        if (error)
            return Result<std::string>::failure(
                storageError("save slot could not be inspected", path, error));
        return Result<std::string>::failure(Error(ErrorCode::NotFound, "save slot was not found")
                                                .addContext("path", path.generic_string()));
    }
    const auto byteCount = filesystem::file_size(path, error);
    if (error)
        return Result<std::string>::failure(
            storageError("save slot size could not be read", path, error));
    if (byteCount > MaximumFileSaveBytes) {
        return Result<std::string>::failure(
            Error(ErrorCode::CapacityExceeded, "save slot exceeds the bounded file size")
                .addContext("path", path.generic_string())
                .addContext("bytes", std::to_string(byteCount)));
    }

    std::ifstream input(path, std::ios::binary);
    if (!input)
        return Result<std::string>::failure(storageError("save slot could not be opened", path));
    std::string data(static_cast<std::size_t>(byteCount), '\0');
    if (!data.empty())
        input.read(data.data(), static_cast<std::streamsize>(data.size()));
    if (!input || input.peek() != std::char_traits<char>::eof()) {
        return Result<std::string>::failure(
            storageError("save slot could not be read completely", path));
    }
    return Result<std::string>::success(std::move(data));
}

Result<void> replaceStorageFile(const filesystem::path& source,
                                const filesystem::path& destination) {
#if defined(_WIN32)
    if (MoveFileExW(source.c_str(), destination.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
        const std::error_code error(static_cast<int>(GetLastError()), std::system_category());
        return Result<void>::failure(
            storageError("save slot could not be committed atomically", destination, error));
    }
#else
    std::error_code error;
    filesystem::rename(source, destination, error);
    if (error) {
        return Result<void>::failure(
            storageError("save slot could not be committed atomically", destination, error));
    }
#endif
    return Result<void>::success();
}

Result<std::uint32_t> parseUnsigned(std::string_view text, const char* field) {
    std::uint64_t value = 0;
    const auto status = parseInteger(text, 10, value);
    if (status == IntegerParseStatus::Invalid) {
        return Result<std::uint32_t>::failure(
            Error(ErrorCode::InvalidFormat, "save header contains an invalid integer")
                .addContext("field", field));
    }
    if (status == IntegerParseStatus::OutOfRange ||
        value > std::numeric_limits<std::uint32_t>::max()) {
        return Result<std::uint32_t>::failure(
            Error(ErrorCode::InvalidFormat, "save header integer is out of range")
                .addContext("field", field));
    }
    return Result<std::uint32_t>::success(static_cast<std::uint32_t>(value));
}

Result<std::size_t> parseSize(std::string_view text) {
    std::uint64_t value = 0;
    const auto status = parseInteger(text, 10, value);
    if (status == IntegerParseStatus::Invalid) {
        return Result<std::size_t>::failure(
            Error(ErrorCode::InvalidFormat, "invalid save payload size"));
    }
    if (status == IntegerParseStatus::OutOfRange ||
        value > std::numeric_limits<std::size_t>::max()) {
        return Result<std::size_t>::failure(
            Error(ErrorCode::InvalidFormat, "save payload size is out of range"));
    }
    return Result<std::size_t>::success(static_cast<std::size_t>(value));
}

Result<std::uint32_t> parseHex(std::string_view text) {
    std::uint64_t value = 0;
    const auto status = parseInteger(text, 16, value);
    if (status != IntegerParseStatus::Success ||
        value > std::numeric_limits<std::uint32_t>::max()) {
        return Result<std::uint32_t>::failure(
            Error(ErrorCode::InvalidFormat, "invalid save checksum"));
    }
    return Result<std::uint32_t>::success(static_cast<std::uint32_t>(value));
}

} // namespace

Result<void> MemorySaveStorage::writeAtomically(std::string_view slot, std::string data) {
    data_[std::string(slot)] = std::move(data);
    return Result<void>::success();
}

Result<std::string> MemorySaveStorage::read(std::string_view slot) const {
    const auto iterator = data_.find(std::string(slot));
    if (iterator == data_.end()) {
        return Result<std::string>::failure(Error(ErrorCode::NotFound, "save slot was not found")
                                                .addContext("slot", std::string(slot)));
    }
    return Result<std::string>::success(iterator->second);
}

Result<void> MemorySaveStorage::remove(std::string_view slot) {
    if (data_.erase(std::string(slot)) == 0U) {
        return Result<void>::failure(Error(ErrorCode::NotFound, "save slot was not found")
                                         .addContext("slot", std::string(slot)));
    }
    return Result<void>::success();
}

std::vector<std::string> MemorySaveStorage::slots() const {
    std::vector<std::string> result;
    result.reserve(data_.size());
    for (const auto& entry : data_)
        result.push_back(entry.first);
    return result;
}

FileSaveStorage::FileSaveStorage(std::string directory, const std::size_t backupCount)
    : directory_(std::move(directory)), backupCount_(std::min(backupCount, MaximumBackupCount)) {}

Result<void> FileSaveStorage::validateSlot(std::string_view slot) const {
    if (slot.empty() || slot.size() > 64U) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "save slot name has an invalid length"));
    }
    for (const auto character : slot) {
        const auto byte = static_cast<unsigned char>(character);
        if (!std::isalnum(byte) && character != '_' && character != '-') {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "save slot name contains an unsafe character"));
        }
    }
    return Result<void>::success();
}

Result<void> FileSaveStorage::writeAtomically(std::string_view slot, std::string data) {
    auto valid = validateSlot(slot);
    if (!valid)
        return valid;
    if (directory_.empty()) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "save storage directory is empty"));
    }
    if (data.size() > MaximumFileSaveBytes) {
        return Result<void>::failure(
            Error(ErrorCode::CapacityExceeded, "save data exceeds the bounded file size"));
    }

    const auto directory = storagePath(directory_);
    std::error_code error;
    filesystem::create_directories(directory, error);
    if (error)
        return Result<void>::failure(
            storageError("save storage directory could not be created", directory, error));
    auto safeDirectory = rejectSymlink(directory);
    if (!safeDirectory)
        return safeDirectory;
    if (!filesystem::is_directory(directory, error) || error) {
        return Result<void>::failure(
            storageError("save storage path is not a directory", directory, error));
    }

    const auto live = slotPath(directory, slot);
    const auto temporary = temporaryPath(live);
    for (const auto& path : {live, temporary}) {
        auto safe = rejectSymlink(path);
        if (!safe)
            return safe;
    }
    for (std::size_t generation = 1U; generation <= backupCount_; ++generation) {
        auto safe = rejectSymlink(backupPath(live, generation));
        if (!safe)
            return safe;
    }

    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
            return Result<void>::failure(
                storageError("save temporary file could not be opened", temporary));
        if (!data.empty())
            output.write(data.data(), static_cast<std::streamsize>(data.size()));
        output.flush();
        if (!output) {
            output.close();
            filesystem::remove(temporary, error);
            return Result<void>::failure(
                storageError("save temporary file could not be written", temporary));
        }
    }

    const bool hasLiveFile = filesystem::exists(live, error);
    if (error) {
        filesystem::remove(temporary, error);
        return Result<void>::failure(storageError("save slot could not be inspected", live, error));
    }
    if (hasLiveFile && backupCount_ > 0U) {
        for (std::size_t generation = backupCount_; generation > 1U; --generation) {
            const auto older = backupPath(live, generation - 1U);
            const auto newer = backupPath(live, generation);
            const bool hasOlder = filesystem::exists(older, error);
            if (error) {
                filesystem::remove(temporary, error);
                return Result<void>::failure(
                    storageError("save backup could not be inspected", older, error));
            }
            if (!hasOlder)
                continue;
            filesystem::remove(newer, error);
            if (error) {
                filesystem::remove(temporary, error);
                return Result<void>::failure(
                    storageError("old save backup could not be replaced", newer, error));
            }
            filesystem::rename(older, newer, error);
            if (error) {
                filesystem::remove(temporary, error);
                return Result<void>::failure(
                    storageError("save backup could not be rotated", older, error));
            }
        }
        filesystem::copy_file(live, backupPath(live, 1U),
                              filesystem::copy_options::overwrite_existing, error);
        if (error) {
            filesystem::remove(temporary, error);
            return Result<void>::failure(
                storageError("current save could not be backed up", live, error));
        }
    }

    auto replaced = replaceStorageFile(temporary, live);
    if (!replaced) {
        filesystem::remove(temporary, error);
        return replaced;
    }
    return Result<void>::success();
}

Result<std::string> FileSaveStorage::read(std::string_view slot) const {
    auto valid = validateSlot(slot);
    if (!valid)
        return Result<std::string>::failure(valid.error());
    if (directory_.empty()) {
        return Result<std::string>::failure(
            Error(ErrorCode::InvalidState, "save storage directory is empty"));
    }
    return readStorageFile(slotPath(storagePath(directory_), slot));
}

Result<std::string> FileSaveStorage::readBackup(std::string_view slot,
                                                const std::size_t generation) const {
    auto valid = validateSlot(slot);
    if (!valid)
        return Result<std::string>::failure(valid.error());
    if (directory_.empty()) {
        return Result<std::string>::failure(
            Error(ErrorCode::InvalidState, "save storage directory is empty"));
    }
    if (generation == 0U || generation > backupCount_) {
        return Result<std::string>::failure(
            Error(ErrorCode::InvalidArgument, "save backup generation is out of range"));
    }
    const auto live = slotPath(storagePath(directory_), slot);
    return readStorageFile(backupPath(live, generation));
}

Result<void> FileSaveStorage::remove(std::string_view slot) {
    auto valid = validateSlot(slot);
    if (!valid)
        return valid;
    if (directory_.empty()) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "save storage directory is empty"));
    }

    const auto live = slotPath(storagePath(directory_), slot);
    auto safe = rejectSymlink(live);
    if (!safe)
        return safe;
    std::error_code error;
    if (!filesystem::exists(live, error)) {
        if (error)
            return Result<void>::failure(
                storageError("save slot could not be inspected", live, error));
        return Result<void>::failure(Error(ErrorCode::NotFound, "save slot was not found")
                                         .addContext("slot", std::string(slot)));
    }

    std::vector<filesystem::path> auxiliaryPaths;
    auxiliaryPaths.reserve(backupCount_ + 1U);
    auxiliaryPaths.push_back(temporaryPath(live));
    for (std::size_t generation = 1U; generation <= backupCount_; ++generation)
        auxiliaryPaths.push_back(backupPath(live, generation));
    for (const auto& path : auxiliaryPaths) {
        safe = rejectSymlink(path);
        if (!safe)
            return safe;
        filesystem::remove(path, error);
        if (error)
            return Result<void>::failure(
                storageError("save auxiliary file could not be removed", path, error));
    }
    filesystem::remove(live, error);
    if (error)
        return Result<void>::failure(storageError("save slot could not be removed", live, error));
    return Result<void>::success();
}

std::vector<std::string> FileSaveStorage::slots() const {
    std::vector<std::string> result;
    if (directory_.empty())
        return result;
    const auto directory = storagePath(directory_);
    std::error_code error;
    if (!filesystem::is_directory(directory, error) || error)
        return result;
    filesystem::directory_iterator iterator(
        directory, filesystem::directory_options::skip_permission_denied, error);
    const filesystem::directory_iterator end;
    while (!error && iterator != end) {
        const auto& entry = *iterator;
        std::error_code entryError;
        const auto status = entry.symlink_status(entryError);
        if (!entryError && filesystem::is_regular_file(status) &&
            entry.path().extension().string() == ".fglsave") {
            const auto name = entry.path().stem().string();
            if (validateSlot(name))
                result.push_back(name);
        }
        iterator.increment(error);
    }
    std::sort(result.begin(), result.end());
    return result;
}

SaveSystem::SaveSystem(std::shared_ptr<ISaveStorage> storage, std::uint32_t currentSchemaVersion)
    : storage_(std::move(storage)), currentSchemaVersion_(currentSchemaVersion) {}

Result<void> SaveSystem::registerMigration(std::uint32_t fromVersion, Migration migration) {
    if (!migration || fromVersion == 0U || fromVersion >= currentSchemaVersion_) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "save migration is invalid"));
    }
    if (migrations_.find(fromVersion) != migrations_.end()) {
        return Result<void>::failure(
            Error(ErrorCode::AlreadyExists, "save migration is already registered")
                .addContext("from_version", std::to_string(fromVersion)));
    }
    migrations_.emplace(fromVersion, std::move(migration));
    return Result<void>::success();
}

Result<void> SaveSystem::validateSlot(std::string_view slot) const {
    if (slot.empty() || slot.size() > 64U) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "save slot name has an invalid length"));
    }
    for (const auto character : slot) {
        const auto byte = static_cast<unsigned char>(character);
        if (!std::isalnum(byte) && character != '_' && character != '-') {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "save slot name contains an unsafe character"));
        }
    }
    return Result<void>::success();
}

std::uint32_t SaveSystem::checksum(std::uint32_t schemaVersion, std::string_view payload) noexcept {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (int shift = 0; shift < 32; shift += 8) {
        checksumByte(crc, static_cast<std::uint8_t>(
                              (schemaVersion >> static_cast<unsigned int>(shift)) & 0xFFU));
    }
    for (const auto character : payload)
        checksumByte(crc, static_cast<std::uint8_t>(character));
    return crc ^ 0xFFFFFFFFU;
}

Result<std::string> SaveSystem::serializeDocument(const SaveDocument& document) {
    if (document.entities.size() > MaximumEntityStates ||
        document.entities.size() > std::numeric_limits<std::uint32_t>::max()) {
        return Result<std::string>::failure(
            Error(ErrorCode::CapacityExceeded, "structured save has too many entity states"));
    }
    std::string output;
    output.reserve(1024U);
    auto magic = appendBytes(output, StateMagic);
    if (!magic)
        return Result<std::string>::failure(magic.error());
    auto version = appendU32(output, StateFormatVersion);
    if (!version)
        return Result<std::string>::failure(version.error());
    std::size_t totalEntries = 0U;
    for (const auto* section : {&document.primitives, &document.player, &document.scene}) {
        auto written = appendStateMap(output, *section, MaximumStateEntries, totalEntries);
        if (!written)
            return Result<std::string>::failure(written.error());
    }
    auto entityCount = appendU32(output, static_cast<std::uint32_t>(document.entities.size()));
    if (!entityCount)
        return Result<std::string>::failure(entityCount.error());
    for (const auto& [entityKey, state] : document.entities) {
        if (!validStateKey(entityKey)) {
            return Result<std::string>::failure(
                Error(ErrorCode::InvalidArgument, "structured save entity key is invalid")
                    .addContext("entity", entityKey));
        }
        auto key = appendLengthPrefixed(output, entityKey, MaximumStateKeyBytes);
        if (!key)
            return Result<std::string>::failure(key.error());
        auto map = appendStateMap(output, state, MaximumFieldsPerEntity, totalEntries);
        if (!map) {
            return Result<std::string>::failure(
                map.error().withContext("entity", entityKey));
        }
    }
    return Result<std::string>::success(std::move(output));
}

Result<SaveDocument> SaveSystem::deserializeDocument(std::string_view payload) {
    if (payload.size() > MaximumStateBytes || payload.size() < StateMagic.size() + 4U ||
        payload.substr(0U, StateMagic.size()) != StateMagic) {
        return Result<SaveDocument>::failure(
            Error(ErrorCode::InvalidFormat, "structured save magic or size is invalid"));
    }
    SaveDocumentReader reader(payload.substr(StateMagic.size()));
    std::uint32_t version = 0U;
    if (!reader.readU32(version)) {
        return Result<SaveDocument>::failure(
            Error(ErrorCode::InvalidFormat, "structured save version is truncated"));
    }
    if (version != StateFormatVersion) {
        return Result<SaveDocument>::failure(
            Error(ErrorCode::UnsupportedVersion, "unsupported structured save version")
                .addContext("version", std::to_string(version)));
    }

    SaveDocument document;
    std::size_t totalEntries = 0U;
    if (!readStateMap(reader, document.primitives, MaximumStateEntries, totalEntries) ||
        !readStateMap(reader, document.player, MaximumStateEntries, totalEntries) ||
        !readStateMap(reader, document.scene, MaximumStateEntries, totalEntries)) {
        return Result<SaveDocument>::failure(
            Error(ErrorCode::InvalidFormat, "structured save section is invalid"));
    }
    std::uint32_t entityCount = 0U;
    if (!reader.readU32(entityCount) || entityCount > MaximumEntityStates) {
        return Result<SaveDocument>::failure(
            Error(ErrorCode::InvalidFormat, "structured save entity count is invalid"));
    }
    for (std::uint32_t index = 0U; index < entityCount; ++index) {
        std::string entityKey;
        SaveStateMap state;
        if (!reader.readString(entityKey, MaximumStateKeyBytes) || !validStateKey(entityKey) ||
            !readStateMap(reader, state, MaximumFieldsPerEntity, totalEntries) ||
            !document.entities.emplace(std::move(entityKey), std::move(state)).second) {
            return Result<SaveDocument>::failure(
                Error(ErrorCode::InvalidFormat, "structured save entity state is invalid"));
        }
    }
    if (!reader.atEnd()) {
        return Result<SaveDocument>::failure(
            Error(ErrorCode::InvalidFormat, "structured save has trailing data"));
    }
    return Result<SaveDocument>::success(std::move(document));
}

Result<void> SaveSystem::save(std::string_view slot, std::string payload) {
    auto validSlot = validateSlot(slot);
    if (!validSlot)
        return validSlot;
    if (!storage_ || currentSchemaVersion_ == 0U) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "save system is not configured"));
    }
    const auto crc = checksum(currentSchemaVersion_, payload);
    std::ostringstream stream;
    stream << "FGLSAVE " << FormatVersion << '\n';
    stream << "schema " << currentSchemaVersion_ << '\n';
    stream << "size " << payload.size() << '\n';
    stream << "checksum " << std::hex << std::setw(8) << std::setfill('0') << crc << "\n\n";
    stream << payload;
    return storage_->writeAtomically(slot, stream.str());
}

Result<void> SaveSystem::saveDocument(std::string_view slot, const SaveDocument& document) {
    auto payload = serializeDocument(document);
    if (!payload)
        return Result<void>::failure(payload.error());
    return save(slot, std::move(payload.value()));
}

Result<LoadedSave> SaveSystem::load(std::string_view slot) const {
    auto validSlot = validateSlot(slot);
    if (!validSlot)
        return Result<LoadedSave>::failure(validSlot.error());
    if (!storage_ || currentSchemaVersion_ == 0U) {
        return Result<LoadedSave>::failure(
            Error(ErrorCode::InvalidState, "save system is not configured"));
    }
    auto stored = storage_->read(slot);
    if (!stored)
        return Result<LoadedSave>::failure(stored.error());
    const auto separator = stored.value().find("\n\n");
    if (separator == std::string::npos) {
        return Result<LoadedSave>::failure(
            Error(ErrorCode::InvalidFormat, "save header terminator is missing"));
    }
    const auto header = stored.value().substr(0, separator);
    auto payload = stored.value().substr(separator + 2U);
    std::istringstream lines(header);
    std::string line;
    std::map<std::string, std::string> fields;
    while (std::getline(lines, line)) {
        const auto split = line.find(' ');
        if (split == std::string::npos ||
            !fields.emplace(line.substr(0, split), line.substr(split + 1U)).second) {
            return Result<LoadedSave>::failure(
                Error(ErrorCode::InvalidFormat, "save header line is invalid"));
        }
    }
    const auto magic = fields.find("FGLSAVE");
    const auto schemaField = fields.find("schema");
    const auto sizeField = fields.find("size");
    const auto checksumField = fields.find("checksum");
    if (magic == fields.end() || schemaField == fields.end() || sizeField == fields.end() ||
        checksumField == fields.end() || fields.size() != 4U) {
        return Result<LoadedSave>::failure(
            Error(ErrorCode::InvalidFormat, "save header fields are incomplete"));
    }
    auto format = parseUnsigned(magic->second, "format");
    if (!format)
        return Result<LoadedSave>::failure(format.error());
    if (format.value() != FormatVersion) {
        return Result<LoadedSave>::failure(
            Error(ErrorCode::UnsupportedVersion, "unsupported save format version"));
    }
    auto schema = parseUnsigned(schemaField->second, "schema");
    if (!schema)
        return Result<LoadedSave>::failure(schema.error());
    auto expectedSize = parseSize(sizeField->second);
    if (!expectedSize)
        return Result<LoadedSave>::failure(expectedSize.error());
    auto expectedChecksum = parseHex(checksumField->second);
    if (!expectedChecksum)
        return Result<LoadedSave>::failure(expectedChecksum.error());
    if (payload.size() != expectedSize.value() ||
        checksum(schema.value(), payload) != expectedChecksum.value()) {
        return Result<LoadedSave>::failure(
            Error(ErrorCode::InvalidFormat, "save payload checksum/size mismatch"));
    }
    if (schema.value() > currentSchemaVersion_) {
        return Result<LoadedSave>::failure(
            Error(ErrorCode::UnsupportedVersion, "save schema is newer than runtime"));
    }

    LoadedSave loaded;
    loaded.storedSchemaVersion = schema.value();
    loaded.currentSchemaVersion = currentSchemaVersion_;
    auto version = schema.value();
    while (version < currentSchemaVersion_) {
        const auto migration = migrations_.find(version);
        if (migration == migrations_.end()) {
            return Result<LoadedSave>::failure(
                Error(ErrorCode::UnsupportedVersion, "required save migration is missing")
                    .addContext("from_version", std::to_string(version)));
        }
        auto migrated = migration->second(payload);
        if (!migrated) {
            return Result<LoadedSave>::failure(
                migrated.error().withContext("from_version", std::to_string(version)));
        }
        payload = std::move(migrated.value());
        ++version;
        loaded.migrated = true;
    }
    loaded.payload = std::move(payload);
    return Result<LoadedSave>::success(std::move(loaded));
}

Result<LoadedSaveDocument> SaveSystem::loadDocument(std::string_view slot) const {
    auto loaded = load(slot);
    if (!loaded)
        return Result<LoadedSaveDocument>::failure(loaded.error());
    auto document = deserializeDocument(loaded.value().payload);
    if (!document)
        return Result<LoadedSaveDocument>::failure(document.error());
    LoadedSaveDocument result;
    result.document = std::move(document.value());
    result.storedSchemaVersion = loaded.value().storedSchemaVersion;
    result.currentSchemaVersion = loaded.value().currentSchemaVersion;
    result.migrated = loaded.value().migrated;
    return Result<LoadedSaveDocument>::success(std::move(result));
}

Result<void> SaveSystem::remove(std::string_view slot) {
    auto valid = validateSlot(slot);
    if (!valid)
        return valid;
    if (!storage_)
        return Result<void>::failure(Error(ErrorCode::InvalidState, "save storage is missing"));
    return storage_->remove(slot);
}

std::vector<std::string> SaveSystem::slots() const {
    return storage_ ? storage_->slots() : std::vector<std::string>{};
}

} // namespace fabgl
