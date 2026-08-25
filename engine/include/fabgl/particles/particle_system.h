#pragma once

#include "fabgl/core/result.h"
#include "fabgl/math/types.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace fabgl {

struct ParticleHandle final {
    std::size_t index = std::numeric_limits<std::size_t>::max();
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return index != std::numeric_limits<std::size_t>::max() && generation != 0;
    }
    friend constexpr bool operator==(ParticleHandle lhs, ParticleHandle rhs) noexcept {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }
    friend constexpr bool operator!=(ParticleHandle lhs, ParticleHandle rhs) noexcept {
        return !(lhs == rhs);
    }
};

struct ParticleSpawn final {
    Vec2 position{};
    Vec2 velocity{};
    Vec2 acceleration{};
    Color color{};
    float size = 1.0F;
    float lifetimeSeconds = 1.0F;
    float rotationDegrees = 0.0F;
    float angularVelocityDegrees = 0.0F;
};

struct Particle final {
    Vec2 position{};
    Vec2 velocity{};
    Vec2 acceleration{};
    Color color{};
    float size = 1.0F;
    float ageSeconds = 0.0F;
    float lifetimeSeconds = 1.0F;
    float rotationDegrees = 0.0F;
    float angularVelocityDegrees = 0.0F;

    [[nodiscard]] float normalizedAge() const noexcept;
};

struct ParticleSystemStats final {
    std::size_t activeParticles = 0;
    std::size_t capacity = 0;
    std::uint64_t totalSpawned = 0;
    std::uint64_t totalDestroyed = 0;
    std::uint64_t totalExpired = 0;
    std::uint64_t rejectedSpawns = 0;
};

class ParticleSystem final {
  public:
    explicit ParticleSystem(std::size_t capacity = 192);

    [[nodiscard]] Result<ParticleHandle> spawn(const ParticleSpawn& initialState);
    [[nodiscard]] bool destroy(ParticleHandle handle) noexcept;
    [[nodiscard]] Result<void> update(float deltaSeconds);
    void clear() noexcept;

    [[nodiscard]] Particle* get(ParticleHandle handle) noexcept;
    [[nodiscard]] const Particle* get(ParticleHandle handle) const noexcept;
    [[nodiscard]] bool isAlive(ParticleHandle handle) const noexcept {
        return get(handle) != nullptr;
    }

    // Slot access permits allocation-free iteration. Inactive slots return nullptr.
    [[nodiscard]] Particle* particleAtSlot(std::size_t slotIndex) noexcept;
    [[nodiscard]] const Particle* particleAtSlot(std::size_t slotIndex) const noexcept;
    [[nodiscard]] ParticleHandle handleAtSlot(std::size_t slotIndex) const noexcept;

    [[nodiscard]] std::size_t capacity() const noexcept {
        return slots_.size();
    }
    [[nodiscard]] std::size_t activeCount() const noexcept {
        return activeCount_;
    }
    [[nodiscard]] ParticleSystemStats stats() const noexcept;

  private:
    struct Slot final {
        Particle particle{};
        std::uint32_t generation = 1;
        bool active = false;
    };

    void releaseSlot(std::size_t slotIndex, bool expired) noexcept;
    static void advanceGeneration(Slot& slot) noexcept;

    std::vector<Slot> slots_;
    std::vector<std::size_t> freeSlots_;
    std::size_t activeCount_ = 0;
    std::uint64_t totalSpawned_ = 0;
    std::uint64_t totalDestroyed_ = 0;
    std::uint64_t totalExpired_ = 0;
    std::uint64_t rejectedSpawns_ = 0;
};

struct ParticleLifetimeStyle final {
    bool colorEnabled = false;
    Color endColor{};
    bool sizeEnabled = false;
    float endSize = 0.0F;
    bool rotationEnabled = false;
    float endRotationDegrees = 0.0F;
};

struct ParticleEmitterSettings final {
    ParticleSpawn particle;
    float spawnRate = 0.0F;
    std::size_t maximumAlive = std::numeric_limits<std::size_t>::max();
    ParticleLifetimeStyle overLifetime;
    bool cullOutsideBounds = false;
    Rect cullingBounds{};
};

struct ParticleEmitterStats final {
    std::size_t activeParticles = 0;
    std::uint64_t emitted = 0;
    std::uint64_t culled = 0;
    std::uint64_t rejected = 0;
};

// A deterministic emitter layered on the fixed-capacity ParticleSystem pool.
// update() advances the backing pool once, applies lifetime channels, culls,
// and then emits from the accumulated spawn rate. Applications sharing one
// pool between emitters should advance only one emitter per frame and use
// burst() for the others.
class ParticleEmitter final {
  public:
    explicit ParticleEmitter(ParticleSystem& system, ParticleEmitterSettings settings = {});
    ~ParticleEmitter();

    ParticleEmitter(const ParticleEmitter&) = delete;
    ParticleEmitter& operator=(const ParticleEmitter&) = delete;

    [[nodiscard]] Result<void> setSettings(ParticleEmitterSettings settings);
    [[nodiscard]] const ParticleEmitterSettings& settings() const noexcept {
        return settings_;
    }
    void setPosition(Vec2 position) noexcept {
        position_ = position;
    }
    [[nodiscard]] Vec2 position() const noexcept {
        return position_;
    }
    void setEmitting(bool emitting) noexcept {
        emitting_ = emitting;
    }
    [[nodiscard]] bool emitting() const noexcept {
        return emitting_;
    }

    [[nodiscard]] Result<std::size_t> update(float deltaSeconds);
    [[nodiscard]] Result<std::size_t> burst(std::size_t count);
    void clear() noexcept;
    [[nodiscard]] ParticleEmitterStats stats() const noexcept;

  private:
    struct OwnedParticle final {
        ParticleHandle handle;
        Color startColor{};
        float startSize = 1.0F;
        float startRotationDegrees = 0.0F;
    };

    [[nodiscard]] static Result<void> validate(const ParticleEmitterSettings& settings);
    [[nodiscard]] std::size_t emit(std::size_t count);
    void refreshOwnedParticles() noexcept;

    ParticleSystem* system_ = nullptr;
    ParticleEmitterSettings settings_;
    Vec2 position_{};
    double spawnAccumulator_ = 0.0;
    bool emitting_ = true;
    std::vector<OwnedParticle> owned_;
    std::uint64_t emitted_ = 0;
    std::uint64_t culled_ = 0;
    std::uint64_t rejected_ = 0;
};

} // namespace fabgl
