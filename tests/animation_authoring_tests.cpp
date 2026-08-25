#include "test_harness.h"

#include "fabgl/animation/animation_authoring.h"

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

using namespace fabgl;

namespace {

AnimationClipAsset clipAsset() {
    AnimationClipAsset asset;
    asset.guid = AssetGuid::fromStableName("test.animation.walk");
    asset.name = "Yuruyus \"Clip\"\nV1";
    asset.durationSeconds = 2.0F;
    asset.looping = true;

    AnimationCurve position;
    FGL_CHECK(position.addKey({2.0F, 20.0F, 0.0F, 0.0F, CurveInterpolation::Linear}));
    FGL_CHECK(position.addKey({0.0F, 0.0F, 0.0F, 0.0F, CurveInterpolation::Linear}));
    FGL_CHECK(position.addKey({1.0F, 10.0F, 1.0F, 1.0F,
                               CurveInterpolation::CubicHermite}));
    asset.tracks.emplace("transform.position.x", std::move(position));

    AnimationCurve visible;
    FGL_CHECK(visible.addKey({0.0F, 1.0F, 0.0F, 0.0F, CurveInterpolation::Step}));
    FGL_CHECK(visible.addKey({2.0F, 0.0F, 0.0F, 0.0F, CurveInterpolation::Linear}));
    asset.tracks.emplace("sprite.visible", std::move(visible));
    asset.events = {{1.5F, "footstep.right"}, {0.5F, "footstep.left"}};
    return asset;
}

std::shared_ptr<const AnimationClip> runtimeClip(std::string name, const float first,
                                                 const float last) {
    AnimationCurve curve;
    FGL_CHECK(curve.addKey({0.0F, first}));
    FGL_CHECK(curve.addKey({1.0F, last}));
    auto clip = std::make_shared<AnimationClip>(std::move(name), 1.0F, true);
    FGL_CHECK(clip->addTrack("position.x", std::move(curve)));
    return clip;
}

AnimatorControllerAsset controllerAsset() {
    const auto idleClip = AssetGuid::fromStableName("test.animation.idle");
    const auto runClip = AssetGuid::fromStableName("test.animation.run");
    AnimatorControllerAsset asset;
    asset.guid = AssetGuid::fromStableName("test.controller.player");
    asset.name = "Player Controller";
    asset.initialState = "idle";
    asset.parameters.emplace(
        "speed", AnimatorParameterDefinition{AnimatorParameterType::Float, false, 0, 0.75F});
    asset.parameters.emplace(
        "armed", AnimatorParameterDefinition{AnimatorParameterType::Boolean, true, 0, 0.0F});
    asset.parameters.emplace(
        "go", AnimatorParameterDefinition{AnimatorParameterType::Trigger, false, 0, 0.0F});
    asset.parameters.emplace(
        "lives", AnimatorParameterDefinition{AnimatorParameterType::Integer, false, 2, 0.0F});
    asset.states.emplace("run", AnimatorStateDefinition{runClip});
    asset.states.emplace("idle", AnimatorStateDefinition{idleClip});
    asset.states.emplace("idle_copy", AnimatorStateDefinition{idleClip});

    AnimatorTransitionDefinition start;
    start.fromState = "idle";
    start.toState = "run";
    start.blendDurationSeconds = 0.4F;
    start.conditions = {
        {"go", AnimationConditionMode::TriggerSet, false, 0, 0.0F},
        {"speed", AnimationConditionMode::FloatGreater, false, 0, 0.5F},
        {"armed", AnimationConditionMode::BooleanEquals, true, 0, 0.0F},
        {"lives", AnimationConditionMode::IntegerGreater, false, 1, 0.0F},
    };
    asset.transitions.push_back(std::move(start));

    AnimatorTransitionDefinition returnToIdle;
    returnToIdle.fromState = "run";
    returnToIdle.toState = "idle";
    returnToIdle.minimumNormalizedTime = 0.25F;
    returnToIdle.hasExitTime = true;
    returnToIdle.exitTime = 1.0F;
    returnToIdle.blendDurationSeconds = 0.1F;
    asset.transitions.push_back(std::move(returnToIdle));
    return asset;
}

std::string replaceFirst(std::string text, const std::string_view from,
                         const std::string_view to) {
    const auto position = text.find(from);
    FGL_CHECK(position != std::string::npos);
    text.replace(position, from.size(), to);
    return text;
}

} // namespace

FGL_TEST(animation_clip_v1_round_trip_is_canonical_and_builds_the_existing_runtime) {
    const auto asset = clipAsset();
    const auto copy = asset;
    auto first = serializeAnimationClipAsset(asset);
    auto copied = serializeAnimationClipAsset(copy);
    FGL_CHECK(first && copied);
    FGL_CHECK(first.value() == copied.value());
    FGL_CHECK(first.value().find("fglanim 1\n") == 0U);
    FGL_CHECK(first.value().find("event 0.5") < first.value().find("event 1.5"));

    auto loaded = deserializeAnimationClipAsset(first.value());
    FGL_CHECK(loaded);
    auto second = serializeAnimationClipAsset(loaded.value());
    FGL_CHECK(second);
    FGL_CHECK(second.value() == first.value());
    FGL_CHECK(loaded.value().guid == asset.guid);
    FGL_CHECK(loaded.value().tracks.size() == 2U);
    FGL_CHECK(loaded.value().events.size() == 2U);

    auto runtime = buildAnimationClip(loaded.value());
    FGL_CHECK(runtime);
    FGL_CHECK(runtime.value()->valid());
    FGL_CHECK(runtime.value()->looping());
    FGL_CHECK_NEAR(runtime.value()->sample(0.5F).at("transform.position.x"), 5.0F,
                   0.0001F);
    const auto events = runtime.value()->eventsCrossed(0.0F, 1.6F);
    FGL_CHECK(events.size() == 2U);
    FGL_CHECK(events[0] == "footstep.left");
    FGL_CHECK(events[1] == "footstep.right");
}

FGL_TEST(animator_controller_v1_round_trip_preserves_typed_parameters_and_transition_priority) {
    const auto asset = controllerAsset();
    auto first = serializeAnimatorControllerAsset(asset);
    FGL_CHECK(first);
    FGL_CHECK(first.value().find("fglcontroller 1\n") == 0U);
    FGL_CHECK(first.value().find("parameter \"armed\"") <
              first.value().find("parameter \"speed\""));
    FGL_CHECK(first.value().find("condition \"armed\"") <
              first.value().find("condition \"speed\""));

    auto loaded = deserializeAnimatorControllerAsset(first.value());
    FGL_CHECK(loaded);
    auto second = serializeAnimatorControllerAsset(loaded.value());
    FGL_CHECK(second);
    FGL_CHECK(second.value() == first.value());
    FGL_CHECK(loaded.value().guid == asset.guid);
    FGL_CHECK(loaded.value().parameters.size() == 4U);
    FGL_CHECK(loaded.value().states.at("run").clip ==
              AssetGuid::fromStableName("test.animation.run"));
    FGL_CHECK(loaded.value().transitions.size() == 2U);
    FGL_CHECK(loaded.value().transitions[0].fromState == "idle");
    FGL_CHECK(loaded.value().transitions[1].fromState == "run");
}

FGL_TEST(animator_controller_build_requires_and_deduplicates_explicit_clip_resolution) {
    const auto asset = controllerAsset();
    FGL_CHECK(!buildAnimatorController(asset, {}));

    std::map<AssetGuid, std::shared_ptr<const AnimationClip>> clips;
    clips.emplace(AssetGuid::fromStableName("test.animation.idle"),
                  runtimeClip("Idle", 0.0F, 10.0F));
    clips.emplace(AssetGuid::fromStableName("test.animation.run"),
                  runtimeClip("Run", 20.0F, 30.0F));
    auto resolutions = std::size_t{0};
    const AnimationClipResolver resolver = [&clips, &resolutions](const AssetGuid guid) {
        ++resolutions;
        const auto found = clips.find(guid);
        if (found == clips.end()) {
            return Result<std::shared_ptr<const AnimationClip>>::failure(
                Error(ErrorCode::NotFound, "test clip is missing"));
        }
        return Result<std::shared_ptr<const AnimationClip>>::success(found->second);
    };
    auto controller = buildAnimatorController(asset, resolver);
    FGL_CHECK(controller);
    FGL_CHECK(resolutions == 2U);
    FGL_CHECK(controller.value()->currentState() == "idle");

    auto beforeTrigger = controller.value()->update(0.0F);
    FGL_CHECK(beforeTrigger && !beforeTrigger.value().transitioned);
    controller.value()->setTrigger("go");
    auto transitioned = controller.value()->update(0.0F);
    FGL_CHECK(transitioned && transitioned.value().transitioned);
    FGL_CHECK(transitioned.value().state == "run");
    auto blended = controller.value()->update(0.2F);
    FGL_CHECK(blended);
    FGL_CHECK_NEAR(blended.value().blendWeight, 0.5F, 0.0001F);
}

FGL_TEST(animation_authoring_v1_rejects_corruption_unknown_tags_versions_and_bounds) {
    auto clip = serializeAnimationClipAsset(clipAsset());
    FGL_CHECK(clip);
    auto unsupportedClip =
        deserializeAnimationClipAsset(replaceFirst(clip.value(), "fglanim 1", "fglanim 2"));
    FGL_CHECK(!unsupportedClip);
    FGL_CHECK(unsupportedClip.error().code() == ErrorCode::UnsupportedVersion);
    FGL_CHECK(!deserializeAnimationClipAsset(
        replaceFirst(clip.value(), " linear", " unknown_interpolation")));
    FGL_CHECK(!deserializeAnimationClipAsset(
        replaceFirst(clip.value(), "duration 2", "duration -1")));
    FGL_CHECK(!deserializeAnimationClipAsset(clip.value() + "trailing\n"));

    AnimationClipFormatLimits clipSourceLimit;
    clipSourceLimit.maximumSourceBytes = 8U;
    auto boundedClip = deserializeAnimationClipAsset(clip.value(), clipSourceLimit);
    FGL_CHECK(!boundedClip);
    FGL_CHECK(boundedClip.error().code() == ErrorCode::CapacityExceeded);

    auto controller = serializeAnimatorControllerAsset(controllerAsset());
    FGL_CHECK(controller);
    auto unsupportedController = deserializeAnimatorControllerAsset(
        replaceFirst(controller.value(), "fglcontroller 1", "fglcontroller 99"));
    FGL_CHECK(!unsupportedController);
    FGL_CHECK(unsupportedController.error().code() == ErrorCode::UnsupportedVersion);
    FGL_CHECK(!deserializeAnimatorControllerAsset(replaceFirst(
        controller.value(), "parameter \"armed\" boolean 1",
        "parameter \"armed\" vector 1")));
    FGL_CHECK(!deserializeAnimatorControllerAsset(replaceFirst(
        controller.value(), "condition \"speed\" float_greater 0.5",
        "condition \"speed\" boolean_equals 1")));
    FGL_CHECK(!deserializeAnimatorControllerAsset(replaceFirst(
        controller.value(), AssetGuid::fromStableName("test.animation.run").toString(),
        "00000000-0000-0000-0000-000000000000")));
    FGL_CHECK(!deserializeAnimatorControllerAsset(controller.value() + "trailing\n"));
}

FGL_TEST(animator_controller_builder_reports_missing_or_invalid_clip_references) {
    const auto asset = controllerAsset();
    const AnimationClipResolver missing = [](AssetGuid) {
        return Result<std::shared_ptr<const AnimationClip>>::failure(
            Error(ErrorCode::NotFound, "clip was not loaded"));
    };
    auto missingResult = buildAnimatorController(asset, missing);
    FGL_CHECK(!missingResult);
    FGL_CHECK(missingResult.error().code() == ErrorCode::NotFound);

    const AnimationClipResolver nullClip = [](AssetGuid) {
        return Result<std::shared_ptr<const AnimationClip>>::success(nullptr);
    };
    auto nullResult = buildAnimatorController(asset, nullClip);
    FGL_CHECK(!nullResult);
    FGL_CHECK(nullResult.error().code() == ErrorCode::NotFound);

    auto invalidReference = asset;
    invalidReference.states.at("run").clip = AssetGuid{};
    FGL_CHECK(!validateAnimatorControllerAsset(invalidReference));
    FGL_CHECK(!serializeAnimatorControllerAsset(invalidReference));

    auto missingParameter = asset;
    missingParameter.transitions[0].conditions[0].parameter = "undeclared";
    auto invalidCondition = validateAnimatorControllerAsset(missingParameter);
    FGL_CHECK(!invalidCondition);
    FGL_CHECK(invalidCondition.error().code() == ErrorCode::TypeMismatch);
}
