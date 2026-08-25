#include <fabgl/frameworks/fps.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <string>

namespace fabgl::frameworks {

namespace {

constexpr std::size_t MaximumFpsGridCells = 256U * 256U;
constexpr std::size_t MaximumSaveKeys = 64U;

Vec2 normalized(const Vec2 value) noexcept {
    const auto length = std::sqrt(value.x * value.x + value.y * value.y);
    return std::isfinite(length) && length > 0.00001F ? Vec2{value.x / length, value.y / length}
                                                      : Vec2{};
}

bool finiteRect(const Rect value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.width) &&
           std::isfinite(value.height) && value.width >= 0.0F && value.height >= 0.0F;
}

bool validSave(const FpsSaveState& state) noexcept {
    if (!std::isfinite(state.player.position.x) || !std::isfinite(state.player.position.y) ||
        !std::isfinite(state.player.yawRadians) || !std::isfinite(state.player.pitch) ||
        state.vitality.health < 0 || state.vitality.health > 100000 || state.vitality.armor < 0 ||
        state.vitality.armor > 100000 || state.ammunition < 0 || state.ammunition > 1000000 ||
        state.reserveAmmunition < 0 || state.reserveAmmunition > 1000000 ||
        state.keys.size() > MaximumSaveKeys) {
        return false;
    }
    std::set<std::uint16_t> unique;
    return std::all_of(state.keys.begin(), state.keys.end(),
                       [&](const auto key) { return key != 0U && unique.insert(key).second; });
}

} // namespace

int HealthArmor::applyDamage(int amount) noexcept {
    amount = std::max(0, amount);
    const auto absorbed = std::min(std::max(0, armor), amount / 2);
    armor = std::max(0, armor - absorbed);
    const auto healthDamage = amount - absorbed;
    health = std::max(0, health - healthDamage);
    return healthDamage;
}

void DoorState::activate() noexcept {
    if (phase == DoorPhase::Closed || phase == DoorPhase::Closing) {
        phase = DoorPhase::Opening;
    } else if (phase == DoorPhase::Open) {
        holdRemaining = holdSeconds;
    }
}

void DoorState::update(const float deltaSeconds) noexcept {
    if (!std::isfinite(deltaSeconds) || !std::isfinite(speed) || !std::isfinite(holdSeconds)) {
        return;
    }
    const auto delta = std::clamp(deltaSeconds, 0.0F, 0.1F);
    switch (phase) {
    case DoorPhase::Closed:
        openness = 0.0F;
        break;
    case DoorPhase::Opening:
        openness = std::min(1.0F, openness + std::max(0.0F, speed) * delta);
        if (openness >= 1.0F) {
            phase = DoorPhase::Open;
            holdRemaining = std::max(0.0F, holdSeconds);
        }
        break;
    case DoorPhase::Open:
        holdRemaining -= delta;
        if (holdRemaining <= 0.0F) {
            phase = DoorPhase::Closing;
        }
        break;
    case DoorPhase::Closing:
        openness = std::max(0.0F, openness - std::max(0.0F, speed) * delta);
        if (openness <= 0.0F) {
            phase = DoorPhase::Closed;
        }
        break;
    }
}

HitscanResult hitscan(const Vec2 origin, Vec2 direction, const float maximumDistance,
                      const std::vector<HitscanTarget>& targets) noexcept {
    direction = normalized(direction);
    if ((direction.x == 0.0F && direction.y == 0.0F) || !std::isfinite(maximumDistance) ||
        maximumDistance <= 0.0F) {
        return {};
    }
    HitscanResult result;
    result.distance = std::numeric_limits<float>::infinity();
    for (std::size_t index = 0; index < targets.size(); ++index) {
        if (!targets[index].active || !std::isfinite(targets[index].radius) ||
            targets[index].radius <= 0.0F) {
            continue;
        }
        const auto relative = targets[index].position - origin;
        const auto projection = relative.x * direction.x + relative.y * direction.y;
        if (projection < 0.0F || projection > maximumDistance) {
            continue;
        }
        const auto closest = origin + direction * projection;
        const auto difference = targets[index].position - closest;
        if (difference.x * difference.x + difference.y * difference.y <=
                targets[index].radius * targets[index].radius &&
            projection < result.distance) {
            result = {true, index, projection};
        }
    }
    if (!result.hit) {
        result.distance = 0.0F;
    }
    return result;
}

bool FpsGrid::valid() const noexcept {
    if (width <= 0 || height <= 0 || width > 256 || height > 256) {
        return false;
    }
    const auto area = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    return area <= MaximumFpsGridCells && cells.size() == area;
}

bool FpsGrid::blocked(const Vec2 position, const float radius) const noexcept {
    if (!valid() || !std::isfinite(position.x) || !std::isfinite(position.y) ||
        !std::isfinite(radius) || radius < 0.0F) {
        return true;
    }
    const auto blockedCell = [&](const float x, const float y) {
        const auto cellX = static_cast<int>(std::floor(x));
        const auto cellY = static_cast<int>(std::floor(y));
        if (cellX < 0 || cellY < 0 || cellX >= width || cellY >= height) {
            return true;
        }
        return cells[static_cast<std::size_t>(cellY) * static_cast<std::size_t>(width) +
                     static_cast<std::size_t>(cellX)] != 0U;
    };
    return blockedCell(position.x - radius, position.y - radius) ||
           blockedCell(position.x + radius, position.y - radius) ||
           blockedCell(position.x - radius, position.y + radius) ||
           blockedCell(position.x + radius, position.y + radius);
}

void updateFirstPerson(FirstPersonState& state, const FirstPersonInput& input,
                       const float movementSpeed, const float lookSensitivity,
                       const float maximumPitch, const float bodyRadius, const float deltaSeconds,
                       const FpsGrid& grid) noexcept {
    if (!grid.valid() || !std::isfinite(state.position.x) || !std::isfinite(state.position.y) ||
        !std::isfinite(state.yawRadians) || !std::isfinite(state.pitch) ||
        !std::isfinite(input.forward) || !std::isfinite(input.strafe) ||
        !std::isfinite(input.mouseYaw) || !std::isfinite(input.mousePitch) ||
        !std::isfinite(deltaSeconds) || !std::isfinite(movementSpeed) ||
        !std::isfinite(lookSensitivity) || !std::isfinite(maximumPitch) ||
        !std::isfinite(bodyRadius)) {
        return;
    }
    const auto delta = std::clamp(deltaSeconds, 0.0F, 0.1F);
    state.yawRadians =
        std::remainder(state.yawRadians + input.mouseYaw * lookSensitivity, 6.283185307179586F);
    state.pitch = std::clamp(state.pitch + input.mousePitch * lookSensitivity,
                             -std::fabs(maximumPitch), std::fabs(maximumPitch));
    auto movement =
        Vec2{std::clamp(input.strafe, -1.0F, 1.0F), std::clamp(input.forward, -1.0F, 1.0F)};
    const auto length = std::sqrt(movement.x * movement.x + movement.y * movement.y);
    if (length > 1.0F) {
        movement = movement * (1.0F / length);
    }
    const auto cosine = std::cos(state.yawRadians);
    const auto sine = std::sin(state.yawRadians);
    const auto displacement =
        Vec2{(movement.y * cosine - movement.x * sine) * std::max(0.0F, movementSpeed) * delta,
             (movement.y * sine + movement.x * cosine) * std::max(0.0F, movementSpeed) * delta};
    const auto xCandidate = Vec2{state.position.x + displacement.x, state.position.y};
    if (!grid.blocked(xCandidate, std::max(0.0F, bodyRadius))) {
        state.position.x = xCandidate.x;
    }
    const auto yCandidate = Vec2{state.position.x, state.position.y + displacement.y};
    if (!grid.blocked(yCandidate, std::max(0.0F, bodyRadius))) {
        state.position.y = yCandidate.y;
    }
}

void FpsWeapon::update(const float deltaSeconds) noexcept {
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F) {
        return;
    }
    const auto delta = std::clamp(deltaSeconds, 0.0F, 0.1F);
    cooldown = std::max(0.0F, cooldown - delta);
    if (reloadRemaining <= 0.0F) {
        return;
    }
    reloadRemaining = std::max(0.0F, reloadRemaining - delta);
    if (reloadRemaining == 0.0F) {
        magazineSize = std::max(0, magazineSize);
        ammunition = std::clamp(ammunition, 0, magazineSize);
        reserveAmmunition = std::max(0, reserveAmmunition);
        const auto transfer = std::min(magazineSize - ammunition, reserveAmmunition);
        ammunition += transfer;
        reserveAmmunition -= transfer;
    }
}

bool FpsWeapon::tryFire() noexcept {
    if (reloading() || cooldown > 0.0F || ammunition <= 0 || !std::isfinite(fireInterval) ||
        fireInterval < 0.0F) {
        return false;
    }
    --ammunition;
    cooldown = fireInterval;
    return true;
}

bool FpsWeapon::beginReload() noexcept {
    if (reloading() || ammunition >= std::max(0, magazineSize) || reserveAmmunition <= 0 ||
        !std::isfinite(reloadSeconds) || reloadSeconds < 0.0F) {
        return false;
    }
    reloadRemaining = reloadSeconds;
    if (reloadRemaining == 0.0F) {
        // Finish an explicitly instantaneous reload through the same bounded transfer path.
        reloadRemaining = std::numeric_limits<float>::min();
        update(std::numeric_limits<float>::min());
    }
    return true;
}

FpsProjectilePool::FpsProjectilePool(const std::size_t capacity)
    : projectiles_(std::min<std::size_t>(capacity, 4096U)) {}

bool FpsProjectilePool::spawn(const Vec2 position, Vec2 direction, const float speed,
                              const float lifetime, const int damage) noexcept {
    direction = normalized(direction);
    if ((direction.x == 0.0F && direction.y == 0.0F) || !std::isfinite(speed) || speed < 0.0F ||
        !std::isfinite(lifetime) || lifetime <= 0.0F || damage < 0) {
        return false;
    }
    const auto available = std::find_if(projectiles_.begin(), projectiles_.end(),
                                        [](const auto& item) { return !item.active; });
    if (available == projectiles_.end()) {
        return false;
    }
    *available = {position, direction * speed, lifetime, damage, true};
    return true;
}

void FpsProjectilePool::update(const float deltaSeconds, const FpsGrid& grid) noexcept {
    const auto delta = std::clamp(deltaSeconds, 0.0F, 0.1F);
    for (auto& projectileItem : projectiles_) {
        if (!projectileItem.active) {
            continue;
        }
        const auto next = projectileItem.position + projectileItem.velocity * delta;
        projectileItem.lifetime -= delta;
        if (projectileItem.lifetime <= 0.0F || grid.blocked(next, 0.05F)) {
            projectileItem.active = false;
        } else {
            projectileItem.position = next;
        }
    }
}

FpsKeyRing::FpsKeyRing(const std::size_t capacity) noexcept
    : capacity_(std::clamp<std::size_t>(capacity, 1U, MaximumSaveKeys)) {
    keys_.reserve(capacity_);
}

bool FpsKeyRing::grant(const std::uint16_t keyId) noexcept {
    if (keyId == 0U || has(keyId) || keys_.size() >= capacity_) {
        return false;
    }
    keys_.push_back(keyId);
    return true;
}

bool FpsKeyRing::has(const std::uint16_t keyId) const noexcept {
    return std::find(keys_.begin(), keys_.end(), keyId) != keys_.end();
}

bool FpsKeyRing::consume(const std::uint16_t keyId) noexcept {
    const auto found = std::find(keys_.begin(), keys_.end(), keyId);
    if (found == keys_.end()) {
        return false;
    }
    keys_.erase(found);
    return true;
}

bool LockedDoorState::activate(FpsKeyRing& keys) noexcept {
    if (requiredKey != 0U && !keys.has(requiredKey)) {
        return false;
    }
    if (requiredKey != 0U && consumeKey && !keys.consume(requiredKey)) {
        return false;
    }
    door.activate();
    return true;
}

void FpsEnemy::chase(const Vec2 target, const float deltaSeconds, const FpsGrid& grid) noexcept {
    if (!active || !vitality.alive()) {
        return;
    }
    const auto offset = target - position;
    const auto distance = std::sqrt(offset.x * offset.x + offset.y * offset.y);
    if (!std::isfinite(distance) || distance <= std::max(0.0F, stopDistance)) {
        return;
    }
    const auto displacement =
        offset * (std::max(0.0F, speed) / distance * std::clamp(deltaSeconds, 0.0F, 0.1F));
    const auto candidate = position + displacement;
    if (!grid.blocked(candidate, 0.2F)) {
        position = candidate;
    }
}

bool applyFpsPickup(FpsPickup& pickup, const Vec2 playerPosition, HealthArmor& health,
                    FpsWeapon& weapon, FpsKeyRing& keys) noexcept {
    const auto offset = pickup.position - playerPosition;
    if (!pickup.active || !std::isfinite(pickup.radius) || pickup.radius < 0.0F ||
        offset.x * offset.x + offset.y * offset.y > pickup.radius * pickup.radius ||
        pickup.amount <= 0) {
        return false;
    }
    health.health = std::clamp(health.health, 0, 100);
    health.armor = std::clamp(health.armor, 0, 100);
    bool applied = false;
    switch (pickup.kind) {
    case FpsPickupKind::Health:
        if (health.health < 100) {
            health.health += std::min(100 - health.health, pickup.amount);
            applied = true;
        }
        break;
    case FpsPickupKind::Armor:
        if (health.armor < 100) {
            health.armor += std::min(100 - health.armor, pickup.amount);
            applied = true;
        }
        break;
    case FpsPickupKind::Ammunition:
        if (pickup.amount <= std::numeric_limits<int>::max() - weapon.reserveAmmunition) {
            weapon.reserveAmmunition += pickup.amount;
            applied = true;
        }
        break;
    case FpsPickupKind::Key:
        applied = keys.grant(pickup.keyId);
        break;
    }
    if (applied) {
        pickup.active = false;
    }
    return applied;
}

FpsTriggerResult activateFpsTriggers(const Vec2 playerPosition,
                                     std::vector<FpsTrigger>& triggers) noexcept {
    FpsTriggerResult result;
    for (auto& trigger : triggers) {
        if (!trigger.active || !finiteRect(trigger.bounds) ||
            !trigger.bounds.contains(playerPosition)) {
            continue;
        }
        ++result.activated;
        result.levelExit = result.levelExit || trigger.kind == FpsTriggerKind::LevelExit;
        result.secretArea = result.secretArea || trigger.kind == FpsTriggerKind::SecretArea;
        if (trigger.oneShot) {
            trigger.active = false;
        }
    }
    return result;
}

FpsHudData makeFpsHud(const HealthArmor& health, const FpsWeapon& weapon,
                      const FpsKeyRing& keys) noexcept {
    return {std::max(0, health.health),
            std::max(0, health.armor),
            std::max(0, weapon.ammunition),
            std::max(0, weapon.reserveAmmunition),
            static_cast<std::uint32_t>(keys.keys().size()),
            weapon.reloading()};
}

Result<std::string> serializeFpsSave(const FpsSaveState& state) {
    if (!validSave(state)) {
        return Result<std::string>::failure(
            Error(ErrorCode::InvalidArgument, "FPS save contains invalid or unbounded data"));
    }
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(9) << "fglfpssave 1\nplayer " << state.player.position.x << ' '
           << state.player.position.y << ' ' << state.player.yawRadians << ' ' << state.player.pitch
           << "\nvitality " << state.vitality.health << ' ' << state.vitality.armor << "\nammo "
           << state.ammunition << ' ' << state.reserveAmmunition << "\nlevel " << state.level
           << "\nsecrets " << state.secretsFound << "\nkeys " << state.keys.size();
    for (const auto key : state.keys) {
        output << ' ' << key;
    }
    output << "\nend\n";
    return Result<std::string>::success(output.str());
}

Result<FpsSaveState> deserializeFpsSave(const std::string_view source,
                                        const std::size_t maximumBytes) {
    if (source.empty() || source.size() > std::min<std::size_t>(maximumBytes, 1024U * 1024U)) {
        return Result<FpsSaveState>::failure(
            Error(ErrorCode::CapacityExceeded, "FPS save exceeds the configured byte bound"));
    }
    std::istringstream input{std::string(source)};
    input.imbue(std::locale::classic());
    std::string token;
    int version = 0;
    FpsSaveState state;
    std::size_t keyCount = 0U;
    if (!(input >> token >> version) || token != "fglfpssave" || version != 1 ||
        !(input >> token) || token != "player" ||
        !(input >> state.player.position.x >> state.player.position.y >> state.player.yawRadians >>
          state.player.pitch) ||
        !(input >> token) || token != "vitality" ||
        !(input >> state.vitality.health >> state.vitality.armor) || !(input >> token) ||
        token != "ammo" || !(input >> state.ammunition >> state.reserveAmmunition) ||
        !(input >> token) || token != "level" || !(input >> state.level) || !(input >> token) ||
        token != "secrets" || !(input >> state.secretsFound) || !(input >> token) ||
        token != "keys" || !(input >> keyCount) || keyCount > MaximumSaveKeys) {
        return Result<FpsSaveState>::failure(
            Error(ErrorCode::InvalidFormat, "FPS save structure is invalid"));
    }
    state.keys.resize(keyCount);
    for (auto& key : state.keys) {
        unsigned int parsed = 0U;
        if (!(input >> parsed) || parsed == 0U ||
            parsed > std::numeric_limits<std::uint16_t>::max()) {
            return Result<FpsSaveState>::failure(
                Error(ErrorCode::InvalidFormat, "FPS save contains an invalid key id"));
        }
        key = static_cast<std::uint16_t>(parsed);
    }
    if (!(input >> token) || token != "end" || (input >> token) || !validSave(state)) {
        return Result<FpsSaveState>::failure(
            Error(ErrorCode::InvalidFormat, "FPS save contains trailing or invalid data"));
    }
    return Result<FpsSaveState>::success(std::move(state));
}

} // namespace fabgl::frameworks
