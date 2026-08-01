#include "fabgl/save/save_system.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace fabgl {
namespace {

void checksumByte(std::uint32_t& crc, std::uint8_t byte) noexcept {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
        const auto mask = static_cast<std::uint32_t>(-(static_cast<std::int32_t>(crc & 1U)));
        crc = (crc >> 1U) ^ (0xEDB88320U & mask);
    }
}

Result<std::uint32_t> parseUnsigned(std::string_view text, const char* field) {
    std::istringstream stream{std::string(text)};
    std::uint64_t value = 0;
    if (!(stream >> value)) {
        return Result<std::uint32_t>::failure(
            Error(ErrorCode::InvalidFormat, "save header contains an invalid integer")
                .addContext("field", field));
    }
    stream >> std::ws;
    if (!stream.eof() || value > std::numeric_limits<std::uint32_t>::max()) {
        return Result<std::uint32_t>::failure(
            Error(ErrorCode::InvalidFormat, "save header integer is out of range")
                .addContext("field", field));
    }
    return Result<std::uint32_t>::success(static_cast<std::uint32_t>(value));
}

Result<std::size_t> parseSize(std::string_view text) {
    std::istringstream stream{std::string(text)};
    std::uint64_t value = 0;
    if (!(stream >> value)) {
        return Result<std::size_t>::failure(
            Error(ErrorCode::InvalidFormat, "invalid save payload size"));
    }
    stream >> std::ws;
    if (!stream.eof() || value > std::numeric_limits<std::size_t>::max()) {
        return Result<std::size_t>::failure(
            Error(ErrorCode::InvalidFormat, "save payload size is out of range"));
    }
    return Result<std::size_t>::success(static_cast<std::size_t>(value));
}

Result<std::uint32_t> parseHex(std::string_view text) {
    std::istringstream stream{std::string(text)};
    std::uint64_t value = 0;
    stream >> std::hex >> value;
    stream >> std::ws;
    if (stream.fail() || !stream.eof() || value > std::numeric_limits<std::uint32_t>::max()) {
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
