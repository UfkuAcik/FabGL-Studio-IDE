#pragma once

// Versioned, fixed-capacity gameplay save support for the ESP32 target runtime.
//
// This is intentionally separate from the editable .fglscene format. It has no
// allocator, iostream, filesystem or Arduino dependency, so the exact codec and
// transaction protocol are exercised by host tests and compiled by Xtensa.

#include "ProjectRuntime.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace fabgl_project_save {

static constexpr std::uint16_t kFormatVersion = 1U;
static constexpr std::uint16_t kDocumentVersion = 1U;
static constexpr std::uint32_t kDefaultSchemaVersion = 1U;
static constexpr std::size_t kHeaderBytes = 32U;
static constexpr std::size_t kMaximumFileBytes = 4U * 1024U;
static constexpr std::size_t kMaximumSlotLength = 24U;
static constexpr std::size_t kMaximumRootLength = 47U;
static constexpr std::size_t kMaximumSaveEntities = 16U;
static constexpr std::size_t kMaximumPrimitiveFields = 8U;
static constexpr std::size_t kMaximumSceneFields = 4U;
static constexpr std::size_t kMaximumPlayerFields = 4U;

enum class Error : std::uint8_t {
    None = 0U,
    InvalidArgument,
    CapacityExceeded,
    StorageUnavailable,
    NotFound,
    IoFailure,
    CommitFailed,
    RollbackFailed,
    InvalidFormat,
    UnsupportedVersion,
    ChecksumMismatch,
    SchemaTooNew,
    MigrationMissing,
    MigrationFailed,
    SceneMismatch,
    EntityMissing,
    PlayerMismatch,
};

enum class StorageStatus : std::uint8_t {
    Ok = 0U,
    NotFound,
    CapacityExceeded,
    IoFailure,
    CommitFailed,
    RollbackFailed,
};

struct Operation final {
    constexpr Operation(const Error errorValue = Error::None,
                        const StorageStatus storageValue = StorageStatus::Ok,
                        const std::size_t byteCount = 0U) noexcept
        : error(errorValue), storageStatus(storageValue), bytes(byteCount) {}

    Error error = Error::None;
    StorageStatus storageStatus = StorageStatus::Ok;
    std::size_t bytes = 0U;

    [[nodiscard]] bool ok() const noexcept {
        return error == Error::None;
    }
    explicit operator bool() const noexcept {
        return ok();
    }
};

struct Vector2 final {
    float x;
    float y;
};

struct Vector3 final {
    float x;
    float y;
    float z;
};

enum class ValueType : std::uint8_t {
    Empty = 0U,
    Boolean = 1U,
    Signed32 = 2U,
    Unsigned32 = 3U,
    Float32 = 4U,
    Vector2 = 5U,
    Vector3 = 6U,
    String = 7U,
    EntityGuid = 8U,
};

union ValuePayload {
    bool booleanValue;
    std::int32_t signedValue;
    std::uint32_t unsignedValue;
    float floatValue;
    Vector2 vector2Value;
    Vector3 vector3Value;
    char stringValue[64];
    std::uint8_t guidValue[16];

    ValuePayload() noexcept {
        std::memset(this, 0, sizeof(*this));
    }
};

struct Value final {
    ValueType type = ValueType::Empty;
    std::uint8_t stringLength = 0U;
    ValuePayload payload;

    static Value boolean(const bool value) noexcept {
        Value result;
        result.type = ValueType::Boolean;
        result.payload.booleanValue = value;
        return result;
    }
    static Value signed32(const std::int32_t value) noexcept {
        Value result;
        result.type = ValueType::Signed32;
        result.payload.signedValue = value;
        return result;
    }
    static Value unsigned32(const std::uint32_t value) noexcept {
        Value result;
        result.type = ValueType::Unsigned32;
        result.payload.unsignedValue = value;
        return result;
    }
    static Value real(const float value) noexcept {
        Value result;
        result.type = ValueType::Float32;
        result.payload.floatValue = value;
        return result;
    }
    static Value vector2(const float x, const float y) noexcept {
        Value result;
        result.type = ValueType::Vector2;
        result.payload.vector2Value.x = x;
        result.payload.vector2Value.y = y;
        return result;
    }
    static Value vector3(const float x, const float y, const float z) noexcept {
        Value result;
        result.type = ValueType::Vector3;
        result.payload.vector3Value.x = x;
        result.payload.vector3Value.y = y;
        result.payload.vector3Value.z = z;
        return result;
    }
    static Value string(const char* text) noexcept {
        Value result;
        if (text == nullptr)
            return result;
        std::size_t length = 0U;
        while (text[length] != '\0' && length < sizeof(result.payload.stringValue) - 1U)
            ++length;
        if (text[length] != '\0')
            return result;
        result.type = ValueType::String;
        result.stringLength = static_cast<std::uint8_t>(length);
        if (length != 0U)
            std::memcpy(result.payload.stringValue, text, length);
        result.payload.stringValue[length] = '\0';
        return result;
    }
    static Value entity(const fabgl_project_runtime::Guid& guid) noexcept {
        Value result;
        result.type = ValueType::EntityGuid;
        std::memcpy(result.payload.guidValue, guid.bytes, sizeof(guid.bytes));
        return result;
    }

    [[nodiscard]] bool valid() const noexcept {
        switch (type) {
        case ValueType::Boolean:
        case ValueType::Signed32:
        case ValueType::Unsigned32:
        case ValueType::EntityGuid:
            return true;
        case ValueType::Float32:
            return std::isfinite(payload.floatValue);
        case ValueType::Vector2:
            return std::isfinite(payload.vector2Value.x) && std::isfinite(payload.vector2Value.y);
        case ValueType::Vector3:
            return std::isfinite(payload.vector3Value.x) && std::isfinite(payload.vector3Value.y) &&
                   std::isfinite(payload.vector3Value.z);
        case ValueType::String:
            return stringLength < sizeof(payload.stringValue) &&
                   payload.stringValue[stringLength] == '\0';
        default:
            return false;
        }
    }
};

struct Field final {
    fabgl_project_runtime::Text<24> key;
    Value value;
};

struct EntityState final {
    fabgl_project_runtime::Guid guid;
    fabgl_project_runtime::Text<40> name;
    bool active = true;
    std::uint16_t components = 0U;
    std::int32_t movementMode = 0;
    Vector3 position{0.0F, 0.0F, 0.0F};
    float rotationZ = 0.0F;
    Vector2 scale{1.0F, 1.0F};
    float velocityY = 0.0F;
    float vehicleSpeed = 0.0F;
};

namespace detail {

template <std::size_t Capacity>
bool assignText(fabgl_project_runtime::Text<Capacity>& output, const char* text) noexcept {
    output.clear();
    if (text == nullptr)
        return false;
    while (*text != '\0') {
        const unsigned char character = static_cast<unsigned char>(*text++);
        if (character < 0x20U || !output.push(static_cast<char>(character))) {
            output.clear();
            return false;
        }
    }
    return true;
}

inline bool keyIsValid(const char* key) noexcept {
    if (key == nullptr || *key == '\0')
        return false;
    std::size_t length = 0U;
    while (key[length] != '\0') {
        const unsigned char character = static_cast<unsigned char>(key[length]);
        if (character < 0x21U || character > 0x7EU || character == '=' || ++length >= 24U) {
            return false;
        }
    }
    return true;
}

template <std::size_t Capacity>
bool putField(Field (&fields)[Capacity], std::size_t& count, const char* key,
              const Value& value) noexcept {
    if (!keyIsValid(key) || !value.valid())
        return false;
    for (std::size_t index = 0U; index < count; ++index) {
        if (fields[index].key.equals(key)) {
            fields[index].value = value;
            return true;
        }
    }
    if (count >= Capacity || !assignText(fields[count].key, key))
        return false;
    fields[count++].value = value;
    return true;
}

template <std::size_t Capacity>
const Value* findField(const Field (&fields)[Capacity], const std::size_t count,
                       const char* key) noexcept {
    if (!keyIsValid(key))
        return nullptr;
    for (std::size_t index = 0U; index < count; ++index) {
        if (fields[index].key.equals(key))
            return &fields[index].value;
    }
    return nullptr;
}

inline bool guidEqual(const fabgl_project_runtime::Guid& left,
                      const fabgl_project_runtime::Guid& right) noexcept {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

inline Error mapStorageError(const StorageStatus status) noexcept {
    switch (status) {
    case StorageStatus::NotFound:
        return Error::NotFound;
    case StorageStatus::CapacityExceeded:
        return Error::CapacityExceeded;
    case StorageStatus::CommitFailed:
        return Error::CommitFailed;
    case StorageStatus::RollbackFailed:
        return Error::RollbackFailed;
    case StorageStatus::IoFailure:
        return Error::IoFailure;
    default:
        return Error::None;
    }
}

} // namespace detail

struct Document final {
    std::uint32_t schemaVersion = kDefaultSchemaVersion;
    std::uint32_t sequence = 0U;
    fabgl_project_runtime::Guid sceneGuid;
    fabgl_project_runtime::Text<64> sceneName;
    fabgl_project_runtime::Guid playerGuid;
    Field primitives[kMaximumPrimitiveFields]{};
    std::size_t primitiveCount = 0U;
    Field sceneFields[kMaximumSceneFields]{};
    std::size_t sceneFieldCount = 0U;
    Field playerFields[kMaximumPlayerFields]{};
    std::size_t playerFieldCount = 0U;
    EntityState entities[kMaximumSaveEntities]{};
    std::size_t entityCount = 0U;

    bool putPrimitive(const char* key, const Value& value) noexcept {
        return detail::putField(primitives, primitiveCount, key, value);
    }
    bool putScene(const char* key, const Value& value) noexcept {
        return detail::putField(sceneFields, sceneFieldCount, key, value);
    }
    bool putPlayer(const char* key, const Value& value) noexcept {
        return detail::putField(playerFields, playerFieldCount, key, value);
    }
    [[nodiscard]] const Value* primitive(const char* key) const noexcept {
        return detail::findField(primitives, primitiveCount, key);
    }
    [[nodiscard]] const Value* scene(const char* key) const noexcept {
        return detail::findField(sceneFields, sceneFieldCount, key);
    }
    [[nodiscard]] const Value* player(const char* key) const noexcept {
        return detail::findField(playerFields, playerFieldCount, key);
    }
};

inline const fabgl_project_runtime::Entity*
findEntity(const fabgl_project_runtime::RuntimeProject& project,
           const fabgl_project_runtime::Guid& guid) noexcept {
    for (std::size_t index = 0U; index < project.scene.entityCount; ++index) {
        if (detail::guidEqual(project.scene.entities[index].guid, guid))
            return &project.scene.entities[index];
    }
    return nullptr;
}

inline fabgl_project_runtime::Entity* findEntity(fabgl_project_runtime::RuntimeProject& project,
                                                 const fabgl_project_runtime::Guid& guid) noexcept {
    for (std::size_t index = 0U; index < project.scene.entityCount; ++index) {
        if (detail::guidEqual(project.scene.entities[index].guid, guid))
            return &project.scene.entities[index];
    }
    return nullptr;
}

inline Operation captureRuntime(const fabgl_project_runtime::RuntimeProject& project,
                                const fabgl_project_runtime::Guid* player, Document& output,
                                const std::uint32_t schemaVersion,
                                const std::uint32_t sequence = 0U) noexcept {
    if (project.scene.entityCount > kMaximumSaveEntities)
        return {Error::CapacityExceeded, StorageStatus::Ok, 0U};
    if (!project.loaded || project.scene.guid.isNil() || schemaVersion == 0U) {
        return {Error::InvalidArgument, StorageStatus::Ok, 0U};
    }
    if (player != nullptr && (player->isNil() || findEntity(project, *player) == nullptr))
        return {Error::PlayerMismatch, StorageStatus::Ok, 0U};

    for (std::size_t left = 0U; left < project.scene.entityCount; ++left) {
        if (project.scene.entities[left].guid.isNil())
            return {Error::InvalidArgument, StorageStatus::Ok, 0U};
        for (std::size_t right = left + 1U; right < project.scene.entityCount; ++right) {
            if (detail::guidEqual(project.scene.entities[left].guid,
                                  project.scene.entities[right].guid)) {
                return {Error::InvalidArgument, StorageStatus::Ok, 0U};
            }
        }
    }

    output.schemaVersion = schemaVersion;
    output.sequence = sequence;
    output.sceneGuid = project.scene.guid;
    output.sceneName = project.scene.name;
    output.playerGuid = player == nullptr ? fabgl_project_runtime::Guid{} : *player;
    output.entityCount = project.scene.entityCount;
    for (std::size_t index = 0U; index < output.entityCount; ++index) {
        const auto& source = project.scene.entities[index];
        if (!std::isfinite(source.x) || !std::isfinite(source.y) || !std::isfinite(source.z) ||
            !std::isfinite(source.rotationZ) || !std::isfinite(source.scaleX) ||
            !std::isfinite(source.scaleY) || !std::isfinite(source.velocityY) ||
            !std::isfinite(source.vehicleSpeed)) {
            return {Error::InvalidArgument, StorageStatus::Ok, 0U};
        }
        auto& target = output.entities[index];
        target.guid = source.guid;
        target.name = source.name;
        target.active = source.active;
        target.components = source.components;
        target.movementMode = source.movementMode;
        target.position = {source.x, source.y, source.z};
        target.rotationZ = source.rotationZ;
        target.scale = {source.scaleX, source.scaleY};
        target.velocityY = source.velocityY;
        target.vehicleSpeed = source.vehicleSpeed;
    }
    return {Error::None, StorageStatus::Ok, 0U};
}

inline Operation
restoreRuntime(const Document& input, fabgl_project_runtime::RuntimeProject& project,
               const fabgl_project_runtime::Guid* expectedPlayer = nullptr) noexcept {
    if (input.entityCount > kMaximumSaveEntities)
        return {Error::CapacityExceeded, StorageStatus::Ok, 0U};
    if (!project.loaded || input.schemaVersion == 0U ||
        !detail::guidEqual(input.sceneGuid, project.scene.guid)) {
        return {Error::SceneMismatch, StorageStatus::Ok, 0U};
    }
    if (expectedPlayer != nullptr && !detail::guidEqual(input.playerGuid, *expectedPlayer)) {
        return {Error::PlayerMismatch, StorageStatus::Ok, 0U};
    }
    if (!input.playerGuid.isNil()) {
        std::size_t savedPlayerMatches = 0U;
        std::size_t runtimePlayerMatches = 0U;
        for (std::size_t index = 0U; index < input.entityCount; ++index)
            savedPlayerMatches +=
                detail::guidEqual(input.playerGuid, input.entities[index].guid) ? 1U : 0U;
        for (std::size_t index = 0U; index < project.scene.entityCount; ++index)
            runtimePlayerMatches +=
                detail::guidEqual(input.playerGuid, project.scene.entities[index].guid) ? 1U : 0U;
        if (savedPlayerMatches != 1U || runtimePlayerMatches != 1U)
            return {Error::PlayerMismatch, StorageStatus::Ok, 0U};
    }

    // Validate every stable identity and every numeric value before mutating the runtime. This
    // keeps a missing entity or malformed value from producing a partially restored scene.
    for (std::size_t index = 0U; index < input.entityCount; ++index) {
        const auto& source = input.entities[index];
        std::size_t targetMatches = 0U;
        for (std::size_t target = 0U; target < project.scene.entityCount; ++target) {
            if (detail::guidEqual(source.guid, project.scene.entities[target].guid))
                ++targetMatches;
        }
        if (source.guid.isNil() || targetMatches != 1U)
            return {Error::EntityMissing, StorageStatus::Ok, 0U};
        if (!std::isfinite(source.position.x) || !std::isfinite(source.position.y) ||
            !std::isfinite(source.position.z) || !std::isfinite(source.rotationZ) ||
            !std::isfinite(source.scale.x) || !std::isfinite(source.scale.y) ||
            !std::isfinite(source.velocityY) || !std::isfinite(source.vehicleSpeed)) {
            return {Error::InvalidFormat, StorageStatus::Ok, 0U};
        }
        for (std::size_t previous = 0U; previous < index; ++previous) {
            if (detail::guidEqual(source.guid, input.entities[previous].guid))
                return {Error::InvalidFormat, StorageStatus::Ok, 0U};
        }
    }

    project.scene.name = input.sceneName;
    for (std::size_t index = 0U; index < input.entityCount; ++index) {
        const auto& source = input.entities[index];
        auto* target = findEntity(project, source.guid);
        target->name = source.name;
        target->active = source.active;
        target->components = source.components;
        target->movementMode = source.movementMode;
        target->x = source.position.x;
        target->y = source.position.y;
        target->z = source.position.z;
        target->rotationZ = source.rotationZ;
        target->scaleX = source.scale.x;
        target->scaleY = source.scale.y;
        target->velocityY = source.velocityY;
        target->vehicleSpeed = source.vehicleSpeed;
    }
    return {Error::None, StorageStatus::Ok, 0U};
}

namespace detail {

class Writer final {
  public:
    Writer(std::uint8_t* bytes, const std::size_t capacity) noexcept
        : bytes_(bytes), capacity_(capacity) {}

    bool u8(const std::uint8_t value) noexcept {
        return raw(&value, 1U);
    }
    bool u16(const std::uint16_t value) noexcept {
        return u8(static_cast<std::uint8_t>(value & 0xFFU)) &&
               u8(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    }
    bool u32(const std::uint32_t value) noexcept {
        return u16(static_cast<std::uint16_t>(value & 0xFFFFU)) &&
               u16(static_cast<std::uint16_t>((value >> 16U) & 0xFFFFU));
    }
    bool i32(const std::int32_t value) noexcept {
        return u32(static_cast<std::uint32_t>(value));
    }
    bool real(const float value) noexcept {
        if (!std::isfinite(value))
            return false;
        std::uint32_t bits = 0U;
        static_assert(sizeof(bits) == sizeof(value), "float32 save ABI required");
        std::memcpy(&bits, &value, sizeof(bits));
        return u32(bits);
    }
    bool raw(const void* data, const std::size_t size) noexcept {
        if (data == nullptr || bytes_ == nullptr || position_ > capacity_ ||
            size > capacity_ - position_) {
            failed_ = true;
            return false;
        }
        if (size != 0U)
            std::memcpy(bytes_ + position_, data, size);
        position_ += size;
        return true;
    }
    template <std::size_t Capacity>
    bool text(const fabgl_project_runtime::Text<Capacity>& value) noexcept {
        return value.length < Capacity && value.length <= 255U &&
               value.value[value.length] == '\0' && u8(static_cast<std::uint8_t>(value.length)) &&
               raw(value.value, value.length);
    }
    bool patchU32(const std::size_t offset, const std::uint32_t value) noexcept {
        if (bytes_ == nullptr || offset > capacity_ || 4U > capacity_ - offset)
            return false;
        for (std::size_t index = 0U; index < 4U; ++index)
            bytes_[offset + index] = static_cast<std::uint8_t>(value >> (index * 8U));
        return true;
    }
    [[nodiscard]] std::size_t position() const noexcept {
        return position_;
    }
    [[nodiscard]] bool failed() const noexcept {
        return failed_;
    }

  private:
    std::uint8_t* bytes_ = nullptr;
    std::size_t capacity_ = 0U;
    std::size_t position_ = 0U;
    bool failed_ = false;
};

class Reader final {
  public:
    Reader(const std::uint8_t* bytes, const std::size_t size) noexcept
        : bytes_(bytes), size_(size) {}

    bool u8(std::uint8_t& value) noexcept {
        return raw(&value, 1U);
    }
    bool u16(std::uint16_t& value) noexcept {
        std::uint8_t low = 0U;
        std::uint8_t high = 0U;
        if (!u8(low) || !u8(high))
            return false;
        value = static_cast<std::uint16_t>(low) | (static_cast<std::uint16_t>(high) << 8U);
        return true;
    }
    bool u32(std::uint32_t& value) noexcept {
        std::uint16_t low = 0U;
        std::uint16_t high = 0U;
        if (!u16(low) || !u16(high))
            return false;
        value = static_cast<std::uint32_t>(low) | (static_cast<std::uint32_t>(high) << 16U);
        return true;
    }
    bool i32(std::int32_t& value) noexcept {
        std::uint32_t bits = 0U;
        if (!u32(bits))
            return false;
        value = static_cast<std::int32_t>(bits);
        return true;
    }
    bool real(float& value) noexcept {
        std::uint32_t bits = 0U;
        if (!u32(bits))
            return false;
        std::memcpy(&value, &bits, sizeof(value));
        return std::isfinite(value);
    }
    bool raw(void* output, const std::size_t size) noexcept {
        if (output == nullptr || bytes_ == nullptr || position_ > size_ ||
            size > size_ - position_) {
            failed_ = true;
            return false;
        }
        if (size != 0U)
            std::memcpy(output, bytes_ + position_, size);
        position_ += size;
        return true;
    }
    template <std::size_t Capacity>
    bool text(fabgl_project_runtime::Text<Capacity>& value) noexcept {
        std::uint8_t length = 0U;
        value.clear();
        if (!u8(length) || static_cast<std::size_t>(length) + 1U > Capacity || position_ > size_ ||
            length > size_ - position_) {
            failed_ = true;
            return false;
        }
        for (std::size_t index = 0U; index < length; ++index) {
            const unsigned char character = bytes_[position_++];
            if (character < 0x20U || !value.push(static_cast<char>(character))) {
                failed_ = true;
                return false;
            }
        }
        return true;
    }
    [[nodiscard]] std::size_t position() const noexcept {
        return position_;
    }
    [[nodiscard]] bool ended() const noexcept {
        return !failed_ && position_ == size_;
    }

  private:
    const std::uint8_t* bytes_ = nullptr;
    std::size_t size_ = 0U;
    std::size_t position_ = 0U;
    bool failed_ = false;
};

inline std::uint32_t crc32Update(std::uint32_t crc, const std::uint8_t byte) noexcept {
    crc ^= byte;
    for (unsigned int bit = 0U; bit < 8U; ++bit)
        crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    return crc;
}

inline std::uint32_t checksum(const std::uint32_t schemaVersion, const std::uint32_t sequence,
                              const std::uint8_t* payload, const std::size_t payloadSize) noexcept {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t index = 0U; index < 4U; ++index)
        crc = crc32Update(crc, static_cast<std::uint8_t>(schemaVersion >> (index * 8U)));
    for (std::size_t index = 0U; index < 4U; ++index)
        crc = crc32Update(crc, static_cast<std::uint8_t>(sequence >> (index * 8U)));
    for (std::size_t index = 0U; index < payloadSize; ++index)
        crc = crc32Update(crc, payload[index]);
    return crc ^ 0xFFFFFFFFU;
}

inline bool writeValue(Writer& writer, const Value& value) noexcept {
    if (!value.valid() || !writer.u8(static_cast<std::uint8_t>(value.type)))
        return false;
    switch (value.type) {
    case ValueType::Boolean:
        return writer.u8(value.payload.booleanValue ? 1U : 0U);
    case ValueType::Signed32:
        return writer.i32(value.payload.signedValue);
    case ValueType::Unsigned32:
        return writer.u32(value.payload.unsignedValue);
    case ValueType::Float32:
        return writer.real(value.payload.floatValue);
    case ValueType::Vector2:
        return writer.real(value.payload.vector2Value.x) &&
               writer.real(value.payload.vector2Value.y);
    case ValueType::Vector3:
        return writer.real(value.payload.vector3Value.x) &&
               writer.real(value.payload.vector3Value.y) &&
               writer.real(value.payload.vector3Value.z);
    case ValueType::String:
        return writer.u8(value.stringLength) &&
               writer.raw(value.payload.stringValue, value.stringLength);
    case ValueType::EntityGuid:
        return writer.raw(value.payload.guidValue, sizeof(value.payload.guidValue));
    default:
        return false;
    }
}

inline bool readValue(Reader& reader, Value& value) noexcept {
    std::uint8_t type = 0U;
    if (!reader.u8(type) || type < static_cast<std::uint8_t>(ValueType::Boolean) ||
        type > static_cast<std::uint8_t>(ValueType::EntityGuid)) {
        return false;
    }
    value = Value{};
    value.type = static_cast<ValueType>(type);
    switch (value.type) {
    case ValueType::Boolean: {
        std::uint8_t encoded = 0U;
        if (!reader.u8(encoded) || encoded > 1U)
            return false;
        value.payload.booleanValue = encoded != 0U;
        return true;
    }
    case ValueType::Signed32:
        return reader.i32(value.payload.signedValue);
    case ValueType::Unsigned32:
        return reader.u32(value.payload.unsignedValue);
    case ValueType::Float32:
        return reader.real(value.payload.floatValue);
    case ValueType::Vector2:
        return reader.real(value.payload.vector2Value.x) &&
               reader.real(value.payload.vector2Value.y);
    case ValueType::Vector3:
        return reader.real(value.payload.vector3Value.x) &&
               reader.real(value.payload.vector3Value.y) &&
               reader.real(value.payload.vector3Value.z);
    case ValueType::String:
        if (!reader.u8(value.stringLength) ||
            value.stringLength >= sizeof(value.payload.stringValue) ||
            !reader.raw(value.payload.stringValue, value.stringLength)) {
            return false;
        }
        value.payload.stringValue[value.stringLength] = '\0';
        return true;
    case ValueType::EntityGuid:
        return reader.raw(value.payload.guidValue, sizeof(value.payload.guidValue));
    default:
        return false;
    }
}

template <std::size_t Capacity>
bool writeFields(Writer& writer, const Field (&fields)[Capacity],
                 const std::size_t count) noexcept {
    if (count > Capacity || !writer.u8(static_cast<std::uint8_t>(count)))
        return false;
    for (std::size_t index = 0U; index < count; ++index) {
        if (fields[index].key.length >= sizeof(fields[index].key.value) ||
            fields[index].key.value[fields[index].key.length] != '\0' ||
            !keyIsValid(fields[index].key.value) || !writer.text(fields[index].key) ||
            !writeValue(writer, fields[index].value)) {
            return false;
        }
        for (std::size_t previous = 0U; previous < index; ++previous) {
            if (fields[index].key.equals(fields[previous].key))
                return false;
        }
    }
    return true;
}

template <std::size_t Capacity>
bool readFields(Reader& reader, Field (&fields)[Capacity], std::size_t& count) noexcept {
    std::uint8_t encodedCount = 0U;
    count = 0U;
    if (!reader.u8(encodedCount) || encodedCount > Capacity)
        return false;
    for (std::size_t index = 0U; index < encodedCount; ++index) {
        if (!reader.text(fields[index].key) || !keyIsValid(fields[index].key.value) ||
            !readValue(reader, fields[index].value)) {
            return false;
        }
        for (std::size_t previous = 0U; previous < index; ++previous) {
            if (fields[index].key.equals(fields[previous].key))
                return false;
        }
        ++count;
    }
    return true;
}

inline std::uint16_t readU16At(const std::uint8_t* bytes, const std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(bytes[offset]) |
           (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

inline std::uint32_t readU32At(const std::uint8_t* bytes, const std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

} // namespace detail

inline Operation encode(const Document& document, std::uint8_t* output,
                        const std::size_t capacity) noexcept {
    if (output == nullptr || document.schemaVersion == 0U || document.sceneGuid.isNil() ||
        document.entityCount > kMaximumSaveEntities || capacity < kHeaderBytes) {
        return {capacity < kHeaderBytes ? Error::CapacityExceeded : Error::InvalidArgument,
                StorageStatus::Ok, 0U};
    }
    detail::Writer writer(output, capacity);
    const char magic[] = {'F', 'G', 'L', 'S'};
    if (!writer.raw(magic, sizeof(magic)) || !writer.u16(kFormatVersion) ||
        !writer.u16(static_cast<std::uint16_t>(kHeaderBytes)) ||
        !writer.u32(document.schemaVersion) || !writer.u32(document.sequence) || !writer.u32(0U) ||
        !writer.u32(0U) || !writer.u32(static_cast<std::uint32_t>(document.entityCount)) ||
        !writer.u32(0U)) {
        return {Error::CapacityExceeded, StorageStatus::Ok, 0U};
    }

    const char payloadMagic[] = {'F', 'G', 'S', 'D'};
    if (!writer.raw(payloadMagic, sizeof(payloadMagic)) || !writer.u16(kDocumentVersion) ||
        !writer.u16(0U) ||
        !writer.raw(document.sceneGuid.bytes, sizeof(document.sceneGuid.bytes)) ||
        !writer.text(document.sceneName) ||
        !writer.raw(document.playerGuid.bytes, sizeof(document.playerGuid.bytes)) ||
        !detail::writeFields(writer, document.primitives, document.primitiveCount) ||
        !detail::writeFields(writer, document.sceneFields, document.sceneFieldCount) ||
        !detail::writeFields(writer, document.playerFields, document.playerFieldCount) ||
        !writer.u16(static_cast<std::uint16_t>(document.entityCount))) {
        return {Error::CapacityExceeded, StorageStatus::Ok, 0U};
    }

    for (std::size_t index = 0U; index < document.entityCount; ++index) {
        const auto& entity = document.entities[index];
        if (entity.guid.isNil() || !writer.raw(entity.guid.bytes, sizeof(entity.guid.bytes)) ||
            !writer.text(entity.name) || !writer.u8(entity.active ? 1U : 0U) ||
            !writer.u16(entity.components) || !writer.i32(entity.movementMode) ||
            !writer.real(entity.position.x) || !writer.real(entity.position.y) ||
            !writer.real(entity.position.z) || !writer.real(entity.rotationZ) ||
            !writer.real(entity.scale.x) || !writer.real(entity.scale.y) ||
            !writer.real(entity.velocityY) || !writer.real(entity.vehicleSpeed)) {
            return {writer.failed() ? Error::CapacityExceeded : Error::InvalidArgument,
                    StorageStatus::Ok, 0U};
        }
        for (std::size_t previous = 0U; previous < index; ++previous) {
            if (detail::guidEqual(entity.guid, document.entities[previous].guid))
                return {Error::InvalidArgument, StorageStatus::Ok, 0U};
        }
    }
    if (!document.playerGuid.isNil()) {
        bool playerFound = false;
        for (std::size_t index = 0U; index < document.entityCount; ++index)
            playerFound = playerFound ||
                          detail::guidEqual(document.playerGuid, document.entities[index].guid);
        if (!playerFound)
            return {Error::InvalidArgument, StorageStatus::Ok, 0U};
    }

    const std::size_t payloadSize = writer.position() - kHeaderBytes;
    if (writer.position() > kMaximumFileBytes ||
        !writer.patchU32(16U, static_cast<std::uint32_t>(payloadSize)) ||
        !writer.patchU32(20U, detail::checksum(document.schemaVersion, document.sequence,
                                               output + kHeaderBytes, payloadSize))) {
        return {Error::CapacityExceeded, StorageStatus::Ok, 0U};
    }
    return {Error::None, StorageStatus::Ok, writer.position()};
}

inline Operation decode(const std::uint8_t* input, const std::size_t size,
                        Document& output) noexcept {
    if (input == nullptr || size < kHeaderBytes || size > kMaximumFileBytes ||
        std::memcmp(input, "FGLS", 4U) != 0) {
        return {Error::InvalidFormat, StorageStatus::Ok, 0U};
    }
    const auto formatVersion = detail::readU16At(input, 4U);
    const auto headerSize = detail::readU16At(input, 6U);
    if (formatVersion != kFormatVersion)
        return {Error::UnsupportedVersion, StorageStatus::Ok, 0U};
    if (headerSize != kHeaderBytes)
        return {Error::InvalidFormat, StorageStatus::Ok, 0U};
    const auto schemaVersion = detail::readU32At(input, 8U);
    const auto sequence = detail::readU32At(input, 12U);
    const auto payloadSize = detail::readU32At(input, 16U);
    const auto expectedChecksum = detail::readU32At(input, 20U);
    const auto entityHint = detail::readU32At(input, 24U);
    const auto reserved = detail::readU32At(input, 28U);
    if (schemaVersion == 0U || payloadSize != size - kHeaderBytes || reserved != 0U ||
        entityHint > kMaximumSaveEntities)
        return {Error::InvalidFormat, StorageStatus::Ok, 0U};
    if (detail::checksum(schemaVersion, sequence, input + kHeaderBytes, payloadSize) !=
        expectedChecksum) {
        return {Error::ChecksumMismatch, StorageStatus::Ok, 0U};
    }

    detail::Reader reader(input + kHeaderBytes, payloadSize);
    char payloadMagic[4]{};
    std::uint16_t documentVersion = 0U;
    std::uint16_t documentReserved = 0U;
    output.primitiveCount = 0U;
    output.sceneFieldCount = 0U;
    output.playerFieldCount = 0U;
    output.entityCount = 0U;
    if (!reader.raw(payloadMagic, sizeof(payloadMagic)) ||
        std::memcmp(payloadMagic, "FGSD", sizeof(payloadMagic)) != 0 ||
        !reader.u16(documentVersion) || !reader.u16(documentReserved)) {
        return {Error::InvalidFormat, StorageStatus::Ok, 0U};
    }
    if (documentVersion != kDocumentVersion)
        return {Error::UnsupportedVersion, StorageStatus::Ok, 0U};
    if (documentReserved != 0U ||
        !reader.raw(output.sceneGuid.bytes, sizeof(output.sceneGuid.bytes)) ||
        output.sceneGuid.isNil() || !reader.text(output.sceneName) ||
        !reader.raw(output.playerGuid.bytes, sizeof(output.playerGuid.bytes)) ||
        !detail::readFields(reader, output.primitives, output.primitiveCount) ||
        !detail::readFields(reader, output.sceneFields, output.sceneFieldCount) ||
        !detail::readFields(reader, output.playerFields, output.playerFieldCount)) {
        return {Error::InvalidFormat, StorageStatus::Ok, 0U};
    }
    std::uint16_t entityCount = 0U;
    if (!reader.u16(entityCount) || entityCount != entityHint ||
        entityCount > kMaximumSaveEntities) {
        return {Error::InvalidFormat, StorageStatus::Ok, 0U};
    }
    for (std::size_t index = 0U; index < entityCount; ++index) {
        auto& entity = output.entities[index];
        std::uint8_t active = 0U;
        if (!reader.raw(entity.guid.bytes, sizeof(entity.guid.bytes)) || entity.guid.isNil() ||
            !reader.text(entity.name) || !reader.u8(active) || active > 1U ||
            !reader.u16(entity.components) || !reader.i32(entity.movementMode) ||
            !reader.real(entity.position.x) || !reader.real(entity.position.y) ||
            !reader.real(entity.position.z) || !reader.real(entity.rotationZ) ||
            !reader.real(entity.scale.x) || !reader.real(entity.scale.y) ||
            !reader.real(entity.velocityY) || !reader.real(entity.vehicleSpeed)) {
            return {Error::InvalidFormat, StorageStatus::Ok, 0U};
        }
        entity.active = active != 0U;
        for (std::size_t previous = 0U; previous < index; ++previous) {
            if (detail::guidEqual(entity.guid, output.entities[previous].guid))
                return {Error::InvalidFormat, StorageStatus::Ok, 0U};
        }
        ++output.entityCount;
    }
    if (!reader.ended() || output.entityCount != entityCount) {
        return {Error::InvalidFormat, StorageStatus::Ok, 0U};
    }
    if (!output.playerGuid.isNil()) {
        bool playerFound = false;
        for (std::size_t index = 0U; index < output.entityCount; ++index)
            playerFound =
                playerFound || detail::guidEqual(output.playerGuid, output.entities[index].guid);
        if (!playerFound)
            return {Error::InvalidFormat, StorageStatus::Ok, 0U};
    }
    output.schemaVersion = schemaVersion;
    output.sequence = sequence;
    return {Error::None, StorageStatus::Ok, size};
}

struct StorageCallbacks final {
    void* context = nullptr;
    StorageStatus (*read)(void*, const char*, std::uint8_t*, std::size_t,
                          std::size_t*) noexcept = nullptr;
    StorageStatus (*stage)(void*, const char*, const std::uint8_t*, std::size_t) noexcept = nullptr;
    // commit must either install temp as live, or leave/restore the previous live contents. A
    // RollbackFailed status means the previous bytes remain recoverable at backupPath.
    StorageStatus (*commit)(void*, const char*, const char*, const char*) noexcept = nullptr;
    void (*discard)(void*, const char*) noexcept = nullptr;
    StorageStatus (*remove)(void*, const char*) noexcept = nullptr;

    [[nodiscard]] bool valid() const noexcept {
        return context != nullptr && read != nullptr && stage != nullptr && commit != nullptr &&
               discard != nullptr;
    }
};

struct MigrationCallbacks final {
    void* context = nullptr;
    bool (*step)(void*, std::uint32_t, Document&) noexcept = nullptr;

    [[nodiscard]] bool valid() const noexcept {
        return context != nullptr && step != nullptr;
    }
};

struct LoadInfo final {
    std::uint32_t storedSchemaVersion = 0U;
    bool migrated = false;
    bool recoveredFromBackup = false;
};

class SaveService final {
  public:
    SaveService(const StorageCallbacks storage, const char* root,
                const std::uint32_t currentSchemaVersion) noexcept
        : storage_(storage), currentSchemaVersion_(currentSchemaVersion) {
        configured_ = assignRoot(root);
    }

    [[nodiscard]] bool configured() const noexcept {
        return configured_ && storage_.valid() && currentSchemaVersion_ != 0U;
    }
    [[nodiscard]] Error lastError() const noexcept {
        return lastError_;
    }

    Operation save(const char* slot, const Document& document, std::uint8_t* working,
                   const std::size_t capacity) noexcept {
        Paths paths;
        if (!configured())
            return remember({Error::StorageUnavailable, StorageStatus::Ok, 0U});
        if (!buildPaths(slot, paths) || document.schemaVersion != currentSchemaVersion_)
            return remember({Error::InvalidArgument, StorageStatus::Ok, 0U});
        const auto encoded = encode(document, working, capacity);
        if (!encoded)
            return remember(encoded);
        const auto staged =
            storage_.stage(storage_.context, paths.temporary, working, encoded.bytes);
        if (staged != StorageStatus::Ok) {
            storage_.discard(storage_.context, paths.temporary);
            return remember({detail::mapStorageError(staged), staged, 0U});
        }
        const auto committed =
            storage_.commit(storage_.context, paths.live, paths.temporary, paths.backup);
        if (committed != StorageStatus::Ok) {
            storage_.discard(storage_.context, paths.temporary);
            return remember({detail::mapStorageError(committed), committed, 0U});
        }
        return remember(encoded);
    }

    Operation load(const char* slot, Document& document, std::uint8_t* working,
                   const std::size_t capacity, LoadInfo* info = nullptr,
                   const MigrationCallbacks migrations = MigrationCallbacks{}) noexcept {
        Paths paths;
        if (info != nullptr)
            *info = LoadInfo{};
        if (!configured())
            return remember({Error::StorageUnavailable, StorageStatus::Ok, 0U});
        if (!buildPaths(slot, paths) || working == nullptr || capacity == 0U)
            return remember({Error::InvalidArgument, StorageStatus::Ok, 0U});

        Operation primary = readAndDecode(paths.live, document, working, capacity);
        bool recovered = false;
        if (!primary && canRecover(primary.error)) {
            const auto backup = readAndDecode(paths.backup, document, working, capacity);
            if (backup) {
                primary = backup;
                recovered = true;
            }
        }
        if (!primary)
            return remember(primary);

        const auto storedSchema = document.schemaVersion;
        if (storedSchema > currentSchemaVersion_)
            return remember({Error::SchemaTooNew, StorageStatus::Ok, primary.bytes});
        while (document.schemaVersion < currentSchemaVersion_) {
            if (!migrations.valid())
                return remember({Error::MigrationMissing, StorageStatus::Ok, primary.bytes});
            const auto from = document.schemaVersion;
            if (!migrations.step(migrations.context, from, document))
                return remember({Error::MigrationFailed, StorageStatus::Ok, primary.bytes});
            // A step migrates exactly N -> N+1. The service owns the version transition so a
            // callback cannot skip an unreviewed schema.
            document.schemaVersion = from + 1U;
        }
        if (storedSchema != currentSchemaVersion_) {
            const auto validated = encode(document, working, capacity);
            if (!validated)
                return remember({Error::MigrationFailed, StorageStatus::Ok, primary.bytes});
        }
        if (info != nullptr) {
            info->storedSchemaVersion = storedSchema;
            info->migrated = storedSchema != currentSchemaVersion_;
            info->recoveredFromBackup = recovered;
        }
        return remember(primary);
    }

    Operation removeSlot(const char* slot) noexcept {
        Paths paths;
        if (!configured() || storage_.remove == nullptr)
            return remember({Error::StorageUnavailable, StorageStatus::Ok, 0U});
        if (!buildPaths(slot, paths))
            return remember({Error::InvalidArgument, StorageStatus::Ok, 0U});
        const auto live = storage_.remove(storage_.context, paths.live);
        storage_.remove(storage_.context, paths.temporary);
        storage_.remove(storage_.context, paths.backup);
        return remember({detail::mapStorageError(live), live, 0U});
    }

  private:
    struct Paths final {
        char live[96]{};
        char temporary[96]{};
        char backup[96]{};
    };

    bool assignRoot(const char* root) noexcept {
        if (root == nullptr || root[0] != '/')
            return false;
        std::size_t length = 0U;
        std::size_t segmentStart = 1U;
        for (; root[length] != '\0'; ++length) {
            const unsigned char character = static_cast<unsigned char>(root[length]);
            if (length >= kMaximumRootLength || character < 0x21U || character > 0x7EU ||
                character == '\\' || character == ':') {
                return false;
            }
            if (character == '/') {
                if (length == 0U)
                    continue;
                const auto segmentLength = length - segmentStart;
                if (segmentLength == 0U || (segmentLength == 1U && root[segmentStart] == '.') ||
                    (segmentLength == 2U && root[segmentStart] == '.' &&
                     root[segmentStart + 1U] == '.')) {
                    return false;
                }
                segmentStart = length + 1U;
            }
        }
        if (length < 2U || root[length - 1U] == '/')
            return false;
        const auto finalLength = length - segmentStart;
        if ((finalLength == 1U && root[segmentStart] == '.') ||
            (finalLength == 2U && root[segmentStart] == '.' && root[segmentStart + 1U] == '.'))
            return false;
        std::memcpy(root_, root, length + 1U);
        rootLength_ = length;
        return true;
    }

    bool buildPaths(const char* slot, Paths& paths) const noexcept {
        if (slot == nullptr || *slot == '\0')
            return false;
        std::size_t slotLength = 0U;
        while (slot[slotLength] != '\0') {
            const char character = slot[slotLength];
            const bool safe =
                (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9') || character == '_' || character == '-';
            if (!safe || ++slotLength > kMaximumSlotLength)
                return false;
        }
        return makePath(paths.live, sizeof(paths.live), slot, slotLength, ".fglsave") &&
               makePath(paths.temporary, sizeof(paths.temporary), slot, slotLength,
                        ".fglsave.tmp") &&
               makePath(paths.backup, sizeof(paths.backup), slot, slotLength, ".fglsave.bak");
    }

    bool makePath(char* output, const std::size_t capacity, const char* slot,
                  const std::size_t slotLength, const char* suffix) const noexcept {
        std::size_t suffixLength = 0U;
        while (suffix[suffixLength] != '\0')
            ++suffixLength;
        const auto required = rootLength_ + 1U + slotLength + suffixLength + 1U;
        if (required > capacity)
            return false;
        std::memcpy(output, root_, rootLength_);
        output[rootLength_] = '/';
        std::memcpy(output + rootLength_ + 1U, slot, slotLength);
        std::memcpy(output + rootLength_ + 1U + slotLength, suffix, suffixLength + 1U);
        return true;
    }

    Operation readAndDecode(const char* path, Document& document, std::uint8_t* working,
                            const std::size_t capacity) const noexcept {
        std::size_t size = 0U;
        const auto status = storage_.read(storage_.context, path, working, capacity, &size);
        if (status != StorageStatus::Ok)
            return {detail::mapStorageError(status), status, 0U};
        if (size > capacity)
            return {Error::CapacityExceeded, StorageStatus::CapacityExceeded, 0U};
        return decode(working, size, document);
    }

    static bool canRecover(const Error error) noexcept {
        return error == Error::NotFound || error == Error::IoFailure ||
               error == Error::InvalidFormat || error == Error::ChecksumMismatch;
    }

    Operation remember(const Operation operation) noexcept {
        lastError_ = operation.error;
        return operation;
    }

    StorageCallbacks storage_;
    std::uint32_t currentSchemaVersion_ = 0U;
    char root_[kMaximumRootLength + 1U]{};
    std::size_t rootLength_ = 0U;
    bool configured_ = false;
    Error lastError_ = Error::None;
};

} // namespace fabgl_project_save
