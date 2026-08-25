#include "fabgl/particles/particle_system.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace fabgl {
namespace {

[[nodiscard]] bool finite(Vec2 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] std::uint8_t interpolateChannel(std::uint8_t start, std::uint8_t end,
                                              float amount) noexcept {
    const auto value = static_cast<float>(start) +
                       (static_cast<float>(end) - static_cast<float>(start)) * amount;
    return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

[[nodiscard]] Color interpolateColor(Color start, Color end, float amount) noexcept {
    return {
        interpolateChannel(start.r, end.r, amount),
        interpolateChannel(start.g, end.g, amount),
        interpolateChannel(start.b, end.b, amount),
        interpolateChannel(start.a, end.a, amount),
    };
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
        initialState.lifetimeSeconds <= 0.0F || !std::isfinite(initialState.rotationDegrees) ||
        !std::isfinite(initialState.angularVelocityDegrees)) {
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
    slot.particle.rotationDegrees = initialState.rotationDegrees;
    slot.particle.angularVelocityDegrees = initialState.angularVelocityDegrees;
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
        particle.rotationDegrees += particle.angularVelocityDegrees * deltaSeconds;
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

ParticleEmitter::ParticleEmitter(ParticleSystem& system, ParticleEmitterSettings settings)
    : system_(&system), settings_(std::move(settings)) {}

ParticleEmitter::~ParticleEmitter() {
    clear();
}

Result<void> ParticleEmitter::validate(const ParticleEmitterSettings& settings) {
    const auto& particle = settings.particle;
    const auto& bounds = settings.cullingBounds;
    if (!finite(particle.position) || !finite(particle.velocity) || !finite(particle.acceleration) ||
        !std::isfinite(particle.size) || particle.size < 0.0F ||
        !std::isfinite(particle.lifetimeSeconds) || particle.lifetimeSeconds <= 0.0F ||
        !std::isfinite(particle.rotationDegrees) ||
        !std::isfinite(particle.angularVelocityDegrees) || !std::isfinite(settings.spawnRate) ||
        settings.spawnRate < 0.0F || settings.maximumAlive == 0U ||
        !std::isfinite(settings.overLifetime.endSize) || settings.overLifetime.endSize < 0.0F ||
        !std::isfinite(settings.overLifetime.endRotationDegrees) || !std::isfinite(bounds.x) ||
        !std::isfinite(bounds.y) || !std::isfinite(bounds.width) ||
        !std::isfinite(bounds.height) || bounds.width < 0.0F || bounds.height < 0.0F) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "particle emitter settings are invalid"));
    }
    return Result<void>::success();
}

Result<void> ParticleEmitter::setSettings(ParticleEmitterSettings settings) {
    auto valid = validate(settings);
    if (!valid)
        return valid;
    settings_ = std::move(settings);
    if (owned_.size() > settings_.maximumAlive) {
        for (std::size_t index = settings_.maximumAlive; index < owned_.size(); ++index)
            static_cast<void>(system_->destroy(owned_[index].handle));
        owned_.resize(settings_.maximumAlive);
    }
    return Result<void>::success();
}

std::size_t ParticleEmitter::emit(std::size_t count) {
    const auto available = settings_.maximumAlive > owned_.size()
                               ? settings_.maximumAlive - owned_.size()
                               : std::size_t{0};
    const auto requested = count;
    count = std::min(count, available);
    std::size_t spawned = 0;
    for (; spawned < count; ++spawned) {
        auto initial = settings_.particle;
        initial.position = initial.position + position_;
        auto result = system_->spawn(initial);
        if (!result)
            break;
        owned_.push_back(
            {result.value(), initial.color, initial.size, initial.rotationDegrees});
    }
    emitted_ += spawned;
    rejected_ += requested - spawned;
    return spawned;
}

void ParticleEmitter::refreshOwnedParticles() noexcept {
    std::size_t output = 0;
    for (std::size_t index = 0; index < owned_.size(); ++index) {
        auto owned = owned_[index];
        auto* particle = system_->get(owned.handle);
        if (particle == nullptr)
            continue;
        if (settings_.cullOutsideBounds && !settings_.cullingBounds.contains(particle->position)) {
            static_cast<void>(system_->destroy(owned.handle));
            ++culled_;
            continue;
        }

        const auto amount = particle->normalizedAge();
        if (settings_.overLifetime.colorEnabled) {
            particle->color =
                interpolateColor(owned.startColor, settings_.overLifetime.endColor, amount);
        }
        if (settings_.overLifetime.sizeEnabled) {
            particle->size = owned.startSize +
                             (settings_.overLifetime.endSize - owned.startSize) * amount;
        }
        if (settings_.overLifetime.rotationEnabled) {
            particle->rotationDegrees =
                owned.startRotationDegrees +
                (settings_.overLifetime.endRotationDegrees - owned.startRotationDegrees) * amount;
        }
        owned_[output++] = owned;
    }
    owned_.resize(output);
}

Result<std::size_t> ParticleEmitter::update(float deltaSeconds) {
    auto valid = validate(settings_);
    if (!valid)
        return Result<std::size_t>::failure(valid.error());
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0F) {
        return Result<std::size_t>::failure(
            Error(ErrorCode::InvalidArgument, "particle emitter delta is invalid"));
    }

    auto advanced = system_->update(deltaSeconds);
    if (!advanced)
        return Result<std::size_t>::failure(advanced.error());
    refreshOwnedParticles();
    if (!emitting_ || settings_.spawnRate == 0.0F)
        return Result<std::size_t>::success(0U);

    spawnAccumulator_ += static_cast<double>(settings_.spawnRate) *
                         static_cast<double>(deltaSeconds);
    const auto whole = std::floor(spawnAccumulator_);
    const auto maximum = static_cast<double>(std::numeric_limits<std::size_t>::max());
    if (!std::isfinite(whole) || whole > maximum) {
        spawnAccumulator_ = 0.0;
        return Result<std::size_t>::failure(
            Error(ErrorCode::CapacityExceeded, "particle emitter spawn count overflowed"));
    }
    const auto count = static_cast<std::size_t>(whole);
    spawnAccumulator_ -= whole;
    return Result<std::size_t>::success(emit(count));
}

Result<std::size_t> ParticleEmitter::burst(std::size_t count) {
    auto valid = validate(settings_);
    if (!valid)
        return Result<std::size_t>::failure(valid.error());
    refreshOwnedParticles();
    return Result<std::size_t>::success(emit(count));
}

void ParticleEmitter::clear() noexcept {
    if (system_ != nullptr) {
        for (const auto& particle : owned_)
            static_cast<void>(system_->destroy(particle.handle));
    }
    owned_.clear();
    spawnAccumulator_ = 0.0;
}

ParticleEmitterStats ParticleEmitter::stats() const noexcept {
    return {owned_.size(), emitted_, culled_, rejected_};
}

} // namespace fabgl
