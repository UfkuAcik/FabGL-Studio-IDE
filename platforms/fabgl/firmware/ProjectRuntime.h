#pragma once

// Allocation-free FabGL Studio project reader used by the ESP32 firmware and
// host regression tests. The reader is deliberately independent from Arduino;
// callers only need to expose size() and byte(offset), which keeps PROGMEM data
// streaming and avoids copying the embedded project pack into RAM.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace fabgl_project_save {
struct Document;
}

namespace fabgl_project_runtime {

// These are hard internal-DRAM capacities, not authoring limits. Keep enough
// headroom for FabGL's VGA/audio/input state and task stacks on the reference
// WROVER profile; the host capability gate mirrors the same values.
constexpr std::size_t kMaximumEntities = 48U;
constexpr std::size_t kMaximumAssets = 64U;
constexpr std::size_t kMaximumInputValues = 48U;
constexpr std::size_t kMaximumInputBindings = 128U;
constexpr std::size_t kMaximumControls = 64U;
constexpr std::size_t kMaximumRaycastCells = 32U * 32U;
constexpr std::size_t kMaximumRaycastColors = 32U;
constexpr std::size_t kMaximumTrackSegments = 256U;
constexpr std::size_t kMaximumParticles = 128U;

constexpr std::uint32_t kManifestPayloadType = 0x4D414E46U;
constexpr std::uint32_t kScenePayloadType = 0x53434E45U;
constexpr std::uint32_t kAssetPayloadType = 0x41535354U;
constexpr std::size_t kPackHeaderSize = 32U;
constexpr std::size_t kPackIndexEntrySize = 40U;
constexpr std::uint32_t kExternalOffsetFlag = 0x80000000U;
constexpr std::uint32_t kExternalOffsetMask = 0x7FFFFFFFU;

enum class StorageClass : std::uint8_t {
    Flash = 0U,
    InternalRam = 1U,
    Psram = 2U,
    Sd = 3U,
};

enum class ErrorCode : std::uint8_t {
    None,
    Truncated,
    InvalidPack,
    CapacityExceeded,
    InvalidManifest,
    InvalidScene,
    MissingAsset,
    UnsupportedAsset,
    TargetMismatch,
};

struct Failure final {
    ErrorCode code = ErrorCode::None;
    std::size_t offset = 0U;
    const char* detail = "ok";

    void set(const ErrorCode value, const std::size_t at, const char* message) noexcept {
        if (code == ErrorCode::None) {
            code = value;
            offset = at;
            detail = message;
        }
    }
};

template <std::size_t Capacity> struct Text final {
    char value[Capacity]{};
    std::size_t length = 0U;

    bool push(const char character) noexcept {
        if (length + 1U >= Capacity)
            return false;
        value[length++] = character;
        value[length] = '\0';
        return true;
    }

    void clear() noexcept {
        length = 0U;
        value[0] = '\0';
    }

    [[nodiscard]] bool equals(const char* other) const noexcept {
        return other != nullptr && std::strcmp(value, other) == 0;
    }

    template <std::size_t Other>
    [[nodiscard]] bool equals(const Text<Other>& other) const noexcept {
        return length == other.length && std::memcmp(value, other.value, length) == 0;
    }
};

struct Guid final {
    std::uint8_t bytes[16]{};

    [[nodiscard]] bool isNil() const noexcept {
        for (const auto byte : bytes) {
            if (byte != 0U)
                return false;
        }
        return true;
    }

    [[nodiscard]] bool operator==(const Guid& other) const noexcept {
        return std::memcmp(bytes, other.bytes, sizeof(bytes)) == 0;
    }
    [[nodiscard]] bool operator!=(const Guid& other) const noexcept {
        return !(*this == other);
    }
};

struct Color final {
    constexpr Color(const std::uint8_t redValue = 255U, const std::uint8_t greenValue = 255U,
                    const std::uint8_t blueValue = 255U,
                    const std::uint8_t alphaValue = 255U) noexcept
        : red(redValue), green(greenValue), blue(blueValue), alpha(alphaValue) {}

    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;
    std::uint8_t alpha;
};

struct PackedEntry final {
    Guid guid;
    std::uint32_t type = 0U;
    StorageClass storage = StorageClass::Flash;
    std::uint32_t offset = 0U;
    std::uint32_t size = 0U;
};

struct PackedAsset final {
    Guid guid;
    Text<128> path;
    StorageClass storage = StorageClass::Flash;
    std::uint32_t contentOffset = 0U;
    std::uint32_t contentSize = 0U;

    [[nodiscard]] bool external() const noexcept {
        return (contentOffset & kExternalOffsetFlag) != 0U;
    }
};

struct PayloadView final {
    PackedEntry manifest;
    PackedEntry scene;
    PackedAsset assets[kMaximumAssets]{};
    std::size_t assetCount = 0U;
};

struct ManifestAsset final {
    Guid guid;
    Text<128> path;
    Text<32> type;
};

struct InputBinding final {
    Text<32> control;
    float scale = 1.0F;
    float threshold = 0.5F;
};

struct InputValue final {
    Text<32> context;
    Text<32> name;
    int priority = 0;
    bool enabled = true;
    bool axis = false;
    std::uint16_t bindingStart = 0U;
    std::uint16_t bindingCount = 0U;
};

struct Manifest final {
    int formatVersion = 0;
    Text<64> name;
    Text<64> esp32Target;
    ManifestAsset assets[kMaximumAssets]{};
    std::size_t assetCount = 0U;
    InputValue inputValues[kMaximumInputValues]{};
    std::size_t inputValueCount = 0U;
    InputBinding inputBindings[kMaximumInputBindings]{};
    std::size_t inputBindingCount = 0U;
};

enum Component : std::uint16_t {
    Sprite = 1U << 0U,
    Character = 1U << 1U,
    Vehicle = 1U << 2U,
    RaycastMap = 1U << 3U,
    FirstPerson = 1U << 4U,
    Camera = 1U << 5U,
    Collider2D = 1U << 6U,
    Rigidbody2D = 1U << 7U,
    ParticleEmitter = 1U << 8U,
    RuntimeUi = 1U << 9U,
};

struct Entity final {
    Guid guid;
    Text<40> name;
    bool active = true;
    std::uint16_t components = 0U;
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float rotationZ = 0.0F;
    float scaleX = 1.0F;
    float scaleY = 1.0F;

    Guid sprite;
    Color tint;

    float moveSpeed = 4.0F;
    int movementMode = 0;
    Text<32> moveXAxis;
    Text<32> moveYAxis;
    Text<32> primaryAction;

    Guid track;
    float acceleration = 8.0F;
    Text<32> steerAxis;
    Text<32> throttleAxis;
    Text<32> brakeAction;
    Text<32> driftAction;

    Guid raycastAsset;
    float raycastCellSize = 1.0F;
    float lookSensitivity = 1.0F;
    Text<32> lookXAxis;

    float velocityY = 0.0F;
    float vehicleSpeed = 0.0F;

    int colliderShape = 0;
    float colliderWidth = 1.0F;
    float colliderHeight = 1.0F;
    bool colliderTrigger = false;
    std::uint32_t colliderLayer = 1U;
    std::uint32_t colliderMask = 0xFFFFFFFFU;
    int bodyType = 2;
    float rigidbodyVelocityX = 0.0F;
    float rigidbodyVelocityY = 0.0F;
    float gravityScale = 1.0F;
    float restitution = 0.0F;

    float particleRate = 10.0F;
    std::uint16_t particleMaximum = 64U;
    std::uint16_t particleBurst = 0U;
    float particleLifetime = 1.0F;
    float particleVelocityX = 0.0F;
    float particleVelocityY = 0.0F;
    float particleAccelerationX = 0.0F;
    float particleAccelerationY = 0.0F;
    Color particleStartColor;
    Color particleEndColor{255U, 255U, 255U, 0U};
    float particleAccumulator = 0.0F;
    bool particleStarted = false;

    int uiWidgetType = 0;
    float uiLeft = 0.0F;
    float uiTop = 0.0F;
    float uiRight = 100.0F;
    float uiBottom = 30.0F;
    bool uiVisible = true;
    Text<64> uiText;
    float uiMinimum = 0.0F;
    float uiMaximum = 1.0F;
    float uiValue = 0.0F;
};

struct RuntimeParticle final {
    float x = 0.0F;
    float y = 0.0F;
    float velocityX = 0.0F;
    float velocityY = 0.0F;
    float accelerationX = 0.0F;
    float accelerationY = 0.0F;
    float age = 0.0F;
    float lifetime = 1.0F;
    std::uint8_t owner = 0U;
    bool active = false;
};

struct Scene final {
    Guid guid;
    Text<64> name;
    Entity entities[kMaximumEntities]{};
    std::size_t entityCount = 0U;
};

struct RaycastMapData final {
    bool valid = false;
    Guid guid;
    int width = 0;
    int height = 0;
    Color palette[kMaximumRaycastColors]{};
    std::size_t paletteCount = 0U;
    std::uint8_t cells[kMaximumRaycastCells]{};
};

struct TrackSegment final {
    float curve = 0.0F;
    float hill = 0.0F;
    float width = 1.0F;
    Color road{80U, 80U, 88U, 255U};
    Color grass{32U, 112U, 54U, 255U};
    Color rumble{235U, 235U, 235U, 255U};
};

struct RacerTrackData final {
    bool valid = false;
    Guid guid;
    float segmentLength = 16.0F;
    TrackSegment segments[kMaximumTrackSegments]{};
    std::size_t segmentCount = 0U;
};

struct IndexedImageView final {
    bool valid = false;
    std::uint16_t width = 0U;
    std::uint16_t height = 0U;
    std::uint16_t paletteCount = 0U;
    std::uint16_t transparentIndex = 255U;
    std::uint32_t pixelCount = 0U;
    std::uint32_t paletteOffset = 0U;
    std::uint32_t runsOffset = 0U;
    std::uint32_t endOffset = 0U;
};

struct Control final {
    Text<32> name;
    float value = 0.0F;
    bool transient = false;
};

struct RuntimeProject final {
    using PersistenceCallback = bool (*)(void*, RuntimeProject&, const char*, const Guid*,
                                         fabgl_project_save::Document*) noexcept;
    using PersistenceErrorCallback = std::uint8_t (*)(const void*) noexcept;

    PayloadView payload;
    Manifest manifest;
    Scene scene;
    RaycastMapData raycastMap;
    RacerTrackData racerTrack;
    Control controls[kMaximumControls]{};
    std::size_t controlCount = 0U;
    RuntimeParticle particles[kMaximumParticles]{};
    std::uint32_t particleRandom = 0x6D2B79F5U;
    bool loaded = false;
    void* persistenceContext = nullptr;
    PersistenceCallback saveCallback = nullptr;
    PersistenceCallback loadCallback = nullptr;
    PersistenceErrorCallback persistenceErrorCallback = nullptr;

    // Persistence is deliberately opt-in gameplay behavior. Loading a project, booting the
    // firmware and running diagnostics never invoke these callbacks. A portable gameplay script
    // must explicitly call saveSlot()/loadSlot() after the platform adapter has been bound.
    void bindPersistence(void* context, const PersistenceCallback save,
                         const PersistenceCallback load,
                         const PersistenceErrorCallback error) noexcept {
        persistenceContext = context;
        saveCallback = save;
        loadCallback = load;
        persistenceErrorCallback = error;
    }

    [[nodiscard]] bool persistenceAvailable() const noexcept {
        return persistenceContext != nullptr && saveCallback != nullptr && loadCallback != nullptr;
    }

    bool saveSlot(const char* slot, const Guid* player = nullptr,
                  fabgl_project_save::Document* gameplayState = nullptr) noexcept {
        return persistenceAvailable() &&
               saveCallback(persistenceContext, *this, slot, player, gameplayState);
    }

    bool loadSlot(const char* slot, const Guid* player = nullptr,
                  fabgl_project_save::Document* gameplayState = nullptr) noexcept {
        return persistenceAvailable() &&
               loadCallback(persistenceContext, *this, slot, player, gameplayState);
    }

    [[nodiscard]] std::uint8_t lastPersistenceError() const noexcept {
        return persistenceErrorCallback == nullptr
                   ? 0U
                   : persistenceErrorCallback(persistenceContext);
    }

    [[nodiscard]] const PackedAsset* findAsset(const Guid& guid) const noexcept {
        for (std::size_t index = 0U; index < payload.assetCount; ++index) {
            if (payload.assets[index].guid == guid)
                return &payload.assets[index];
        }
        return nullptr;
    }

    bool setControl(const char* name, const float value, const bool transient = false) noexcept {
        if (name == nullptr || !std::isfinite(value))
            return false;
        for (std::size_t index = 0U; index < controlCount; ++index) {
            if (controls[index].name.equals(name)) {
                controls[index].value = std::max(-1.0F, std::min(1.0F, value));
                controls[index].transient = transient;
                return true;
            }
        }
        if (controlCount >= kMaximumControls)
            return false;
        auto& control = controls[controlCount++];
        while (*name != '\0') {
            if (!control.name.push(*name++))
                return false;
        }
        control.value = std::max(-1.0F, std::min(1.0F, value));
        control.transient = transient;
        return true;
    }

    [[nodiscard]] float controlValue(const Text<32>& name) const noexcept {
        for (std::size_t index = 0U; index < controlCount; ++index) {
            if (controls[index].name.equals(name))
                return controls[index].value;
        }
        return 0.0F;
    }

    [[nodiscard]] const InputValue* selectedInput(const Text<32>& name,
                                                  const bool axis) const noexcept {
        const InputValue* selected = nullptr;
        for (std::size_t index = 0U; index < manifest.inputValueCount; ++index) {
            const auto& value = manifest.inputValues[index];
            if (!value.enabled || value.axis != axis || !value.name.equals(name))
                continue;
            if (selected == nullptr || value.priority > selected->priority ||
                (value.priority == selected->priority &&
                 std::strcmp(value.context.value, selected->context.value) < 0)) {
                selected = &value;
            }
        }
        return selected;
    }

    [[nodiscard]] float axis(const Text<32>& name) const noexcept {
        const auto* value = selectedInput(name, true);
        if (value == nullptr)
            return 0.0F;
        float result = 0.0F;
        for (std::size_t offset = 0U; offset < value->bindingCount; ++offset) {
            const auto& binding = manifest.inputBindings[value->bindingStart + offset];
            const auto control = controlValue(binding.control);
            if (std::fabs(control) >= binding.threshold)
                result += control * binding.scale;
        }
        return std::max(-1.0F, std::min(1.0F, result));
    }

    [[nodiscard]] bool action(const Text<32>& name) const noexcept {
        const auto* value = selectedInput(name, false);
        if (value == nullptr)
            return false;
        for (std::size_t offset = 0U; offset < value->bindingCount; ++offset) {
            const auto& binding = manifest.inputBindings[value->bindingStart + offset];
            if (std::fabs(controlValue(binding.control) * binding.scale) >= binding.threshold)
                return true;
        }
        return false;
    }

    void fixedUpdate(const float) noexcept {}

    void physicsUpdate(const float requestedDelta) noexcept {
        const auto delta = std::max(0.0F, std::min(0.1F, requestedDelta));
        for (std::size_t index = 0U; index < scene.entityCount; ++index) {
            auto& entity = scene.entities[index];
            if (!entity.active || (entity.components & Component::Rigidbody2D) == 0U)
                continue;
            if (entity.bodyType == 2)
                entity.rigidbodyVelocityY += 9.81F * entity.gravityScale * delta;
            if (entity.bodyType != 0) {
                entity.x += entity.rigidbodyVelocityX * delta;
                entity.y += entity.rigidbodyVelocityY * delta;
            }
        }
        for (std::size_t firstIndex = 0U; firstIndex < scene.entityCount; ++firstIndex) {
            auto& first = scene.entities[firstIndex];
            if (!first.active || (first.components & Component::Collider2D) == 0U)
                continue;
            for (std::size_t secondIndex = firstIndex + 1U; secondIndex < scene.entityCount;
                 ++secondIndex) {
                auto& second = scene.entities[secondIndex];
                if (!second.active || (second.components & Component::Collider2D) == 0U ||
                    (first.colliderMask & second.colliderLayer) == 0U ||
                    (second.colliderMask & first.colliderLayer) == 0U)
                    continue;
                const auto firstHalfWidth =
                    std::fabs(first.colliderWidth * first.scaleX) * 0.5F;
                const auto firstHalfHeight =
                    std::fabs(first.colliderHeight * first.scaleY) * 0.5F;
                const auto secondHalfWidth =
                    std::fabs(second.colliderWidth * second.scaleX) * 0.5F;
                const auto secondHalfHeight =
                    std::fabs(second.colliderHeight * second.scaleY) * 0.5F;
                const auto overlapX = firstHalfWidth + secondHalfWidth -
                                      std::fabs(second.x - first.x);
                const auto overlapY = firstHalfHeight + secondHalfHeight -
                                      std::fabs(second.y - first.y);
                if (overlapX <= 0.0F || overlapY <= 0.0F || first.colliderTrigger ||
                    second.colliderTrigger)
                    continue;
                const bool firstDynamic =
                    (first.components & Component::Rigidbody2D) != 0U && first.bodyType == 2;
                const bool secondDynamic =
                    (second.components & Component::Rigidbody2D) != 0U && second.bodyType == 2;
                if (!firstDynamic && !secondDynamic)
                    continue;
                const float firstShare = firstDynamic ? (secondDynamic ? 0.5F : 1.0F) : 0.0F;
                const float secondShare = secondDynamic ? (firstDynamic ? 0.5F : 1.0F) : 0.0F;
                const auto restitution = std::max(first.restitution, second.restitution);
                if (overlapX < overlapY) {
                    const float direction = second.x >= first.x ? 1.0F : -1.0F;
                    first.x -= direction * overlapX * firstShare;
                    second.x += direction * overlapX * secondShare;
                    if (firstDynamic)
                        first.rigidbodyVelocityX = -first.rigidbodyVelocityX * restitution;
                    if (secondDynamic)
                        second.rigidbodyVelocityX = -second.rigidbodyVelocityX * restitution;
                } else {
                    const float direction = second.y >= first.y ? 1.0F : -1.0F;
                    first.y -= direction * overlapY * firstShare;
                    second.y += direction * overlapY * secondShare;
                    if (firstDynamic)
                        first.rigidbodyVelocityY = -first.rigidbodyVelocityY * restitution;
                    if (secondDynamic)
                        second.rigidbodyVelocityY = -second.rigidbodyVelocityY * restitution;
                }
            }
        }
    }

    void variableUpdate(const float requestedDelta) noexcept {
        const auto delta = std::max(0.0F, std::min(0.1F, requestedDelta));
        for (std::size_t index = 0U; index < scene.entityCount; ++index) {
            auto& entity = scene.entities[index];
            if (!entity.active)
                continue;
            if ((entity.components & Component::Character) != 0U) {
                const auto moveX = axis(entity.moveXAxis);
                const auto moveY = axis(entity.moveYAxis);
                if (entity.movementMode == 0) {
                    entity.x += moveX * entity.moveSpeed * delta;
                    if (action(entity.primaryAction) && entity.y >= 188.0F)
                        entity.velocityY = -std::max(24.0F, entity.moveSpeed * 6.0F);
                    entity.velocityY += 72.0F * delta;
                    entity.y += entity.velocityY * delta;
                    if (entity.y > 188.0F) {
                        entity.y = 188.0F;
                        entity.velocityY = 0.0F;
                    }
                } else {
                    entity.x += moveX * entity.moveSpeed * delta;
                    entity.y += moveY * entity.moveSpeed * delta;
                }
                entity.x = std::max(-64.0F, std::min(384.0F, entity.x));
                entity.y = std::max(-64.0F, std::min(264.0F, entity.y));
            }
            if ((entity.components & Component::Vehicle) != 0U) {
                const auto throttle = std::max(0.0F, axis(entity.throttleAxis));
                const auto brake = action(entity.brakeAction);
                entity.vehicleSpeed += entity.acceleration * throttle * delta;
                entity.vehicleSpeed -= (brake ? 14.0F : 1.5F) * delta;
                entity.vehicleSpeed = std::max(0.0F, std::min(120.0F, entity.vehicleSpeed));
                entity.x += axis(entity.steerAxis) * (2.0F + entity.vehicleSpeed * 0.04F) * delta;
                entity.x = std::max(-1.2F, std::min(1.2F, entity.x));
                entity.y = entity.vehicleSpeed;
                entity.z += entity.vehicleSpeed * delta;
            }
            if ((entity.components & Component::FirstPerson) != 0U) {
                entity.rotationZ += axis(entity.lookXAxis) * entity.lookSensitivity * delta;
                const auto forward = axis(entity.moveYAxis);
                const auto side = axis(entity.moveXAxis);
                const auto cosine = std::cos(entity.rotationZ);
                const auto sine = std::sin(entity.rotationZ);
                entity.x += (cosine * forward - sine * side) * entity.moveSpeed * delta;
                entity.y += (sine * forward + cosine * side) * entity.moveSpeed * delta;
            }
        }
        for (std::size_t index = 0U; index < controlCount; ++index) {
            if (controls[index].transient)
                controls[index].value = 0.0F;
        }
    }

    void animationUpdate(const float requestedDelta) noexcept {
        const auto delta = std::max(0.0F, std::min(0.1F, requestedDelta));
        for (std::size_t index = 0U; index < kMaximumParticles; ++index) {
            auto& particle = particles[index];
            if (!particle.active)
                continue;
            particle.age += delta;
            if (particle.age >= particle.lifetime) {
                particle.active = false;
                continue;
            }
            particle.velocityX += particle.accelerationX * delta;
            particle.velocityY += particle.accelerationY * delta;
            particle.x += particle.velocityX * delta;
            particle.y += particle.velocityY * delta;
        }
        for (std::size_t owner = 0U; owner < scene.entityCount; ++owner) {
            auto& entity = scene.entities[owner];
            if (!entity.active || (entity.components & Component::ParticleEmitter) == 0U)
                continue;
            std::size_t active = 0U;
            for (std::size_t index = 0U; index < kMaximumParticles; ++index) {
                if (particles[index].active && particles[index].owner == owner)
                    ++active;
            }
            std::size_t requested = 0U;
            if (!entity.particleStarted) {
                requested = entity.particleBurst;
                entity.particleStarted = true;
            }
            entity.particleAccumulator += entity.particleRate * delta;
            const auto continuous = static_cast<std::size_t>(entity.particleAccumulator);
            entity.particleAccumulator -= static_cast<float>(continuous);
            requested += continuous;
            const auto available = entity.particleMaximum > active
                                       ? static_cast<std::size_t>(entity.particleMaximum) - active
                                       : 0U;
            requested = std::min(requested, available);
            for (std::size_t spawn = 0U; spawn < requested; ++spawn) {
                RuntimeParticle* particle = nullptr;
                for (std::size_t index = 0U; index < kMaximumParticles; ++index) {
                    if (!particles[index].active) {
                        particle = &particles[index];
                        break;
                    }
                }
                if (particle == nullptr)
                    break;
                particleRandom = particleRandom * 1664525U + 1013904223U;
                const float jitter =
                    static_cast<float>((particleRandom >> 24U) & 0xFFU) / 255.0F - 0.5F;
                *particle = RuntimeParticle{};
                particle->x = entity.x;
                particle->y = entity.y;
                particle->velocityX = entity.particleVelocityX + jitter;
                particle->velocityY = entity.particleVelocityY - jitter;
                particle->accelerationX = entity.particleAccelerationX;
                particle->accelerationY = entity.particleAccelerationY;
                particle->lifetime = entity.particleLifetime;
                particle->owner = static_cast<std::uint8_t>(owner);
                particle->active = true;
            }
        }
    }

    void update(const float requestedDelta) noexcept {
        variableUpdate(requestedDelta);
    }

    [[nodiscard]] std::size_t activeParticleCount() const noexcept {
        std::size_t count = 0U;
        for (std::size_t index = 0U; index < kMaximumParticles; ++index) {
            if (particles[index].active)
                ++count;
        }
        return count;
    }
};

static constexpr std::size_t kSoakEntityCapacity = 16U;

struct SoakStep final {
    bool projectSceneActive = false;
    bool assetResident = false;
    bool entityCreated = false;
    bool entityDestroyed = false;
    std::size_t entitySlot = 0U;
};

// Fixed-capacity state machine shared by the firmware soak loop and host tests. It deliberately
// owns no dynamic storage: a long run exercises transitions, asset residency and entity churn
// without making the diagnostic itself a source of heap fragmentation.
struct SoakWorkload final {
    bool projectSceneActive = true;
    bool assetResident = false;
    bool entities[kSoakEntityCapacity]{};
    std::size_t cursor = 0U;
    std::size_t liveEntities = 0U;
    std::uint32_t iterations = 0U;
    std::uint32_t sceneTransitions = 0U;
    std::uint32_t assetLoads = 0U;
    std::uint32_t assetUnloads = 0U;
    std::uint32_t audioPlays = 0U;
    std::uint32_t entityCreates = 0U;
    std::uint32_t entityDestroys = 0U;

    [[nodiscard]] SoakStep advance(const bool assetAvailable) noexcept {
        SoakStep step;
        projectSceneActive = !projectSceneActive;
        step.projectSceneActive = projectSceneActive;
        ++sceneTransitions;

        if (assetResident) {
            assetResident = false;
            ++assetUnloads;
        } else if (assetAvailable) {
            assetResident = true;
            ++assetLoads;
        }
        step.assetResident = assetResident;

        step.entitySlot = cursor;
        if (entities[cursor]) {
            entities[cursor] = false;
            --liveEntities;
            ++entityDestroys;
            step.entityDestroyed = true;
        } else {
            entities[cursor] = true;
            ++liveEntities;
            ++entityCreates;
            step.entityCreated = true;
        }
        cursor = (cursor + 1U) % kSoakEntityCapacity;
        ++audioPlays;
        ++iterations;
        return step;
    }

    [[nodiscard]] bool invariantHolds() const noexcept {
        std::size_t counted = 0U;
        for (const bool alive : entities) {
            if (alive)
                ++counted;
        }
        return cursor < kSoakEntityCapacity && liveEntities <= kSoakEntityCapacity &&
               counted == liveEntities && sceneTransitions == iterations &&
               audioPlays == iterations && assetLoads >= assetUnloads &&
               assetLoads - assetUnloads == (assetResident ? 1U : 0U) &&
               entityCreates >= entityDestroys && entityCreates - entityDestroys == liveEntities;
    }
};

namespace detail {

[[nodiscard]] inline int hexNibble(const std::uint8_t value) noexcept {
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return -1;
}

template <typename Reader>
[[nodiscard]] std::uint16_t u16(const Reader& reader, const std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(reader.byte(offset)) |
           static_cast<std::uint16_t>(reader.byte(offset + 1U) << 8U);
}

template <typename Reader>
[[nodiscard]] std::uint32_t u32(const Reader& reader, const std::size_t offset) noexcept {
    std::uint32_t result = 0U;
    for (unsigned shift = 0U; shift < 32U; shift += 8U)
        result |= static_cast<std::uint32_t>(reader.byte(offset + shift / 8U)) << shift;
    return result;
}

template <typename Reader>
[[nodiscard]] std::uint64_t u64(const Reader& reader, const std::size_t offset) noexcept {
    std::uint64_t result = 0U;
    for (unsigned shift = 0U; shift < 64U; shift += 8U)
        result |= static_cast<std::uint64_t>(reader.byte(offset + shift / 8U)) << shift;
    return result;
}

template <typename Reader>
[[nodiscard]] std::uint64_t checksum(const Reader& reader, const std::size_t offset,
                                     const std::size_t size) noexcept {
    constexpr std::uint64_t basis = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    auto result = basis;
    for (std::size_t index = 0U; index < size; ++index) {
        result ^= reader.byte(offset + index);
        result *= prime;
    }
    return result;
}

template <typename Reader>
[[nodiscard]] bool startsWith(const Reader& reader, const std::size_t offset,
                              const std::size_t size, const char* expected) noexcept {
    std::size_t index = 0U;
    while (expected[index] != '\0') {
        if (index >= size ||
            reader.byte(offset + index) != static_cast<std::uint8_t>(expected[index]))
            return false;
        ++index;
    }
    return true;
}

template <typename Reader>
[[nodiscard]] bool safeRelativePath(const Reader& reader, const std::size_t offset,
                                    const std::size_t length) noexcept {
    if (length == 0U || reader.byte(offset) == '/' || reader.byte(offset) == '\\')
        return false;
    std::size_t segmentStart = 0U;
    for (std::size_t index = 0U; index <= length; ++index) {
        const auto character =
            index == length ? static_cast<std::uint8_t>('/') : reader.byte(offset + index);
        if (index < length &&
            (character < 0x20U || character > 0x7EU || character == '\\' || character == ':'))
            return false;
        if (character != '/')
            continue;
        const auto count = index - segmentStart;
        if (count == 0U || (count == 1U && reader.byte(offset + segmentStart) == '.') ||
            (count == 2U && reader.byte(offset + segmentStart) == '.' &&
             reader.byte(offset + segmentStart + 1U) == '.'))
            return false;
        segmentStart = index + 1U;
    }
    return true;
}

template <typename Reader, std::size_t Capacity>
bool copyBytes(const Reader& reader, const std::size_t offset, const std::size_t length,
               Text<Capacity>& output) noexcept {
    output.clear();
    if (length + 1U > Capacity)
        return false;
    for (std::size_t index = 0U; index < length; ++index) {
        if (!output.push(static_cast<char>(reader.byte(offset + index))))
            return false;
    }
    return true;
}

template <typename Reader>
bool inspectPack(const Reader& reader, const std::size_t expectedAssets,
                 const std::uint64_t expectedPayloadChecksum,
                 const std::uint64_t expectedBuildChecksum, PayloadView& output,
                 Failure& failure) noexcept {
    if (reader.size() < kPackHeaderSize || !startsWith(reader, 0U, reader.size(), "FGLP") ||
        u16(reader, 4U) != 1U || u16(reader, 6U) != 0U) {
        failure.set(ErrorCode::InvalidPack, 0U, "invalid pack header");
        return false;
    }
    if (expectedAssets > kMaximumAssets) {
        failure.set(ErrorCode::CapacityExceeded, 8U, "too many embedded assets");
        return false;
    }
    const auto entryCount = u32(reader, 8U);
    const auto alignment = u32(reader, 12U);
    const auto entrySize = u32(reader, 16U);
    const auto dataOffset = u32(reader, 20U);
    const auto buildChecksum = u64(reader, 24U);
    const auto indexBytes = static_cast<std::uint64_t>(entryCount) * kPackIndexEntrySize;
    if (entryCount != expectedAssets + 2U || alignment == 0U || alignment > 4096U ||
        (alignment & (alignment - 1U)) != 0U || entrySize != kPackIndexEntrySize ||
        dataOffset < kPackHeaderSize || dataOffset > reader.size() ||
        indexBytes > dataOffset - kPackHeaderSize || buildChecksum != expectedBuildChecksum ||
        checksum(reader, kPackHeaderSize, reader.size() - kPackHeaderSize) != buildChecksum ||
        checksum(reader, 0U, reader.size()) != expectedPayloadChecksum) {
        failure.set(ErrorCode::InvalidPack, 8U, "pack metadata or checksum mismatch");
        return false;
    }

    std::size_t manifests = 0U;
    std::size_t scenes = 0U;
    Guid previous{};
    bool hasPrevious = false;
    for (std::uint32_t index = 0U; index < entryCount; ++index) {
        const auto entryOffset = kPackHeaderSize + static_cast<std::size_t>(index) * entrySize;
        PackedEntry entry;
        for (std::size_t byte = 0U; byte < 16U; ++byte)
            entry.guid.bytes[byte] = reader.byte(entryOffset + byte);
        entry.type = u32(reader, entryOffset + 16U);
        const auto storage = reader.byte(entryOffset + 20U);
        entry.storage = static_cast<StorageClass>(storage);
        entry.offset = u32(reader, entryOffset + 24U);
        entry.size = u32(reader, entryOffset + 28U);
        const auto entryChecksum = u64(reader, entryOffset + 32U);
        const auto rangeValid = entry.offset >= dataOffset && entry.offset <= reader.size() &&
                                entry.size <= reader.size() - entry.offset;
        if (entry.guid.isNil() || storage > 3U || !rangeValid ||
            (entry.offset & (alignment - 1U)) != 0U ||
            checksum(reader, entry.offset, entry.size) != entryChecksum) {
            failure.set(ErrorCode::InvalidPack, entryOffset, "invalid pack index entry");
            return false;
        }
        if (hasPrevious && std::memcmp(previous.bytes, entry.guid.bytes, 16U) >= 0) {
            failure.set(ErrorCode::InvalidPack, entryOffset, "pack GUID index is not sorted");
            return false;
        }
        previous = entry.guid;
        hasPrevious = true;

        if (entry.type == kManifestPayloadType) {
            output.manifest = entry;
            ++manifests;
        } else if (entry.type == kScenePayloadType) {
            output.scene = entry;
            ++scenes;
        } else if (entry.type == kAssetPayloadType) {
            if (output.assetCount >= kMaximumAssets || entry.size < 8U ||
                !startsWith(reader, entry.offset, entry.size, "FGLA") ||
                u16(reader, entry.offset + 4U) != 1U) {
                failure.set(ErrorCode::InvalidPack, entry.offset, "invalid asset wrapper");
                return false;
            }
            const auto pathLength = static_cast<std::size_t>(u16(reader, entry.offset + 6U));
            if (pathLength > entry.size - 8U ||
                !safeRelativePath(reader, entry.offset + 8U, pathLength)) {
                failure.set(ErrorCode::InvalidPack, entry.offset, "unsafe asset path");
                return false;
            }
            auto& asset = output.assets[output.assetCount++];
            asset.guid = entry.guid;
            asset.storage = entry.storage;
            if (!copyBytes(reader, entry.offset + 8U, pathLength, asset.path)) {
                failure.set(ErrorCode::CapacityExceeded, entry.offset, "asset path is too long");
                return false;
            }
            asset.contentOffset = entry.offset + 8U + static_cast<std::uint32_t>(pathLength);
            asset.contentSize = entry.size - 8U - static_cast<std::uint32_t>(pathLength);
        } else {
            failure.set(ErrorCode::InvalidPack, entryOffset, "unsupported pack entry type");
            return false;
        }
    }
    if (manifests != 1U || scenes != 1U || output.assetCount != expectedAssets) {
        failure.set(ErrorCode::InvalidPack, 8U, "pack entry counts do not match export metadata");
        return false;
    }
    return true;
}

template <typename Reader> class JsonCursor final {
  public:
    JsonCursor(const Reader& source, const std::size_t offset, const std::size_t size,
               Failure& error) noexcept
        : reader_(source), position_(offset), end_(offset + size), failure_(error) {}

    [[nodiscard]] std::size_t position() const noexcept {
        return position_;
    }

    void whitespace() noexcept {
        while (position_ < end_) {
            const auto value = reader_.byte(position_);
            if (value != ' ' && value != '\n' && value != '\r' && value != '\t')
                break;
            ++position_;
        }
    }

    bool consume(const char expected) noexcept {
        whitespace();
        if (position_ >= end_ || reader_.byte(position_) != static_cast<std::uint8_t>(expected))
            return false;
        ++position_;
        return true;
    }

    [[nodiscard]] bool at(const char value) noexcept {
        whitespace();
        return position_ < end_ && reader_.byte(position_) == static_cast<std::uint8_t>(value);
    }

    template <std::size_t Capacity> bool string(Text<Capacity>& output) noexcept {
        output.clear();
        if (!consume('"'))
            return false;
        while (position_ < end_) {
            auto value = reader_.byte(position_++);
            if (value == '"')
                return true;
            if (value < 0x20U)
                return false;
            if (value == '\\') {
                if (position_ >= end_)
                    return false;
                const auto escaped = reader_.byte(position_++);
                switch (escaped) {
                case '"':
                    value = '"';
                    break;
                case '\\':
                    value = '\\';
                    break;
                case '/':
                    value = '/';
                    break;
                case 'b':
                    value = '\b';
                    break;
                case 'f':
                    value = '\f';
                    break;
                case 'n':
                    value = '\n';
                    break;
                case 'r':
                    value = '\r';
                    break;
                case 't':
                    value = '\t';
                    break;
                case 'u': {
                    if (end_ - position_ < 4U)
                        return false;
                    unsigned code = 0U;
                    for (int index = 0; index < 4; ++index) {
                        const auto nibble = hexNibble(reader_.byte(position_++));
                        if (nibble < 0)
                            return false;
                        code = (code << 4U) | static_cast<unsigned>(nibble);
                    }
                    value = code >= 0x20U && code <= 0x7EU ? static_cast<std::uint8_t>(code) : '_';
                    break;
                }
                default:
                    return false;
                }
            }
            if (!output.push(static_cast<char>(value)))
                return false;
        }
        return false;
    }

    bool boolean(bool& output) noexcept {
        whitespace();
        if (literal("true")) {
            output = true;
            return true;
        }
        if (literal("false")) {
            output = false;
            return true;
        }
        return false;
    }

    bool number(float& output) noexcept {
        whitespace();
        char token[48]{};
        std::size_t length = 0U;
        while (position_ < end_ && length + 1U < sizeof(token)) {
            const auto value = static_cast<char>(reader_.byte(position_));
            if ((value < '0' || value > '9') && value != '-' && value != '+' && value != '.' &&
                value != 'e' && value != 'E')
                break;
            token[length++] = value;
            ++position_;
        }
        if (length == 0U)
            return false;
        char* parsedEnd = nullptr;
        output = std::strtof(token, &parsedEnd);
        return parsedEnd == token + length && std::isfinite(output);
    }

    bool integer(int& output) noexcept {
        float value = 0.0F;
        if (!number(value) || value < -2147483647.0F || value > 2147483647.0F ||
            std::floor(value) != value)
            return false;
        output = static_cast<int>(value);
        return true;
    }

    bool skip(const unsigned depth = 0U) noexcept {
        if (depth > 16U)
            return false;
        whitespace();
        if (at('"')) {
            Text<2> ignored;
            // Avoid a tiny destination rejecting a long skipped string.
            if (!consume('"'))
                return false;
            while (position_ < end_) {
                const auto value = reader_.byte(position_++);
                if (value == '"')
                    return true;
                if (value < 0x20U)
                    return false;
                if (value == '\\') {
                    if (position_ >= end_)
                        return false;
                    const auto escaped = reader_.byte(position_++);
                    if (escaped == 'u') {
                        if (end_ - position_ < 4U)
                            return false;
                        for (int index = 0; index < 4; ++index) {
                            if (hexNibble(reader_.byte(position_++)) < 0)
                                return false;
                        }
                    }
                }
            }
            return false;
        }
        if (consume('{')) {
            if (consume('}'))
                return true;
            for (;;) {
                Text<64> key;
                if (!string(key) || !consume(':') || !skip(depth + 1U))
                    return false;
                if (consume('}'))
                    return true;
                if (!consume(','))
                    return false;
            }
        }
        if (consume('[')) {
            if (consume(']'))
                return true;
            for (;;) {
                if (!skip(depth + 1U))
                    return false;
                if (consume(']'))
                    return true;
                if (!consume(','))
                    return false;
            }
        }
        if (literal("true") || literal("false") || literal("null"))
            return true;
        float ignoredNumber = 0.0F;
        return number(ignoredNumber);
    }

    [[nodiscard]] bool ended() noexcept {
        whitespace();
        return position_ == end_;
    }

  private:
    bool literal(const char* value) noexcept {
        whitespace();
        std::size_t count = 0U;
        while (value[count] != '\0')
            ++count;
        if (count > end_ - position_)
            return false;
        for (std::size_t index = 0U; index < count; ++index) {
            if (reader_.byte(position_ + index) != static_cast<std::uint8_t>(value[index]))
                return false;
        }
        position_ += count;
        return true;
    }

    const Reader& reader_;
    std::size_t position_;
    std::size_t end_;
    Failure& failure_;
};

inline bool parseGuidText(const char* text, Guid& output) noexcept {
    if (text == nullptr || std::strlen(text) != 36U)
        return false;
    constexpr int hyphens[] = {8, 13, 18, 23};
    for (const auto hyphen : hyphens) {
        if (text[hyphen] != '-')
            return false;
    }
    std::size_t outputIndex = 0U;
    for (std::size_t index = 0U; index < 36U;) {
        if (text[index] == '-') {
            ++index;
            continue;
        }
        if (index + 1U >= 36U || outputIndex >= 16U)
            return false;
        const auto high = hexNibble(static_cast<std::uint8_t>(text[index++]));
        const auto low = hexNibble(static_cast<std::uint8_t>(text[index++]));
        if (high < 0 || low < 0)
            return false;
        output.bytes[outputIndex++] = static_cast<std::uint8_t>((high << 4U) | low);
    }
    return outputIndex == 16U && !output.isNil();
}

template <typename Cursor> bool parseBinding(Cursor& cursor, Manifest& manifest) noexcept {
    if (manifest.inputBindingCount >= kMaximumInputBindings || !cursor.consume('{'))
        return false;
    InputBinding binding;
    bool hasControl = false;
    if (cursor.consume('}'))
        return false;
    for (;;) {
        Text<32> key;
        if (!cursor.string(key) || !cursor.consume(':'))
            return false;
        if (key.equals("control")) {
            if (!cursor.string(binding.control) || binding.control.length == 0U)
                return false;
            hasControl = true;
        } else if (key.equals("scale")) {
            if (!cursor.number(binding.scale))
                return false;
        } else if (key.equals("threshold")) {
            if (!cursor.number(binding.threshold) || binding.threshold < 0.0F ||
                binding.threshold > 1.0F)
                return false;
        } else if (!cursor.skip()) {
            return false;
        }
        if (cursor.consume('}'))
            break;
        if (!cursor.consume(','))
            return false;
    }
    if (!hasControl)
        return false;
    manifest.inputBindings[manifest.inputBindingCount++] = binding;
    return true;
}

template <typename Cursor>
bool parseInputValue(Cursor& cursor, Manifest& manifest, const bool axis, const Text<32>& context,
                     const int priority, const bool enabled) noexcept {
    if (manifest.inputValueCount >= kMaximumInputValues || !cursor.consume('{'))
        return false;
    InputValue value;
    value.axis = axis;
    value.context = context;
    value.priority = priority;
    value.enabled = enabled;
    value.bindingStart = static_cast<std::uint16_t>(manifest.inputBindingCount);
    bool hasName = false;
    bool hasBindings = false;
    if (cursor.consume('}'))
        return false;
    for (;;) {
        Text<32> key;
        if (!cursor.string(key) || !cursor.consume(':'))
            return false;
        if (key.equals("name")) {
            if (!cursor.string(value.name) || value.name.length == 0U)
                return false;
            hasName = true;
        } else if (key.equals("bindings")) {
            if (!cursor.consume('['))
                return false;
            if (!cursor.consume(']')) {
                for (;;) {
                    if (!parseBinding(cursor, manifest))
                        return false;
                    if (cursor.consume(']'))
                        break;
                    if (!cursor.consume(','))
                        return false;
                }
            }
            hasBindings = true;
        } else if (!cursor.skip()) {
            return false;
        }
        if (cursor.consume('}'))
            break;
        if (!cursor.consume(','))
            return false;
    }
    const auto bindingCount = manifest.inputBindingCount - value.bindingStart;
    if (!hasName || !hasBindings || bindingCount == 0U || bindingCount > 65535U)
        return false;
    value.bindingCount = static_cast<std::uint16_t>(bindingCount);
    manifest.inputValues[manifest.inputValueCount++] = value;
    return true;
}

template <typename Cursor>
bool parseInputValues(Cursor& cursor, Manifest& manifest, const bool axis, const Text<32>& context,
                      const int priority, const bool enabled) noexcept {
    if (!cursor.consume('['))
        return false;
    if (cursor.consume(']'))
        return true;
    for (;;) {
        if (!parseInputValue(cursor, manifest, axis, context, priority, enabled))
            return false;
        if (cursor.consume(']'))
            return true;
        if (!cursor.consume(','))
            return false;
    }
}

template <typename Cursor> bool parseContext(Cursor& cursor, Manifest& manifest) noexcept {
    if (!cursor.consume('{'))
        return false;
    Text<32> context;
    int priority = 0;
    bool enabled = true;
    const auto valueStart = manifest.inputValueCount;
    bool hasName = false;
    if (cursor.consume('}'))
        return false;
    for (;;) {
        Text<32> key;
        if (!cursor.string(key) || !cursor.consume(':'))
            return false;
        if (key.equals("name")) {
            if (!cursor.string(context) || context.length == 0U)
                return false;
            hasName = true;
        } else if (key.equals("priority")) {
            if (!cursor.integer(priority))
                return false;
        } else if (key.equals("enabled")) {
            if (!cursor.boolean(enabled))
                return false;
        } else if (key.equals("actions")) {
            if (!parseInputValues(cursor, manifest, false, context, priority, enabled))
                return false;
        } else if (key.equals("axes")) {
            if (!parseInputValues(cursor, manifest, true, context, priority, enabled))
                return false;
        } else if (!cursor.skip()) {
            return false;
        }
        if (cursor.consume('}'))
            break;
        if (!cursor.consume(','))
            return false;
    }
    if (!hasName)
        return false;
    // Canonical JSON writes context metadata first, but applying it here also
    // keeps the parser correct for valid objects with reordered keys.
    for (std::size_t index = valueStart; index < manifest.inputValueCount; ++index) {
        manifest.inputValues[index].context = context;
        manifest.inputValues[index].priority = priority;
        manifest.inputValues[index].enabled = enabled;
    }
    return true;
}

template <typename Cursor> bool parseInput(Cursor& cursor, Manifest& manifest) noexcept {
    if (!cursor.consume('{'))
        return false;
    if (cursor.consume('}'))
        return true;
    for (;;) {
        Text<32> key;
        if (!cursor.string(key) || !cursor.consume(':'))
            return false;
        if (key.equals("contexts")) {
            if (!cursor.consume('['))
                return false;
            if (!cursor.consume(']')) {
                for (;;) {
                    if (!parseContext(cursor, manifest))
                        return false;
                    if (cursor.consume(']'))
                        break;
                    if (!cursor.consume(','))
                        return false;
                }
            }
        } else if (!cursor.skip()) {
            return false;
        }
        if (cursor.consume('}'))
            return true;
        if (!cursor.consume(','))
            return false;
    }
}

template <typename Cursor> bool parseManifestAssets(Cursor& cursor, Manifest& manifest) noexcept {
    if (!cursor.consume('['))
        return false;
    if (cursor.consume(']'))
        return true;
    for (;;) {
        if (manifest.assetCount >= kMaximumAssets || !cursor.consume('{'))
            return false;
        ManifestAsset asset;
        bool hasGuid = false;
        bool hasPath = false;
        bool hasType = false;
        if (cursor.consume('}'))
            return false;
        for (;;) {
            Text<32> key;
            if (!cursor.string(key) || !cursor.consume(':'))
                return false;
            if (key.equals("guid")) {
                Text<40> guid;
                if (!cursor.string(guid) || !parseGuidText(guid.value, asset.guid))
                    return false;
                hasGuid = true;
            } else if (key.equals("path")) {
                if (!cursor.string(asset.path) || asset.path.length == 0U)
                    return false;
                hasPath = true;
            } else if (key.equals("type")) {
                if (!cursor.string(asset.type) || asset.type.length == 0U)
                    return false;
                hasType = true;
            } else if (!cursor.skip()) {
                return false;
            }
            if (cursor.consume('}'))
                break;
            if (!cursor.consume(','))
                return false;
        }
        if (!hasGuid || !hasPath || !hasType)
            return false;
        manifest.assets[manifest.assetCount++] = asset;
        if (cursor.consume(']'))
            return true;
        if (!cursor.consume(','))
            return false;
    }
}

template <typename Cursor> bool parseTargets(Cursor& cursor, Manifest& manifest) noexcept {
    if (!cursor.consume('{'))
        return false;
    if (cursor.consume('}'))
        return true;
    for (;;) {
        Text<32> key;
        if (!cursor.string(key) || !cursor.consume(':'))
            return false;
        if (key.equals("esp32")) {
            if (!cursor.string(manifest.esp32Target))
                return false;
        } else if (!cursor.skip()) {
            return false;
        }
        if (cursor.consume('}'))
            return true;
        if (!cursor.consume(','))
            return false;
    }
}

template <typename Reader>
bool parseManifest(const Reader& reader, const PackedEntry& entry, Manifest& output,
                   Failure& failure) noexcept {
    JsonCursor<Reader> cursor(reader, entry.offset, entry.size, failure);
    if (!cursor.consume('{')) {
        failure.set(ErrorCode::InvalidManifest, cursor.position(), "manifest is not an object");
        return false;
    }
    if (cursor.consume('}')) {
        failure.set(ErrorCode::InvalidManifest, cursor.position(), "manifest is empty");
        return false;
    }
    for (;;) {
        Text<32> key;
        if (!cursor.string(key) || !cursor.consume(':'))
            break;
        if (key.equals("formatVersion")) {
            if (!cursor.integer(output.formatVersion))
                break;
        } else if (key.equals("name")) {
            if (!cursor.string(output.name))
                break;
        } else if (key.equals("assets")) {
            if (!parseManifestAssets(cursor, output))
                break;
        } else if (key.equals("input")) {
            if (!parseInput(cursor, output))
                break;
        } else if (key.equals("targetProfiles")) {
            if (!parseTargets(cursor, output))
                break;
        } else if (!cursor.skip()) {
            break;
        }
        if (cursor.consume('}')) {
            if (cursor.ended() && output.formatVersion == 2)
                return true;
            break;
        }
        if (!cursor.consume(','))
            break;
    }
    failure.set(ErrorCode::InvalidManifest, cursor.position(), "manifest parse failed");
    return false;
}

template <typename Reader> class LineReader final {
  public:
    LineReader(const Reader& source, const std::size_t offset, const std::size_t size,
               Failure& error) noexcept
        : reader_(source), position_(offset), end_(offset + size), failure_(error) {}

    bool next(char* output, const std::size_t capacity) noexcept {
        if (position_ >= end_ || capacity == 0U)
            return false;
        std::size_t length = 0U;
        while (position_ < end_) {
            const auto value = reader_.byte(position_++);
            if (value == '\n')
                break;
            if (value == '\0' || (value < 0x20U && value != '\r' && value != '\t'))
                return false;
            if (value != '\r') {
                if (length + 1U >= capacity)
                    return false;
                output[length++] = static_cast<char>(value);
            }
        }
        output[length] = '\0';
        ++line_;
        return length != 0U;
    }

    [[nodiscard]] bool ended() const noexcept {
        return position_ == end_;
    }
    [[nodiscard]] std::size_t position() const noexcept {
        return position_;
    }
    [[nodiscard]] std::size_t line() const noexcept {
        return line_;
    }

  private:
    const Reader& reader_;
    std::size_t position_;
    std::size_t end_;
    std::size_t line_ = 0U;
    Failure& failure_;
};

class Tokens final {
  public:
    explicit Tokens(const char* source) noexcept : cursor_(source) {}

    void spaces() noexcept {
        while (*cursor_ == ' ' || *cursor_ == '\t')
            ++cursor_;
    }

    bool word(char* output, const std::size_t capacity) noexcept {
        spaces();
        if (*cursor_ == '\0' || *cursor_ == '"' || capacity == 0U)
            return false;
        std::size_t length = 0U;
        while (*cursor_ != '\0' && *cursor_ != ' ' && *cursor_ != '\t') {
            if (length + 1U >= capacity)
                return false;
            output[length++] = *cursor_++;
        }
        output[length] = '\0';
        return length != 0U;
    }

    template <std::size_t Capacity> bool quoted(Text<Capacity>& output) noexcept {
        output.clear();
        spaces();
        if (*cursor_++ != '"')
            return false;
        while (*cursor_ != '\0') {
            auto value = *cursor_++;
            if (value == '"')
                return true;
            if (value == '\\') {
                value = *cursor_++;
                if (value == '\0')
                    return false;
                switch (value) {
                case 'n':
                    value = '\n';
                    break;
                case 'r':
                    value = '\r';
                    break;
                case 't':
                    value = '\t';
                    break;
                default:
                    break;
                }
            }
            if (!output.push(value))
                return false;
        }
        return false;
    }

    bool floating(float& output) noexcept {
        spaces();
        char* end = nullptr;
        output = std::strtof(cursor_, &end);
        if (end == cursor_ || !std::isfinite(output))
            return false;
        cursor_ = end;
        return true;
    }

    bool integer(int& output) noexcept {
        spaces();
        char* end = nullptr;
        const auto value = std::strtol(cursor_, &end, 10);
        if (end == cursor_ || value < -2147483647L || value > 2147483647L)
            return false;
        cursor_ = end;
        output = static_cast<int>(value);
        return true;
    }

    bool unsignedInteger(std::size_t& output) noexcept {
        spaces();
        if (*cursor_ == '-')
            return false;
        char* end = nullptr;
        const auto value = std::strtoul(cursor_, &end, 10);
        if (end == cursor_)
            return false;
        cursor_ = end;
        output = static_cast<std::size_t>(value);
        return true;
    }

    bool guid(Guid& output) noexcept {
        char token[40]{};
        return word(token, sizeof(token)) && parseGuidText(token, output);
    }

    bool color(Color& output) noexcept {
        int red = 0;
        int green = 0;
        int blue = 0;
        int alpha = 0;
        if (!integer(red) || !integer(green) || !integer(blue) || !integer(alpha) || red < 0 ||
            red > 255 || green < 0 || green > 255 || blue < 0 || blue > 255 || alpha < 0 ||
            alpha > 255)
            return false;
        output = {static_cast<std::uint8_t>(red), static_cast<std::uint8_t>(green),
                  static_cast<std::uint8_t>(blue), static_cast<std::uint8_t>(alpha)};
        return true;
    }

    [[nodiscard]] bool ended() noexcept {
        spaces();
        return *cursor_ == '\0';
    }

  private:
    const char* cursor_;
};

inline bool marker(Tokens& tokens, const char* expected) noexcept {
    char actual[48]{};
    return tokens.word(actual, sizeof(actual)) && std::strcmp(actual, expected) == 0;
}

inline bool lineEquals(const char* line, const char* expected) noexcept {
    return std::strcmp(line, expected) == 0;
}

inline void setDefaultNames(Entity& entity) noexcept {
    const auto set = [](Text<32>& text, const char* value) {
        text.clear();
        while (*value != '\0')
            text.push(*value++);
    };
    set(entity.moveXAxis, "MoveX");
    set(entity.moveYAxis, "MoveY");
    set(entity.primaryAction, "Jump");
    set(entity.steerAxis, "Steer");
    set(entity.throttleAxis, "Throttle");
    set(entity.brakeAction, "Brake");
    set(entity.driftAction, "Handbrake");
    set(entity.lookXAxis, "LookX");
}

inline bool parseComponentProperty(const char* line, const char* component, Entity& entity,
                                   bool& propertyEnabled) noexcept {
    Tokens tokens(line);
    Text<48> property;
    char type[24]{};
    if (!marker(tokens, "property") || !tokens.quoted(property) || !tokens.word(type, sizeof(type)))
        return false;
    if (std::strcmp(component, "fabgl.Transform") == 0) {
        float first = 0.0F;
        float second = 0.0F;
        float third = 0.0F;
        const bool typeMatches =
            std::strcmp(type, "vec3") == 0 ||
            (property.equals("localRotation") && std::strcmp(type, "euler") == 0);
        if (!typeMatches || !tokens.floating(first) || !tokens.floating(second) ||
            !tokens.floating(third) || !tokens.ended())
            return false;
        if (property.equals("localPosition")) {
            entity.x = first;
            entity.y = second;
            entity.z = third;
        } else if (property.equals("localRotation")) {
            entity.rotationZ = third;
        } else if (property.equals("localScale")) {
            entity.scaleX = first;
            entity.scaleY = second;
        }
        return true;
    }
    if (property.equals("enabled")) {
        int enabled = 0;
        if (std::strcmp(type, "bool") != 0 || !tokens.integer(enabled) ||
            (enabled != 0 && enabled != 1) || !tokens.ended())
            return false;
        propertyEnabled = enabled != 0;
        return true;
    }

    const auto asset = [&](Guid& destination) {
        return std::strcmp(type, "asset") == 0 && tokens.guid(destination) && tokens.ended();
    };
    const auto real = [&](float& destination) {
        return std::strcmp(type, "float") == 0 && tokens.floating(destination) && tokens.ended();
    };
    const auto enumeration = [&](int& destination) {
        return std::strcmp(type, "enum") == 0 && tokens.integer(destination) && tokens.ended();
    };
    const auto string = [&](Text<32>& destination) {
        return std::strcmp(type, "string") == 0 && tokens.quoted(destination) && tokens.ended();
    };
    const auto boolean = [&](bool& destination) {
        int value = 0;
        if (std::strcmp(type, "bool") != 0 || !tokens.integer(value) ||
            (value != 0 && value != 1) || !tokens.ended())
            return false;
        destination = value != 0;
        return true;
    };
    const auto vector2 = [&](float& x, float& y) {
        return std::strcmp(type, "vec2") == 0 && tokens.floating(x) && tokens.floating(y) &&
               tokens.ended();
    };
    const auto unsignedValue = [&](std::uint32_t& destination, const char* expectedType) {
        std::size_t value = 0U;
        if (std::strcmp(type, expectedType) != 0 || !tokens.unsignedInteger(value) ||
            value > 0xFFFFFFFFU || !tokens.ended())
            return false;
        destination = static_cast<std::uint32_t>(value);
        return true;
    };

    if (std::strcmp(component, "fabgl.SpriteRenderer") == 0) {
        if (property.equals("sprite"))
            return asset(entity.sprite);
        if (property.equals("tint"))
            return std::strcmp(type, "color") == 0 && tokens.color(entity.tint) && tokens.ended();
    } else if (std::strcmp(component, "fabgl.CharacterBody2D") == 0) {
        if (property.equals("moveSpeed"))
            return real(entity.moveSpeed);
        if (property.equals("movementMode"))
            return enumeration(entity.movementMode);
        if (property.equals("moveXAxis"))
            return string(entity.moveXAxis);
        if (property.equals("moveYAxis"))
            return string(entity.moveYAxis);
        if (property.equals("primaryAction"))
            return string(entity.primaryAction);
    } else if (std::strcmp(component, "fabgl.VehicleController") == 0) {
        if (property.equals("acceleration"))
            return real(entity.acceleration);
        if (property.equals("track"))
            return asset(entity.track);
        if (property.equals("steerAxis"))
            return string(entity.steerAxis);
        if (property.equals("throttleAxis"))
            return string(entity.throttleAxis);
        if (property.equals("brakeAction"))
            return string(entity.brakeAction);
        if (property.equals("driftAction"))
            return string(entity.driftAction);
    } else if (std::strcmp(component, "fabgl.RaycastMap") == 0) {
        if (property.equals("map"))
            return asset(entity.raycastAsset);
        if (property.equals("cellSize")) {
            int raw = 0;
            if (std::strcmp(type, "fixed") != 0 || !tokens.integer(raw) || !tokens.ended())
                return false;
            entity.raycastCellSize = static_cast<float>(raw) / 65536.0F;
            return entity.raycastCellSize > 0.0F;
        }
    } else if (std::strcmp(component, "fabgl.FirstPersonController") == 0) {
        if (property.equals("moveSpeed"))
            return real(entity.moveSpeed);
        if (property.equals("lookSensitivity"))
            return real(entity.lookSensitivity);
        if (property.equals("moveXAxis"))
            return string(entity.moveXAxis);
        if (property.equals("moveYAxis"))
            return string(entity.moveYAxis);
        if (property.equals("lookXAxis"))
            return string(entity.lookXAxis);
        if (property.equals("primaryAction"))
            return string(entity.primaryAction);
    } else if (std::strcmp(component, "fabgl.Collider2D") == 0) {
        if (property.equals("shape"))
            return enumeration(entity.colliderShape);
        if (property.equals("size"))
            return vector2(entity.colliderWidth, entity.colliderHeight);
        if (property.equals("trigger"))
            return boolean(entity.colliderTrigger);
        if (property.equals("layer"))
            return unsignedValue(entity.colliderLayer, "flags");
        if (property.equals("collisionMask"))
            return unsignedValue(entity.colliderMask, "flags");
    } else if (std::strcmp(component, "fabgl.Rigidbody2D") == 0) {
        if (property.equals("bodyType"))
            return enumeration(entity.bodyType);
        if (property.equals("velocity"))
            return vector2(entity.rigidbodyVelocityX, entity.rigidbodyVelocityY);
        if (property.equals("gravityScale"))
            return real(entity.gravityScale);
        if (property.equals("restitution"))
            return real(entity.restitution);
    } else if (std::strcmp(component, "fabgl.ParticleEmitter") == 0) {
        if (property.equals("rate"))
            return real(entity.particleRate);
        if (property.equals("maxParticles")) {
            std::uint32_t value = 0U;
            if (!unsignedValue(value, "uint") || value > kMaximumParticles)
                return false;
            entity.particleMaximum = static_cast<std::uint16_t>(value);
            return true;
        }
        if (property.equals("burstOnStart")) {
            std::uint32_t value = 0U;
            if (!unsignedValue(value, "uint") || value > kMaximumParticles)
                return false;
            entity.particleBurst = static_cast<std::uint16_t>(value);
            return true;
        }
        if (property.equals("lifetime"))
            return real(entity.particleLifetime);
        if (property.equals("velocity"))
            return vector2(entity.particleVelocityX, entity.particleVelocityY);
        if (property.equals("acceleration"))
            return vector2(entity.particleAccelerationX, entity.particleAccelerationY);
        if (property.equals("startColor"))
            return std::strcmp(type, "color") == 0 && tokens.color(entity.particleStartColor) &&
                   tokens.ended();
        if (property.equals("endColor"))
            return std::strcmp(type, "color") == 0 && tokens.color(entity.particleEndColor) &&
                   tokens.ended();
    } else if (std::strcmp(component, "fabgl.UITransform") == 0) {
        if (property.equals("widgetType"))
            return enumeration(entity.uiWidgetType);
        if (property.equals("offsetMinimum"))
            return vector2(entity.uiLeft, entity.uiTop);
        if (property.equals("offsetMaximum"))
            return vector2(entity.uiRight, entity.uiBottom);
        if (property.equals("visible"))
            return boolean(entity.uiVisible);
        if (property.equals("text"))
            return std::strcmp(type, "string") == 0 && tokens.quoted(entity.uiText) &&
                   tokens.ended();
        if (property.equals("minimum"))
            return real(entity.uiMinimum);
        if (property.equals("maximum"))
            return real(entity.uiMaximum);
        if (property.equals("value"))
            return real(entity.uiValue);
    }
    // Unknown serialized properties are still bounded by the line reader and
    // were validated by the desktop serializer before export.
    return true;
}

template <typename Reader>
bool parseScene(const Reader& reader, const PackedEntry& entry, Scene& output,
                Failure& failure) noexcept {
    LineReader<Reader> lines(reader, entry.offset, entry.size, failure);
    char line[512]{};
    if (!lines.next(line, sizeof(line)) || !lineEquals(line, "fglscene 2")) {
        failure.set(ErrorCode::InvalidScene, entry.offset, "unsupported scene header");
        return false;
    }
    if (!lines.next(line, sizeof(line)))
        goto invalid;
    {
        Tokens tokens(line);
        if (!marker(tokens, "scene_guid") || !tokens.guid(output.guid) || !tokens.ended())
            goto invalid;
    }
    if (!lines.next(line, sizeof(line)))
        goto invalid;
    {
        Tokens tokens(line);
        if (!marker(tokens, "scene_name") || !tokens.quoted(output.name) || !tokens.ended())
            goto invalid;
    }

    while (lines.next(line, sizeof(line))) {
        if (lineEquals(line, "scene_end")) {
            if (lines.ended())
                return true;
            goto invalid;
        }
        if (!lineEquals(line, "entity_begin") || output.entityCount >= kMaximumEntities) {
            failure.set(output.entityCount >= kMaximumEntities ? ErrorCode::CapacityExceeded
                                                               : ErrorCode::InvalidScene,
                        lines.position(), "invalid or excessive entity record");
            return false;
        }
        auto& entity = output.entities[output.entityCount];
        setDefaultNames(entity);
        if (!lines.next(line, sizeof(line)))
            goto invalid;
        {
            Tokens tokens(line);
            if (!marker(tokens, "guid") || !tokens.guid(entity.guid) || !tokens.ended())
                goto invalid;
        }
        if (!lines.next(line, sizeof(line)))
            goto invalid;
        {
            Tokens tokens(line);
            if (!marker(tokens, "name") || !tokens.quoted(entity.name) || !tokens.ended())
                goto invalid;
        }
        if (!lines.next(line, sizeof(line)))
            goto invalid;
        {
            Tokens tokens(line);
            int active = 0;
            if (!marker(tokens, "active") || !tokens.integer(active) ||
                (active != 0 && active != 1) || !tokens.ended())
                goto invalid;
            entity.active = active != 0;
        }
        if (!lines.next(line, sizeof(line)))
            goto invalid;
        {
            Tokens tokens(line);
            char parent[48]{};
            if (!marker(tokens, "parent") || !tokens.word(parent, sizeof(parent)) ||
                !tokens.ended())
                goto invalid;
            Guid parentGuid;
            if (std::strcmp(parent, "nil") != 0 && !parseGuidText(parent, parentGuid))
                goto invalid;
        }
        if (!lines.next(line, sizeof(line)))
            goto invalid;
        std::size_t componentCount = 0U;
        {
            Tokens tokens(line);
            if (!marker(tokens, "component_count") || !tokens.unsignedInteger(componentCount) ||
                componentCount == 0U || componentCount > 64U || !tokens.ended())
                goto invalid;
        }
        bool hasTransform = false;
        for (std::size_t componentIndex = 0U; componentIndex < componentCount; ++componentIndex) {
            if (!lines.next(line, sizeof(line)) || !lineEquals(line, "component_begin"))
                goto invalid;
            if (!lines.next(line, sizeof(line)))
                goto invalid;
            {
                Tokens tokens(line);
                Guid ignored;
                if (!marker(tokens, "type_id") || !tokens.guid(ignored) || !tokens.ended())
                    goto invalid;
            }
            if (!lines.next(line, sizeof(line)))
                goto invalid;
            Text<64> typeName;
            {
                Tokens tokens(line);
                if (!marker(tokens, "type_name") || !tokens.quoted(typeName) || !tokens.ended())
                    goto invalid;
            }
            if (!lines.next(line, sizeof(line)))
                goto invalid;
            bool componentEnabled = false;
            {
                Tokens tokens(line);
                int enabled = 0;
                if (!marker(tokens, "enabled") || !tokens.integer(enabled) ||
                    (enabled != 0 && enabled != 1) || !tokens.ended())
                    goto invalid;
                componentEnabled = enabled != 0;
            }
            if (!lines.next(line, sizeof(line)))
                goto invalid;
            std::size_t propertyCount = 0U;
            {
                Tokens tokens(line);
                if (!marker(tokens, "property_count") || !tokens.unsignedInteger(propertyCount) ||
                    propertyCount > 64U || !tokens.ended())
                    goto invalid;
            }
            bool propertyEnabled = true;
            for (std::size_t propertyIndex = 0U; propertyIndex < propertyCount; ++propertyIndex) {
                if (!lines.next(line, sizeof(line)) ||
                    !parseComponentProperty(line, typeName.value, entity, propertyEnabled))
                    goto invalid;
            }
            if (!lines.next(line, sizeof(line)) || !lineEquals(line, "component_end"))
                goto invalid;
            const auto enabled = componentEnabled && propertyEnabled;
            if (typeName.equals("fabgl.Transform"))
                hasTransform = true;
            else if (enabled && typeName.equals("fabgl.SpriteRenderer"))
                entity.components |= Component::Sprite;
            else if (enabled && typeName.equals("fabgl.CharacterBody2D"))
                entity.components |= Component::Character;
            else if (enabled && typeName.equals("fabgl.VehicleController"))
                entity.components |= Component::Vehicle;
            else if (enabled && typeName.equals("fabgl.RaycastMap"))
                entity.components |= Component::RaycastMap;
            else if (enabled && typeName.equals("fabgl.FirstPersonController"))
                entity.components |= Component::FirstPerson;
            else if (enabled && typeName.equals("fabgl.Camera"))
                entity.components |= Component::Camera;
            else if (enabled && typeName.equals("fabgl.Collider2D"))
                entity.components |= Component::Collider2D;
            else if (enabled && typeName.equals("fabgl.Rigidbody2D"))
                entity.components |= Component::Rigidbody2D;
            else if (enabled && typeName.equals("fabgl.ParticleEmitter"))
                entity.components |= Component::ParticleEmitter;
            else if (enabled && typeName.equals("fabgl.UITransform"))
                entity.components |= Component::RuntimeUi;
        }
        if (!hasTransform || !lines.next(line, sizeof(line)) || !lineEquals(line, "entity_end"))
            goto invalid;
        ++output.entityCount;
    }

invalid:
    failure.set(ErrorCode::InvalidScene, lines.position(), "scene parse failed");
    return false;
}

template <typename Reader>
bool parseIndexedImage(const Reader& reader, const PackedAsset& asset, IndexedImageView& output,
                       Failure& failure) noexcept {
    constexpr std::size_t header = 20U;
    if (asset.contentSize < header ||
        !startsWith(reader, asset.contentOffset, asset.contentSize, "FGLI") ||
        u16(reader, asset.contentOffset + 4U) != 1U) {
        failure.set(ErrorCode::UnsupportedAsset, asset.contentOffset, "invalid FGLI image");
        return false;
    }
    const auto flags = u16(reader, asset.contentOffset + 6U);
    output.width = u16(reader, asset.contentOffset + 8U);
    output.height = u16(reader, asset.contentOffset + 10U);
    output.paletteCount = u16(reader, asset.contentOffset + 12U);
    output.transparentIndex = u16(reader, asset.contentOffset + 14U);
    output.pixelCount = u32(reader, asset.contentOffset + 16U);
    const auto expected = static_cast<std::uint64_t>(output.width) * output.height;
    const auto paletteBytes = static_cast<std::uint64_t>(output.paletteCount) * 4U;
    if ((flags != 0U && flags != 1U) || output.width == 0U || output.height == 0U ||
        output.width > 512U || output.height > 512U || output.paletteCount == 0U ||
        output.paletteCount > 256U || output.pixelCount != expected ||
        paletteBytes > asset.contentSize - header ||
        (flags == 0U && output.transparentIndex != 255U) ||
        (flags == 1U && output.transparentIndex >= output.paletteCount)) {
        failure.set(ErrorCode::UnsupportedAsset, asset.contentOffset, "invalid FGLI metadata");
        return false;
    }
    output.paletteOffset = asset.contentOffset + static_cast<std::uint32_t>(header);
    output.runsOffset = output.paletteOffset + static_cast<std::uint32_t>(paletteBytes);
    output.endOffset = asset.contentOffset + asset.contentSize;
    std::uint64_t decoded = 0U;
    auto cursor = output.runsOffset;
    while (cursor < output.endOffset && decoded < output.pixelCount) {
        if (output.endOffset - cursor < 2U)
            return false;
        const auto count = reader.byte(cursor++);
        const auto palette = reader.byte(cursor++);
        if (count == 0U || palette >= output.paletteCount || decoded + count > output.pixelCount)
            return false;
        decoded += count;
    }
    if (decoded != output.pixelCount || cursor != output.endOffset) {
        failure.set(ErrorCode::UnsupportedAsset, cursor, "invalid FGLI RLE stream");
        return false;
    }
    output.valid = true;
    return true;
}

template <typename Reader>
bool parseRaycast(const Reader& reader, const PackedAsset& asset, RaycastMapData& output,
                  Failure& failure) noexcept {
    LineReader<Reader> lines(reader, asset.contentOffset, asset.contentSize, failure);
    char line[512]{};
    if (!lines.next(line, sizeof(line)) || !lineEquals(line, "fglray 1"))
        goto invalid;
    if (!lines.next(line, sizeof(line)))
        goto invalid;
    {
        Tokens tokens(line);
        if (!marker(tokens, "guid") || !tokens.guid(output.guid) || !tokens.ended() ||
            output.guid != asset.guid)
            goto invalid;
    }
    if (!lines.next(line, sizeof(line)))
        goto invalid;
    {
        Tokens tokens(line);
        if (!marker(tokens, "size") || !tokens.integer(output.width) ||
            !tokens.integer(output.height) || !tokens.ended() || output.width < 1 ||
            output.height < 1 || output.width > 32 || output.height > 32 ||
            static_cast<std::size_t>(output.width) >
                kMaximumRaycastCells / static_cast<std::size_t>(output.height))
            goto invalid;
    }
    if (!lines.next(line, sizeof(line)))
        goto invalid;
    {
        Tokens tokens(line);
        if (!marker(tokens, "palette") || !tokens.unsignedInteger(output.paletteCount) ||
            output.paletteCount < 2U || output.paletteCount > kMaximumRaycastColors ||
            !tokens.ended())
            goto invalid;
    }
    for (std::size_t index = 0U; index < output.paletteCount; ++index) {
        if (!lines.next(line, sizeof(line)))
            goto invalid;
        Tokens tokens(line);
        if (!marker(tokens, "color") || !tokens.color(output.palette[index]) || !tokens.ended())
            goto invalid;
    }
    if (!lines.next(line, sizeof(line)))
        goto invalid;
    {
        Tokens tokens(line);
        std::size_t cells = 0U;
        if (!marker(tokens, "cells") || !tokens.unsignedInteger(cells) ||
            cells != static_cast<std::size_t>(output.width * output.height) || !tokens.ended())
            goto invalid;
    }
    for (int y = 0; y < output.height; ++y) {
        if (!lines.next(line, sizeof(line)))
            goto invalid;
        Tokens tokens(line);
        char row[2U * 32U + 1U]{};
        if (!marker(tokens, "row") || !tokens.word(row, sizeof(row)) || !tokens.ended() ||
            std::strlen(row) != static_cast<std::size_t>(output.width) * 2U)
            goto invalid;
        for (int x = 0; x < output.width; ++x) {
            const auto high = hexNibble(static_cast<std::uint8_t>(row[x * 2]));
            const auto low = hexNibble(static_cast<std::uint8_t>(row[x * 2 + 1]));
            const auto value = high < 0 || low < 0 ? 256 : (high << 4) | low;
            if (value < 0 || static_cast<std::size_t>(value) >= output.paletteCount)
                goto invalid;
            output.cells[static_cast<std::size_t>(y * output.width + x)] =
                static_cast<std::uint8_t>(value);
        }
    }
    if (!lines.next(line, sizeof(line)) || !lineEquals(line, "end") || !lines.ended())
        goto invalid;
    output.valid = true;
    return true;

invalid:
    failure.set(ErrorCode::UnsupportedAsset, lines.position(), "fglray parse failed");
    return false;
}

template <typename Reader>
bool parseTrack(const Reader& reader, const PackedAsset& asset, RacerTrackData& output,
                Failure& failure) noexcept {
    LineReader<Reader> lines(reader, asset.contentOffset, asset.contentSize, failure);
    char line[768]{};
    if (!lines.next(line, sizeof(line)) || !lineEquals(line, "fgltrack 1"))
        goto invalid;
    if (!lines.next(line, sizeof(line)))
        goto invalid;
    {
        Tokens tokens(line);
        if (!marker(tokens, "track_guid") || !tokens.guid(output.guid) || !tokens.ended() ||
            output.guid != asset.guid)
            goto invalid;
    }
    if (!lines.next(line, sizeof(line)))
        goto invalid;
    {
        Tokens tokens(line);
        Text<128> name;
        if (!marker(tokens, "track_name") || !tokens.quoted(name) || !tokens.ended())
            goto invalid;
    }
    if (!lines.next(line, sizeof(line)))
        goto invalid;
    {
        Tokens tokens(line);
        if (!marker(tokens, "segment_length") || !tokens.floating(output.segmentLength) ||
            output.segmentLength <= 0.0F || !tokens.ended())
            goto invalid;
    }
    // start_segment, finish_segment and weather are validated structurally;
    // rendering only needs the segment stream.
    for (const char* expected : {"start_segment", "finish_segment", "weather"}) {
        if (!lines.next(line, sizeof(line)))
            goto invalid;
        Tokens tokens(line);
        if (!marker(tokens, expected))
            goto invalid;
    }
    if (!lines.next(line, sizeof(line)))
        goto invalid;
    {
        Tokens tokens(line);
        if (!marker(tokens, "segment_count") || !tokens.unsignedInteger(output.segmentCount) ||
            output.segmentCount == 0U || output.segmentCount > kMaximumTrackSegments ||
            !tokens.ended())
            goto invalid;
    }
    for (std::size_t index = 0U; index < output.segmentCount; ++index) {
        if (!lines.next(line, sizeof(line)))
            goto invalid;
        Tokens tokens(line);
        auto& segment = output.segments[index];
        if (!marker(tokens, "segment") || !tokens.floating(segment.curve) ||
            !tokens.floating(segment.hill) || !tokens.floating(segment.width) ||
            !tokens.color(segment.road) || !tokens.color(segment.grass) ||
            !tokens.color(segment.rumble) || !tokens.ended() || segment.width < 0.25F ||
            segment.width > 2.0F)
            goto invalid;
    }
    for (const char* countMarker :
         {"checkpoint_count", "roadside_count", "background_count", "opponent_count"}) {
        if (!lines.next(line, sizeof(line)))
            goto invalid;
        Tokens tokens(line);
        std::size_t count = 0U;
        if (!marker(tokens, countMarker) || !tokens.unsignedInteger(count) || count > 1024U ||
            !tokens.ended())
            goto invalid;
        for (std::size_t index = 0U; index < count; ++index) {
            if (!lines.next(line, sizeof(line)))
                goto invalid;
            Tokens record(line);
            const char* expected = std::strcmp(countMarker, "checkpoint_count") == 0 ? "checkpoint"
                                   : std::strcmp(countMarker, "roadside_count") == 0 ? "roadside"
                                   : std::strcmp(countMarker, "background_count") == 0
                                       ? "background"
                                       : "opponent";
            if (!marker(record, expected))
                goto invalid;
        }
    }
    if (!lines.next(line, sizeof(line)) || !lineEquals(line, "track_end") || !lines.ended())
        goto invalid;
    output.valid = true;
    return true;

invalid:
    failure.set(ErrorCode::UnsupportedAsset, lines.position(), "fgltrack parse failed");
    return false;
}

} // namespace detail

// Routes tagged external offsets to a bounded SD/file reader while preserving
// the existing embedded PROGMEM reader contract. External offsets never enter
// the manifest/scene parser; they are assigned only after both packs validate.
template <typename EmbeddedReader, typename ExternalReader>
struct RoutedAssetReader final {
    const EmbeddedReader& embedded;
    const ExternalReader& external;

    [[nodiscard]] std::size_t size() const noexcept {
        return static_cast<std::size_t>(kExternalOffsetFlag) + external.size();
    }

    [[nodiscard]] std::uint8_t byte(const std::size_t offset) const noexcept {
        if ((offset & static_cast<std::size_t>(kExternalOffsetFlag)) != 0U)
            return external.byte(offset & static_cast<std::size_t>(kExternalOffsetMask));
        return embedded.byte(offset);
    }
};

// Validates a separately stored FGLP pack containing only StorageClass::Sd
// FGLA wrappers, then atomically binds every placeholder from the embedded
// pack. No project-visible asset becomes external until the complete file and
// all path/GUID/checksum pairs have passed validation.
template <typename Reader>
bool bindExternalAssetPack(const Reader& reader, const std::size_t expectedAssets,
                           const std::uint64_t expectedPayloadChecksum,
                           const std::uint64_t expectedBuildChecksum, RuntimeProject& output,
                           Failure& failure) noexcept {
    if (expectedAssets == 0U)
        return true;
    if (expectedAssets > kMaximumAssets || reader.size() < kPackHeaderSize ||
        reader.size() >= static_cast<std::size_t>(kExternalOffsetFlag) ||
        !detail::startsWith(reader, 0U, reader.size(), "FGLP") || detail::u16(reader, 4U) != 1U ||
        detail::u16(reader, 6U) != 0U) {
        failure.set(ErrorCode::InvalidPack, 0U, "invalid external asset pack header");
        return false;
    }
    const auto entryCount = detail::u32(reader, 8U);
    const auto alignment = detail::u32(reader, 12U);
    const auto entrySize = detail::u32(reader, 16U);
    const auto dataOffset = detail::u32(reader, 20U);
    const auto buildChecksum = detail::u64(reader, 24U);
    const auto indexBytes = static_cast<std::uint64_t>(entryCount) * kPackIndexEntrySize;
    if (entryCount != expectedAssets || alignment == 0U || alignment > 4096U ||
        (alignment & (alignment - 1U)) != 0U || entrySize != kPackIndexEntrySize ||
        dataOffset < kPackHeaderSize || dataOffset > reader.size() ||
        indexBytes > dataOffset - kPackHeaderSize || buildChecksum != expectedBuildChecksum ||
        detail::checksum(reader, kPackHeaderSize, reader.size() - kPackHeaderSize) !=
            buildChecksum ||
        detail::checksum(reader, 0U, reader.size()) != expectedPayloadChecksum) {
        failure.set(ErrorCode::InvalidPack, 8U, "external asset pack metadata mismatch");
        return false;
    }

    struct ExternalBinding final {
        Guid guid;
        std::uint32_t contentOffset = 0U;
        std::uint32_t contentSize = 0U;
    };
    ExternalBinding staged[kMaximumAssets]{};
    std::size_t stagedCount = 0U;
    Guid previous{};
    bool hasPrevious = false;
    for (std::uint32_t index = 0U; index < entryCount; ++index) {
        const auto entryOffset = kPackHeaderSize + static_cast<std::size_t>(index) * entrySize;
        Guid guid;
        for (std::size_t byte = 0U; byte < 16U; ++byte)
            guid.bytes[byte] = reader.byte(entryOffset + byte);
        const auto type = detail::u32(reader, entryOffset + 16U);
        const auto storage = reader.byte(entryOffset + 20U);
        const auto offset = detail::u32(reader, entryOffset + 24U);
        const auto size = detail::u32(reader, entryOffset + 28U);
        const auto checksum = detail::u64(reader, entryOffset + 32U);
        const bool rangeValid = offset >= dataOffset && offset <= reader.size() &&
                                size <= reader.size() - offset;
        if (guid.isNil() || type != kAssetPayloadType ||
            storage != static_cast<std::uint8_t>(StorageClass::Sd) || !rangeValid ||
            (offset & (alignment - 1U)) != 0U || size < 8U ||
            detail::checksum(reader, offset, size) != checksum ||
            !detail::startsWith(reader, offset, size, "FGLA") || detail::u16(reader, offset + 4U) != 1U ||
            (hasPrevious && std::memcmp(previous.bytes, guid.bytes, 16U) >= 0)) {
            failure.set(ErrorCode::InvalidPack, entryOffset,
                        "invalid external asset pack entry");
            return false;
        }
        previous = guid;
        hasPrevious = true;
        const auto pathLength = static_cast<std::size_t>(detail::u16(reader, offset + 6U));
        if (pathLength > size - 8U || !detail::safeRelativePath(reader, offset + 8U, pathLength)) {
            failure.set(ErrorCode::InvalidPack, offset, "invalid external asset path");
            return false;
        }
        const auto* placeholder = output.findAsset(guid);
        if (placeholder == nullptr || placeholder->storage != StorageClass::Sd ||
            placeholder->contentSize != 0U) {
            failure.set(ErrorCode::MissingAsset, entryOffset,
                        "external asset has no embedded SD placeholder");
            return false;
        }
        ExternalBinding& bound = staged[stagedCount++];
        bound.guid = guid;
        Text<128> path;
        if (!detail::copyBytes(reader, offset + 8U, pathLength, path) ||
            !path.equals(placeholder->path)) {
            failure.set(ErrorCode::InvalidPack, offset, "external asset path mismatch");
            return false;
        }
        const auto contentOffset = offset + 8U + static_cast<std::uint32_t>(pathLength);
        if (contentOffset > kExternalOffsetMask) {
            failure.set(ErrorCode::CapacityExceeded, offset,
                        "external asset offset exceeds routed-reader limit");
            return false;
        }
        bound.contentOffset = contentOffset | kExternalOffsetFlag;
        bound.contentSize = size - 8U - static_cast<std::uint32_t>(pathLength);
    }

    for (std::size_t index = 0U; index < output.payload.assetCount; ++index) {
        auto& destination = output.payload.assets[index];
        if (destination.storage != StorageClass::Sd || destination.contentSize != 0U)
            continue;
        const ExternalBinding* source = nullptr;
        for (std::size_t stagedIndex = 0U; stagedIndex < stagedCount; ++stagedIndex) {
            if (staged[stagedIndex].guid == destination.guid) {
                source = &staged[stagedIndex];
                break;
            }
        }
        if (source == nullptr) {
            failure.set(ErrorCode::MissingAsset, 0U,
                        "embedded SD placeholder is absent from external pack");
            return false;
        }
        destination.contentOffset = source->contentOffset;
        destination.contentSize = source->contentSize;
    }
    return true;
}

template <typename Reader>
bool loadDeferredRuntimeAssets(const Reader& reader, RuntimeProject& output,
                               Failure& failure) noexcept {
    for (std::size_t index = 0U; index < output.scene.entityCount; ++index) {
        const auto& entity = output.scene.entities[index];
        if ((entity.components & Component::RaycastMap) != 0U && !entity.raycastAsset.isNil() &&
            !output.raycastMap.valid) {
            const auto* asset = output.findAsset(entity.raycastAsset);
            if (asset == nullptr || asset->contentSize == 0U ||
                !detail::parseRaycast(reader, *asset, output.raycastMap, failure))
                return false;
            break;
        }
    }
    for (std::size_t index = 0U; index < output.scene.entityCount; ++index) {
        const auto& entity = output.scene.entities[index];
        if ((entity.components & Component::Vehicle) != 0U && !entity.track.isNil() &&
            !output.racerTrack.valid) {
            const auto* asset = output.findAsset(entity.track);
            if (asset == nullptr || asset->contentSize == 0U ||
                !detail::parseTrack(reader, *asset, output.racerTrack, failure))
                return false;
            break;
        }
    }
    return true;
}

template <typename Reader>
bool loadProject(const Reader& reader, const std::size_t expectedAssetCount,
                 const std::uint64_t expectedPayloadChecksum,
                 const std::uint64_t expectedBuildChecksum, const char* expectedTarget,
                 RuntimeProject& output, Failure& failure) noexcept {
    if (!detail::inspectPack(reader, expectedAssetCount, expectedPayloadChecksum,
                             expectedBuildChecksum, output.payload, failure) ||
        !detail::parseManifest(reader, output.payload.manifest, output.manifest, failure) ||
        !detail::parseScene(reader, output.payload.scene, output.scene, failure))
        return false;

    if (expectedTarget != nullptr && output.manifest.esp32Target.length != 0U &&
        !output.manifest.esp32Target.equals(expectedTarget)) {
        failure.set(ErrorCode::TargetMismatch, output.payload.manifest.offset,
                    "manifest ESP32 target does not match firmware profile");
        return false;
    }
    for (std::size_t index = 0U; index < output.manifest.assetCount; ++index) {
        const auto& declared = output.manifest.assets[index];
        const auto* packed = output.findAsset(declared.guid);
        if (packed == nullptr || !packed->path.equals(declared.path)) {
            failure.set(ErrorCode::MissingAsset, output.payload.manifest.offset,
                        "manifest asset is missing from pack");
            return false;
        }
    }

    for (std::size_t index = 0U; index < output.scene.entityCount; ++index) {
        const auto& entity = output.scene.entities[index];
        if ((entity.components & Component::RaycastMap) != 0U && !entity.raycastAsset.isNil()) {
            const auto* asset = output.findAsset(entity.raycastAsset);
            if (asset == nullptr)
                return false;
            if (asset->storage == StorageClass::Sd && asset->contentSize == 0U)
                break;
            if (!detail::parseRaycast(reader, *asset, output.raycastMap, failure))
                return false;
            break;
        }
    }
    for (std::size_t index = 0U; index < output.scene.entityCount; ++index) {
        const auto& entity = output.scene.entities[index];
        if ((entity.components & Component::Vehicle) != 0U && !entity.track.isNil()) {
            const auto* asset = output.findAsset(entity.track);
            if (asset == nullptr)
                return false;
            if (asset->storage == StorageClass::Sd && asset->contentSize == 0U)
                break;
            if (!detail::parseTrack(reader, *asset, output.racerTrack, failure))
                return false;
            break;
        }
    }
    output.loaded = true;
    return true;
}

template <typename Reader>
bool inspectIndexedImage(const Reader& reader, const RuntimeProject& project, const Guid& guid,
                         IndexedImageView& output, Failure& failure) noexcept {
    const auto* asset = project.findAsset(guid);
    if (asset == nullptr) {
        failure.set(ErrorCode::MissingAsset, 0U, "sprite asset is missing");
        return false;
    }
    return detail::parseIndexedImage(reader, *asset, output, failure);
}

} // namespace fabgl_project_runtime
