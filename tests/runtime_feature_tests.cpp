#include "test_harness.h"

#include "fabgl/animation/animation.h"
#include "fabgl/navigation/ai_behaviors.h"
#include "fabgl/particles/particle_system.h"
#include "fabgl/physics/physics3d.h"
#include "fabgl/ui/runtime_widgets.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

using namespace fabgl;

namespace {

std::shared_ptr<AnimationClip> clip(std::string name, float first, float last) {
    AnimationCurve curve;
    FGL_CHECK(curve.addKey({0.0F, first}));
    FGL_CHECK(curve.addKey({1.0F, last}));
    auto result = std::make_shared<AnimationClip>(std::move(name), 1.0F, true);
    FGL_CHECK(result->addTrack("position.x", curve));
    return result;
}

UILayoutProperties fillLayout() {
    UILayoutProperties properties;
    properties.anchors = {{0.0F, 0.0F}, {1.0F, 1.0F}};
    properties.minimumOffset = {};
    properties.maximumOffset = {};
    return properties;
}

bool hasEvent(const std::vector<UIEvent>& events, UIEventType type, UIElementId target) {
    for (const auto& event : events) {
        if (event.type == type && event.target == target)
            return true;
    }
    return false;
}

} // namespace

FGL_TEST(animator_supports_typed_conditions_exit_time_trigger_consumption_and_blending) {
    AnimatorController animator;
    FGL_CHECK(animator.addState("idle", clip("Idle", 0.0F, 10.0F)));
    FGL_CHECK(animator.addState("run", clip("Run", 20.0F, 30.0F)));
    FGL_CHECK(animator.addState("done", clip("Done", 40.0F, 50.0F)));

    AnimationTransition start;
    start.fromState = "idle";
    start.toState = "run";
    start.conditions = {
        {"armed", AnimationConditionMode::BooleanEquals, true, 0, 0.0F},
        {"lives", AnimationConditionMode::IntegerGreater, false, 1, 0.0F},
        {"speed", AnimationConditionMode::FloatGreater, false, 0, 0.5F},
        {"go", AnimationConditionMode::TriggerSet, false, 0, 0.0F},
    };
    start.hasExitTime = true;
    start.exitTime = 0.5F;
    start.blendDurationSeconds = 0.4F;
    FGL_CHECK(animator.addTransition(start));

    AnimationTransition consumedTrigger;
    consumedTrigger.fromState = "run";
    consumedTrigger.toState = "done";
    consumedTrigger.conditions = {
        {"go", AnimationConditionMode::TriggerSet, false, 0, 0.0F},
    };
    FGL_CHECK(animator.addTransition(consumedTrigger));
    FGL_CHECK(animator.play("idle"));
    animator.setBoolean("armed", true);
    animator.setInteger("lives", 2);
    animator.setFloat("speed", 0.75F);
    animator.setTrigger("go");

    auto beforeExit = animator.update(0.25F);
    FGL_CHECK(beforeExit && !beforeExit.value().transitioned);
    auto transition = animator.update(0.30F);
    FGL_CHECK(transition && transition.value().transitioned);
    FGL_CHECK(transition.value().state == "run");
    FGL_CHECK_NEAR(transition.value().blendWeight, 0.0F, 0.0001F);
    FGL_CHECK_NEAR(transition.value().values["position.x"], 5.5F, 0.0001F);

    auto blended = animator.update(0.20F);
    FGL_CHECK(blended && blended.value().state == "run");
    FGL_CHECK_NEAR(blended.value().blendWeight, 0.5F, 0.0001F);
    FGL_CHECK_NEAR(blended.value().values["position.x"], 14.75F, 0.0001F);
    FGL_CHECK(animator.update(0.20F));
    auto triggerWasConsumed = animator.update(0.0F);
    FGL_CHECK(triggerWasConsumed && triggerWasConsumed.value().state == "run");
    animator.setTrigger("go");
    auto done = animator.update(0.0F);
    FGL_CHECK(done && done.value().transitioned && done.value().state == "done");
}

FGL_TEST(particle_emitter_rates_bursts_lifetime_channels_culling_and_pool_reuse) {
    ParticleSystem pool(4);
    ParticleEmitterSettings settings;
    settings.spawnRate = 2.0F;
    settings.maximumAlive = 3U;
    settings.particle.color = {255, 0, 0, 255};
    settings.particle.size = 1.0F;
    settings.particle.lifetimeSeconds = 2.0F;
    settings.overLifetime.colorEnabled = true;
    settings.overLifetime.endColor = {0, 0, 255, 0};
    settings.overLifetime.sizeEnabled = true;
    settings.overLifetime.endSize = 3.0F;
    settings.overLifetime.rotationEnabled = true;
    settings.overLifetime.endRotationDegrees = 180.0F;
    settings.cullOutsideBounds = true;
    settings.cullingBounds = {-10.0F, -10.0F, 20.0F, 20.0F};
    ParticleEmitter emitter(pool, settings);

    auto firstTick = emitter.update(0.5F);
    FGL_CHECK(firstTick && firstTick.value() == 1U);
    const auto first = pool.handleAtSlot(0U);
    FGL_CHECK(first.valid());
    auto secondTick = emitter.update(0.5F);
    FGL_CHECK(secondTick && secondTick.value() == 1U);
    const auto* particle = pool.get(first);
    FGL_CHECK(particle != nullptr);
    FGL_CHECK_NEAR(particle->size, 1.5F, 0.0001F);
    FGL_CHECK_NEAR(particle->rotationDegrees, 45.0F, 0.0001F);
    FGL_CHECK(particle->color.r == 191U && particle->color.b == 64U);

    auto burst = emitter.burst(5U);
    FGL_CHECK(burst && burst.value() == 1U);
    FGL_CHECK(emitter.stats().rejected == 4U);
    pool.get(first)->position = {100.0F, 0.0F};
    auto cull = emitter.update(0.0F);
    FGL_CHECK(cull && emitter.stats().culled == 1U);
    FGL_CHECK(!pool.isAlive(first));
    FGL_CHECK(emitter.burst(1U).value() == 1U);
    FGL_CHECK(emitter.stats().activeParticles == 3U);
    emitter.clear();
    FGL_CHECK(pool.activeCount() == 0U);
}

FGL_TEST(runtime_ui_layout_pointer_keyboard_theme_and_scaling_are_deterministic) {
    RuntimeUI ui;
    UITheme theme;
    theme.padding = 0.0F;
    theme.spacing = 0.0F;
    FGL_CHECK(ui.setTheme(theme));
    FGL_CHECK(ui.setScale(2.0F));
    auto root = ui.addWidget(UIWidgetType::Layout, std::nullopt, fillLayout());
    FGL_CHECK(root);
    ui.widget(root.value())->layoutDirection = UILayoutDirection::Horizontal;
    auto button = ui.addWidget(UIWidgetType::Button, root.value());
    auto toggle = ui.addWidget(UIWidgetType::Toggle, root.value());
    auto slider = ui.addWidget(UIWidgetType::Slider, root.value());
    FGL_CHECK(button && toggle && slider);
    FGL_CHECK(ui.setText(button.value(), "Start"));
    FGL_CHECK(ui.setRange(slider.value(), 0.0F, 1.0F, 0.1F));
    FGL_CHECK(ui.layout({0.0F, 0.0F, 400.0F, 120.0F}));
    const auto buttonRect = ui.screenRect(button.value());
    FGL_CHECK(buttonRect && buttonRect->width > 133.0F && buttonRect->width < 134.0F);

    FGL_CHECK(ui.pointerMove({20.0F, 20.0F}));
    FGL_CHECK(ui.pointerDown({20.0F, 20.0F}));
    FGL_CHECK(ui.pointerUp({20.0F, 20.0F}));
    auto events = ui.consumeEvents();
    FGL_CHECK(hasEvent(events, UIEventType::Clicked, button.value()));
    FGL_CHECK(hasEvent(events, UIEventType::PointerEntered, button.value()));

    FGL_CHECK(ui.pointerDown({160.0F, 20.0F}));
    FGL_CHECK(ui.pointerUp({160.0F, 20.0F}));
    FGL_CHECK(ui.widget(toggle.value())->checked);
    FGL_CHECK(ui.keyDown(UIKey::Space));
    FGL_CHECK(!ui.widget(toggle.value())->checked);

    FGL_CHECK(ui.pointerDown({360.0F, 20.0F}));
    FGL_CHECK(ui.pointerUp({360.0F, 20.0F}));
    FGL_CHECK(ui.widget(slider.value())->value >= 0.6F);
    FGL_CHECK(ui.keyDown(UIKey::End));
    FGL_CHECK_NEAR(ui.widget(slider.value())->value, 1.0F, 0.0001F);
    FGL_CHECK(!ui.setScale(0.0F));
}

FGL_TEST(runtime_ui_supports_panel_image_text_progress_list_and_selection_events) {
    RuntimeUI ui;
    auto panel = ui.addWidget(UIWidgetType::Panel, std::nullopt, fillLayout());
    auto image = ui.addWidget(UIWidgetType::Image, panel.value());
    auto text = ui.addWidget(UIWidgetType::Text, panel.value());
    auto progress = ui.addWidget(UIWidgetType::Progress, panel.value());
    auto list = ui.addWidget(UIWidgetType::List, panel.value());
    FGL_CHECK(panel && image && text && progress && list);
    FGL_CHECK(ui.setText(text.value(), "Ready"));
    FGL_CHECK(ui.setRange(progress.value(), 0.0F, 100.0F, 1.0F));
    FGL_CHECK(ui.setValue(progress.value(), 75.0F));
    FGL_CHECK(ui.setItems(list.value(), {"Low", "Medium", "High"}));
    auto listProperties = ui.model().find(list.value())->properties;
    listProperties.minimumOffset = {0.0F, 0.0F};
    listProperties.maximumOffset = {100.0F, 90.0F};
    FGL_CHECK(ui.model().setProperties(list.value(), listProperties));
    FGL_CHECK(ui.layout({0.0F, 0.0F, 100.0F, 90.0F}));
    FGL_CHECK(ui.pointerDown({50.0F, 45.0F}));
    FGL_CHECK(ui.pointerUp({50.0F, 45.0F}));
    FGL_CHECK(ui.widget(list.value())->selectedItem == 1U);
    const auto events = ui.consumeEvents();
    FGL_CHECK(hasEvent(events, UIEventType::SelectionChanged, list.value()));
    FGL_CHECK_NEAR(ui.widget(progress.value())->value, 75.0F, 0.0001F);
}

FGL_TEST(ai_state_machine_runs_lifecycle_callbacks_and_ordered_transitions) {
    AiStateMachine machine;
    bool alert = false;
    int idleUpdates = 0;
    int chaseEnters = 0;
    int idleExits = 0;
    AiStateCallbacks idle;
    idle.onUpdate = [&idleUpdates](float) {
        ++idleUpdates;
        return Result<void>::success();
    };
    idle.onExit = [&idleExits]() {
        ++idleExits;
        return Result<void>::success();
    };
    AiStateCallbacks chase;
    chase.onEnter = [&chaseEnters]() {
        ++chaseEnters;
        return Result<void>::success();
    };
    FGL_CHECK(machine.addState("idle", idle));
    FGL_CHECK(machine.addState("chase", chase));
    FGL_CHECK(machine.addTransition({"idle", "chase", [&alert]() { return alert; }}));
    FGL_CHECK(machine.transitionTo("idle"));
    FGL_CHECK(machine.update(0.1F));
    FGL_CHECK(idleUpdates == 1);
    alert = true;
    FGL_CHECK(machine.update(0.1F));
    FGL_CHECK(machine.currentState() == "chase");
    FGL_CHECK(chaseEnters == 1 && idleExits == 1);
    FGL_CHECK(!machine.update(-1.0F));
}

FGL_TEST(waypoint_los_and_racing_behaviors_produce_real_runtime_decisions) {
    WaypointFollower follower;
    WaypointFollowerSettings followerSettings;
    followerSettings.speed = 2.0F;
    followerSettings.arrivalRadius = 0.0F;
    FGL_CHECK(follower.configure({{1.0F, 0.0F}, {2.0F, 0.0F}}, followerSettings));
    auto first = follower.update({}, 0.75F);
    FGL_CHECK(first && first.value().reachedWaypoint && !first.value().finished);
    FGL_CHECK_NEAR(first.value().position.x, 1.5F, 0.0001F);
    auto last = follower.update(first.value().position, 0.25F);
    FGL_CHECK(last && last.value().finished);
    FGL_CHECK_NEAR(last.value().position.x, 2.0F, 0.0001F);

    PhysicsWorld2D world;
    PhysicsBody2D obstacle;
    obstacle.position = {2.0F, 0.0F};
    obstacle.shape = CircleShape{0.5F};
    obstacle.layer = 1U;
    FGL_CHECK(world.addBody(obstacle));
    auto blocked = hasLineOfSight(world, {}, {4.0F, 0.0F}, 1U);
    auto clear = hasLineOfSight(world, {}, {4.0F, 0.0F}, 2U);
    FGL_CHECK(blocked && !blocked.value());
    FGL_CHECK(clear && clear.value());

    RacingBehavior racing;
    RacingBehaviorSettings racingSettings;
    racingSettings.arrivalRadius = 0.25F;
    FGL_CHECK(racing.configure({{0.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 10.0F}},
                               racingSettings));
    auto straight = racing.update({}, 0.0F, 0.0F);
    FGL_CHECK(straight && straight.value().targetWaypoint == 1U);
    FGL_CHECK_NEAR(straight.value().steering, 0.0F, 0.0001F);
    FGL_CHECK(straight.value().throttle > 0.0F);
    auto corner = racing.update({10.0F, 0.0F}, 0.0F, 20.0F);
    FGL_CHECK(corner && corner.value().targetWaypoint == 2U);
    FGL_CHECK(corner.value().steering > 0.9F);
    FGL_CHECK(corner.value().brake > 0.0F);
}

FGL_TEST(ai_behavior_controller_and_door_aware_navigation_cover_gameplay_states) {
    AiBehaviorController2D controller;
    FGL_CHECK(controller.configure({}, {{1.0F, 0.0F}, {2.0F, 0.0F}}));
    FGL_CHECK(controller.state() == AiBehavior2DState::Patrol);
    auto patrol = controller.update({{}, std::nullopt, false, 0.1F});
    FGL_CHECK(patrol && patrol.value().velocity.x > 0.0F);

    auto attack = controller.update({{}, Vec2{0.5F, 0.0F}, true, 0.1F});
    FGL_CHECK(attack && attack.value().state == AiBehavior2DState::Attack);
    FGL_CHECK(attack.value().attack);
    FGL_CHECK(controller.setState(AiBehavior2DState::Flee));
    auto flee = controller.update({{}, Vec2{0.5F, 0.0F}, true, 0.1F});
    FGL_CHECK(flee && flee.value().state == AiBehavior2DState::Flee);
    FGL_CHECK(flee.value().velocity.x < 0.0F);

    FGL_CHECK(controller.setState(AiBehavior2DState::Chase));
    auto chase = controller.update({{}, Vec2{4.0F, 0.0F}, true, 0.1F});
    FGL_CHECK(chase && chase.value().state == AiBehavior2DState::Chase);
    FGL_CHECK(chase.value().velocity.x > 0.0F);
    auto search = controller.update({{}, std::nullopt, false, 0.1F});
    FGL_CHECK(search && search.value().state == AiBehavior2DState::Search);
    auto returnToSpawn = controller.update({{}, std::nullopt, false, 3.0F});
    FGL_CHECK(returnToSpawn &&
              returnToSpawn.value().state == AiBehavior2DState::ReturnToSpawn);
    auto resumedPatrol = controller.update({{}, std::nullopt, false, 0.1F});
    FGL_CHECK(resumedPatrol && resumedPatrol.value().state == AiBehavior2DState::Patrol);

    GridNavigation navigation(3U, 1U);
    const std::vector<NavigationDoor2D> lockedDoors{{{1, 0}, false, "blue"}};
    auto blocked = findDoorAwarePath(navigation, {0, 0}, {2, 0}, lockedDoors);
    FGL_CHECK(!blocked && blocked.error().code() == ErrorCode::NotFound);
    auto unlocked =
        findDoorAwarePath(navigation, {0, 0}, {2, 0}, lockedDoors, {"blue"});
    FGL_CHECK(unlocked && unlocked.value().size() == 3U);
    const std::vector<NavigationDoor2D> openDoors{{{1, 0}, true, {}}};
    FGL_CHECK(findDoorAwarePath(navigation, {0, 0}, {2, 0}, openDoors));
    const std::vector<NavigationDoor2D> duplicateDoors{
        {{1, 0}, true, {}},
        {{1, 0}, false, {}},
    };
    FGL_CHECK(!findDoorAwarePath(navigation, {0, 0}, {2, 0}, duplicateDoors));
}

FGL_TEST(experimental_physics3d_detects_contacts_overlaps_and_filtered_rays) {
    using namespace fabgl::experimental;
    PhysicsWorld3D world;
    PhysicsBody3D box;
    box.shape = AabbShape3D{{1.0F, 1.0F, 1.0F}};
    box.layer = 1U;
    box.collisionMask = 2U;
    auto boxId = world.addBody(box);
    FGL_CHECK(boxId);
    PhysicsBody3D sphere;
    sphere.position = {1.5F, 0.0F, 0.0F};
    sphere.shape = SphereShape3D{1.0F};
    sphere.layer = 2U;
    sphere.collisionMask = 1U;
    auto sphereId = world.addBody(sphere);
    FGL_CHECK(sphereId);

    auto contacts = world.detectContacts();
    FGL_CHECK(contacts && contacts.value().size() == 1U);
    FGL_CHECK(contacts.value()[0].first == boxId.value());
    FGL_CHECK(contacts.value()[0].second == sphereId.value());
    FGL_CHECK_NEAR(contacts.value()[0].penetration, 0.5F, 0.0001F);
    auto ray = world.raycast({-5.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F}, 20.0F, 2U);
    FGL_CHECK(ray && ray.value() && ray.value()->body == sphereId.value());
    FGL_CHECK_NEAR(ray.value()->distance, 5.5F, 0.0001F);
    auto reverseRay =
        world.raycast({5.0F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F}, 20.0F, 1U);
    FGL_CHECK(reverseRay && reverseRay.value() && reverseRay.value()->body == boxId.value());
    FGL_CHECK_NEAR(reverseRay.value()->normal.x, 1.0F, 0.0001F);
    auto overlaps = world.overlap({1.5F, 0.0F, 0.0F}, SphereShape3D{0.75F}, 3U);
    FGL_CHECK(overlaps && overlaps.value().size() == 2U);
    FGL_CHECK(!world.raycast({}, {}, 1.0F));
    FGL_CHECK(!world.overlap({}, SphereShape3D{-1.0F}));
}

FGL_TEST(physics3d_capsule_plane_character_vehicle_and_debug_paths_are_deterministic) {
    using namespace fabgl::experimental;

    PhysicsWorld3D characterWorld;
    PhysicsBody3D floor;
    floor.shape = PlaneShape3D{{0.0F, 4.0F, 0.0F}};
    auto floorId = characterWorld.addBody(floor);
    FGL_CHECK(floorId);

    PhysicsBody3D capsule;
    capsule.position = {0.0F, 1.0F, 0.0F};
    capsule.shape = CapsuleShape3D{0.5F, 0.5F};
    auto capsuleId = characterWorld.addBody(capsule);
    FGL_CHECK(capsuleId);
    auto contacts = characterWorld.detectContacts();
    FGL_CHECK(contacts && contacts.value().size() == 1U);
    FGL_CHECK(contacts.value()[0].first == floorId.value());
    FGL_CHECK(contacts.value()[0].second == capsuleId.value());
    FGL_CHECK_NEAR(contacts.value()[0].penetration, 0.0F, 0.0001F);

    auto capsuleRay = characterWorld.raycast({0.0F, 3.0F, 0.0F}, {0.0F, -1.0F, 0.0F},
                                             10.0F, 0xFFFFFFFFU, false);
    FGL_CHECK(capsuleRay && capsuleRay.value());
    FGL_CHECK(capsuleRay.value()->body == capsuleId.value());
    FGL_CHECK_NEAR(capsuleRay.value()->distance, 1.0F, 0.0001F);

    CharacterController3DSettings characterSettings;
    characterSettings.gravity = {0.0F, -10.0F, 0.0F};
    auto character = characterWorld.moveCharacter({0.0F, 2.5F, 0.0F}, {}, 0.5F,
                                                   characterSettings);
    FGL_CHECK(character);
    FGL_CHECK(character.value().grounded);
    FGL_CHECK_NEAR(character.value().velocity.y, 0.0F, 0.0001F);
    FGL_CHECK(character.value().position.y >= 1.000F);
    FGL_CHECK(!character.value().touchedBodies.empty());

    const auto debug = characterWorld.debugPrimitives();
    FGL_CHECK(debug.size() == 2U);
    FGL_CHECK(debug[0].shape == PhysicsDebugShape3D::Plane);
    FGL_CHECK_NEAR(debug[0].normal.y, 1.0F, 0.0001F);
    FGL_CHECK(debug[1].shape == PhysicsDebugShape3D::Capsule);

    PhysicsWorld3D kinematicWorld;
    PhysicsBody3D wall;
    wall.position = {2.0F, 1.0F, 0.0F};
    wall.shape = AabbShape3D{{0.5F, 2.0F, 2.0F}};
    FGL_CHECK(kinematicWorld.addBody(wall));
    PhysicsBody3D mover;
    mover.position = {0.0F, 1.0F, 0.0F};
    mover.velocity = {4.0F, 0.0F, 0.0F};
    mover.shape = CapsuleShape3D{0.4F, 0.5F};
    mover.kinematic = true;
    auto moverId = kinematicWorld.addBody(mover);
    FGL_CHECK(moverId);
    auto moved = kinematicWorld.moveKinematic(moverId.value(), {4.0F, 0.0F, 0.0F});
    FGL_CHECK(moved && moved.value().hitWall);
    FGL_CHECK(moved.value().position.x <= 1.101F);
    FGL_CHECK_NEAR(moved.value().velocity.x, 0.0F, 0.0001F);
    FGL_CHECK(!kinematicWorld.moveKinematic(PhysicsBody3DId{999U}, {}));

    PhysicsWorld3D vehicleWorld;
    PhysicsBody3D vehicleWall;
    vehicleWall.position = {0.0F, 0.4F, 3.0F};
    vehicleWall.shape = AabbShape3D{{2.0F, 1.0F, 0.5F}};
    FGL_CHECK(vehicleWorld.addBody(vehicleWall));
    ArcadeVehicle3DSettings vehicleSettings;
    vehicleSettings.acceleration = 10.0F;
    vehicleSettings.drag = 0.0F;
    vehicleSettings.collisionBox = AabbShape3D{{0.8F, 0.4F, 0.8F}};
    ArcadeVehicle3DState vehicle;
    vehicle.position = {0.0F, 0.4F, 0.0F};
    auto driven = vehicleWorld.stepArcadeVehicle(vehicle, 1.0F, 0.0F, 0.0F, 1.0F,
                                                 vehicleSettings);
    FGL_CHECK(driven && driven.value().collided);
    FGL_CHECK(driven.value().position.z <= 1.701F);
    FGL_CHECK_NEAR(driven.value().speed, 0.0F, 0.0001F);

    PhysicsBody3D invalidPlane;
    invalidPlane.shape = PlaneShape3D{{}};
    FGL_CHECK(!vehicleWorld.addBody(invalidPlane));
}
