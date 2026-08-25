#include "test_harness.h"

#include "fabgl/animation/animation.h"
#include "fabgl/physics/physics2d.h"

#include <limits>
#include <memory>
#include <string>
#include <utility>

using namespace fabgl;

FGL_TEST(physics2d_resolves_aabb_circle_contacts_and_respects_layers) {
    PhysicsWorld2D world;
    PhysicsBody2D box;
    box.shape = AabbShape{{1.0F, 1.0F}};
    box.layer = 1U;
    box.collisionMask = 2U;
    auto boxId = world.addBody(box);
    FGL_CHECK(boxId);

    PhysicsBody2D circle;
    circle.position = {1.5F, 0.0F};
    circle.shape = CircleShape{1.0F};
    circle.layer = 2U;
    circle.collisionMask = 1U;
    circle.dynamic = true;
    auto circleId = world.addBody(circle);
    FGL_CHECK(circleId);
    FGL_CHECK(world.step(0.0F));
    FGL_CHECK(world.contacts().size() == 1);
    const auto& contact = world.contacts()[0];
    FGL_CHECK(contact.first == boxId.value());
    FGL_CHECK(contact.second == circleId.value());
    FGL_CHECK(!contact.trigger);
    FGL_CHECK_NEAR(contact.penetration, 0.5F, 0.0001F);
    FGL_CHECK_NEAR(world.findBody(circleId.value())->position.x, 2.0F, 0.0001F);

    world.findBody(circleId.value())->collisionMask = 0U;
    FGL_CHECK(world.setPosition(circleId.value(), {1.5F, 0.0F}));
    FGL_CHECK(world.step(0.0F));
    FGL_CHECK(world.contacts().empty());
}

FGL_TEST(physics2d_triggers_do_not_resolve_and_contacts_are_deterministic) {
    PhysicsWorld2D world;
    PhysicsBody2D first;
    first.shape = CircleShape{1.0F};
    first.trigger = true;
    auto firstId = world.addBody(first);
    FGL_CHECK(firstId);
    PhysicsBody2D second;
    second.position = {0.5F, 0.0F};
    second.shape = CircleShape{1.0F};
    second.dynamic = true;
    auto secondId = world.addBody(second);
    FGL_CHECK(secondId);
    PhysicsBody2D third;
    third.position = {-0.5F, 0.0F};
    third.shape = CircleShape{1.0F};
    third.trigger = true;
    auto thirdId = world.addBody(third);
    FGL_CHECK(thirdId);

    FGL_CHECK(world.step(0.0F));
    FGL_CHECK(world.contacts().size() == 3);
    FGL_CHECK(world.contacts()[0].first == firstId.value());
    FGL_CHECK(world.contacts()[0].second == secondId.value());
    FGL_CHECK(world.contacts()[0].trigger);
    FGL_CHECK_NEAR(world.findBody(secondId.value())->position.x, 0.5F, 0.0001F);
}

FGL_TEST(physics2d_raycast_returns_closest_filtered_shape) {
    PhysicsWorld2D world;
    PhysicsBody2D ignored;
    ignored.position = {2.0F, 0.0F};
    ignored.shape = CircleShape{0.5F};
    ignored.layer = 1U;
    auto ignoredId = world.addBody(ignored);
    FGL_CHECK(ignoredId);
    PhysicsBody2D target;
    target.position = {4.0F, 0.0F};
    target.shape = AabbShape{{0.5F, 1.0F}};
    target.layer = 2U;
    auto targetId = world.addBody(target);
    FGL_CHECK(targetId);

    auto hit = world.raycast({0.0F, 0.0F}, {2.0F, 0.0F}, 10.0F, 2U);
    FGL_CHECK(hit && hit.value());
    FGL_CHECK(hit.value()->body == targetId.value());
    FGL_CHECK_NEAR(hit.value()->distance, 3.5F, 0.0001F);
    FGL_CHECK_NEAR(hit.value()->normal.x, -1.0F, 0.0001F);
    auto miss = world.raycast({0.0F, 3.0F}, {1.0F, 0.0F}, 10.0F);
    FGL_CHECK(miss && !miss.value());
    FGL_CHECK(!world.raycast({}, {}, 10.0F));
}

FGL_TEST(physics2d_applies_gravity_and_mass_based_restitution_impulses) {
    PhysicsWorld2D gravityWorld;
    FGL_CHECK(gravityWorld.setGravity({0.0F, 10.0F}));
    PhysicsBody2D falling;
    falling.dynamic = true;
    falling.gravityScale = 0.5F;
    auto fallingId = gravityWorld.addBody(falling);
    FGL_CHECK(fallingId);
    FGL_CHECK(gravityWorld.step(0.5F));
    FGL_CHECK_NEAR(gravityWorld.findBody(fallingId.value())->velocity.y, 2.5F, 0.0001F);
    FGL_CHECK_NEAR(gravityWorld.findBody(fallingId.value())->position.y, 1.25F, 0.0001F);
    FGL_CHECK(!gravityWorld.setGravity({std::numeric_limits<float>::infinity(), 0.0F}));

    PhysicsWorld2D bounceWorld;
    FGL_CHECK(bounceWorld.setGravity({}));
    PhysicsBody2D wall;
    wall.position = {2.0F, 0.0F};
    wall.shape = AabbShape{{0.5F, 2.0F}};
    wall.restitution = 1.0F;
    FGL_CHECK(bounceWorld.addBody(wall));
    PhysicsBody2D ball;
    ball.shape = CircleShape{0.5F};
    ball.velocity = {4.0F, 0.0F};
    ball.dynamic = true;
    ball.restitution = 1.0F;
    ball.friction = 0.0F;
    auto ballId = bounceWorld.addBody(ball);
    FGL_CHECK(ballId);
    FGL_CHECK(bounceWorld.step(0.3F));
    FGL_CHECK(bounceWorld.contacts().size() == 1);
    FGL_CHECK_NEAR(bounceWorld.findBody(ballId.value())->velocity.x, -4.0F, 0.0001F);
}

FGL_TEST(physics2d_supports_kinematic_point_raycast_and_overlap_queries) {
    PhysicsWorld2D world;
    FGL_CHECK(world.setGravity({0.0F, 100.0F}));
    PhysicsBody2D kinematic;
    kinematic.position = {1.0F, 0.0F};
    kinematic.velocity = {2.0F, 0.0F};
    kinematic.shape = PointShape{};
    kinematic.kinematic = true;
    kinematic.layer = 2U;
    auto pointId = world.addBody(kinematic);
    FGL_CHECK(pointId);
    FGL_CHECK(world.step(0.5F));
    FGL_CHECK_NEAR(world.findBody(pointId.value())->position.x, 2.0F, 0.0001F);
    FGL_CHECK_NEAR(world.findBody(pointId.value())->velocity.y, 0.0F, 0.0001F);

    PhysicsBody2D circle;
    circle.position = {2.25F, 0.0F};
    circle.shape = CircleShape{0.5F};
    circle.layer = 4U;
    auto circleId = world.addBody(circle);
    FGL_CHECK(circleId);

    auto hit = world.raycast({}, {1.0F, 0.0F}, 10.0F, 2U);
    FGL_CHECK(hit && hit.value());
    FGL_CHECK(hit.value()->body == pointId.value());
    FGL_CHECK_NEAR(hit.value()->distance, 2.0F, 0.0001F);

    auto overlaps = world.overlap({2.0F, 0.0F}, AabbShape{{0.3F, 0.3F}}, 6U);
    FGL_CHECK(overlaps);
    FGL_CHECK(overlaps.value().size() == 2);
    FGL_CHECK(overlaps.value()[0] == pointId.value());
    FGL_CHECK(overlaps.value()[1] == circleId.value());
    FGL_CHECK(!world.overlap({}, CircleShape{-1.0F}));

    PhysicsBody2D invalid;
    invalid.dynamic = true;
    invalid.kinematic = true;
    FGL_CHECK(!world.addBody(invalid));
}

FGL_TEST(physics2d_tile_collision_character_controller_and_debug_geometry_are_bounded) {
    PhysicsWorld2D world;
    TileCollisionMap2D tiles;
    tiles.width = 4U;
    tiles.height = 3U;
    tiles.solidCells = {
        0U, 0U, 0U, 0U,
        0U, 0U, 0U, 0U,
        1U, 1U, 1U, 1U,
    };
    auto tileMapId = world.addTileCollisionMap(tiles);
    FGL_CHECK(tileMapId);
    FGL_CHECK(world.bodyCount() == 4U);
    FGL_CHECK(world.findTileCollisionMap(tileMapId.value()) != nullptr);

    auto floorRay = world.raycast({1.5F, 0.0F}, {0.0F, 1.0F}, 10.0F);
    FGL_CHECK(floorRay && floorRay.value());
    FGL_CHECK_NEAR(floorRay.value()->distance, 2.0F, 0.0001F);

    CharacterController2DSettings settings;
    settings.halfExtents = {0.4F, 0.4F};
    settings.gravity = {0.0F, 20.0F};
    auto movement = world.moveCharacter({1.5F, 0.5F}, {}, 0.5F, settings);
    FGL_CHECK(movement);
    FGL_CHECK(movement.value().grounded);
    FGL_CHECK_NEAR(movement.value().velocity.y, 0.0F, 0.0001F);
    FGL_CHECK(movement.value().position.y < 1.601F);
    FGL_CHECK(!movement.value().touchedBodies.empty());

    const auto debug = world.debugPrimitives();
    FGL_CHECK(debug.size() == 4U);
    FGL_CHECK(debug[0].shape == PhysicsDebugShape2D::Aabb);
    FGL_CHECK_NEAR(debug[0].halfExtents.x, 0.5F, 0.0001F);

    FGL_CHECK(world.removeTileCollisionMap(tileMapId.value()));
    FGL_CHECK(world.bodyCount() == 0U);
    FGL_CHECK(!world.removeTileCollisionMap(tileMapId.value()));

    TileCollisionMap2D malformed;
    malformed.width = 2U;
    malformed.height = 2U;
    malformed.solidCells = {1U};
    FGL_CHECK(!world.addTileCollisionMap(std::move(malformed)));
    CharacterController2DSettings invalidSettings;
    invalidSettings.maximumSubsteps = 0U;
    FGL_CHECK(!world.moveCharacter({}, {}, 1.0F, invalidSettings));
}

FGL_TEST(animation_curve_supports_step_linear_and_cubic_sampling) {
    AnimationCurve linear;
    FGL_CHECK(linear.addKey({0.0F, 0.0F, 0.0F, 0.0F, CurveInterpolation::Linear}));
    FGL_CHECK(linear.addKey({1.0F, 10.0F, 0.0F, 0.0F, CurveInterpolation::Linear}));
    FGL_CHECK_NEAR(linear.sample(0.25F), 2.5F, 0.0001F);
    FGL_CHECK_NEAR(linear.sample(-1.0F), 0.0F, 0.0001F);

    AnimationCurve step;
    FGL_CHECK(step.addKey({0.0F, 2.0F, 0.0F, 0.0F, CurveInterpolation::Step}));
    FGL_CHECK(step.addKey({1.0F, 8.0F, 0.0F, 0.0F, CurveInterpolation::Linear}));
    FGL_CHECK_NEAR(step.sample(0.9F), 2.0F, 0.0001F);

    AnimationCurve cubic;
    FGL_CHECK(cubic.addKey({0.0F, 0.0F, 0.0F, 0.0F, CurveInterpolation::CubicHermite}));
    FGL_CHECK(cubic.addKey({1.0F, 1.0F, 0.0F, 0.0F, CurveInterpolation::Linear}));
    FGL_CHECK_NEAR(cubic.sample(0.5F), 0.5F, 0.0001F);
    FGL_CHECK(!cubic.addKey({-1.0F, 0.0F}));
}

FGL_TEST(animation_clips_emit_looped_events_and_animator_transitions) {
    AnimationCurve idleCurve;
    FGL_CHECK(idleCurve.addKey({0.0F, 0.0F}));
    FGL_CHECK(idleCurve.addKey({1.0F, 1.0F}));
    auto idle = std::make_shared<AnimationClip>("Idle", 1.0F, true);
    FGL_CHECK(idle->addTrack("sprite.frame", idleCurve));
    FGL_CHECK(idle->addEvent({0.25F, "blink"}));
    const auto repeatedEvents = idle->eventsCrossed(0.2F, 1.3F);
    FGL_CHECK(repeatedEvents.size() == 2);
    FGL_CHECK(repeatedEvents[0] == "blink" && repeatedEvents[1] == "blink");

    AnimationCurve runCurve;
    FGL_CHECK(runCurve.addKey({0.0F, 10.0F}));
    FGL_CHECK(runCurve.addKey({1.0F, 20.0F}));
    auto run = std::make_shared<AnimationClip>("Run", 1.0F, true);
    FGL_CHECK(run->addTrack("sprite.frame", runCurve));

    AnimatorController animator;
    FGL_CHECK(animator.addState("idle", idle));
    FGL_CHECK(animator.addState("run", run));
    FGL_CHECK(animator.addTransition({"idle", "run", "moving", true, 0.5F}));
    FGL_CHECK(animator.play("idle"));
    auto first = animator.update(0.3F);
    FGL_CHECK(first);
    FGL_CHECK(first.value().events.size() == 1 && first.value().events[0] == "blink");
    FGL_CHECK(!first.value().transitioned);
    animator.setBoolean("moving", true);
    auto transition = animator.update(0.3F);
    FGL_CHECK(transition);
    FGL_CHECK(transition.value().transitioned);
    FGL_CHECK(transition.value().state == "run");
    FGL_CHECK_NEAR(transition.value().values["sprite.frame"], 10.0F, 0.0001F);
    FGL_CHECK(!animator.update(-1.0F));
}
