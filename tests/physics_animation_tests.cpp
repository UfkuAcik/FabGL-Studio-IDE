#include "test_harness.h"

#include "fabgl/animation/animation.h"
#include "fabgl/physics/physics2d.h"

#include <memory>
#include <string>

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
