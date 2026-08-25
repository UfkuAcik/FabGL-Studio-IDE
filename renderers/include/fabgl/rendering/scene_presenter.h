#pragma once

#include <fabgl/core/guid.h>
#include <fabgl/rendering/framebuffer.h>
#include <fabgl/rendering/lowpoly_renderer.h>
#include <fabgl/rendering/racer_renderer.h>
#include <fabgl/rendering/raycast_renderer.h>
#include <fabgl/rendering/renderer_2d.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fabgl {
class Scene;
class SceneRuntime;
} // namespace fabgl

namespace fabgl::rendering {

enum class ScenePresentationMode : std::uint8_t {
    TwoDimensional,
    Raycast,
    Racer,
    LowPoly,
};

struct ScenePresentationResources final {
    std::function<std::shared_ptr<const Sprite>(AssetGuid)> sprite;
    std::function<std::shared_ptr<const Material>(AssetGuid)> material;
    std::function<std::shared_ptr<const Tilemap>(AssetGuid)> tilemap;
    std::function<std::shared_ptr<const RaycastMap>(AssetGuid)> raycastMap;
    std::function<std::shared_ptr<const RacerTrackAsset>(AssetGuid)> racerTrack;
    std::function<std::shared_ptr<const LowPolyMesh>(AssetGuid)> mesh;
};

struct ScenePresentationStats final {
    ScenePresentationMode mode = ScenePresentationMode::TwoDimensional;
    std::uint32_t activeEntities = 0U;
    std::uint32_t drawCalls = 0U;
    std::uint32_t sprites = 0U;
    std::uint32_t tiles = 0U;
    std::uint32_t rays = 0U;
    std::uint32_t triangles = 0U;
    std::uint32_t particles = 0U;
    std::uint32_t uiWidgets = 0U;
    std::uint32_t uiGlyphs = 0U;
    std::uint32_t missingAssets = 0U;
    std::vector<AssetGuid> unresolvedAssets;
};

// Converts reflected scene components into renderer commands. Both the PC
// player and Studio Game View use this class; project preview selection is
// therefore data-driven and does not depend on a hard-coded demo name.
class ScenePresenter final {
  public:
    explicit ScenePresenter(Framebuffer& framebuffer, ScenePresentationResources resources = {});

    [[nodiscard]] ScenePresentationStats render(const Scene& scene,
                                                const SceneRuntime* runtime = nullptr,
                                                float elapsedSeconds = 0.0F) noexcept;
    void setResources(ScenePresentationResources resources);

  private:
    Framebuffer* framebuffer_ = nullptr;
    ScenePresentationResources resources_;
    Renderer2D renderer2D_;
    RaycastRenderer raycastRenderer_;
    RacerRenderer racerRenderer_;
    LowPolyRenderer lowPolyRenderer_;
    Sprite placeholderSprite_;
    Tilemap placeholderTilemap_;
    RaycastMap placeholderRaycastMap_;
    std::vector<RoadSegment> placeholderTrack_;
    LowPolyMesh placeholderMesh_;
};

[[nodiscard]] const char* scenePresentationModeName(ScenePresentationMode mode) noexcept;

} // namespace fabgl::rendering
