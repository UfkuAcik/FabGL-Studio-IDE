#include <fabgl/rendering/scene_presenter.h>

#include <fabgl/runtime/scene_runtime.h>
#include <fabgl/scene/builtin_components.h>
#include <fabgl/scene/entity.h>
#include <fabgl/scene/scene.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <tuple>
#include <utility>

namespace fabgl::rendering {
namespace {

[[nodiscard]] ComponentTypeGuid builtinTypeId(const char* shortName) {
    return ComponentTypeGuid::fromStableName(std::string("fabgl.component.") + shortName + ".v1");
}

[[nodiscard]] const DataComponent* component(const Entity& entity, const char* shortName) noexcept {
    return dynamic_cast<const DataComponent*>(entity.getComponent(builtinTypeId(shortName)));
}

template <typename Type>
[[nodiscard]] Type property(const DataComponent& value, const char* name, Type fallback) noexcept {
    auto result = value.get(name);
    if (!result)
        return fallback;
    const auto* typed = std::get_if<Type>(&result.value());
    return typed == nullptr ? fallback : *typed;
}

[[nodiscard]] bool enabled(const DataComponent* value) noexcept {
    return value != nullptr && value->activeAndEnabled() && property(*value, "enabled", true);
}

[[nodiscard]] Vec3 worldPosition(const Scene& scene, const Entity& entity) noexcept {
    auto transform = scene.worldTransform(entity.id());
    return transform ? transform.value().transformPoint({}) : entity.transform().localPosition();
}

[[nodiscard]] Color guidColor(const EntityGuid guid) noexcept {
    const auto& bytes = guid.bytes();
    return {static_cast<std::uint8_t>(72U + bytes[2] % 160U),
            static_cast<std::uint8_t>(72U + bytes[7] % 160U),
            static_cast<std::uint8_t>(72U + bytes[13] % 160U), 255U};
}

[[nodiscard]] bool nonZero(AssetGuid guid) noexcept {
    return guid != AssetGuid{};
}

void recordMissing(ScenePresentationStats& stats, AssetGuid guid) {
    if (!nonZero(guid))
        return;
    ++stats.missingAssets;
    if (std::find(stats.unresolvedAssets.begin(), stats.unresolvedAssets.end(), guid) ==
        stats.unresolvedAssets.end()) {
        stats.unresolvedAssets.push_back(guid);
    }
}

[[nodiscard]] Color cameraClear(const Scene& scene) noexcept {
    for (const auto* entity : scene.entities()) {
        const auto* camera = component(*entity, "Camera");
        if (entity->active() && enabled(camera))
            return property(*camera, "clearColor", Color{18U, 24U, 34U, 255U});
    }
    return {18U, 24U, 34U, 255U};
}

[[nodiscard]] const Entity* firstWith(const Scene& scene, const char* shortName) noexcept {
    for (const auto* entity : scene.entities()) {
        if (entity->active() && enabled(component(*entity, shortName)))
            return entity;
    }
    return nullptr;
}

struct PixelRect final {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    [[nodiscard]] int width() const noexcept {
        return right - left;
    }
    [[nodiscard]] int height() const noexcept {
        return bottom - top;
    }
    [[nodiscard]] bool valid() const noexcept {
        return right > left && bottom > top;
    }
};

[[nodiscard]] PixelRect pixelRect(const Rect value) noexcept {
    return {static_cast<int>(std::floor(value.x)), static_cast<int>(std::floor(value.y)),
            static_cast<int>(std::ceil(value.x + value.width)),
            static_cast<int>(std::ceil(value.y + value.height))};
}

[[nodiscard]] bool effectivelyVisible(const RuntimeUI& ui, UIElementId id) noexcept {
    const UIElement* element = ui.model().find(id);
    std::size_t remaining = 1024U;
    while (element != nullptr && remaining-- > 0U) {
        if (!element->properties.visible)
            return false;
        element = element->parent ? ui.model().find(*element->parent) : nullptr;
    }
    return remaining > 0U;
}

[[nodiscard]] std::size_t uiDepth(const RuntimeUI& ui, UIElementId id) noexcept {
    std::size_t depth = 0U;
    const UIElement* element = ui.model().find(id);
    while (element != nullptr && element->parent && depth < 1024U) {
        ++depth;
        element = ui.model().find(*element->parent);
    }
    return depth;
}

using GlyphRows = std::array<std::uint8_t, 7U>;

[[nodiscard]] GlyphRows glyphRows(char character) noexcept {
    const auto upper = static_cast<char>(
        std::toupper(static_cast<unsigned char>(character)));
    switch (upper) {
    case 'A': return {14, 17, 17, 31, 17, 17, 17};
    case 'B': return {30, 17, 17, 30, 17, 17, 30};
    case 'C': return {14, 17, 16, 16, 16, 17, 14};
    case 'D': return {30, 17, 17, 17, 17, 17, 30};
    case 'E': return {31, 16, 16, 30, 16, 16, 31};
    case 'F': return {31, 16, 16, 30, 16, 16, 16};
    case 'G': return {14, 17, 16, 23, 17, 17, 14};
    case 'H': return {17, 17, 17, 31, 17, 17, 17};
    case 'I': return {14, 4, 4, 4, 4, 4, 14};
    case 'J': return {7, 2, 2, 2, 18, 18, 12};
    case 'K': return {17, 18, 20, 24, 20, 18, 17};
    case 'L': return {16, 16, 16, 16, 16, 16, 31};
    case 'M': return {17, 27, 21, 21, 17, 17, 17};
    case 'N': return {17, 25, 21, 19, 17, 17, 17};
    case 'O': return {14, 17, 17, 17, 17, 17, 14};
    case 'P': return {30, 17, 17, 30, 16, 16, 16};
    case 'Q': return {14, 17, 17, 17, 21, 18, 13};
    case 'R': return {30, 17, 17, 30, 20, 18, 17};
    case 'S': return {15, 16, 16, 14, 1, 1, 30};
    case 'T': return {31, 4, 4, 4, 4, 4, 4};
    case 'U': return {17, 17, 17, 17, 17, 17, 14};
    case 'V': return {17, 17, 17, 17, 17, 10, 4};
    case 'W': return {17, 17, 17, 21, 21, 21, 10};
    case 'X': return {17, 17, 10, 4, 10, 17, 17};
    case 'Y': return {17, 17, 10, 4, 4, 4, 4};
    case 'Z': return {31, 1, 2, 4, 8, 16, 31};
    case '0': return {14, 17, 19, 21, 25, 17, 14};
    case '1': return {4, 12, 4, 4, 4, 4, 14};
    case '2': return {14, 17, 1, 2, 4, 8, 31};
    case '3': return {30, 1, 1, 14, 1, 1, 30};
    case '4': return {2, 6, 10, 18, 31, 2, 2};
    case '5': return {31, 16, 16, 30, 1, 1, 30};
    case '6': return {14, 16, 16, 30, 17, 17, 14};
    case '7': return {31, 1, 2, 4, 8, 8, 8};
    case '8': return {14, 17, 17, 14, 17, 17, 14};
    case '9': return {14, 17, 17, 15, 1, 1, 14};
    case '.': return {0, 0, 0, 0, 0, 12, 12};
    case ',': return {0, 0, 0, 0, 0, 12, 8};
    case ':': return {0, 4, 4, 0, 4, 4, 0};
    case '-': return {0, 0, 0, 31, 0, 0, 0};
    case '!': return {4, 4, 4, 4, 4, 0, 4};
    case '?': return {14, 17, 1, 2, 4, 0, 4};
    default: return {31, 17, 5, 4, 5, 17, 31};
    }
}

[[nodiscard]] std::uint32_t drawText(Framebuffer& framebuffer, const std::string& text,
                                     int x, int y, int scale, Color color,
                                     const PixelRect& clip) noexcept {
    std::uint32_t glyphs = 0U;
    const auto startX = x;
    for (const auto character : text) {
        if (character == '\n') {
            x = startX;
            y += 8 * scale;
            continue;
        }
        if (character == ' ') {
            x += 6 * scale;
            continue;
        }
        if (x >= clip.right || y >= clip.bottom)
            break;
        const auto rows = glyphRows(character);
        for (int row = 0; row < 7; ++row) {
            for (int column = 0; column < 5; ++column) {
                if ((rows[static_cast<std::size_t>(row)] & (1U << (4 - column))) == 0U)
                    continue;
                for (int py = 0; py < scale; ++py) {
                    for (int px = 0; px < scale; ++px) {
                        const auto targetX = x + column * scale + px;
                        const auto targetY = y + row * scale + py;
                        if (targetX >= clip.left && targetX < clip.right &&
                            targetY >= clip.top && targetY < clip.bottom) {
                            framebuffer.blendPixel(targetX, targetY, color);
                        }
                    }
                }
            }
        }
        ++glyphs;
        x += 6 * scale;
    }
    return glyphs;
}

void drawBorder(Framebuffer& framebuffer, const PixelRect& rect, Color color) noexcept {
    framebuffer.drawLine(rect.left, rect.top, rect.right - 1, rect.top, color);
    framebuffer.drawLine(rect.left, rect.bottom - 1, rect.right - 1, rect.bottom - 1, color);
    framebuffer.drawLine(rect.left, rect.top, rect.left, rect.bottom - 1, color);
    framebuffer.drawLine(rect.right - 1, rect.top, rect.right - 1, rect.bottom - 1, color);
}

} // namespace

ScenePresenter::ScenePresenter(Framebuffer& framebuffer, ScenePresentationResources resources)
    : framebuffer_(&framebuffer), resources_(std::move(resources)), renderer2D_(framebuffer),
      raycastRenderer_(framebuffer), racerRenderer_(framebuffer), lowPolyRenderer_(framebuffer),
      placeholderSprite_(makeCheckerSprite(8, 8, {245U, 202U, 64U, 255U}, {220U, 80U, 50U, 255U})),
      placeholderRaycastMap_(makeDemoRaycastMap()), placeholderTrack_(makeDemoTrack()),
      placeholderMesh_(makeDemoCube()) {
    placeholderTilemap_.width = 40;
    placeholderTilemap_.height = 23;
    placeholderTilemap_.tileSize = 8;
    placeholderTilemap_.tiles = {
        makeCheckerSprite(8, 8, {35U, 45U, 62U, 255U}, {39U, 50U, 68U, 255U}),
        makeCheckerSprite(8, 8, {58U, 126U, 76U, 255U}, {50U, 108U, 64U, 255U}),
    };
    placeholderTilemap_.cells.resize(
        static_cast<std::size_t>(placeholderTilemap_.width * placeholderTilemap_.height));
    for (int y = 0; y < placeholderTilemap_.height; ++y) {
        for (int x = 0; x < placeholderTilemap_.width; ++x) {
            placeholderTilemap_.cells[static_cast<std::size_t>(y * placeholderTilemap_.width + x)] =
                static_cast<std::uint16_t>((x / 4 + y / 3) & 1);
        }
    }
}

void ScenePresenter::setResources(ScenePresentationResources resources) {
    resources_ = std::move(resources);
}

ScenePresentationStats ScenePresenter::render(const Scene& scene, const SceneRuntime* runtime,
                                              const float elapsedSeconds) noexcept {
    ScenePresentationStats stats;
    for (const auto* entity : scene.entities())
        stats.activeEntities += entity->active() ? 1U : 0U;

    const auto* raycastEntity = firstWith(scene, "RaycastMap");
    const auto* vehicleEntity = firstWith(scene, "VehicleController");
    const auto* meshEntity = firstWith(scene, "MeshRenderer");
    if (raycastEntity != nullptr) {
        stats.mode = ScenePresentationMode::Raycast;
        const auto* mapComponent = component(*raycastEntity, "RaycastMap");
        const auto mapGuid = property(*mapComponent, "map", AssetGuid{});
        const auto resolved = resources_.raycastMap && nonZero(mapGuid)
                                  ? resources_.raycastMap(mapGuid)
                                  : std::shared_ptr<const RaycastMap>{};
        if (!resolved)
            recordMissing(stats, mapGuid);
        RaycastCamera camera;
        const auto* cameraEntity = firstWith(scene, "Camera");
        if (cameraEntity != nullptr) {
            const auto position = worldPosition(scene, *cameraEntity);
            const auto angle = cameraEntity->transform().localRotation().z;
            camera.position = {position.x, position.y};
            camera.direction = {std::cos(angle), std::sin(angle)};
        }
        std::vector<Billboard> billboards;
        for (const auto* entity : scene.entities()) {
            const auto* sprite = component(*entity, "SpriteRenderer");
            if (!entity->active() || !enabled(sprite))
                continue;
            const auto position = worldPosition(scene, *entity);
            billboards.push_back({{position.x, position.y},
                                  property(*sprite, "tint", guidColor(entity->id())),
                                  0.28F});
        }
        const auto rendered = raycastRenderer_.render(resolved ? *resolved : placeholderRaycastMap_,
                                                      camera, billboards);
        stats.rays = rendered.rays;
        stats.sprites = rendered.billboards;
        stats.drawCalls = 1U + rendered.billboards;
        return stats;
    }

    if (vehicleEntity != nullptr) {
        stats.mode = ScenePresentationMode::Racer;
        const auto* vehicle = component(*vehicleEntity, "VehicleController");
        const auto trackGuid = property(*vehicle, "track", AssetGuid{});
        const auto resolved = resources_.racerTrack && nonZero(trackGuid)
                                  ? resources_.racerTrack(trackGuid)
                                  : std::shared_ptr<const RacerTrackAsset>{};
        if (!resolved)
            recordMissing(stats, trackGuid);
        const auto position = worldPosition(scene, *vehicleEntity);
        RacerCamera camera;
        camera.lateral = position.x;
        camera.distance = position.z;
        camera.speed = std::max(0.0F, position.y);
        const auto rendered =
            resolved ? racerRenderer_.render(*resolved, camera, resources_.sprite)
                     : racerRenderer_.render(placeholderTrack_, camera);
        stats.drawCalls = 1U;
        stats.sprites = rendered.roadsideObjectsDrawn + rendered.opponentsDrawn;
        stats.missingAssets += rendered.missingSpriteAssets;
        return stats;
    }

    if (meshEntity != nullptr) {
        stats.mode = ScenePresentationMode::LowPoly;
        framebuffer_->clear(cameraClear(scene));
        for (const auto* entity : scene.entities()) {
            const auto* renderer = component(*entity, "MeshRenderer");
            if (!entity->active() || !enabled(renderer))
                continue;
            const auto meshGuid = property(*renderer, "mesh", AssetGuid{});
            const auto resolved = resources_.mesh && nonZero(meshGuid)
                                      ? resources_.mesh(meshGuid)
                                      : std::shared_ptr<const LowPolyMesh>{};
            if (!resolved)
                recordMissing(stats, meshGuid);
            const auto materialGuid = property(*renderer, "material", AssetGuid{});
            const auto material = resources_.material && nonZero(materialGuid)
                                      ? resources_.material(materialGuid)
                                      : std::shared_ptr<const Material>{};
            if (!material)
                recordMissing(stats, materialGuid);
            std::shared_ptr<const Sprite> texture;
            if (material && material->baseTexture && resources_.sprite) {
                texture = resources_.sprite(*material->baseTexture);
                if (!texture)
                    recordMissing(stats, *material->baseTexture);
            }
            LowPolyMaterialBinding binding;
            binding.material = material.get();
            if (texture) {
                binding.texture = {texture->width, texture->height, texture->pixels.data(),
                                   texture->pixels.size()};
            }
            auto world = scene.worldTransform(entity->id());
            const auto rendered = lowPolyRenderer_.render(
                resolved ? *resolved : placeholderMesh_,
                world ? world.value() : Mat4::identity(), LowPolyCamera{}, {}, {}, binding);
            stats.triangles += rendered.drawn;
            ++stats.drawCalls;
        }
        return stats;
    }

    stats.mode = ScenePresentationMode::TwoDimensional;
    framebuffer_->clear(cameraClear(scene));
    renderer2D_.resetCounters();
    for (const auto* entity : scene.entities()) {
        if (!entity->active())
            continue;
        const auto position = worldPosition(scene, *entity);
        if (const auto* tilemapComponent = component(*entity, "TilemapRenderer");
            enabled(tilemapComponent)) {
            const auto guid = property(*tilemapComponent, "tilemap", AssetGuid{});
            const auto resolved = resources_.tilemap && nonZero(guid)
                                      ? resources_.tilemap(guid)
                                      : std::shared_ptr<const Tilemap>{};
            if (!resolved)
                recordMissing(stats, guid);
            const auto& map = resolved ? *resolved : placeholderTilemap_;
            renderer2D_.drawTilemap(map, {-position.x, -position.y},
                                    {0.0F, 0.0F, static_cast<float>(framebuffer_->width()),
                                     static_cast<float>(framebuffer_->height())});
            stats.tiles += static_cast<std::uint32_t>(map.cells.size());
        }
    }

    struct SpriteItem final {
        float depth = 0.0F;
        EntityGuid id;
        const Entity* entity = nullptr;
        const DataComponent* component = nullptr;
    };
    std::vector<SpriteItem> sprites;
    for (const auto* entity : scene.entities()) {
        const auto* renderer = component(*entity, "SpriteRenderer");
        if (entity->active() && enabled(renderer)) {
            sprites.push_back({worldPosition(scene, *entity).z, entity->id(), entity, renderer});
        }
    }
    std::sort(sprites.begin(), sprites.end(), [](const SpriteItem& lhs, const SpriteItem& rhs) {
        return std::tie(lhs.depth, lhs.id) < std::tie(rhs.depth, rhs.id);
    });
    for (const auto& item : sprites) {
        const auto guid = property(*item.component, "sprite", AssetGuid{});
        const auto resolved = resources_.sprite && nonZero(guid) ? resources_.sprite(guid)
                                                                 : std::shared_ptr<const Sprite>{};
        if (!resolved)
            recordMissing(stats, guid);
        const auto& sprite = resolved ? *resolved : placeholderSprite_;
        const auto position = worldPosition(scene, *item.entity);
        const auto scale = item.entity->transform().localScale();
        const auto integerScale = std::clamp(
            static_cast<int>(std::lround(std::max(std::fabs(scale.x), std::fabs(scale.y)))), 1, 16);
        auto tint = property(*item.component, "tint", guidColor(item.id));
        if (!resolved && tint == Color{255U, 255U, 255U, 255U})
            tint = guidColor(item.id);
        const auto bob = static_cast<int>(std::sin(elapsedSeconds * 2.0F + position.z) * 0.5F);
        renderer2D_.draw({&sprite, static_cast<int>(std::lround(position.x)),
                          static_cast<int>(std::lround(position.y)) + bob, integerScale,
                          scale.x < 0.0F, scale.y < 0.0F, tint});
    }

    if (runtime != nullptr) {
        for (const auto* entity : scene.entities()) {
            const auto particleStats = runtime->particleStatsFor(entity->id());
            if (particleStats.activeParticles == 0U)
                continue;
            const auto position = worldPosition(scene, *entity);
            const auto count = static_cast<std::uint32_t>(
                std::min<std::size_t>(particleStats.activeParticles, 32U));
            for (std::uint32_t index = 0U; index < count; ++index) {
                const auto x =
                    static_cast<int>(position.x) + static_cast<int>((index * 17U) % 19U) - 9;
                const auto y = static_cast<int>(position.y) - static_cast<int>((index * 11U) % 13U);
                framebuffer_->blendPixel(x, y, {245U, 190U, 78U, 180U});
            }
            stats.sprites += count;
            const auto active = static_cast<std::uint64_t>(particleStats.activeParticles);
            stats.particles =
                active > static_cast<std::uint64_t>(
                             std::numeric_limits<std::uint32_t>::max() - stats.particles)
                    ? std::numeric_limits<std::uint32_t>::max()
                    : stats.particles + static_cast<std::uint32_t>(active);
        }

        struct UIItem final {
            std::size_t depth = 0U;
            UIElementId id;
            EntityGuid entityId;
            const Entity* entity = nullptr;
            const UIWidget* widget = nullptr;
            const UIElement* element = nullptr;
            PixelRect rect;
        };
        std::vector<UIItem> uiItems;
        const auto& ui = runtime->ui();
        for (const auto* entity : scene.entities()) {
            const auto elementId = runtime->uiElementFor(entity->id());
            if (!entity->active() || !elementId || !effectivelyVisible(ui, *elementId))
                continue;
            const auto* widget = ui.widget(*elementId);
            const auto* element = ui.model().find(*elementId);
            const auto screen = ui.screenRect(*elementId);
            if (widget == nullptr || element == nullptr || !screen)
                continue;
            auto rect = pixelRect(*screen);
            rect.left = std::clamp(rect.left, 0, framebuffer_->width());
            rect.top = std::clamp(rect.top, 0, framebuffer_->height());
            rect.right = std::clamp(rect.right, 0, framebuffer_->width());
            rect.bottom = std::clamp(rect.bottom, 0, framebuffer_->height());
            if (!rect.valid())
                continue;
            uiItems.push_back(
                {uiDepth(ui, *elementId), *elementId, entity->id(), entity, widget, element, rect});
        }
        std::sort(uiItems.begin(), uiItems.end(), [](const UIItem& lhs, const UIItem& rhs) {
            return std::tie(lhs.depth, lhs.id, lhs.entityId) <
                   std::tie(rhs.depth, rhs.id, rhs.entityId);
        });

        const auto& theme = ui.theme();
        const auto fontScale =
            std::clamp(static_cast<int>(std::lround(theme.fontScale * ui.scale())), 1, 4);
        for (const auto& item : uiItems) {
            auto foreground = item.element->properties.enabled ? theme.foreground : theme.disabled;
            if (const auto* textComponent = component(*item.entity, "UIText");
                enabled(textComponent)) {
                foreground = property(*textComponent, "color", foreground);
            }
            bool drewWidget = false;
            int textX = item.rect.left + 4;
            int textY = item.rect.top + std::max(1, (item.rect.height() - 7 * fontScale) / 2);

            switch (item.widget->type) {
            case UIWidgetType::Panel:
                framebuffer_->fillRect(item.rect.left, item.rect.top, item.rect.width(),
                                       item.rect.height(), theme.panel);
                ++stats.drawCalls;
                drewWidget = true;
                break;
            case UIWidgetType::Layout:
                drawBorder(*framebuffer_, item.rect, theme.panel);
                ++stats.drawCalls;
                drewWidget = true;
                break;
            case UIWidgetType::Button: {
                const auto fill = item.element->properties.enabled ? theme.accent : theme.disabled;
                framebuffer_->fillRect(item.rect.left, item.rect.top, item.rect.width(),
                                       item.rect.height(), fill);
                drawBorder(*framebuffer_, item.rect, foreground);
                stats.drawCalls += 2U;
                const auto textWidth = item.widget->text.empty()
                                           ? 0
                                           : static_cast<int>(item.widget->text.size()) *
                                                     6 * fontScale -
                                                 fontScale;
                textX = item.rect.left + std::max(2, (item.rect.width() - textWidth) / 2);
                drewWidget = true;
                break;
            }
            case UIWidgetType::Toggle: {
                const auto size = std::max(4, std::min(item.rect.height() - 4, 12 * fontScale));
                const PixelRect box{item.rect.left + 2, item.rect.top + 2,
                                    item.rect.left + 2 + size, item.rect.top + 2 + size};
                framebuffer_->fillRect(box.left, box.top, box.width(), box.height(), theme.panel);
                drawBorder(*framebuffer_, box, foreground);
                stats.drawCalls += 2U;
                if (item.widget->checked) {
                    framebuffer_->drawLine(box.left + 2, box.top + size / 2,
                                           box.left + size / 2, box.bottom - 3, theme.accent);
                    framebuffer_->drawLine(box.left + size / 2, box.bottom - 3,
                                           box.right - 2, box.top + 2, theme.accent);
                    ++stats.drawCalls;
                }
                textX = box.right + 4;
                drewWidget = true;
                break;
            }
            case UIWidgetType::Slider:
            case UIWidgetType::Progress: {
                framebuffer_->fillRect(item.rect.left, item.rect.top, item.rect.width(),
                                       item.rect.height(), theme.panel);
                const auto range = item.widget->maximum - item.widget->minimum;
                const auto fraction = range > 0.0F
                                          ? std::clamp((item.widget->value - item.widget->minimum) /
                                                           range,
                                                       0.0F, 1.0F)
                                          : 0.0F;
                const auto inset = 2;
                const auto available = std::max(0, item.rect.width() - inset * 2);
                const auto filled = static_cast<int>(std::lround(
                    static_cast<float>(available) * fraction));
                if (filled > 0) {
                    framebuffer_->fillRect(item.rect.left + inset, item.rect.top + inset, filled,
                                           std::max(1, item.rect.height() - inset * 2),
                                           theme.accent);
                }
                drawBorder(*framebuffer_, item.rect, foreground);
                stats.drawCalls += filled > 0 ? 3U : 2U;
                if (item.widget->type == UIWidgetType::Slider) {
                    const auto knobX = item.rect.left + inset + filled;
                    framebuffer_->drawLine(knobX, item.rect.top + 1, knobX,
                                           item.rect.bottom - 2, foreground);
                    ++stats.drawCalls;
                }
                drewWidget = true;
                break;
            }
            case UIWidgetType::Image: {
                const auto guid = item.widget->imageAsset;
                const auto resolved = resources_.sprite && nonZero(guid)
                                          ? resources_.sprite(guid)
                                          : std::shared_ptr<const Sprite>{};
                if (!resolved)
                    recordMissing(stats, guid);
                const auto& sprite = resolved ? *resolved : placeholderSprite_;
                const auto scale = std::clamp(
                    std::min(item.rect.width() / std::max(1, sprite.width),
                             item.rect.height() / std::max(1, sprite.height)),
                    1, 16);
                renderer2D_.draw({&sprite, item.rect.left, item.rect.top, scale, false, false,
                                  Color{255U, 255U, 255U, 255U}});
                drewWidget = true;
                break;
            }
            case UIWidgetType::Text:
                drewWidget = !item.widget->text.empty();
                break;
            case UIWidgetType::List:
                framebuffer_->fillRect(item.rect.left, item.rect.top, item.rect.width(),
                                       item.rect.height(), theme.panel);
                drawBorder(*framebuffer_, item.rect, foreground);
                stats.drawCalls += 2U;
                drewWidget = true;
                break;
            }

            std::string text = item.widget->text;
            if (text.empty() && item.widget->type == UIWidgetType::List &&
                item.widget->selectedItem &&
                *item.widget->selectedItem < item.widget->items.size()) {
                text = item.widget->items[*item.widget->selectedItem];
            }
            if (!text.empty()) {
                const auto glyphs = drawText(*framebuffer_, text, textX, textY, fontScale,
                                             foreground, item.rect);
                if (glyphs > 0U) {
                    stats.uiGlyphs += glyphs;
                    ++stats.drawCalls;
                    drewWidget = true;
                }
            }
            if (drewWidget)
                ++stats.uiWidgets;
        }
    }
    stats.drawCalls += renderer2D_.drawCalls();
    stats.sprites += renderer2D_.spritesSubmitted();
    return stats;
}

const char* scenePresentationModeName(const ScenePresentationMode mode) noexcept {
    switch (mode) {
    case ScenePresentationMode::TwoDimensional:
        return "2d";
    case ScenePresentationMode::Raycast:
        return "raycast";
    case ScenePresentationMode::Racer:
        return "racer";
    case ScenePresentationMode::LowPoly:
        return "lowpoly";
    }
    return "unknown";
}

} // namespace fabgl::rendering
