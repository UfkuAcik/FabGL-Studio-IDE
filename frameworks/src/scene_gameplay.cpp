#include <fabgl/frameworks/scene_gameplay.h>

#include <fabgl/frameworks/fps.h>
#include <fabgl/input/input_map.h>
#include <fabgl/scene/builtin_components.h>
#include <fabgl/scene/entity.h>
#include <fabgl/scene/scene.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace fabgl::frameworks {
namespace {

[[nodiscard]] ComponentTypeGuid builtinTypeId(const std::string_view shortName) {
    return ComponentTypeGuid::fromStableName(std::string("fabgl.component.") +
                                              std::string(shortName) + ".v1");
}

[[nodiscard]] DataComponent* component(Entity& entity, const std::string_view shortName) noexcept {
    return dynamic_cast<DataComponent*>(entity.getComponent(builtinTypeId(shortName)));
}

template <typename Type>
[[nodiscard]] Type property(const DataComponent& value, const std::string_view name,
                            Type fallback) noexcept {
    auto read = value.get(name);
    if (!read)
        return fallback;
    const auto* typed = std::get_if<Type>(&read.value());
    return typed == nullptr ? fallback : *typed;
}

[[nodiscard]] bool enabled(const DataComponent* value) noexcept {
    return value != nullptr && value->activeAndEnabled() && property(*value, "enabled", true);
}

[[nodiscard]] float axis(const InputMap& input, const DataComponent& value,
                         const std::string_view propertyName, std::string fallback) {
    return input.axis(property(value, propertyName, std::move(fallback)));
}

[[nodiscard]] ActionState action(const InputMap& input, const DataComponent& value,
                                 const std::string_view propertyName, std::string fallback) {
    return input.action(property(value, propertyName, std::move(fallback)));
}

[[nodiscard]] bool nameContains(const Entity& entity, const std::string_view needle) {
    auto name = entity.name();
    std::transform(name.begin(), name.end(), name.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    auto expected = std::string(needle);
    std::transform(expected.begin(), expected.end(), expected.begin(),
                   [](const unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return name.find(expected) != std::string::npos;
}

[[nodiscard]] Rect entityBounds2D(Entity& entity, const Vec2 fallbackSize = {1.0F, 1.0F}) {
    const auto position = entity.transform().localPosition();
    const auto scale = entity.transform().localScale();
    auto size = fallbackSize;
    if (const auto* collider = component(entity, "Collider2D"); enabled(collider))
        size = property(*collider, "size", size);
    const auto width = std::max(0.01F, std::fabs(size.x * scale.x));
    const auto height = std::max(0.01F, std::fabs(size.y * scale.y));
    return {position.x - width * 0.5F, position.y - height * 0.5F, width, height};
}

[[nodiscard]] std::vector<Rect> collectPlatforms(Scene& scene, const EntityGuid controlled) {
    std::vector<Rect> result;
    for (auto* entity : scene.entities()) {
        if (!entity->active() || entity->id() == controlled)
            continue;
        auto* collider = component(*entity, "Collider2D");
        if (!enabled(collider) || property(*collider, "trigger", false) ||
            property(*collider, "shape", std::int64_t{0}) != 0) {
            continue;
        }
        result.push_back(entityBounds2D(*entity));
    }
    return result;
}

[[nodiscard]] Vec2 normalized(const Vec2 value, const Vec2 fallback = {1.0F, 0.0F}) noexcept {
    const auto magnitude = std::sqrt(value.x * value.x + value.y * value.y);
    return std::isfinite(magnitude) && magnitude > 0.00001F
               ? Vec2{value.x / magnitude, value.y / magnitude}
               : fallback;
}

[[nodiscard]] Vec3 normalized(const Vec3 value, const Vec3 fallback = {0.0F, 0.0F, 1.0F}) noexcept {
    const auto magnitude =
        std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    return std::isfinite(magnitude) && magnitude > 0.00001F
               ? value * (1.0F / magnitude)
               : fallback;
}

void writeHealth(Entity& entity, const int current, const int maximum) {
    auto* health = component(entity, "Health");
    if (!enabled(health))
        return;
    auto currentResult = health->set("current", std::int64_t{current});
    auto maximumResult = health->set("maximum", std::int64_t{maximum});
    static_cast<void>(currentResult);
    static_cast<void>(maximumResult);
}

} // namespace

struct SceneGameplayRuntime::State final {
    struct PlatformerBinding final {
        PlatformerState player;
        PlatformerController controller;
        PlatformerWorld world{128U};
        PlatformerWorldState worldState;
        PlatformerCamera camera;
        bool configured = false;
    };

    struct TopDownBinding final {
        TopDownState player;
        Weapon weapon;
        ProjectilePool projectiles{32U};
        TopDownHealth health;
        TopDownInventory inventory{16U};
        TopDownArena arena;
        std::vector<TopDownEnemy> enemies;
        std::vector<EntityGuid> enemyEntities;
        std::vector<TopDownPickup> pickups;
        std::vector<TopDownRoomTransition> transitions;
        std::optional<std::uint32_t> currentRoom;
        bool configured = false;
    };

    struct FpsBinding final {
        FirstPersonState player;
        FpsGrid grid;
        FpsWeapon weapon;
        FpsProjectilePool projectiles{32U};
        HealthArmor vitality;
        FpsKeyRing keys{16U};
        LockedDoorState door;
        std::vector<FpsEnemy> enemies;
        std::vector<EntityGuid> enemyEntities;
        std::vector<FpsPickup> pickups;
        std::vector<FpsTrigger> triggers;
        bool configured = false;
    };

    struct TpsBinding final {
        ThirdPersonState player;
        ThirdPersonCamera camera;
        ThirdPersonCharacterConfig config;
        TpsWeapon weapon;
        std::vector<TpsEnemy> enemies;
        std::vector<EntityGuid> enemyEntities;
        std::vector<TpsPickup> pickups;
        std::vector<Rect> obstacles;
        std::optional<std::size_t> selectedTarget;
        bool configured = false;
    };

    struct RacerBinding final {
        VehicleState player;
        VehicleConfig config;
        LapTracker laps{4U, 2};
        RaceCountdown countdown{1.0F};
        std::vector<OpponentDriverState> opponents{3U};
        float nextCheckpointDistance = 4.0F;
        float elapsedSeconds = 0.0F;
        std::uint32_t checkpointsPassed = 0U;
        bool configured = false;
    };

    std::map<EntityGuid, PlatformerBinding> platformers;
    std::map<EntityGuid, TopDownBinding> topDown;
    std::map<EntityGuid, FpsBinding> firstPerson;
    std::map<EntityGuid, TpsBinding> thirdPerson;
    std::map<EntityGuid, RacerBinding> vehicles;
};

SceneGameplayRuntime::SceneGameplayRuntime(Scene& scene, const SceneGameplayLimits limits)
    : scene_(&scene), limits_(limits), state_(std::make_unique<State>()) {}

SceneGameplayRuntime::~SceneGameplayRuntime() = default;

Result<void> SceneGameplayRuntime::initialize() {
    if (initialized_) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "scene gameplay runtime is already initialized"));
    }
    if (limits_.maximumControlledEntities == 0U || limits_.maximumControlledEntities > 4096U ||
        !std::isfinite(limits_.maximumDeltaSeconds) || limits_.maximumDeltaSeconds <= 0.0F ||
        limits_.maximumDeltaSeconds > 1.0F) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "scene gameplay limits are invalid"));
    }
    state_ = std::make_unique<State>();
    stats_ = {};
    initialized_ = true;
    return Result<void>::success();
}

Result<void> SceneGameplayRuntime::update(InputMap& input, const float deltaSeconds) {
    if (!initialized_) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "scene gameplay runtime is not initialized"));
    }
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0F) {
        return Result<void>::failure(Error(ErrorCode::InvalidArgument,
                                           "scene gameplay delta must be finite and non-negative"));
    }
    const auto delta = std::min(deltaSeconds, limits_.maximumDeltaSeconds);
    ++stats_.updates;
    std::size_t controlled = 0U;

    for (auto* entity : scene_->entities()) {
        if (!entity->active())
            continue;

        if (auto* character = component(*entity, "CharacterBody2D"); enabled(character)) {
            if (++controlled > limits_.maximumControlledEntities) {
                return Result<void>::failure(
                    Error(ErrorCode::CapacityExceeded, "scene has too many controlled entities"));
            }
            const auto mode = property(*character, "movementMode", std::int64_t{0});
            const auto moveX = axis(input, *character, "moveXAxis", "MoveX");
            const auto moveY = axis(input, *character, "moveYAxis", "MoveY");
            const auto primary = action(input, *character, "primaryAction", "Jump");
            const auto speed = static_cast<float>(property(*character, "moveSpeed", 4.0));

            if (mode == 0) {
                auto& binding = state_->platformers.try_emplace(entity->id()).first->second;
                if (!binding.configured) {
                    const auto position = entity->transform().localPosition();
                    binding.player.position = {position.x, position.y};
                    PlatformerConfig config;
                    config.moveSpeed = std::max(0.0F, speed);
                    config.acceleration = std::max(1.0F, config.moveSpeed * 10.0F);
                    binding.controller = PlatformerController(config);
                    if (auto* health = component(*entity, "Health"); enabled(health)) {
                        binding.worldState.maximumHealth = static_cast<int>(
                            property(*health, "maximum", std::int64_t{3}));
                        binding.worldState.health =
                            static_cast<int>(property(*health, "current", std::int64_t{3}));
                    }
                    binding.camera.viewport = {320.0F, 180.0F};
                    binding.camera.worldBounds = {0.0F, 0.0F, 640.0F, 360.0F};
                    std::uint32_t id = 1U;
                    for (auto* item : scene_->entities()) {
                        if (!item->active() || item->id() == entity->id())
                            continue;
                        const auto bounds = entityBounds2D(*item, {8.0F, 8.0F});
                        if (nameContains(*item, "movingplatform")) {
                            MovingPlatform platform;
                            platform.bounds = bounds;
                            platform.velocity = {12.0F, 0.0F};
                            platform.minimum = {bounds.x - 16.0F, bounds.y};
                            platform.maximum = {bounds.x + 16.0F, bounds.y};
                            static_cast<void>(binding.world.addMovingPlatform(platform));
                        } else if (nameContains(*item, "collectible")) {
                            static_cast<void>(binding.world.addCollectible({id++, bounds, 10, true}));
                        } else if (nameContains(*item, "enemy")) {
                            PlatformerEnemy enemy;
                            enemy.id = id++;
                            enemy.bounds = bounds;
                            enemy.patrolMinimumX = bounds.x - 10.0F;
                            enemy.patrolMaximumX = bounds.x + 10.0F;
                            enemy.speed = 8.0F;
                            static_cast<void>(binding.world.addEnemy(enemy));
                        } else if (nameContains(*item, "checkpoint")) {
                            static_cast<void>(binding.world.addCheckpoint(
                                {id++, bounds, {bounds.x, bounds.y - 12.0F}}));
                        } else if (nameContains(*item, "levelexit") ||
                                   nameContains(*item, "transition")) {
                            static_cast<void>(
                                binding.world.addTransition({id++, bounds, true}));
                        }
                    }
                    binding.configured = true;
                }

                const auto previousCollected = binding.worldState.collected;
                const auto previousHealth = binding.worldState.health;
                const auto previousCheckpoint = binding.worldState.checkpointId;
                const auto previousLevel = binding.worldState.requestedLevel;
                binding.world.update(binding.controller, binding.player, binding.worldState,
                                     binding.camera, {moveX, primary.pressed, primary.held}, delta,
                                     collectPlatforms(*scene_, entity->id()));
                stats_.platformerCollectibles +=
                    binding.worldState.collected - previousCollected;
                stats_.platformerDamageEvents +=
                    binding.worldState.health < previousHealth ? 1U : 0U;
                stats_.platformerCheckpoints +=
                    binding.worldState.checkpointId != previousCheckpoint &&
                            binding.worldState.checkpointId.has_value()
                        ? 1U
                        : 0U;
                stats_.platformerTransitions +=
                    binding.worldState.requestedLevel != previousLevel &&
                            binding.worldState.requestedLevel.has_value()
                        ? 1U
                        : 0U;
                stats_.platformerHealth = binding.world.hud(binding.worldState).health;
                auto position = entity->transform().localPosition();
                position.x = binding.player.position.x;
                position.y = binding.player.position.y;
                entity->transform().setLocalPosition(position);
                writeHealth(*entity, binding.worldState.health, binding.worldState.maximumHealth);
            } else if (mode == 1) {
                auto& binding = state_->topDown.try_emplace(entity->id()).first->second;
                if (!binding.configured) {
                    const auto position = entity->transform().localPosition();
                    binding.player.position = {position.x, position.y};
                    binding.weapon.automatic = false;
                    if (auto* health = component(*entity, "Health"); enabled(health)) {
                        binding.health.maximum =
                            static_cast<int>(property(*health, "maximum", std::int64_t{100}));
                        binding.health.current =
                            static_cast<int>(property(*health, "current", std::int64_t{100}));
                    }
                    binding.arena.bounds = {0.0F, 0.0F, 320.0F, 180.0F};
                    std::uint32_t id = 1U;
                    for (auto* item : scene_->entities()) {
                        if (!item->active() || item->id() == entity->id())
                            continue;
                        const auto itemPosition = item->transform().localPosition();
                        if (nameContains(*item, "enemy")) {
                            TopDownEnemy enemy;
                            enemy.position = {itemPosition.x, itemPosition.y};
                            enemy.speed = 14.0F;
                            enemy.health.maximum = 20;
                            enemy.health.current = 20;
                            binding.enemies.push_back(enemy);
                            binding.enemyEntities.push_back(item->id());
                        } else if (nameContains(*item, "pickup")) {
                            TopDownPickup pickup;
                            pickup.id = id++;
                            pickup.bounds = entityBounds2D(*item, {12.0F, 12.0F});
                            pickup.kind = nameContains(*item, "ammo")
                                              ? TopDownPickupKind::Ammunition
                                              : nameContains(*item, "health")
                                                    ? TopDownPickupKind::Health
                                                    : TopDownPickupKind::Inventory;
                            pickup.itemId = 7U;
                            pickup.amount = 5;
                            binding.pickups.push_back(pickup);
                        } else if (nameContains(*item, "arena")) {
                            binding.arena.bounds = entityBounds2D(*item, {320.0F, 180.0F});
                        } else if (nameContains(*item, "roomexit")) {
                            binding.transitions.push_back(
                                {id++, entityBounds2D(*item, {12.0F, 12.0F}), true});
                        }
                    }
                    binding.configured = true;
                }

                auto aim = binding.player.aim;
                for (const auto& enemy : binding.enemies) {
                    if (enemy.active) {
                        aim = normalized(enemy.position - binding.player.position, aim);
                        break;
                    }
                }
                updateTopDown(binding.player, {moveX, moveY}, aim, std::max(0.0F, speed), delta,
                              TopDownDirectionMode::EightWay);
                binding.player.position = binding.arena.constrain(binding.player.position,
                                                                  {8.0F, 8.0F});
                binding.weapon.update(delta);
                binding.projectiles.update(delta);
                if (binding.weapon.tryFire(primary.pressed, primary.held)) {
                    ++stats_.topDownShots;
                    static_cast<void>(binding.projectiles.spawn(binding.player.position,
                                                                binding.player.aim, 120.0F, 1.0F,
                                                                20));
                    std::vector<TopDownHitscanTarget> targets;
                    targets.reserve(binding.enemies.size());
                    for (const auto& enemy : binding.enemies)
                        targets.push_back({enemy.position, 6.0F, enemy.active});
                    const auto hit = topDownHitscan(binding.player.position, binding.player.aim,
                                                    200.0F, targets);
                    if (hit.hit && hit.targetIndex < binding.enemies.size()) {
                        auto& enemy = binding.enemies[hit.targetIndex];
                        static_cast<void>(enemy.health.damage(25));
                        enemy.active = enemy.health.alive();
                        ++stats_.topDownHits;
                    }
                }
                for (auto& enemy : binding.enemies)
                    if (enemy.active)
                        enemy.chase(binding.player.position, delta);
                for (auto& pickup : binding.pickups)
                    stats_.topDownPickups +=
                        applyTopDownPickup(pickup, binding.player.position, binding.health,
                                           binding.weapon, binding.inventory)
                            ? 1U
                            : 0U;
                const auto room = topDownRoomAt(binding.player.position, binding.transitions);
                if (room && room != binding.currentRoom) {
                    binding.currentRoom = room;
                    ++stats_.topDownRoomTransitions;
                }
                stats_.topDownEnemies = static_cast<std::uint32_t>(std::count_if(
                    binding.enemies.begin(), binding.enemies.end(),
                    [](const TopDownEnemy& enemy) { return enemy.active; }));
                for (std::size_t index = 0U; index < binding.enemies.size() &&
                                                   index < binding.enemyEntities.size();
                     ++index) {
                    if (auto* target = scene_->findEntity(binding.enemyEntities[index])) {
                        auto targetPosition = target->transform().localPosition();
                        targetPosition.x = binding.enemies[index].position.x;
                        targetPosition.y = binding.enemies[index].position.y;
                        target->transform().setLocalPosition(targetPosition);
                        target->setActive(binding.enemies[index].active);
                    }
                }
                auto position = entity->transform().localPosition();
                position.x = binding.player.position.x;
                position.y = binding.player.position.y;
                entity->transform().setLocalPosition(position);
                writeHealth(*entity, binding.health.current, binding.health.maximum);
            } else {
                return Result<void>::failure(
                    Error(ErrorCode::InvalidArgument, "CharacterBody2D movementMode is unsupported")
                        .addContext("entity", entity->id().toString()));
            }
        }

        if (auto* vehicle = component(*entity, "VehicleController"); enabled(vehicle)) {
            if (++controlled > limits_.maximumControlledEntities) {
                return Result<void>::failure(
                    Error(ErrorCode::CapacityExceeded, "scene has too many controlled entities"));
            }
            auto& binding = state_->vehicles.try_emplace(entity->id()).first->second;
            if (!binding.configured) {
                const auto position = entity->transform().localPosition();
                binding.player.lateral = position.x;
                binding.player.speed = std::max(0.0F, position.y);
                binding.player.distance = position.z;
                binding.config.acceleration = static_cast<float>(
                    std::max(0.0, property(*vehicle, "acceleration", 8.0)));
                for (std::size_t index = 0U; index < binding.opponents.size(); ++index) {
                    binding.opponents[index].vehicle.distance = static_cast<float>(index) * 2.0F;
                    binding.opponents[index].vehicle.lateral =
                        static_cast<float>(static_cast<int>(index) - 1) * 0.35F;
                }
                binding.configured = true;
            }
            binding.elapsedSeconds += delta;
            binding.countdown.update(delta);
            const auto steering = axis(input, *vehicle, "steerAxis", "Steer");
            const auto throttle = std::max(0.0F, axis(input, *vehicle, "throttleAxis", "Throttle"));
            const auto brake = action(input, *vehicle, "brakeAction", "Brake");
            const auto drift = action(input, *vehicle, "driftAction", "Handbrake");
            if (binding.countdown.complete()) {
                updateVehicle(binding.player, binding.config, throttle, brake.held ? 1.0F : 0.0F,
                              steering, drift.held, delta);
            }
            for (std::size_t index = 0U; index < binding.opponents.size(); ++index) {
                OpponentDriverConfig driver;
                driver.targetSpeed = 18.0F + static_cast<float>(index) * 2.0F;
                driver.skill = 0.45F + static_cast<float>(index) * 0.15F;
                OpponentPerception perception;
                perception.curveAhead = std::sin(binding.opponents[index].vehicle.distance * 0.05F);
                perception.slowerVehicleAhead = index > 0U;
                perception.slowerVehicleLateral =
                    binding.opponents[index == 0U ? 0U : index - 1U].vehicle.lateral;
                static_cast<void>(updateOpponentDriver(binding.opponents[index], binding.config,
                                                       driver, perception, delta));
            }
            while (binding.player.distance >= binding.nextCheckpointDistance &&
                   !binding.laps.finished()) {
                if (binding.laps.crossCheckpoint(binding.laps.nextCheckpoint())) {
                    ++binding.checkpointsPassed;
                    ++stats_.racerCheckpointCrossings;
                }
                binding.nextCheckpointDistance += 4.0F;
            }
            std::vector<RaceProgress> progress;
            progress.push_back({1U, static_cast<std::uint32_t>(binding.laps.completedLaps()),
                                binding.checkpointsPassed, std::fmod(binding.player.distance, 16.0F),
                                binding.laps.finished(), binding.elapsedSeconds});
            for (std::size_t index = 0U; index < binding.opponents.size(); ++index) {
                const auto distance = binding.opponents[index].vehicle.distance;
                progress.push_back(
                    {static_cast<std::uint16_t>(index + 2U),
                     static_cast<std::uint32_t>(std::max(0.0F, std::floor(distance / 16.0F))),
                     static_cast<std::uint32_t>(std::max(0.0F, std::floor(distance / 4.0F))),
                     std::fmod(std::max(0.0F, distance), 16.0F), false, 0.0F});
            }
            const auto ranked = rankRacePositions(progress);
            const auto playerRank =
                std::find_if(ranked.begin(), ranked.end(),
                             [](const RacePosition& value) { return value.participantId == 1U; });
            const auto position =
                playerRank == ranked.end() ? std::uint16_t{1U} : playerRank->position;
            const auto hud = makeRaceHud(binding.player, binding.laps, position,
                                         static_cast<std::uint16_t>(progress.size()),
                                         binding.countdown);
            stats_.racerOpponents = static_cast<std::uint32_t>(binding.opponents.size());
            stats_.racerPosition = hud.position;
            stats_.racerLap = hud.currentLap;
            stats_.racerGear = hud.gear;
            stats_.racerSpeedKph = hud.speedKph;
            stats_.racerCountdown = hud.countdownNumber;
            stats_.racerFinished = hud.finishVisible;
            entity->transform().setLocalPosition(
                {binding.player.lateral, binding.player.speed, binding.player.distance});
        }

        if (auto* controller = component(*entity, "FirstPersonController"); enabled(controller)) {
            if (++controlled > limits_.maximumControlledEntities) {
                return Result<void>::failure(
                    Error(ErrorCode::CapacityExceeded, "scene has too many controlled entities"));
            }
            auto& binding = state_->firstPerson.try_emplace(entity->id()).first->second;
            if (!binding.configured) {
                const auto position = entity->transform().localPosition();
                binding.player.position = {position.x, position.y};
                binding.grid.width = 16;
                binding.grid.height = 16;
                binding.grid.cells.assign(256U, 0U);
                for (int coordinate = 0; coordinate < 16; ++coordinate) {
                    binding.grid.cells[static_cast<std::size_t>(coordinate)] = 1U;
                    binding.grid.cells[240U + static_cast<std::size_t>(coordinate)] = 1U;
                    binding.grid.cells[static_cast<std::size_t>(coordinate) * 16U] = 1U;
                    binding.grid.cells[static_cast<std::size_t>(coordinate) * 16U + 15U] = 1U;
                }
                binding.door.requiredKey = 7U;
                std::uint32_t id = 1U;
                for (auto* item : scene_->entities()) {
                    if (!item->active() || item->id() == entity->id())
                        continue;
                    const auto itemPosition = item->transform().localPosition();
                    if (nameContains(*item, "enemy")) {
                        FpsEnemy enemy;
                        enemy.position = {itemPosition.x, itemPosition.y};
                        enemy.speed = 0.5F;
                        enemy.vitality.health = 20;
                        binding.enemies.push_back(enemy);
                        binding.enemyEntities.push_back(item->id());
                    } else if (nameContains(*item, "pickup")) {
                        FpsPickup pickup;
                        pickup.id = id++;
                        pickup.position = {itemPosition.x, itemPosition.y};
                        pickup.radius = 0.75F;
                        pickup.kind = nameContains(*item, "key")
                                          ? FpsPickupKind::Key
                                          : nameContains(*item, "ammo")
                                                ? FpsPickupKind::Ammunition
                                                : nameContains(*item, "armor")
                                                      ? FpsPickupKind::Armor
                                                      : FpsPickupKind::Health;
                        pickup.amount = 8;
                        pickup.keyId = 7U;
                        binding.pickups.push_back(pickup);
                    } else if (nameContains(*item, "secret")) {
                        binding.triggers.push_back(
                            {id++, entityBounds2D(*item, {1.0F, 1.0F}),
                             FpsTriggerKind::SecretArea, true, true});
                    } else if (nameContains(*item, "levelexit")) {
                        binding.triggers.push_back(
                            {id++, entityBounds2D(*item, {1.0F, 1.0F}),
                             FpsTriggerKind::LevelExit, true, true});
                    }
                }
                binding.configured = true;
            }
            for (auto& pickup : binding.pickups)
                stats_.fpsPickups += applyFpsPickup(pickup, binding.player.position,
                                                    binding.vitality, binding.weapon, binding.keys)
                                         ? 1U
                                         : 0U;
            const auto moveX = axis(input, *controller, "moveXAxis", "MoveX");
            const auto moveY = axis(input, *controller, "moveYAxis", "MoveY");
            const auto lookX = axis(input, *controller, "lookXAxis", "LookX");
            const auto speed = static_cast<float>(property(*controller, "moveSpeed", 4.0));
            const auto sensitivity =
                static_cast<float>(property(*controller, "lookSensitivity", 1.0));
            updateFirstPerson(binding.player, {moveY, moveX, lookX, 0.0F},
                              std::max(0.0F, speed), std::max(0.0F, sensitivity), 48.0F, 0.2F,
                              delta, binding.grid);
            binding.weapon.update(delta);
            binding.projectiles.update(delta, binding.grid);
            const auto primary = action(input, *controller, "primaryAction", "Fire");
            if (primary.pressed && binding.weapon.tryFire()) {
                ++stats_.fpsShots;
                const Vec2 forward{std::cos(binding.player.yawRadians),
                                   std::sin(binding.player.yawRadians)};
                static_cast<void>(binding.projectiles.spawn(binding.player.position, forward, 6.0F,
                                                            1.5F, 20));
                std::vector<HitscanTarget> targets;
                targets.reserve(binding.enemies.size());
                for (const auto& enemy : binding.enemies)
                    targets.push_back({enemy.position, 0.6F, enemy.active});
                const auto hit = hitscan(binding.player.position, forward, 20.0F, targets);
                if (hit.hit && hit.targetIndex < binding.enemies.size()) {
                    auto& enemy = binding.enemies[hit.targetIndex];
                    static_cast<void>(enemy.vitality.applyDamage(25));
                    enemy.active = enemy.vitality.alive();
                    ++stats_.fpsHits;
                }
                if (binding.door.activate(binding.keys))
                    ++stats_.fpsDoorActivations;
            }
            binding.door.door.update(delta);
            for (auto& enemy : binding.enemies)
                if (enemy.active)
                    enemy.chase(binding.player.position, delta, binding.grid);
            const auto triggers = activateFpsTriggers(binding.player.position, binding.triggers);
            stats_.fpsSecrets += triggers.secretArea ? 1U : 0U;
            const auto hud = makeFpsHud(binding.vitality, binding.weapon, binding.keys);
            stats_.fpsKeys = hud.keys;
            for (std::size_t index = 0U; index < binding.enemies.size() &&
                                               index < binding.enemyEntities.size();
                 ++index) {
                if (auto* target = scene_->findEntity(binding.enemyEntities[index])) {
                    auto targetPosition = target->transform().localPosition();
                    targetPosition.x = binding.enemies[index].position.x;
                    targetPosition.y = binding.enemies[index].position.y;
                    target->transform().setLocalPosition(targetPosition);
                    target->setActive(binding.enemies[index].active);
                }
            }
            auto position = entity->transform().localPosition();
            position.x = binding.player.position.x;
            position.y = binding.player.position.y;
            entity->transform().setLocalRotation({0.0F, 0.0F, binding.player.yawRadians});
            entity->transform().setLocalPosition(position);
            writeHealth(*entity, binding.vitality.health, 100);
        }

        if (auto* controller = component(*entity, "ThirdPersonController"); enabled(controller)) {
            if (++controlled > limits_.maximumControlledEntities) {
                return Result<void>::failure(
                    Error(ErrorCode::CapacityExceeded, "scene has too many controlled entities"));
            }
            auto& binding = state_->thirdPerson.try_emplace(entity->id()).first->second;
            if (!binding.configured) {
                binding.player.position = entity->transform().localPosition();
                binding.player.health = 80;
                binding.camera.pivot = binding.player.position;
                binding.config.speed =
                    static_cast<float>(std::max(0.0, property(*controller, "moveSpeed", 4.0)));
                std::uint32_t id = 1U;
                for (auto* item : scene_->entities()) {
                    if (!item->active() || item->id() == entity->id())
                        continue;
                    if (nameContains(*item, "target") || nameContains(*item, "enemy")) {
                        binding.enemies.push_back({item->transform().localPosition(), 20, true});
                        binding.enemyEntities.push_back(item->id());
                    } else if (nameContains(*item, "pickup")) {
                        binding.pickups.push_back(
                            {id++, item->transform().localPosition(), 1.0F, 20, 5, true});
                    } else if (nameContains(*item, "obstacle")) {
                        const auto position = item->transform().localPosition();
                        const auto scale = item->transform().localScale();
                        binding.obstacles.push_back({position.x - std::fabs(scale.x) * 0.5F,
                                                     position.z - std::fabs(scale.z) * 0.5F,
                                                     std::max(0.1F, std::fabs(scale.x)),
                                                     std::max(0.1F, std::fabs(scale.z))});
                    }
                }
                binding.configured = true;
            }
            const auto moveX = axis(input, *controller, "moveXAxis", "MoveX");
            const auto moveY = axis(input, *controller, "moveYAxis", "MoveY");
            const auto lookX = axis(input, *controller, "lookXAxis", "LookX");
            const auto primary = action(input, *controller, "primaryAction", "Fire");
            binding.camera.orbit(lookX * delta, 0.0F, 0.0F);
            updateThirdPersonCharacter(binding.player, {{moveX, moveY}, false},
                                       binding.camera.yawRadians, 0.0F, delta, binding.config,
                                       binding.obstacles);
            binding.camera.pivot = binding.player.position;
            static_cast<void>(binding.camera.resolveCollision(binding.obstacles));
            binding.weapon.update(delta);
            std::vector<TargetCandidate> candidates;
            candidates.reserve(binding.enemies.size());
            for (const auto& enemy : binding.enemies)
                candidates.push_back({enemy.position, enemy.active});
            const Vec3 facing{std::sin(binding.player.facingRadians), 0.0F,
                              std::cos(binding.player.facingRadians)};
            const auto selected =
                selectTarget(binding.player.position, facing, binding.weapon.range, 0.5F, candidates);
            if (selected && selected != binding.selectedTarget)
                ++stats_.tpsTargets;
            binding.selectedTarget = selected;
            if (primary.pressed) {
                auto direction = facing;
                if (selected && *selected < binding.enemies.size())
                    direction = normalized(binding.enemies[*selected].position - binding.player.position);
                const auto attack = attackTps(binding.weapon, binding.player.position, direction,
                                              candidates);
                if (attack.fired)
                    ++stats_.tpsShots;
                if (attack.target && *attack.target < binding.enemies.size()) {
                    auto& enemy = binding.enemies[*attack.target];
                    enemy.health = std::max(0, enemy.health - attack.damage);
                    enemy.active = enemy.health > 0;
                    ++stats_.tpsHits;
                }
            }
            for (auto& pickup : binding.pickups)
                stats_.tpsPickups += applyTpsPickup(pickup, binding.player, binding.weapon) ? 1U : 0U;
            for (std::size_t index = 0U; index < binding.enemies.size() &&
                                               index < binding.enemyEntities.size();
                 ++index) {
                if (auto* target = scene_->findEntity(binding.enemyEntities[index]))
                    target->setActive(binding.enemies[index].active);
            }
            entity->transform().setLocalPosition(binding.player.position);
            entity->transform().setLocalRotation({0.0F, binding.player.facingRadians, 0.0F});
            writeHealth(*entity, binding.player.health, 100);
        }
    }
    return Result<void>::success();
}

void SceneGameplayRuntime::shutdown() noexcept {
    if (state_) {
        state_->platformers.clear();
        state_->topDown.clear();
        state_->firstPerson.clear();
        state_->thirdPerson.clear();
        state_->vehicles.clear();
    }
    initialized_ = false;
}

std::size_t SceneGameplayRuntime::controlledEntityCount() const noexcept {
    if (!state_)
        return 0U;
    return state_->platformers.size() + state_->topDown.size() + state_->firstPerson.size() +
           state_->thirdPerson.size() + state_->vehicles.size();
}

} // namespace fabgl::frameworks
