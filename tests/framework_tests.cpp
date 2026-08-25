#include "test_harness.h"

#include <fabgl/frameworks/fps.h>
#include <fabgl/frameworks/platformer.h>
#include <fabgl/frameworks/racer.h>
#include <fabgl/frameworks/top_down.h>
#include <fabgl/frameworks/tps.h>

#include <vector>

FGL_TEST(platformer_supports_ground_jump_buffer_and_one_way_platform) {
    fabgl::frameworks::PlatformerController controller;
    fabgl::frameworks::PlatformerState state;
    state.position = {10.0F, 0.0F};
    const std::vector<fabgl::Rect> ground = {{0.0F, 40.0F, 100.0F, 10.0F}};
    for (auto frame = 0; frame < 60; ++frame) {
        controller.step(state, {}, 1.0F / 60.0F, ground);
    }
    FGL_CHECK(state.grounded);
    FGL_CHECK_NEAR(state.position.y, 28.0F, 0.001F);
    controller.step(state, {0.0F, true, true}, 1.0F / 60.0F, ground);
    FGL_CHECK(!state.grounded);
    FGL_CHECK(state.velocity.y < 0.0F);
}

FGL_TEST(
    platformer_world_integrates_moving_platform_collectible_enemy_checkpoint_camera_hud_and_exit) {
    fabgl::frameworks::PlatformerWorld world(8U);
    FGL_CHECK(world.addMovingPlatform(
        {{0.0F, 20.0F, 20.0F, 2.0F}, {10.0F, 0.0F}, {0.0F, 20.0F}, {10.0F, 20.0F}, true}));
    FGL_CHECK(world.addCollectible({1U, {1.0F, 8.0F, 10.0F, 12.0F}, 5, true}));
    FGL_CHECK(!world.addCollectible({1U, {1.0F, 8.0F, 1.0F, 1.0F}, 1, true}));
    FGL_CHECK(world.addEnemy({2U, {2.0F, 8.0F, 8.0F, 12.0F}, 2.0F, 2.0F, 0.0F, 1, true, true}));
    FGL_CHECK(world.addCheckpoint({3U, {1.0F, 8.0F, 12.0F, 12.0F}, {7.0F, 8.0F}}));
    FGL_CHECK(world.addTransition({9U, {1.0F, 8.0F, 12.0F, 12.0F}, true}));

    fabgl::frameworks::PlatformerController controller;
    fabgl::frameworks::PlatformerState player;
    player.position = {1.0F, 8.0F};
    player.grounded = true;
    fabgl::frameworks::PlatformerWorldState state;
    fabgl::frameworks::PlatformerCamera camera;
    camera.viewport = {20.0F, 10.0F};
    camera.worldBounds = {0.0F, 0.0F, 100.0F, 50.0F};
    camera.followRate = 100.0F;
    world.update(controller, player, state, camera, {}, 0.1F, {});
    FGL_CHECK(player.position.x > 1.5F);
    FGL_CHECK(state.score == 5 && state.collected == 1U);
    FGL_CHECK(state.health == 2 && state.damageCooldown > 0.0F);
    FGL_CHECK(state.checkpointId.has_value() && *state.checkpointId == 3U);
    FGL_CHECK(state.respawnPosition == (fabgl::Vec2{7.0F, 8.0F}));
    FGL_CHECK(state.requestedLevel.has_value() && *state.requestedLevel == 9U);
    const auto hud = world.hud(state);
    FGL_CHECK(hud.health == 2 && hud.score == 5 && !hud.gameOverVisible);
    FGL_CHECK(camera.position.x >= 0.0F && camera.position.y >= 0.0F);
}

FGL_TEST(platformer_world_interactions_honor_the_controller_body_size) {
    fabgl::frameworks::PlatformerConfig config;
    config.bodySize = {20.0F, 4.0F};
    config.gravity = 0.0F;
    fabgl::frameworks::PlatformerController controller(config);
    FGL_CHECK(controller.bodySize() == (fabgl::Vec2{20.0F, 4.0F}));
    fabgl::frameworks::PlatformerWorld world(2U);
    FGL_CHECK(world.addCollectible({1U, {15.0F, 0.0F, 2.0F, 2.0F}, 7, true}));
    fabgl::frameworks::PlatformerState player;
    fabgl::frameworks::PlatformerWorldState state;
    fabgl::frameworks::PlatformerCamera camera;
    world.update(controller, player, state, camera, {}, 1.0F / 60.0F, {});
    FGL_CHECK(state.collected == 1U && state.score == 7);
}

FGL_TEST(top_down_weapon_and_projectile_pool_are_bounded) {
    fabgl::frameworks::TopDownState state;
    fabgl::frameworks::updateTopDown(state, {1.0F, 1.0F}, {0.0F, 1.0F}, 10.0F, 1.0F);
    FGL_CHECK_NEAR(state.position.x, 0.7071067F, 0.001F);
    FGL_CHECK_NEAR(state.aim.y, 1.0F, 0.001F);

    fabgl::frameworks::Weapon weapon;
    FGL_CHECK(weapon.tryFire(true, true));
    FGL_CHECK(!weapon.tryFire(true, true));
    weapon.update(1.0F);
    FGL_CHECK(weapon.tryFire(true, false));

    fabgl::frameworks::ProjectilePool pool(1U);
    FGL_CHECK(pool.spawn({}, {1.0F, 0.0F}, 10.0F, 0.1F, 5));
    FGL_CHECK(!pool.spawn({}, {1.0F, 0.0F}, 10.0F, 1.0F, 5));
    pool.update(0.2F);
    FGL_CHECK(pool.spawn({}, {0.0F, 1.0F}, 5.0F, 1.0F, 2));
}

FGL_TEST(
    top_down_framework_covers_direction_hitscan_health_enemy_pickups_arena_rooms_and_inventory) {
    const auto four = fabgl::frameworks::quantizeTopDownDirection(
        {0.2F, 1.0F}, fabgl::frameworks::TopDownDirectionMode::FourWay);
    FGL_CHECK(four == (fabgl::Vec2{0.0F, 1.0F}));
    const auto eight = fabgl::frameworks::quantizeTopDownDirection(
        {1.0F, 1.0F}, fabgl::frameworks::TopDownDirectionMode::EightWay);
    FGL_CHECK_NEAR(eight.x, 0.7071067F, 0.001F);

    const std::vector<fabgl::frameworks::TopDownHitscanTarget> targets = {
        {{6.0F, 0.0F}, 0.5F, true}, {{3.0F, 0.1F}, 0.25F, true}};
    const auto hit = fabgl::frameworks::topDownHitscan({}, {1.0F, 0.0F}, 10.0F, targets);
    FGL_CHECK(hit.hit && hit.targetIndex == 1U);

    fabgl::frameworks::TopDownEnemy enemy;
    enemy.position = {5.0F, 0.0F};
    enemy.speed = 10.0F;
    enemy.chase({}, 0.1F);
    FGL_CHECK(enemy.position.x < 5.0F);
    fabgl::frameworks::TopDownHealth health{100, 50};
    FGL_CHECK(health.damage(20) == 20 && health.heal(5) == 5);

    fabgl::frameworks::TopDownInventory inventory(1U);
    FGL_CHECK(inventory.add(7U, 2U));
    FGL_CHECK(!inventory.add(8U));
    FGL_CHECK(inventory.consume(7U) && inventory.count(7U) == 1U);
    fabgl::frameworks::Weapon weapon;
    weapon.ammunition = 1;
    weapon.reserveAmmunition = 4;
    FGL_CHECK(weapon.reload() == 4 && weapon.ammunition == 5);
    fabgl::frameworks::TopDownPickup pickup{
        1U,  {0.0F, 0.0F, 2.0F, 2.0F}, fabgl::frameworks::TopDownPickupKind::Ammunition, 0U, 3,
        true};
    FGL_CHECK(
        fabgl::frameworks::applyTopDownPickup(pickup, {1.0F, 1.0F}, health, weapon, inventory));
    FGL_CHECK(!pickup.active && weapon.reserveAmmunition == 3);

    const fabgl::frameworks::TopDownArena arena{{0.0F, 0.0F, 10.0F, 8.0F}};
    FGL_CHECK(arena.constrain({20.0F, -2.0F}, {2.0F, 2.0F}) == (fabgl::Vec2{8.0F, 0.0F}));
    const std::vector<fabgl::frameworks::TopDownRoomTransition> transitions = {
        {4U, {7.0F, 0.0F, 3.0F, 3.0F}, true}};
    const auto room = fabgl::frameworks::topDownRoomAt({8.0F, 1.0F}, transitions);
    FGL_CHECK(room.has_value() && *room == 4U);
}

FGL_TEST(fps_health_door_and_hitscan_behave_deterministically) {
    fabgl::frameworks::HealthArmor health{100, 20};
    FGL_CHECK(health.applyDamage(30) == 15);
    FGL_CHECK(health.health == 85 && health.armor == 5);

    fabgl::frameworks::DoorState door;
    door.speed = 10.0F;
    door.activate();
    door.update(0.1F);
    FGL_CHECK(door.phase == fabgl::frameworks::DoorPhase::Open);
    FGL_CHECK(!door.blocksMovement());

    const std::vector<fabgl::frameworks::HitscanTarget> targets = {{{5.0F, 0.1F}, 0.5F, true},
                                                                   {{3.0F, 0.0F}, 0.25F, true}};
    const auto hit = fabgl::frameworks::hitscan({}, {1.0F, 0.0F}, 10.0F, targets);
    FGL_CHECK(hit.hit && hit.targetIndex == 1U);
    FGL_CHECK_NEAR(hit.distance, 3.0F, 0.001F);
}

FGL_TEST(
    fps_framework_integrates_controller_weapons_projectiles_enemy_keys_pickups_triggers_hud_and_save) {
    fabgl::frameworks::FpsGrid grid;
    grid.width = 5;
    grid.height = 5;
    grid.cells = {1U, 1U, 1U, 1U, 1U, 1U, 0U, 0U, 0U, 1U, 1U, 0U, 0U,
                  0U, 1U, 1U, 0U, 0U, 0U, 1U, 1U, 1U, 1U, 1U, 1U};
    FGL_CHECK(grid.valid());
    fabgl::frameworks::FirstPersonState player;
    player.position = {1.5F, 1.5F};
    fabgl::frameworks::updateFirstPerson(player, {1.0F, 0.0F, 0.0F, 100.0F}, 2.0F, 0.1F, 10.0F,
                                         0.2F, 0.1F, grid);
    FGL_CHECK(player.position.x > 1.5F && player.pitch == 10.0F);

    fabgl::frameworks::FpsWeapon weapon;
    weapon.ammunition = 1;
    weapon.reserveAmmunition = 5;
    weapon.reloadSeconds = 0.1F;
    FGL_CHECK(weapon.tryFire());
    weapon.update(1.0F);
    FGL_CHECK(weapon.beginReload());
    weapon.update(0.1F);
    FGL_CHECK(weapon.ammunition == 5 && weapon.reserveAmmunition == 0);
    fabgl::frameworks::FpsProjectilePool projectiles(1U);
    FGL_CHECK(projectiles.spawn({2.5F, 2.5F}, {1.0F, 0.0F}, 20.0F, 1.0F, 4));
    projectiles.update(0.1F, grid);
    FGL_CHECK(!projectiles.projectiles()[0].active);

    fabgl::frameworks::FpsKeyRing keys(2U);
    FGL_CHECK(keys.grant(7U));
    fabgl::frameworks::LockedDoorState locked;
    locked.requiredKey = 7U;
    locked.consumeKey = true;
    FGL_CHECK(locked.activate(keys));
    FGL_CHECK(!keys.has(7U) && locked.door.phase == fabgl::frameworks::DoorPhase::Opening);

    fabgl::frameworks::FpsEnemy enemy;
    enemy.position = {3.0F, 3.0F};
    enemy.chase({2.0F, 3.0F}, 0.1F, grid);
    FGL_CHECK(enemy.position.x < 3.0F);
    fabgl::frameworks::HealthArmor vitality{50, 10};
    fabgl::frameworks::FpsPickup pickup{
        1U, {2.0F, 2.0F}, 0.5F, fabgl::frameworks::FpsPickupKind::Key, 1, 9U, true};
    FGL_CHECK(fabgl::frameworks::applyFpsPickup(pickup, {2.0F, 2.0F}, vitality, weapon, keys));
    std::vector<fabgl::frameworks::FpsTrigger> triggers = {
        {1U, {1.0F, 1.0F, 2.0F, 2.0F}, fabgl::frameworks::FpsTriggerKind::SecretArea, true, true},
        {2U, {1.0F, 1.0F, 2.0F, 2.0F}, fabgl::frameworks::FpsTriggerKind::LevelExit, true, true}};
    const auto triggered = fabgl::frameworks::activateFpsTriggers({2.0F, 2.0F}, triggers);
    FGL_CHECK(triggered.activated == 2U && triggered.levelExit && triggered.secretArea);
    const auto hud = fabgl::frameworks::makeFpsHud(vitality, weapon, keys);
    FGL_CHECK(hud.health == 50 && hud.keys == 1U);

    fabgl::frameworks::FpsSaveState save;
    save.player = player;
    save.vitality = vitality;
    save.ammunition = weapon.ammunition;
    save.reserveAmmunition = weapon.reserveAmmunition;
    save.keys = keys.keys();
    save.level = 3U;
    save.secretsFound = 2U;
    const auto encoded = fabgl::frameworks::serializeFpsSave(save);
    FGL_CHECK(encoded);
    const auto decoded = fabgl::frameworks::deserializeFpsSave(encoded.value());
    FGL_CHECK(decoded && decoded.value().level == 3U && decoded.value().keys.size() == 1U);
    FGL_CHECK(!fabgl::frameworks::deserializeFpsSave(encoded.value() + "trailing"));
}

FGL_TEST(racer_vehicle_and_lap_tracker_validate_order) {
    fabgl::frameworks::VehicleState vehicle;
    const fabgl::frameworks::VehicleConfig config;
    for (auto frame = 0; frame < 120; ++frame) {
        fabgl::frameworks::updateVehicle(vehicle, config, 1.0F, 0.0F, 0.4F, false, 1.0F / 60.0F);
    }
    FGL_CHECK(vehicle.speed > 0.0F && vehicle.distance > 0.0F);
    FGL_CHECK(vehicle.heading > 0.0F);
    FGL_CHECK(vehicle.gear >= 1 && vehicle.gear <= config.forwardGears);
    FGL_CHECK(vehicle.normalizedRpm >= 0.0F && vehicle.normalizedRpm <= 1.0F);

    fabgl::frameworks::LapTracker laps(3U, 2);
    FGL_CHECK(!laps.crossCheckpoint(1U));
    FGL_CHECK(laps.crossCheckpoint(0U));
    FGL_CHECK(laps.crossCheckpoint(1U));
    FGL_CHECK(laps.crossCheckpoint(2U));
    FGL_CHECK(laps.lap() == 2);
    FGL_CHECK(laps.completedLaps() == 1);
    FGL_CHECK(laps.nextCheckpoint() == 0U);
}

FGL_TEST(racer_countdown_automatic_gears_finish_hud_and_positions_are_deterministic) {
    fabgl::frameworks::RaceCountdown countdown(3.0F);
    FGL_CHECK(countdown.displayedNumber() == 3);
    countdown.update(1.1F);
    FGL_CHECK(countdown.displayedNumber() == 2);
    countdown.update(5.0F);
    FGL_CHECK(countdown.complete());
    FGL_CHECK(countdown.displayedNumber() == 0);

    fabgl::frameworks::VehicleState vehicle;
    fabgl::frameworks::VehicleConfig config;
    config.forwardGears = 4;
    for (auto frame = 0; frame < 600; ++frame) {
        fabgl::frameworks::updateVehicle(vehicle, config, 1.0F, 0.0F, 0.0F, false, 1.0F / 60.0F);
    }
    FGL_CHECK(vehicle.gear >= 2 && vehicle.gear <= 4);
    fabgl::frameworks::VehicleState reversing;
    for (auto frame = 0; frame < 30; ++frame) {
        fabgl::frameworks::updateVehicle(reversing, config, -1.0F, 0.0F, 0.0F, false, 1.0F / 60.0F);
    }
    FGL_CHECK(reversing.gear == -1);

    const std::vector<fabgl::frameworks::RaceProgress> progress = {
        {30U, 1U, 2U, 25.0F, false, 0.0F},
        {10U, 0U, 0U, 0.0F, true, 72.5F},
        {20U, 1U, 2U, 40.0F, false, 0.0F},
        {11U, 0U, 0U, 0.0F, true, 75.0F},
    };
    const auto positions = fabgl::frameworks::rankRacePositions(progress);
    FGL_CHECK(positions.size() == 4U);
    FGL_CHECK(positions[0].participantId == 10U && positions[0].position == 1U);
    FGL_CHECK(positions[1].participantId == 11U && positions[1].position == 2U);
    FGL_CHECK(positions[2].participantId == 20U && positions[2].position == 3U);

    fabgl::frameworks::LapTracker laps(2U, 1);
    FGL_CHECK(laps.crossCheckpoint(0U));
    FGL_CHECK(laps.crossCheckpoint(1U));
    FGL_CHECK(laps.finished());
    const auto hud = fabgl::frameworks::makeRaceHud(vehicle, laps, 3U, 4U, countdown);
    FGL_CHECK(hud.speedKph > 0U && hud.gear == vehicle.gear);
    FGL_CHECK(hud.currentLap == 1 && hud.targetLaps == 1);
    FGL_CHECK(hud.position == 3U && hud.participantCount == 4U);
    FGL_CHECK(hud.countdownNumber == 0 && hud.finishVisible);
}

FGL_TEST(racer_opponent_ai_slows_for_corners_overtakes_and_recovers_to_track) {
    fabgl::frameworks::VehicleConfig vehicleConfig;
    fabgl::frameworks::OpponentDriverConfig driverConfig;
    driverConfig.targetSpeed = 60.0F;
    driverConfig.skill = 1.0F;

    fabgl::frameworks::OpponentDriverState cornering;
    cornering.vehicle.speed = 55.0F;
    fabgl::frameworks::OpponentPerception corner;
    corner.curveAhead = 0.08F;
    auto cornerControl = fabgl::frameworks::updateOpponentDriver(
        cornering, vehicleConfig, driverConfig, corner, 1.0F / 60.0F);
    FGL_CHECK(cornerControl.brake > 0.0F);
    FGL_CHECK(cornerControl.steering < 0.0F);

    fabgl::frameworks::OpponentDriverState overtaking;
    fabgl::frameworks::OpponentPerception traffic;
    traffic.slowerVehicleAhead = true;
    traffic.slowerVehicleLateral = 0.2F;
    auto overtakeControl = fabgl::frameworks::updateOpponentDriver(
        overtaking, vehicleConfig, driverConfig, traffic, 1.0F / 60.0F);
    FGL_CHECK(overtaking.desiredLateral < 0.0F);
    FGL_CHECK(overtakeControl.steering < 0.0F);

    fabgl::frameworks::OpponentDriverState recovering;
    recovering.vehicle.lateral = 1.2F;
    fabgl::frameworks::OpponentPerception offRoad;
    offRoad.roadHalfWidth = 1.0F;
    auto recoveryControl = fabgl::frameworks::updateOpponentDriver(
        recovering, vehicleConfig, driverConfig, offRoad, 1.0F / 60.0F);
    FGL_CHECK(recoveryControl.recovering);
    FGL_CHECK(recoveryControl.steering < 0.0F);
    FGL_CHECK(recovering.desiredLateral == 0.0F);
}

FGL_TEST(tps_movement_is_camera_relative_and_target_lock_prefers_facing) {
    fabgl::frameworks::ThirdPersonState state;
    fabgl::frameworks::updateThirdPerson(state, {0.0F, 1.0F}, 0.0F, 8.0F, 100.0F, 0.1F);
    FGL_CHECK(state.position.z > 0.0F);
    const std::vector<fabgl::frameworks::TargetCandidate> targets = {
        {{1.0F, 0.0F, 4.0F}, true}, {{0.0F, 0.0F, 3.0F}, true}, {{0.0F, 0.0F, -2.0F}, true}};
    const auto selected =
        fabgl::frameworks::selectTarget({}, {0.0F, 0.0F, 1.0F}, 10.0F, 0.5F, targets);
    FGL_CHECK(selected.has_value());
    FGL_CHECK(*selected == 1U);
}

FGL_TEST(
    tps_framework_integrates_character_camera_collision_attacks_pickups_and_presentation_tier) {
    fabgl::frameworks::ThirdPersonState state;
    state.position = {0.0F, 0.0F, 0.0F};
    state.grounded = true;
    fabgl::frameworks::ThirdPersonCharacterConfig config;
    const std::vector<fabgl::Rect> obstacles = {{-0.5F, 0.5F, 1.0F, 1.0F}};
    fabgl::frameworks::updateThirdPersonCharacter(state, {{0.0F, 1.0F}, true}, 0.0F, 0.0F, 0.1F,
                                                  config, obstacles);
    FGL_CHECK(state.position.y > 0.0F && !state.grounded);
    FGL_CHECK(state.position.z < 0.5F);

    fabgl::frameworks::ThirdPersonCamera camera;
    camera.pivot = {0.0F, 1.0F, 0.0F};
    camera.distance = 4.0F;
    camera.pitchRadians = 0.0F;
    const auto desired = camera.desiredPosition();
    const auto resolved = camera.resolveCollision({{-1.0F, -3.0F, 2.0F, 1.0F}});
    FGL_CHECK(resolved.z > desired.z);
    camera.orbit(0.5F, 2.0F, 100.0F);
    FGL_CHECK(camera.pitchRadians <= 1.2F && camera.distance == camera.maximumDistance);

    const std::vector<fabgl::frameworks::TargetCandidate> targets = {{{0.0F, 0.0F, 3.0F}, true},
                                                                     {{2.0F, 0.0F, 3.0F}, true}};
    fabgl::frameworks::TpsWeapon weapon;
    const auto hitscan = fabgl::frameworks::attackTps(weapon, {}, {0.0F, 0.0F, 1.0F}, targets);
    FGL_CHECK(hitscan.fired && hitscan.target.has_value() && *hitscan.target == 0U);
    weapon.update(1.0F);
    weapon.mode = fabgl::frameworks::TpsAttackMode::Projectile;
    const auto projectile = fabgl::frameworks::attackTps(weapon, {}, {0.0F, 0.0F, 1.0F}, targets);
    FGL_CHECK(projectile.fired && projectile.projectileVelocity.z > 0.0F);
    state.health = 50;
    fabgl::frameworks::TpsPickup pickup{1U, state.position, 1.0F, 20, 3, true};
    FGL_CHECK(fabgl::frameworks::applyTpsPickup(pickup, state, weapon));
    FGL_CHECK(state.health == 70 && !pickup.active);
    FGL_CHECK(fabgl::frameworks::isExperimental(
        fabgl::frameworks::TpsCharacterPresentation::LowPolyExperimental));
    FGL_CHECK(
        !fabgl::frameworks::isExperimental(fabgl::frameworks::TpsCharacterPresentation::Billboard));
}
