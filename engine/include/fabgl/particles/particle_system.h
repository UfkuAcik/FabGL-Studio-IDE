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
};

struct Particle final {
    Vec2 position{};
    Vec2 velocity{};
    Vec2 acceleration{};
    Color color{};
    float size = 1.0F;
    float ageSeconds = 0.0F;
    float lifetimeSeconds = 1.0F;

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

} // namespace fabgl
