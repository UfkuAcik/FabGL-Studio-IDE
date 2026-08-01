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

FGL_TEST(racer_vehicle_and_lap_tracker_validate_order) {
    fabgl::frameworks::VehicleState vehicle;
    const fabgl::frameworks::VehicleConfig config;
    for (auto frame = 0; frame < 120; ++frame) {
        fabgl::frameworks::updateVehicle(vehicle, config, 1.0F, 0.0F, 0.4F, false, 1.0F / 60.0F);
    }
    FGL_CHECK(vehicle.speed > 0.0F && vehicle.distance > 0.0F);
    FGL_CHECK(vehicle.heading > 0.0F);

    fabgl::frameworks::LapTracker laps(3U, 2);
    FGL_CHECK(!laps.crossCheckpoint(1U));
    FGL_CHECK(laps.crossCheckpoint(0U));
    FGL_CHECK(laps.crossCheckpoint(1U));
    FGL_CHECK(laps.crossCheckpoint(2U));
    FGL_CHECK(laps.lap() == 2);
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
