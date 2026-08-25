#include "test_harness.h"

#include <fabgl/rendering/lowpoly_renderer.h>
#include <fabgl/rendering/racer_renderer.h>
#include <fabgl/rendering/raycast_renderer.h>
#include <fabgl/rendering/renderer_2d.h>

#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr int Width = 160;
constexpr int Height = 90;
constexpr std::size_t WarmupFrames = 3U;
constexpr std::size_t MeasuredFrames = 48U;
constexpr std::uint64_t ExpectedWorkloadChecksum = 9215901677396929035ULL;
constexpr std::uint64_t MinimumOperations = 500'000ULL;
constexpr std::uint64_t MaximumOperations = 2'000'000ULL;
constexpr std::uint64_t MinimumDraws = 10'000ULL;
constexpr std::uint64_t MaximumDraws = 50'000ULL;
constexpr std::size_t MaximumDeclaredMemoryBytes = 4U * 1024U * 1024U;

#if defined(NDEBUG)
constexpr double DefaultTimeBudgetMilliseconds = 12'000.0;
constexpr const char* BuildMode = "optimized";
#else
constexpr double DefaultTimeBudgetMilliseconds = 30'000.0;
constexpr const char* BuildMode = "debug";
#endif

struct WorkloadResult final {
    std::uint64_t checksum = 1469598103934665603ULL;
    std::uint64_t operations = 0U;
    std::uint64_t draws = 0U;
};

template <typename T> [[nodiscard]] std::size_t vectorBytes(const std::vector<T>& values) {
    return values.capacity() * sizeof(T);
}

void absorb(WorkloadResult& result, const std::uint64_t value) noexcept {
    result.checksum ^= value;
    result.checksum *= 1099511628211ULL;
}

[[nodiscard]] fabgl::rendering::RaycastTexture makeTexture(const fabgl::Color first,
                                                           const fabgl::Color second) {
    fabgl::rendering::RaycastTexture texture;
    texture.width = 8;
    texture.height = 8;
    texture.pixels.resize(64U);
    for (int y = 0; y < texture.height; ++y) {
        for (int x = 0; x < texture.width; ++x) {
            const auto index = static_cast<std::size_t>(y * texture.width + x);
            texture.pixels[index] = ((x / 2 + y / 2) % 2 == 0) ? first : second;
        }
    }
    return texture;
}

class RepresentativeRendererWorkload final {
  public:
    RepresentativeRendererWorkload()
        : framebuffer_(Width, Height), renderer2d_(framebuffer_, 128U),
          raycast_(framebuffer_), racer_(framebuffer_), lowpoly_(framebuffer_),
          sprite_(fabgl::rendering::makeCheckerSprite(
              8, 8, {244U, 196U, 64U, 255U}, {52U, 122U, 214U, 220U})),
          rayMap_(fabgl::rendering::makeDemoRaycastMap()),
          track_(fabgl::rendering::makeDemoTrack()), mesh_(fabgl::rendering::makeDemoCube()) {
        configureTilemap();
        configureRaycast();
        rayBillboards_ = {{{4.5F, 3.5F}, {230U, 70U, 65U, 255U}, 0.28F, &rayTexture_,
                           fabgl::rendering::Billboard::Kind::Enemy},
                          {{7.5F, 4.5F}, {70U, 220U, 100U, 230U}, 0.22F, &rayTexture_,
                           fabgl::rendering::Billboard::Kind::Item}};
        lowpolyBillboards_ = {{{-1.6F, 0.0F, 1.0F}, {0.45F, 0.9F},
                               {70U, 220U, 110U, 220U}},
                              {{1.6F, 0.0F, 1.5F}, {0.45F, 0.9F},
                               {220U, 90U, 75U, 220U}}};
    }

    [[nodiscard]] WorkloadResult run(const std::size_t frames) {
        WorkloadResult result;
        for (std::size_t frame = 0U; frame < frames; ++frame) {
            const auto phase = static_cast<float>(frame) * 0.0375F;
            run2d(frame, phase, result);
            runRaycast(phase, result);
            runRacer(phase, result);
            runLowpoly(phase, result);
        }
        return result;
    }

    [[nodiscard]] std::size_t declaredMemoryBytes() const noexcept {
        std::size_t bytes = vectorBytes(framebuffer_.pixels()) + vectorBytes(sprite_.pixels) +
                            vectorBytes(tilemap_.cells) + vectorBytes(tilemap_.tiles) +
                            vectorBytes(tilemap_.layers) + vectorBytes(rayMap_.cells) +
                            vectorBytes(rayMap_.wallPalette) + vectorBytes(rayMap_.wallTextures) +
                            vectorBytes(rayMap_.sectorLighting) + vectorBytes(rayMap_.doors) +
                            vectorBytes(rayBillboards_) + vectorBytes(track_) +
                            vectorBytes(mesh_.vertices) + vectorBytes(mesh_.triangles) +
                            vectorBytes(lowpolyBillboards_);
        for (const auto& tile : tilemap_.tiles) {
            bytes += vectorBytes(tile.pixels) + vectorBytes(tile.indices);
        }
        for (const auto& layer : tilemap_.layers) {
            bytes += vectorBytes(layer.cells);
        }
        for (const auto& texture : rayMap_.wallTextures) {
            bytes += vectorBytes(texture.pixels);
        }
        bytes += vectorBytes(rayTexture_.pixels);
        // Public APIs intentionally hide renderer scratch vectors. Account for their documented
        // bounded envelopes: two ray-column arrays and the configured 2D submission queue.
        bytes += 2U * static_cast<std::size_t>(raySettings_.internalWidth) * sizeof(float);
        bytes += 128U * (sizeof(fabgl::rendering::LayeredSpriteDraw) + sizeof(std::uint64_t));
        return bytes;
    }

  private:
    void configureTilemap() {
        tilemap_.width = 24;
        tilemap_.height = 16;
        tilemap_.tileSize = 8;
        tilemap_.chunkSize = 8;
        tilemap_.tiles = {
            fabgl::rendering::makeCheckerSprite(
                8, 8, {35U, 52U, 76U, 255U}, {42U, 65U, 92U, 255U}),
            fabgl::rendering::makeCheckerSprite(
                8, 8, {56U, 122U, 70U, 230U}, {75U, 150U, 86U, 230U})};
        const auto cellCount = static_cast<std::size_t>(tilemap_.width * tilemap_.height);
        fabgl::rendering::TilemapLayer ground;
        ground.cells.resize(cellCount);
        ground.parallax = {1.0F, 1.0F};
        fabgl::rendering::TilemapLayer detail;
        detail.cells.resize(cellCount);
        detail.parallax = {0.75F, 0.9F};
        detail.opacity = 150U;
        for (std::size_t index = 0U; index < cellCount; ++index) {
            ground.cells[index] = static_cast<std::uint16_t>(index % 2U);
            detail.cells[index] = static_cast<std::uint16_t>((index / 7U) % 2U);
        }
        tilemap_.layers = {std::move(ground), std::move(detail)};
    }

    void configureRaycast() {
        rayTexture_ = makeTexture({210U, 75U, 62U, 255U}, {78U, 105U, 180U, 210U});
        rayMap_.wallTextures.assign(4U, rayTexture_);
        rayMap_.sectorLighting.assign(rayMap_.cells.size(), 220U);
        raySettings_.internalWidth = 96;
        raySettings_.floorAndCeiling = true;
        raySettings_.distanceFog = true;
        raySettings_.minimap = true;
        raySettings_.weaponOverlay = true;
        raySettings_.fixedPointCoordinates = true;
        raySettings_.floorTexture = &rayTexture_;
        raySettings_.ceilingTexture = &rayTexture_;
        raySettings_.weaponTexture = &rayTexture_;
    }

    void run2d(const std::size_t frame, const float phase, WorkloadResult& result) {
        framebuffer_.clear({12U, 18U, 30U, 255U});
        renderer2d_.resetCounters();
        const fabgl::Vec2 camera{phase * 13.0F, phase * 4.0F};
        const auto tileStats = renderer2d_.drawTilemapDetailed(
            tilemap_, camera, {0.0F, 0.0F, static_cast<float>(Width), static_cast<float>(Height)},
            phase);
        for (std::size_t index = 0U; index < 32U; ++index) {
            fabgl::rendering::LayeredSpriteDraw command;
            command.command.sprite = &sprite_;
            command.command.x = static_cast<int>((index * 29U + frame * 3U) % 176U) - 8;
            command.command.y = static_cast<int>((index * 17U + frame * 2U) % 98U) - 4;
            command.command.rotationDegrees =
                static_cast<float>((index * 11U + frame * 5U) % 360U);
            command.sortingLayer = static_cast<int>(index % 4U);
            command.zOrder = static_cast<int>(index);
            command.parallax = index % 3U == 0U ? fabgl::Vec2{0.5F, 0.75F}
                                                : fabgl::Vec2{1.0F, 1.0F};
            command.uiOverlay = index >= 30U;
            const auto accepted = renderer2d_.submit(command);
            result.operations += accepted ? 1U : 0U;
        }
        renderer2d_.flush(
            camera, {0.0F, 0.0F, static_cast<float>(Width), static_cast<float>(Height)});
        result.operations += static_cast<std::uint64_t>(tileStats.tiles) +
                             static_cast<std::uint64_t>(tileStats.culledTiles);
        result.draws += renderer2d_.drawCalls();
        absorb(result, framebuffer_.checksum());
    }

    void runRaycast(const float phase, WorkloadResult& result) {
        fabgl::rendering::RaycastCamera camera;
        camera.position = {2.5F, 2.5F};
        camera.direction = {std::cos(phase * 0.6F), std::sin(phase * 0.6F)};
        camera.pitch = std::sin(phase) * 8.0F;
        const auto stats = raycast_.render(rayMap_, camera, rayBillboards_, raySettings_);
        result.operations += static_cast<std::uint64_t>(stats.rays) +
                             static_cast<std::uint64_t>(stats.ddaSteps) +
                             static_cast<std::uint64_t>(stats.floorCeilingPixels) +
                             static_cast<std::uint64_t>(stats.transparentSpritePixels);
        result.draws += static_cast<std::uint64_t>(stats.rays) +
                        static_cast<std::uint64_t>(stats.billboards);
        absorb(result, framebuffer_.checksum());
    }

    void runRacer(const float phase, WorkloadResult& result) {
        fabgl::rendering::RacerCamera camera;
        camera.distance = phase * 31.0F;
        camera.lateral = std::sin(phase * 1.7F) * 0.4F;
        camera.speed = 75.0F;
        const auto stats = racer_.render(track_, camera);
        result.operations += static_cast<std::uint64_t>(stats.segmentsSampled) +
                             static_cast<std::uint64_t>(stats.weatherPixelsBlended);
        result.draws += stats.scanlines;
        absorb(result, framebuffer_.checksum());
    }

    void runLowpoly(const float phase, WorkloadResult& result) {
        framebuffer_.clear({18U, 20U, 28U, 255U});
        fabgl::rendering::LowPolyCamera camera;
        camera.fogStart = 4.0F;
        camera.fogEnd = 18.0F;
        fabgl::rendering::LowPolyRenderSettings settings;
        settings.maximumTriangles = 64U;
        settings.maximumBillboards = 8U;
        const auto model = fabgl::Mat4::rotationY(phase) * fabgl::Mat4::rotationX(phase * 0.4F);
        const auto stats = lowpoly_.render(mesh_, model, camera, settings, lowpolyBillboards_);
        result.operations += static_cast<std::uint64_t>(stats.submitted) +
                             static_cast<std::uint64_t>(stats.culled) +
                             static_cast<std::uint64_t>(stats.frustumCulled);
        result.draws += stats.drawn;
        absorb(result, framebuffer_.checksum());
    }

    fabgl::rendering::Framebuffer framebuffer_;
    fabgl::rendering::Renderer2D renderer2d_;
    fabgl::rendering::RaycastRenderer raycast_;
    fabgl::rendering::RacerRenderer racer_;
    fabgl::rendering::LowPolyRenderer lowpoly_;
    fabgl::rendering::Sprite sprite_;
    fabgl::rendering::Tilemap tilemap_;
    fabgl::rendering::RaycastMap rayMap_;
    fabgl::rendering::RaycastTexture rayTexture_;
    fabgl::rendering::RaycastRenderSettings raySettings_;
    std::vector<fabgl::rendering::Billboard> rayBillboards_;
    std::vector<fabgl::rendering::RoadSegment> track_;
    fabgl::rendering::LowPolyMesh mesh_;
    std::vector<fabgl::rendering::LowPolyBillboard> lowpolyBillboards_;
};

[[nodiscard]] double timeBudgetMilliseconds() {
    const auto* overrideText = std::getenv("FGL_PERFORMANCE_BUDGET_MS");
    if (overrideText == nullptr || overrideText[0] == '\0') {
        return DefaultTimeBudgetMilliseconds;
    }
    errno = 0;
    char* end = nullptr;
    const auto value = std::strtod(overrideText, &end);
    if (errno != 0 || end == overrideText || end == nullptr || *end != '\0' ||
        !std::isfinite(value) || value < 1.0 || value > 80'000.0) {
        throw std::invalid_argument(
            "FGL_PERFORMANCE_BUDGET_MS must be a finite value from 1 to 80000");
    }
    return value;
}

} // namespace

FGL_TEST(representative_renderers_stay_within_pc_regression_budgets) {
    RepresentativeRendererWorkload workload;
    static_cast<void>(workload.run(WarmupFrames));

    const auto started = Clock::now();
    const auto measured = workload.run(MeasuredFrames);
    const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    const auto timeBudget = timeBudgetMilliseconds();
    const auto memoryBytes = workload.declaredMemoryBytes();

    std::cout << "[PERFORMANCE] mode=" << BuildMode << " frames=" << MeasuredFrames
              << " elapsed_ms=" << elapsed << " budget_ms=" << timeBudget
              << " checksum=" << measured.checksum << " operations=" << measured.operations
              << " draws=" << measured.draws << " declared_memory_bytes=" << memoryBytes << '\n';

    FGL_CHECK(measured.checksum == ExpectedWorkloadChecksum);
    FGL_CHECK(measured.operations >= MinimumOperations);
    FGL_CHECK(measured.operations <= MaximumOperations);
    FGL_CHECK(measured.draws >= MinimumDraws);
    FGL_CHECK(measured.draws <= MaximumDraws);
    FGL_CHECK(memoryBytes <= MaximumDeclaredMemoryBytes);
    FGL_CHECK(elapsed <= timeBudget);
}
