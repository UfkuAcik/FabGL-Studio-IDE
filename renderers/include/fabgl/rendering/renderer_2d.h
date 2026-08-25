#pragma once

#include <fabgl/core/guid.h>
#include <fabgl/material/material.h>
#include <fabgl/math/types.h>
#include <fabgl/rendering/framebuffer.h>

#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace fabgl::rendering {

struct Sprite final {
    int width = 0;
    int height = 0;
    std::vector<Color> pixels;
    // Indexed sprites avoid expanding palette assets on constrained targets. When populated,
    // indices take precedence over pixels and are resolved through SpriteDraw::material.
    std::vector<std::uint8_t> indices;

    [[nodiscard]] bool valid() const noexcept;
};

struct SpriteDraw final {
    const Sprite* sprite = nullptr;
    int x = 0;
    int y = 0;
    int scale = 1;
    bool flipX = false;
    bool flipY = false;
    Color tint{255, 255, 255, 255};
    const Material* material = nullptr;
    // A zero-sized source region means the complete sprite. Keeping these fields at the end
    // preserves source compatibility with the original aggregate command.
    Rect sourceRegion{};
    float rotationDegrees = 0.0F;
};

struct SpriteAnimationFrame final {
    Rect sourceRegion{};
    float durationSeconds = 0.1F;
};

struct SpriteAnimationClip final {
    const Sprite* atlas = nullptr;
    std::vector<SpriteAnimationFrame> frames;
    bool loop = true;

    [[nodiscard]] bool valid() const noexcept;
};

class SpriteAnimator final {
  public:
    void reset() noexcept;
    void update(const SpriteAnimationClip& clip, float deltaSeconds) noexcept;
    [[nodiscard]] std::size_t frameIndex() const noexcept {
        return frameIndex_;
    }
    [[nodiscard]] bool finished() const noexcept {
        return finished_;
    }
    [[nodiscard]] SpriteDraw draw(const SpriteAnimationClip& clip, int x, int y,
                                  int scale = 1) const noexcept;

  private:
    std::size_t frameIndex_ = 0U;
    float elapsed_ = 0.0F;
    bool finished_ = false;
};

struct LayeredSpriteDraw final {
    SpriteDraw command{};
    int sortingLayer = 0;
    int zOrder = 0;
    Vec2 parallax{1.0F, 1.0F};
    bool uiOverlay = false;
};

struct BitmapFont final {
    const Sprite* atlas = nullptr;
    std::uint32_t firstCodepoint = 32U;
    std::uint32_t glyphCount = 0U;
    int glyphWidth = 0;
    int glyphHeight = 0;
    int columns = 0;
    int horizontalSpacing = 0;

    [[nodiscard]] bool valid() const noexcept;
};

enum class TilemapLayerKind : std::uint8_t { Tiles, Collision, Objects };

struct TilemapLayer final {
    std::vector<std::uint16_t> cells;
    TilemapLayerKind kind = TilemapLayerKind::Tiles;
    Vec2 parallax{1.0F, 1.0F};
    std::uint8_t opacity = 255U;
    bool visible = true;
};

struct TilemapObject final {
    TilemapObject() = default;
    TilemapObject(const std::uint32_t objectId, const std::uint16_t objectLayer,
                  const std::uint16_t objectType, const Rect objectBounds,
                  const AssetGuid objectAsset = {})
        : id(objectId), layer(objectLayer), type(objectType), bounds(objectBounds),
          asset(objectAsset) {}

    std::uint32_t id = 0U;
    std::uint16_t layer = 0U;
    std::uint16_t type = 0U;
    Rect bounds{};
    AssetGuid asset{};
};

struct TileAnimation final {
    TileAnimation() = default;
    TileAnimation(const std::uint16_t source, std::vector<std::uint16_t> animationFrames,
                  const float seconds)
        : sourceTile(source), frames(std::move(animationFrames)), frameSeconds(seconds) {}

    std::uint16_t sourceTile = 0U;
    std::vector<std::uint16_t> frames;
    float frameSeconds = 0.1F;
    std::vector<float> frameDurationsSeconds;
};

struct TilemapChunk final {
    std::uint16_t layer = 0U;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct Tilemap final {
    int width = 0;
    int height = 0;
    int tileSize = 8;
    std::vector<std::uint16_t> cells;
    std::vector<Sprite> tiles;
    std::vector<TilemapLayer> layers;
    std::vector<TilemapObject> objects;
    std::vector<TileAnimation> animations;
    std::vector<TilemapChunk> chunks;
    std::vector<std::uint16_t> solidTiles;
    int chunkSize = 16;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::uint16_t tileAt(std::size_t layer, int x, int y,
                                       float elapsedSeconds = 0.0F) const noexcept;
    [[nodiscard]] bool collides(int x, int y) const noexcept;
    [[nodiscard]] std::vector<const TilemapObject*> objectsIn(Rect area,
                                                              std::size_t maximum = 64U) const;
};

struct TilemapDrawStats final {
    std::uint32_t layers = 0U;
    std::uint32_t chunks = 0U;
    std::uint32_t tiles = 0U;
    std::uint32_t culledTiles = 0U;
};

class Renderer2D final {
  public:
    explicit Renderer2D(Framebuffer& framebuffer, std::size_t maximumQueuedSprites = 4096U)
        : framebuffer_(framebuffer), maximumQueuedSprites_(maximumQueuedSprites) {}

    void draw(const SpriteDraw& command) noexcept;
    [[nodiscard]] bool submit(const LayeredSpriteDraw& command) noexcept;
    void flush(Vec2 camera, Rect viewport) noexcept;
    void drawText(const BitmapFont& font, std::string_view text, int x, int y,
                  Color tint = {255U, 255U, 255U, 255U}, int scale = 1) noexcept;
    void drawTilemap(const Tilemap& map, Vec2 camera, Rect viewport,
                     float elapsedSeconds = 0.0F) noexcept;
    [[nodiscard]] TilemapDrawStats drawTilemapDetailed(const Tilemap& map, Vec2 camera,
                                                       Rect viewport,
                                                       float elapsedSeconds = 0.0F) noexcept;

    [[nodiscard]] std::uint32_t drawCalls() const noexcept {
        return drawCalls_;
    }
    [[nodiscard]] std::uint32_t spritesSubmitted() const noexcept {
        return spritesSubmitted_;
    }
    [[nodiscard]] std::uint32_t spritesCulled() const noexcept {
        return spritesCulled_;
    }
    [[nodiscard]] std::size_t queuedSprites() const noexcept {
        return queue_.size();
    }
    void resetCounters() noexcept;

  private:
    struct QueuedSprite final {
        LayeredSpriteDraw command{};
        std::uint64_t sequence = 0U;
    };

    [[nodiscard]] static Color tint(Color source, Color tintColor) noexcept;
    void drawMaterial(const SpriteDraw& command) noexcept;
    [[nodiscard]] static Rect sourceRegion(const SpriteDraw& command) noexcept;
    [[nodiscard]] static Rect destinationBounds(const SpriteDraw& command) noexcept;

    Framebuffer& framebuffer_;
    std::uint32_t drawCalls_ = 0;
    std::uint32_t spritesSubmitted_ = 0;
    std::uint32_t spritesCulled_ = 0;
    std::size_t maximumQueuedSprites_ = 4096U;
    std::uint64_t nextSequence_ = 0U;
    std::vector<QueuedSprite> queue_;
};

[[nodiscard]] Sprite makeCheckerSprite(int width, int height, Color first, Color second);

} // namespace fabgl::rendering
