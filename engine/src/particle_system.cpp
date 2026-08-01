#include "fabgl/particles/particle_system.h"

#include <algorithm>
#include <cmath>

namespace fabgl {
namespace {

[[nodiscard]] bool finite(Vec2 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

} // namespace

float Particle::normalizedAge() const noexcept {
    if (lifetimeSeconds <= 0.0F) {
        return 1.0F;
    }
    return std::clamp(ageSeconds / lifetimeSeconds, 0.0F, 1.0F);
}

ParticleSystem::ParticleSystem(std::size_t capacity) : slots_(capacity) {
    freeSlots_.reserve(capacity);
    for (std::size_t index = capacity; index > 0; --index) {
        freeSlots_.push_back(index - 1U);
    }
}

Result<ParticleHandle> ParticleSystem::spawn(const ParticleSpawn& initialState) {
    if (!finite(initialState.position) || !finite(initialState.velocity) ||
        !finite(initialState.acceleration) || !std::isfinite(initialState.size) ||
        initialState.size < 0.0F || !std::isfinite(initialState.lifetimeSeconds) ||
        initialState.lifetimeSeconds <= 0.0F) {
        return Result<ParticleHandle>::failure(
            Error(ErrorCode::InvalidArgument, "particle initial state contains an invalid value"));
    }
    if (freeSlots_.empty()) {
        ++rejectedSpawns_;
        return Result<ParticleHandle>::failure(
            Error(ErrorCode::CapacityExceeded, "particle pool capacity has been reached"));
    }

    const auto slotIndex = freeSlots_.back();
    freeSlots_.pop_back();
    auto& slot = slots_[slotIndex];
    slot.particle = {
        initialState.position,        initialState.velocity, initialState.acceleration,
        initialState.color,           initialState.size,     0.0F,
        initialState.lifetimeSeconds,
    };
    slot.active = true;
    ++activeCount_;
    ++totalSpawned_;
    return Result<ParticleHandle>::success({slotIndex, slot.generation});
}

bool ParticleSystem::destroy(ParticleHandle handle) noexcept {
    if (get(handle) == nullptr) {
        return false;
    }
    releaseSlot(handle.index, false);
    return true;
}

Result<void> ParticleSystem::update(float deltaSeconds) {
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0F) {
        return Result<void>::failure(Error(ErrorCode::InvalidArgument,
                                           "particle delta time must be finite and non-negative"));
    }
    const float halfDeltaSquared = 0.5F * deltaSeconds * deltaSeconds;
    for (std::size_t index = 0; index < slots_.size(); ++index) {
        auto& slot = slots_[index];
        if (!slot.active) {
            continue;
        }

        auto& particle = slot.particle;
        particle.position = particle.position + particle.velocity * deltaSeconds +
                            particle.acceleration * halfDeltaSquared;
        particle.velocity = particle.velocity + particle.acceleration * deltaSeconds;
        particle.ageSeconds += deltaSeconds;
        if (particle.ageSeconds >= particle.lifetimeSeconds) {
            releaseSlot(index, true);
        }
    }
    return Result<void>::success();
}

void ParticleSystem::clear() noexcept {
    freeSlots_.clear();
    for (std::size_t index = slots_.size(); index > 0; --index) {
        auto& slot = slots_[index - 1U];
        if (slot.active) {
            slot.active = false;
            advanceGeneration(slot);
            ++totalDestroyed_;
        }
        freeSlots_.push_back(index - 1U);
    }
    activeCount_ = 0;
}

Particle* ParticleSystem::get(ParticleHandle handle) noexcept {
    if (!handle.valid() || handle.index >= slots_.size()) {
        return nullptr;
    }
    auto& slot = slots_[handle.index];
    return slot.active && slot.generation == handle.generation ? &slot.particle : nullptr;
}

const Particle* ParticleSystem::get(ParticleHandle handle) const noexcept {
    if (!handle.valid() || handle.index >= slots_.size()) {
        return nullptr;
    }
    const auto& slot = slots_[handle.index];
    return slot.active && slot.generation == handle.generation ? &slot.particle : nullptr;
}

Particle* ParticleSystem::particleAtSlot(std::size_t slotIndex) noexcept {
    if (slotIndex >= slots_.size() || !slots_[slotIndex].active) {
        return nullptr;
    }
    return &slots_[slotIndex].particle;
}

const Particle* ParticleSystem::particleAtSlot(std::size_t slotIndex) const noexcept {
    if (slotIndex >= slots_.size() || !slots_[slotIndex].active) {
        return nullptr;
    }
    return &slots_[slotIndex].particle;
}

ParticleHandle ParticleSystem::handleAtSlot(std::size_t slotIndex) const noexcept {
    if (slotIndex >= slots_.size() || !slots_[slotIndex].active) {
        return {};
    }
    return {slotIndex, slots_[slotIndex].generation};
}

ParticleSystemStats ParticleSystem::stats() const noexcept {
    return {
        activeCount_, slots_.size(), totalSpawned_, totalDestroyed_, totalExpired_, rejectedSpawns_,
    };
}

void ParticleSystem::releaseSlot(std::size_t slotIndex, bool expired) noexcept {
    auto& slot = slots_[slotIndex];
    if (!slot.active) {
        return;
    }
    slot.active = false;
    advanceGeneration(slot);
    freeSlots_.push_back(slotIndex);
    --activeCount_;
    ++totalDestroyed_;
    if (expired) {
        ++totalExpired_;
    }
}

void ParticleSystem::advanceGeneration(Slot& slot) noexcept {
    ++slot.generation;
    if (slot.generation == 0) {
        slot.generation = 1;
    }
}

} // namespace fabgl
