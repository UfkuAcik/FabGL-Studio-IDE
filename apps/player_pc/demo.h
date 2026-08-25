#pragma once

#include <fabgl/core/guid.h>
#include <fabgl/frameworks/fps.h>
#include <fabgl/frameworks/platformer.h>
#include <fabgl/frameworks/racer.h>
#include <fabgl/frameworks/top_down.h>
#include <fabgl/frameworks/tps.h>
#include <fabgl/rendering/framebuffer.h>
#include <fabgl/rendering/lowpoly_renderer.h>
#include <fabgl/rendering/racer_renderer.h>
#include <fabgl/rendering/raycast_renderer.h>
#include <fabgl/rendering/renderer_2d.h>

#include <map>
#include <optional>
#include <string>

namespace fabgl {
class Scene;
}

namespace fabgl::player {

enum class DemoKind {
    Empty,
    Platformer2D,
    TopDown,
    RaycastFps,
    Racer,
    LowPolyExperimental,
    UiShowcase,
    AudioShowcase,
    AnimationShowcase,
    AssetStreaming
};

struct InputState final {
    bool left = false;
    bool right = false;
    bool forward = false;
    bool backward = false;
    bool action = false;
    bool quit = false;
    // Canonical PC controls (Key.*, Mouse.*, Gamepad.*) are kept separately from
    // the legacy demo directions so project input bindings are not hard-coded.
    std::map<std::string, float> controls;
};

class Demo final {
  public:
    Demo(rendering::Framebuffer& framebuffer, DemoKind kind);

    void bindScene(Scene& scene) noexcept;
    void update(float deltaSeconds, const InputState& input) noexcept;
    void render() noexcept;
    [[nodiscard]] std::string title() const;

  private:
    void renderEmpty() noexcept;
    void renderPlatformer() noexcept;
    void renderTopDown() noexcept;
    void renderRaycast() noexcept;
    void renderRacer() noexcept;
    void renderLowPoly() noexcept;
    void renderUiShowcase() noexcept;
    void renderAudioShowcase() noexcept;
    void renderAnimationShowcase() noexcept;
    void renderAssetStreaming() noexcept;
    [[nodiscard]] bool raycastWalkable(Vec2 position) const noexcept;

    rendering::Framebuffer& framebuffer_;
    DemoKind kind_ = DemoKind::Platformer2D;
    rendering::Renderer2D renderer2D_;
    rendering::RaycastRenderer raycastRenderer_;
    rendering::RacerRenderer racerRenderer_;
    rendering::LowPolyRenderer lowPolyRenderer_;
    rendering::Sprite playerSprite_;
    rendering::Tilemap tilemap_;
    rendering::RaycastMap raycastMap_;
    std::vector<rendering::RoadSegment> track_;
    rendering::LowPolyMesh cube_;
    Vec2 playerPosition_{32.0F, 80.0F};
    Vec2 playerVelocity_{};
    frameworks::PlatformerController platformerController_;
    frameworks::PlatformerState platformerState_;
    frameworks::TopDownState topDownState_;
    frameworks::ProjectilePool projectiles_{24U};
    frameworks::Weapon topDownWeapon_;
    frameworks::DoorState fpsDoor_;
    frameworks::VehicleConfig vehicleConfig_;
    frameworks::VehicleState vehicleState_;
    frameworks::ThirdPersonState thirdPersonState_;
    float raycastAngle_ = 0.0F;
    rendering::RaycastCamera raycastCamera_{};
    rendering::RacerCamera racerCamera_{};
    float cubeAngle_ = 0.0F;
    float elapsedSeconds_ = 0.0F;
    int showcaseSelection_ = 0;
    Scene* scene_ = nullptr;
    std::optional<EntityGuid> playerEntity_;
};

[[nodiscard]] DemoKind parseDemoKind(const std::string& value);

} // namespace fabgl::player
