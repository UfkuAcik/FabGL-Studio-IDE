#include "test_harness.h"

#include "fabgl/input/input_map.h"
#include "fabgl/runtime/engine_loop.h"

#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace fabgl;

FGL_TEST(engine_loop_runs_all_phases_with_fixed_accumulation_and_metrics) {
    EngineLoop loop({0.1, 0.5, 2});
    std::vector<std::string> phases;
    int shutdownCount = 0;
    EngineLoopCallbacks callbacks;
    callbacks.initialize = [&phases] {
        phases.push_back("initialize");
        return Result<void>::success();
    };
    callbacks.loadResources = [&phases] {
        phases.push_back("resources");
        return Result<void>::success();
    };
    callbacks.loadScene = [&phases] {
        phases.push_back("scene");
        return Result<void>::success();
    };
    callbacks.fixedUpdate = [&phases](double delta) {
        FGL_CHECK_NEAR(delta, 0.1, 0.00001F);
        phases.push_back("fixed");
        return Result<void>::success();
    };
    callbacks.physicsUpdate = [&phases](double) {
        phases.push_back("physics");
        return Result<void>::success();
    };
    callbacks.variableUpdate = [&phases](double) {
        phases.push_back("variable");
        return Result<void>::success();
    };
    callbacks.animationUpdate = [&phases](double) {
        phases.push_back("animation");
        return Result<void>::success();
    };
    callbacks.audioUpdate = [&phases](double) {
        phases.push_back("audio");
        return Result<void>::success();
    };
    callbacks.renderSubmission = [&phases](double alpha) {
        FGL_CHECK(alpha >= 0.0 && alpha < 1.0);
        phases.push_back("submit");
        return Result<void>::success();
    };
    callbacks.render = [&phases] {
        phases.push_back("render");
        return Result<void>::success();
    };
    callbacks.present = [&phases] {
        phases.push_back("present");
        return Result<void>::success();
    };
    callbacks.shutdown = [&shutdownCount] { ++shutdownCount; };
    loop.setCallbacks(std::move(callbacks));

    FGL_CHECK(loop.initialize());
    auto first = loop.tick(0.25);
    FGL_CHECK(first);
    FGL_CHECK(first.value().frameIndex == 1);
    FGL_CHECK(first.value().fixedUpdateCount == 2);
    FGL_CHECK(!first.value().catchUpLimited);
    FGL_CHECK_NEAR(first.value().accumulatorSeconds, 0.05, 0.00001F);
    FGL_CHECK(first.value().measuredCpuSeconds >= 0.0);
    const std::vector<std::string> expected{
        "initialize", "resources", "scene", "fixed",  "physics", "fixed",  "physics",
        "variable",   "animation", "audio", "submit", "render",  "present"};
    FGL_CHECK(phases == expected);

    auto limited = loop.tick(0.55);
    FGL_CHECK(limited);
    FGL_CHECK(limited.value().frameDeltaClamped);
    FGL_CHECK(limited.value().catchUpLimited);
    FGL_CHECK(limited.value().fixedUpdateCount == 2);
    FGL_CHECK(limited.value().droppedFixedUpdateCount == 3);
    FGL_CHECK_NEAR(limited.value().interpolationAlpha, 0.5, 0.0001F);
    loop.shutdown();
    loop.shutdown();
    FGL_CHECK(shutdownCount == 1);
}

FGL_TEST(engine_loop_validates_state_config_and_phase_failures) {
    EngineLoop loop;
    FGL_CHECK(!loop.tick(0.1));
    FGL_CHECK(!loop.setConfig({0.0, 1.0, 1}));
    FGL_CHECK(loop.setConfig({0.02, 1.0, 1}));
    EngineLoopCallbacks callbacks;
    callbacks.loadScene = [] {
        return Result<void>::failure(Error(ErrorCode::NotFound, "scene missing"));
    };
    loop.setCallbacks(std::move(callbacks));
    auto initialized = loop.initialize();
    FGL_CHECK(!initialized);
    FGL_CHECK(initialized.error().context().back().value == "scene_loading");
}

FGL_TEST(input_map_tracks_actions_axes_rebinding_and_edges) {
    InputMap input;
    FGL_CHECK(input.defineContext("gameplay", 0));
    FGL_CHECK(input.bindAction("gameplay", "Jump", {"Key.Space", 1.0F, 0.5F}));
    FGL_CHECK(input.bindAxis("gameplay", "MoveX", {"Key.A", -1.0F, 0.5F}));
    FGL_CHECK(input.bindAxis("gameplay", "MoveX", {"Key.D", 1.0F, 0.5F}));

    input.update();
    FGL_CHECK(!input.action("Jump").held);
    FGL_CHECK(input.setControlValue("Key.Space", 1.0F));
    FGL_CHECK(input.setControlValue("Key.D", 0.75F));
    input.update();
    FGL_CHECK(input.action("Jump").held);
    FGL_CHECK(input.action("Jump").pressed);
    FGL_CHECK(!input.action("Jump").released);
    FGL_CHECK_NEAR(input.axis("MoveX"), 0.75F, 0.0001F);
    input.update();
    FGL_CHECK(input.action("Jump").held && !input.action("Jump").pressed);

    FGL_CHECK(input.setControlValue("Key.Space", 0.0F));
    input.update();
    FGL_CHECK(input.action("Jump").released);
    FGL_CHECK(input.rebindAction("gameplay", "Jump", 0, {"Key.J", 1.0F, 0.5F}));
    FGL_CHECK(input.setControlValue("Key.J", 1.0F));
    input.update();
    FGL_CHECK(input.action("Jump").pressed);

    FGL_CHECK(input.setControlValue("Key.A", 1.0F));
    FGL_CHECK(input.setControlValue("Key.D", 1.0F));
    input.update();
    FGL_CHECK_NEAR(input.axis("MoveX"), 0.0F, 0.0001F);
}

FGL_TEST(input_context_priority_overrides_lower_context_and_can_be_disabled) {
    InputMap input;
    FGL_CHECK(input.defineContext("gameplay", 0));
    FGL_CHECK(input.defineContext("menu", 10, false));
    FGL_CHECK(input.bindAction("gameplay", "Accept", {"Key.Enter", 1.0F, 0.5F}));
    FGL_CHECK(input.bindAction("menu", "Accept", {"Key.Space", 1.0F, 0.5F}));
    FGL_CHECK(input.setControlValue("Key.Enter", 1.0F));
    input.update();
    FGL_CHECK(input.action("Accept").held);

    FGL_CHECK(input.setContextEnabled("menu", true));
    input.update();
    FGL_CHECK(input.action("Accept").released);
    FGL_CHECK(input.setControlValue("Key.Space", 1.0F));
    input.update();
    FGL_CHECK(input.action("Accept").pressed);
    FGL_CHECK(input.setContextEnabled("menu", false));
    input.update();
    FGL_CHECK(input.action("Accept").held);
    FGL_CHECK(!input.rebindAxis("missing", "Move", 0, {"Key.A"}));
    FGL_CHECK(!input.setControlValue("bad", std::numeric_limits<float>::infinity()));
}
