#include "demo.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace fabgl::player {

namespace {

constexpr float Pi = 3.14159265358979323846F;

} // namespace

Demo::Demo(rendering::Framebuffer& framebuffer, DemoKind kind)
    : framebuffer_(framebuffer), kind_(kind), renderer2D_(framebuffer),
      raycastRenderer_(framebuffer), racerRenderer_(framebuffer), lowPolyRenderer_(framebuffer),
      playerSprite_(rendering::makeCheckerSprite(8, 8, {245, 202, 64, 255}, {220, 80, 50, 255})),
      raycastMap_(rendering::makeDemoRaycastMap()), track_(rendering::makeDemoTrack()),
      cube_(rendering::makeDemoCube()) {
    tilemap_.width = 40;
    tilemap_.height = 23;
    tilemap_.tileSize = 8;
    tilemap_.tiles = {rendering::makeCheckerSprite(8, 8, {35, 45, 62, 255}, {39, 50, 68, 255}),
                      rendering::makeCheckerSprite(8, 8, {58, 126, 76, 255}, {50, 108, 64, 255}),
                      rendering::makeCheckerSprite(8, 8, {132, 83, 52, 255}, {112, 69, 44, 255})};
    tilemap_.cells.assign(static_cast<std::size_t>(tilemap_.width * tilemap_.height), 0U);
    for (auto row = 0; row < tilemap_.height; ++row) {
        for (auto column = 0; column < tilemap_.width; ++column) {
            const auto offset = static_cast<std::size_t>(row * tilemap_.width + column);
            if (row >= 18 || (row == 14 && column > 8 && column < 18) ||
                (row == 10 && column > 22 && column < 33)) {
                tilemap_.cells[offset] = row == 18 ? 1U : 2U;
            }
        }
    }
    raycastCamera_.position = {2.5F, 2.5F};
}

void Demo::update(float deltaSeconds, const InputState& input) noexcept {
    const auto delta = std::clamp(deltaSeconds, 0.0F, 0.05F);
    elapsedSeconds_ += delta;
    switch (kind_) {
    case DemoKind::Empty:
        break;
    case DemoKind::Platformer2D: {
        const auto horizontal = (input.right ? 1.0F : 0.0F) - (input.left ? 1.0F : 0.0F);
        playerVelocity_.x = horizontal * 70.0F;
        playerVelocity_.y += 210.0F * delta;
        if (input.action && playerPosition_.y >= 136.0F) {
            playerVelocity_.y = -115.0F;
        }
        playerPosition_ = playerPosition_ + playerVelocity_ * delta;
        if (playerPosition_.y > 136.0F) {
            playerPosition_.y = 136.0F;
            playerVelocity_.y = 0.0F;
        }
        playerPosition_.x =
            std::clamp(playerPosition_.x, 0.0F,
                       static_cast<float>(framebuffer_.width() - playerSprite_.width));
        break;
    }
    case DemoKind::TopDown: {
        const auto horizontal = (input.right ? 1.0F : 0.0F) - (input.left ? 1.0F : 0.0F);
        const auto vertical = (input.backward ? 1.0F : 0.0F) - (input.forward ? 1.0F : 0.0F);
        playerPosition_.x = std::clamp(playerPosition_.x + horizontal * 76.0F * delta, 18.0F,
                                       static_cast<float>(framebuffer_.width() - 26));
        playerPosition_.y = std::clamp(playerPosition_.y + vertical * 76.0F * delta, 18.0F,
                                       static_cast<float>(framebuffer_.height() - 26));
        break;
    }
    case DemoKind::RaycastFps: {
        const auto turn = (input.right ? 1.0F : 0.0F) - (input.left ? 1.0F : 0.0F);
        raycastAngle_ += turn * delta * 1.8F;
        raycastCamera_.direction = {std::cos(raycastAngle_), std::sin(raycastAngle_)};
        const auto movement = (input.forward ? 1.0F : 0.0F) - (input.backward ? 1.0F : 0.0F);
        const auto candidate =
            raycastCamera_.position + raycastCamera_.direction * (movement * delta * 2.2F);
        if (raycastWalkable({candidate.x, raycastCamera_.position.y})) {
            raycastCamera_.position.x = candidate.x;
        }
        if (raycastWalkable({raycastCamera_.position.x, candidate.y})) {
            raycastCamera_.position.y = candidate.y;
        }
        break;
    }
    case DemoKind::Racer: {
        const auto throttle = input.forward ? 1.0F : 0.0F;
        const auto brake = input.backward ? 1.0F : 0.0F;
        racerCamera_.speed +=
            (throttle * 34.0F - brake * 50.0F - racerCamera_.speed * 0.18F) * delta;
        racerCamera_.speed = std::clamp(racerCamera_.speed, 0.0F, 95.0F);
        const auto steering = (input.right ? 1.0F : 0.0F) - (input.left ? 1.0F : 0.0F);
        racerCamera_.lateral =
            std::clamp(racerCamera_.lateral + steering * delta * 1.2F, -1.2F, 1.2F);
        racerCamera_.distance += racerCamera_.speed * delta;
        break;
    }
    case DemoKind::LowPolyExperimental:
        cubeAngle_ += delta * (input.action ? 2.4F : 0.8F);
        break;
    case DemoKind::UiShowcase:
        if (input.action) {
            showcaseSelection_ = (showcaseSelection_ + 1) % 3;
        }
        break;
    case DemoKind::AudioShowcase:
    case DemoKind::AnimationShowcase:
    case DemoKind::AssetStreaming:
        break;
    }
}

void Demo::render() noexcept {
    switch (kind_) {
    case DemoKind::Empty:
        renderEmpty();
        break;
    case DemoKind::Platformer2D:
        renderPlatformer();
        break;
    case DemoKind::TopDown:
        renderTopDown();
        break;
    case DemoKind::RaycastFps:
        renderRaycast();
        break;
    case DemoKind::Racer:
        renderRacer();
        break;
    case DemoKind::LowPolyExperimental:
        renderLowPoly();
        break;
    case DemoKind::UiShowcase:
        renderUiShowcase();
        break;
    case DemoKind::AudioShowcase:
        renderAudioShowcase();
        break;
    case DemoKind::AnimationShowcase:
        renderAnimationShowcase();
        break;
    case DemoKind::AssetStreaming:
        renderAssetStreaming();
        break;
    }
}

std::string Demo::title() const {
    switch (kind_) {
    case DemoKind::Empty:
        return "FabGL Studio - Empty Project";
    case DemoKind::Platformer2D:
        return "FabGL Studio - 2D Platformer";
    case DemoKind::TopDown:
        return "FabGL Studio - Top-Down Arena";
    case DemoKind::RaycastFps:
        return "FabGL Studio - Raycast FPS";
    case DemoKind::Racer:
        return "FabGL Studio - Pseudo-3D Racer";
    case DemoKind::LowPolyExperimental:
        return "FabGL Studio - Low-Poly Experimental";
    case DemoKind::UiShowcase:
        return "FabGL Studio - UI Showcase";
    case DemoKind::AudioShowcase:
        return "FabGL Studio - Audio Mixer Visualization";
    case DemoKind::AnimationShowcase:
        return "FabGL Studio - Animation Showcase";
    case DemoKind::AssetStreaming:
        return "FabGL Studio - Asset Streaming Showcase";
    }
    return "FabGL Studio Player";
}

void Demo::renderEmpty() noexcept {
    framebuffer_.clear({22, 27, 38, 255});
    for (auto x = 0; x < framebuffer_.width(); x += 16) {
        framebuffer_.drawLine(x, 0, x, framebuffer_.height() - 1, {32, 39, 53, 255});
    }
    for (auto y = 0; y < framebuffer_.height(); y += 16) {
        framebuffer_.drawLine(0, y, framebuffer_.width() - 1, y, {32, 39, 53, 255});
    }
    framebuffer_.drawLine(framebuffer_.width() / 2 - 6, framebuffer_.height() / 2,
                          framebuffer_.width() / 2 + 6, framebuffer_.height() / 2,
                          {96, 130, 180, 255});
    framebuffer_.drawLine(framebuffer_.width() / 2, framebuffer_.height() / 2 - 6,
                          framebuffer_.width() / 2, framebuffer_.height() / 2 + 6,
                          {96, 130, 180, 255});
}

void Demo::renderPlatformer() noexcept {
    framebuffer_.clear({29, 39, 58, 255});
    renderer2D_.resetCounters();
    renderer2D_.drawTilemap(tilemap_, {0.0F, 0.0F},
                            {0.0F, 0.0F, static_cast<float>(framebuffer_.width()),
                             static_cast<float>(framebuffer_.height())});
    renderer2D_.draw({&playerSprite_,
                      static_cast<int>(playerPosition_.x),
                      static_cast<int>(playerPosition_.y),
                      1,
                      playerVelocity_.x < 0.0F,
                      false,
                      {255, 255, 255, 255}});
}

void Demo::renderTopDown() noexcept {
    framebuffer_.clear({18, 42, 40, 255});
    for (auto x = 16; x < framebuffer_.width() - 16; x += 24) {
        framebuffer_.drawLine(x, 12, x, framebuffer_.height() - 13, {26, 58, 53, 255});
    }
    for (auto y = 12; y < framebuffer_.height() - 12; y += 24) {
        framebuffer_.drawLine(12, y, framebuffer_.width() - 13, y, {26, 58, 53, 255});
    }
    framebuffer_.drawLine(12, 12, framebuffer_.width() - 13, 12, {87, 126, 91, 255});
    framebuffer_.drawLine(12, framebuffer_.height() - 13, framebuffer_.width() - 13,
                          framebuffer_.height() - 13, {87, 126, 91, 255});
    framebuffer_.drawLine(12, 12, 12, framebuffer_.height() - 13, {87, 126, 91, 255});
    framebuffer_.drawLine(framebuffer_.width() - 13, 12, framebuffer_.width() - 13,
                          framebuffer_.height() - 13, {87, 126, 91, 255});
    for (auto index = 0; index < 5; ++index) {
        const auto angle = elapsedSeconds_ * (0.45F + static_cast<float>(index) * 0.07F) +
                           static_cast<float>(index) * 1.25F;
        const auto radiusX = static_cast<float>(42 + index * 9);
        const auto radiusY = static_cast<float>(28 + index * 5);
        const auto x = framebuffer_.width() / 2 + static_cast<int>(std::cos(angle) * radiusX);
        const auto y = framebuffer_.height() / 2 + static_cast<int>(std::sin(angle) * radiusY);
        framebuffer_.fillRect(x - 4, y - 4, 8, 8, {205, 70, 72, 255});
    }
    framebuffer_.fillRect(static_cast<int>(playerPosition_.x) - 5,
                          static_cast<int>(playerPosition_.y) - 5, 11, 11, {70, 180, 220, 255});
    framebuffer_.drawLine(static_cast<int>(playerPosition_.x), static_cast<int>(playerPosition_.y),
                          static_cast<int>(playerPosition_.x) + 12,
                          static_cast<int>(playerPosition_.y), {245, 220, 110, 255});
}

void Demo::renderRaycast() noexcept {
    const std::vector<rendering::Billboard> billboards = {
        {{4.5F, 4.5F}, {240, 190, 40, 255}, 0.28F}, {{10.0F, 3.5F}, {70, 210, 110, 255}, 0.34F}};
    static_cast<void>(raycastRenderer_.render(raycastMap_, raycastCamera_, billboards));
    framebuffer_.fillRect(framebuffer_.width() / 2 - 4, framebuffer_.height() - 25, 8, 22,
                          {70, 75, 82, 255});
}

void Demo::renderRacer() noexcept {
    static_cast<void>(racerRenderer_.render(track_, racerCamera_));
    const auto carX = framebuffer_.width() / 2 + static_cast<int>(racerCamera_.lateral * 14.0F);
    framebuffer_.fillRect(carX - 10, framebuffer_.height() - 25, 20, 15, {220, 55, 48, 255});
    framebuffer_.fillRect(carX - 7, framebuffer_.height() - 22, 14, 5, {60, 105, 145, 255});
}

void Demo::renderLowPoly() noexcept {
    framebuffer_.clear({24, 28, 38, 255});
    const auto model = Mat4::translation({0.0F, 0.0F, 1.0F}) * Mat4::rotationY(cubeAngle_) *
                       Mat4::rotationX(cubeAngle_ * 0.63F);
    static_cast<void>(lowPolyRenderer_.render(cube_, model, {}));
}

void Demo::renderUiShowcase() noexcept {
    framebuffer_.clear({20, 24, 34, 255});
    framebuffer_.fillRect(18, 14, 284, 152, {36, 43, 58, 255});
    framebuffer_.fillRect(18, 14, 284, 22, {58, 69, 91, 255});
    framebuffer_.fillRect(34, 52, 164, 12, {19, 23, 31, 255});
    framebuffer_.fillRect(35, 53, 117, 10, {66, 184, 111, 255});
    for (auto index = 0; index < 3; ++index) {
        const auto y = 82 + index * 24;
        const auto selected = index == showcaseSelection_;
        framebuffer_.fillRect(34, y, 116, 17,
                              selected ? Color{65, 128, 214, 255} : Color{49, 58, 76, 255});
        framebuffer_.fillRect(42, y + 6, 56 + index * 13, 4, {205, 215, 232, 255});
    }
    framebuffer_.fillRect(218, 54, 64, 64, {27, 32, 43, 255});
    framebuffer_.fillRect(231, 67, 38, 38, {155, 93, 210, 255});
    framebuffer_.fillRect(218, 132, 64, 14, {220, 155, 55, 255});
}

void Demo::renderAudioShowcase() noexcept {
    framebuffer_.clear({17, 20, 29, 255});
    for (auto channel = 0; channel < 8; ++channel) {
        const auto x = 34 + channel * 33;
        const auto channelPhase = static_cast<float>(channel);
        const auto wave =
            (std::sin(elapsedSeconds_ * (1.4F + channelPhase * 0.13F) + channelPhase) + 1.0F) *
            0.5F;
        const auto level = 18 + static_cast<int>(wave * 105.0F);
        framebuffer_.fillRect(x, 24, 18, 126, {31, 38, 51, 255});
        framebuffer_.fillRect(x + 2, 148 - level, 14, level,
                              level > 98 ? Color{226, 82, 64, 255} : Color{70, 191, 123, 255});
        framebuffer_.fillRect(x - 2, 154, 22, 8, {72, 84, 108, 255});
    }
    framebuffer_.drawLine(24, 50, 296, 50, {68, 75, 91, 255});
    framebuffer_.drawLine(24, 100, 296, 100, {68, 75, 91, 255});
}

void Demo::renderAnimationShowcase() noexcept {
    framebuffer_.clear({31, 30, 50, 255});
    const auto phase = elapsedSeconds_ * 2.0F;
    const auto x = 150 + static_cast<int>(std::sin(phase) * 105.0F);
    const auto y = 78 + static_cast<int>(std::sin(phase * 2.0F) * 30.0F);
    const auto squash = 10 + static_cast<int>((std::sin(phase * 4.0F) + 1.0F) * 3.0F);
    framebuffer_.fillRect(x - squash, y - (20 - squash / 2), squash * 2, 40 - squash,
                          {235, 141, 72, 255});
    framebuffer_.fillRect(x - 5, y - 3, 3, 3, {20, 25, 35, 255});
    framebuffer_.fillRect(x + 3, y - 3, 3, 3, {20, 25, 35, 255});
    framebuffer_.drawLine(24, 139, 295, 139, {105, 96, 139, 255});
    for (auto key = 0; key < 9; ++key) {
        const auto keyX = 30 + key * 32;
        framebuffer_.fillRect(keyX, 135, 5, 9,
                              key == static_cast<int>(elapsedSeconds_ * 4.0F) % 9
                                  ? Color{247, 205, 75, 255}
                                  : Color{129, 112, 167, 255});
    }
}

void Demo::renderAssetStreaming() noexcept {
    framebuffer_.clear({15, 25, 34, 255});
    const auto active = static_cast<int>(elapsedSeconds_ * 1.5F) % 12;
    for (auto row = 0; row < 3; ++row) {
        for (auto column = 0; column < 4; ++column) {
            const auto index = row * 4 + column;
            const auto x = 22 + column * 74;
            const auto y = 24 + row * 47;
            const auto resident = index <= active;
            framebuffer_.fillRect(x, y, 58, 34,
                                  resident ? Color{54, 117, 146, 255} : Color{36, 47, 59, 255});
            framebuffer_.fillRect(x + 5, y + 5, resident ? 42 : 9, 5,
                                  resident ? Color{112, 205, 185, 255} : Color{77, 83, 94, 255});
            framebuffer_.fillRect(x + 5, y + 16, resident ? 30 : 16, 4,
                                  resident ? Color{231, 184, 85, 255} : Color{77, 83, 94, 255});
        }
    }
    const auto progress = static_cast<int>((static_cast<float>(active + 1) / 12.0F) * 276.0F);
    framebuffer_.fillRect(22, 164, 276, 5, {36, 47, 59, 255});
    framebuffer_.fillRect(22, 164, progress, 5, {80, 184, 129, 255});
}

bool Demo::raycastWalkable(Vec2 position) const noexcept {
    return raycastMap_.cell(static_cast<int>(position.x), static_cast<int>(position.y)) == 0U;
}

DemoKind parseDemoKind(const std::string& value) {
    if (value == "empty") {
        return DemoKind::Empty;
    }
    if (value == "2d" || value == "platformer") {
        return DemoKind::Platformer2D;
    }
    if (value == "topdown" || value == "top-down") {
        return DemoKind::TopDown;
    }
    if (value == "raycast" || value == "fps") {
        return DemoKind::RaycastFps;
    }
    if (value == "racer") {
        return DemoKind::Racer;
    }
    if (value == "lowpoly" || value == "tps") {
        return DemoKind::LowPolyExperimental;
    }
    if (value == "ui") {
        return DemoKind::UiShowcase;
    }
    if (value == "audio") {
        return DemoKind::AudioShowcase;
    }
    if (value == "animation") {
        return DemoKind::AnimationShowcase;
    }
    if (value == "streaming") {
        return DemoKind::AssetStreaming;
    }
    throw std::invalid_argument("unknown demo: " + value);
}

} // namespace fabgl::player
