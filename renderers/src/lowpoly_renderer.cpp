#include <fabgl/rendering/lowpoly_renderer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace fabgl::rendering {

namespace {

struct ProjectedTriangle final {
    std::array<Vec2, 3> points{};
    std::array<Vec2, 3> textureCoordinates{};
    float depth = 0.0F;
    Color color{};
    MaterialBlendMode blend = MaterialBlendMode::Opaque;
    LowPolyTextureView texture;
    bool billboard = false;
    Rect billboardBounds{};
};

[[nodiscard]] float edge(const Vec2 a, const Vec2 b, const Vec2 point) noexcept {
    return (point.x - a.x) * (b.y - a.y) - (point.y - a.y) * (b.x - a.x);
}

[[nodiscard]] Vec3 subtract(Vec3 lhs, Vec3 rhs) noexcept {
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

[[nodiscard]] Vec3 cross(Vec3 lhs, Vec3 rhs) noexcept {
    return {lhs.y * rhs.z - lhs.z * rhs.y, lhs.z * rhs.x - lhs.x * rhs.z,
            lhs.x * rhs.y - lhs.y * rhs.x};
}

[[nodiscard]] float dot(const Vec3 lhs, const Vec3 rhs) noexcept {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

[[nodiscard]] float length(const Vec3 value) noexcept {
    return std::sqrt(dot(value, value));
}

[[nodiscard]] bool finite(const Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] Color multiply(Color lhs, Color rhs) noexcept {
    const auto channel = [](std::uint8_t a, std::uint8_t b) {
        return static_cast<std::uint8_t>((static_cast<std::uint32_t>(a) * b + 127U) / 255U);
    };
    return {channel(lhs.r, rhs.r), channel(lhs.g, rhs.g), channel(lhs.b, rhs.b),
            channel(lhs.a, rhs.a)};
}

[[nodiscard]] Color average(Color a, Color b, Color c) noexcept {
    const auto channel = [](std::uint8_t first, std::uint8_t second, std::uint8_t third) {
        return static_cast<std::uint8_t>((static_cast<std::uint32_t>(first) + second + third) / 3U);
    };
    return {channel(a.r, b.r, c.r), channel(a.g, b.g, c.g), channel(a.b, b.b, c.b),
            channel(a.a, b.a, c.a)};
}

[[nodiscard]] Color interpolate(Color source, Color target, float amount) noexcept {
    amount = std::clamp(amount, 0.0F, 1.0F);
    const auto channel = [amount](std::uint8_t from, std::uint8_t to) {
        return static_cast<std::uint8_t>(
            std::clamp(std::lround(static_cast<float>(from) +
                                   (static_cast<float>(to) - static_cast<float>(from)) * amount),
                       0L, 255L));
    };
    return {channel(source.r, target.r), channel(source.g, target.g), channel(source.b, target.b),
            channel(source.a, target.a)};
}

[[nodiscard]] Color sampleNearest(const LowPolyTextureView& texture, const Vec2 uv) noexcept {
    const auto u = std::clamp(uv.x, 0.0F, 1.0F);
    // Wavefront/atlas UVs use a bottom-left origin while framebuffer rows use top-left.
    const auto v = 1.0F - std::clamp(uv.y, 0.0F, 1.0F);
    const auto x = static_cast<int>(std::lround(u * static_cast<float>(texture.width - 1)));
    const auto y = static_cast<int>(std::lround(v * static_cast<float>(texture.height - 1)));
    return texture.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(texture.width) +
                          static_cast<std::size_t>(x)];
}

void submitPixel(Framebuffer& framebuffer, const int x, const int y, const Color source,
                 const MaterialBlendMode blend) noexcept {
    if (source.a == 0U) {
        return;
    }
    if (blend == MaterialBlendMode::Alpha) {
        framebuffer.blendPixel(x, y, source);
        return;
    }
    if (blend == MaterialBlendMode::Opaque) {
        framebuffer.setPixel(x, y, source);
        return;
    }
    const auto destination = framebuffer.pixel(x, y);
    const auto alpha = static_cast<std::uint32_t>(source.a);
    const auto mix = [alpha](const std::uint8_t original, const std::uint8_t effected) {
        return static_cast<std::uint8_t>(
            (static_cast<std::uint32_t>(effected) * alpha +
             static_cast<std::uint32_t>(original) * (255U - alpha) + 127U) /
            255U);
    };
    if (blend == MaterialBlendMode::Additive) {
        const auto add = [](const std::uint8_t lhs, const std::uint8_t rhs) {
            return static_cast<std::uint8_t>(
                std::min(255U, static_cast<std::uint32_t>(lhs) + rhs));
        };
        framebuffer.setPixel(
            x, y,
            {mix(destination.r, add(destination.r, source.r)),
             mix(destination.g, add(destination.g, source.g)),
             mix(destination.b, add(destination.b, source.b)), 255U});
        return;
    }
    const auto multiplyChannel = [](const std::uint8_t lhs, const std::uint8_t rhs) {
        return static_cast<std::uint8_t>(
            (static_cast<std::uint32_t>(lhs) * rhs + 127U) / 255U);
    };
    framebuffer.setPixel(
        x, y,
        {mix(destination.r, multiplyChannel(destination.r, source.r)),
         mix(destination.g, multiplyChannel(destination.g, source.g)),
         mix(destination.b, multiplyChannel(destination.b, source.b)), 255U});
}

[[nodiscard]] std::uint32_t drawTexturedTriangle(Framebuffer& framebuffer,
                                                 const ProjectedTriangle& triangle) noexcept {
    const auto area = edge(triangle.points[1], triangle.points[2], triangle.points[0]);
    if (std::fabs(area) < std::numeric_limits<float>::epsilon()) {
        return 0U;
    }
    const auto minimumX = std::max(
        0, static_cast<int>(std::floor(std::min({triangle.points[0].x, triangle.points[1].x,
                                                triangle.points[2].x}))));
    const auto maximumX = std::min(
        framebuffer.width() - 1,
        static_cast<int>(std::ceil(std::max({triangle.points[0].x, triangle.points[1].x,
                                            triangle.points[2].x}))));
    const auto minimumY = std::max(
        0, static_cast<int>(std::floor(std::min({triangle.points[0].y, triangle.points[1].y,
                                                triangle.points[2].y}))));
    const auto maximumY = std::min(
        framebuffer.height() - 1,
        static_cast<int>(std::ceil(std::max({triangle.points[0].y, triangle.points[1].y,
                                            triangle.points[2].y}))));
    std::uint32_t pixels = 0U;
    for (auto y = minimumY; y <= maximumY; ++y) {
        for (auto x = minimumX; x <= maximumX; ++x) {
            const Vec2 sample{static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F};
            const auto first = edge(triangle.points[1], triangle.points[2], sample) / area;
            const auto second = edge(triangle.points[2], triangle.points[0], sample) / area;
            const auto third = edge(triangle.points[0], triangle.points[1], sample) / area;
            if (first < 0.0F || second < 0.0F || third < 0.0F) {
                continue;
            }
            const Vec2 uv{triangle.textureCoordinates[0].x * first +
                              triangle.textureCoordinates[1].x * second +
                              triangle.textureCoordinates[2].x * third,
                          triangle.textureCoordinates[0].y * first +
                              triangle.textureCoordinates[1].y * second +
                              triangle.textureCoordinates[2].y * third};
            const auto color = multiply(sampleNearest(triangle.texture, uv), triangle.color);
            submitPixel(framebuffer, x, y, color, triangle.blend);
            if (color.a != 0U && pixels != std::numeric_limits<std::uint32_t>::max()) {
                ++pixels;
            }
        }
    }
    return pixels;
}

} // namespace

bool LowPolyTextureView::valid(const int maximumDimension) const noexcept {
    if (width <= 0 || height <= 0 || maximumDimension <= 0 || width > maximumDimension ||
        height > maximumDimension || pixels == nullptr) {
        return false;
    }
    const auto wideWidth = static_cast<std::size_t>(width);
    const auto wideHeight = static_cast<std::size_t>(height);
    return wideWidth <= std::numeric_limits<std::size_t>::max() / wideHeight &&
           pixelCount >= wideWidth * wideHeight;
}

LowPolyStats LowPolyRenderer::render(const LowPolyMesh& mesh, const Mat4& model,
                                     const LowPolyCamera& camera,
                                     const LowPolyRenderSettings& settings,
                                     const std::vector<LowPolyBillboard>& billboards,
                                     const LowPolyMaterialBinding& materialBinding) noexcept {
    LowPolyStats stats{};
    if (!finite(camera.position) || !std::isfinite(camera.focalLength) ||
        !std::isfinite(camera.yawRadians) || !std::isfinite(camera.pitchRadians) ||
        !std::isfinite(camera.nearPlane) || camera.nearPlane <= 0.0F ||
        !std::isfinite(camera.farPlane) || camera.farPlane <= camera.nearPlane ||
        !std::isfinite(settings.orthographicHeight) || settings.orthographicHeight <= 0.0F ||
        !finite(settings.directionalLight) || !std::isfinite(settings.directionalStrength) ||
        !std::isfinite(settings.ambientLight) || settings.maximumTextureDimension <= 0) {
        return stats;
    }
    const auto qualityTriangleLimit = settings.quality == LowPolyQuality::Low ? std::size_t{512U}
                                      : settings.quality == LowPolyQuality::Medium
                                          ? std::size_t{2048U}
                                          : std::size_t{65536U};
    const auto triangleLimit =
        std::min(mesh.triangles.size(), std::min(settings.maximumTriangles, qualityTriangleLimit));
    const auto billboardLimit =
        std::min(billboards.size(), std::min(settings.maximumBillboards, std::size_t{4096U}));
    const auto rejected =
        mesh.triangles.size() - triangleLimit + billboards.size() - billboardLimit;
    stats.capacityRejected = static_cast<std::uint32_t>(
        std::min(rejected, static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())));

    std::vector<ProjectedTriangle> projected;
    projected.reserve(triangleLimit + billboardLimit);
    auto lightDirection = settings.directionalLight;
    const auto lightLength = length(lightDirection);
    if (lightLength > 0.0001F) {
        lightDirection = lightDirection * (1.0F / lightLength);
    } else {
        lightDirection = {0.0F, 0.0F, -1.0F};
    }
    const auto ambient = std::clamp(settings.ambientLight, 0.0F, 1.0F);
    const auto yawCosine = std::cos(camera.yawRadians);
    const auto yawSine = std::sin(camera.yawRadians);
    const auto pitchCosine = std::cos(camera.pitchRadians);
    const auto pitchSine = std::sin(camera.pitchRadians);
    const auto worldToView = [&](const Vec3 world) {
        const auto translated = subtract(world, camera.position);
        const auto yawX = yawCosine * translated.x - yawSine * translated.z;
        const auto yawZ = yawSine * translated.x + yawCosine * translated.z;
        return Vec3{yawX, pitchCosine * translated.y + pitchSine * yawZ,
                    -pitchSine * translated.y + pitchCosine * yawZ};
    };
    const auto project = [&](const Vec3 point) -> Vec2 {
        if (settings.projection == LowPolyProjection::Orthographic) {
            const auto scale =
                static_cast<float>(framebuffer_.height()) / settings.orthographicHeight;
            return {static_cast<float>(framebuffer_.width()) * 0.5F + point.x * scale,
                    static_cast<float>(framebuffer_.height()) * 0.5F - point.y * scale};
        }
        return {
            static_cast<float>(framebuffer_.width()) * 0.5F +
                point.x * camera.focalLength * static_cast<float>(framebuffer_.height()) / point.z,
            static_cast<float>(framebuffer_.height()) * 0.5F -
                point.y * camera.focalLength * static_cast<float>(framebuffer_.height()) / point.z};
    };

    for (std::size_t triangleIndex = 0U; triangleIndex < triangleLimit; ++triangleIndex) {
        const auto& triangle = mesh.triangles[triangleIndex];
        ++stats.submitted;
        if (triangle.a >= mesh.vertices.size() || triangle.b >= mesh.vertices.size() ||
            triangle.c >= mesh.vertices.size()) {
            ++stats.culled;
            continue;
        }
        std::array<Vec3, 3> viewPoints{
            worldToView(model.transformPoint(mesh.vertices[triangle.a].position)),
            worldToView(model.transformPoint(mesh.vertices[triangle.b].position)),
            worldToView(model.transformPoint(mesh.vertices[triangle.c].position))};
        if (!finite(viewPoints[0]) || !finite(viewPoints[1]) || !finite(viewPoints[2]) ||
            viewPoints[0].z <= camera.nearPlane || viewPoints[1].z <= camera.nearPlane ||
            viewPoints[2].z <= camera.nearPlane || viewPoints[0].z >= camera.farPlane ||
            viewPoints[1].z >= camera.farPlane || viewPoints[2].z >= camera.farPlane) {
            ++stats.culled;
            continue;
        }
        const auto normal =
            cross(subtract(viewPoints[1], viewPoints[0]), subtract(viewPoints[2], viewPoints[0]));
        const auto* material =
            triangle.material != nullptr ? triangle.material : materialBinding.material;
        const bool usesResolvedTexture =
            material != nullptr && materialBinding.material == material && settings.textures &&
            settings.quality != LowPolyQuality::Low &&
            material->colorMode == MaterialColorMode::Texture && material->baseTexture &&
            materialBinding.texture.valid(settings.maximumTextureDimension);
        if (settings.backfaceCulling && normal.z >= 0.0F &&
            (material == nullptr || !material->doubleSided)) {
            ++stats.culled;
            continue;
        }

        ProjectedTriangle item;
        item.depth = (viewPoints[0].z + viewPoints[1].z + viewPoints[2].z) / 3.0F;
        item.color = triangle.color;
        if (material != nullptr) {
            item.blend = material->blend;
            if (usesResolvedTexture) {
                // Texture pixels are the base color. Mesh face colors remain the fallback for an
                // unresolved texture, but must not unpredictably tint a resolved material.
                item.color = {255U, 255U, 255U, 255U};
            } else if (material->colorMode == MaterialColorMode::Flat) {
                item.color = material->flatColor;
            } else if (material->colorMode == MaterialColorMode::Vertex &&
                       settings.quality == LowPolyQuality::High) {
                item.color = multiply(item.color, average(mesh.vertices[triangle.a].color,
                                                          mesh.vertices[triangle.b].color,
                                                          mesh.vertices[triangle.c].color));
            }
            item.color = multiply(item.color, material->tint);
        }
        const auto normalLength = std::max(0.0001F, length(normal));
        const auto normalizedNormal = normal * (1.0F / normalLength);
        const auto directional =
            settings.quality == LowPolyQuality::Low
                ? ambient
                : std::max(ambient, dot(normalizedNormal, lightDirection) *
                                        std::max(0.0F, settings.directionalStrength));
        const auto light = std::clamp(directional, 0.0F, 1.0F);
        if (material == nullptr || material->lighting != MaterialLightingMode::Unlit) {
            item.color.r = static_cast<std::uint8_t>(static_cast<float>(item.color.r) * light);
            item.color.g = static_cast<std::uint8_t>(static_cast<float>(item.color.g) * light);
            item.color.b = static_cast<std::uint8_t>(static_cast<float>(item.color.b) * light);
        }
        if (material != nullptr && material->emissiveStrength != 0U) {
            const auto addEmission = [strength =
                                          static_cast<std::uint32_t>(material->emissiveStrength)](
                                         std::uint8_t color, std::uint8_t emission) {
                return static_cast<std::uint8_t>(
                    std::min(255U, static_cast<std::uint32_t>(color) +
                                       static_cast<std::uint32_t>(emission) * strength / 255U));
            };
            item.color.r = addEmission(item.color.r, material->emissive.r);
            item.color.g = addEmission(item.color.g, material->emissive.g);
            item.color.b = addEmission(item.color.b, material->emissive.b);
        }
        if (settings.quality != LowPolyQuality::Low && material != nullptr &&
            material->participatesInFog && camera.fogEnd > camera.fogStart) {
            const auto fogAmount =
                (item.depth - camera.fogStart) / (camera.fogEnd - camera.fogStart);
            item.color = interpolate(item.color, camera.fogColor, fogAmount);
        }
        for (std::size_t index = 0; index < viewPoints.size(); ++index) {
            item.points[index] = project(viewPoints[index]);
        }
        if (usesResolvedTexture) {
            item.texture = materialBinding.texture;
            item.textureCoordinates = {mesh.vertices[triangle.a].uv,
                                       mesh.vertices[triangle.b].uv,
                                       mesh.vertices[triangle.c].uv};
        }
        const auto allLeft = std::all_of(item.points.begin(), item.points.end(),
                                         [](const auto point) { return point.x < 0.0F; });
        const auto allRight =
            std::all_of(item.points.begin(), item.points.end(), [&](const auto point) {
                return point.x >= static_cast<float>(framebuffer_.width());
            });
        const auto allAbove = std::all_of(item.points.begin(), item.points.end(),
                                          [](const auto point) { return point.y < 0.0F; });
        const auto allBelow =
            std::all_of(item.points.begin(), item.points.end(), [&](const auto point) {
                return point.y >= static_cast<float>(framebuffer_.height());
            });
        if (allLeft || allRight || allAbove || allBelow) {
            ++stats.culled;
            ++stats.frustumCulled;
            continue;
        }
        projected.push_back(item);
    }

    for (std::size_t billboardIndex = 0U; billboardIndex < billboardLimit; ++billboardIndex) {
        const auto& billboard = billboards[billboardIndex];
        if (!finite(billboard.position) || !std::isfinite(billboard.size.x) ||
            !std::isfinite(billboard.size.y) || billboard.size.x <= 0.0F ||
            billboard.size.y <= 0.0F) {
            ++stats.culled;
            continue;
        }
        const auto view = worldToView(billboard.position);
        if (view.z <= camera.nearPlane || view.z >= camera.farPlane) {
            ++stats.culled;
            continue;
        }
        const auto center = project(view);
        const auto scale =
            settings.projection == LowPolyProjection::Orthographic
                ? static_cast<float>(framebuffer_.height()) / settings.orthographicHeight
                : camera.focalLength * static_cast<float>(framebuffer_.height()) / view.z;
        ProjectedTriangle item;
        item.depth = view.z;
        item.color = billboard.color;
        item.blend =
            billboard.color.a == 255U ? MaterialBlendMode::Opaque : MaterialBlendMode::Alpha;
        item.billboard = true;
        item.billboardBounds = {center.x - billboard.size.x * scale * 0.5F,
                                center.y - billboard.size.y * scale * 0.5F,
                                billboard.size.x * scale, billboard.size.y * scale};
        if (item.billboardBounds.x + item.billboardBounds.width <= 0.0F ||
            item.billboardBounds.y + item.billboardBounds.height <= 0.0F ||
            item.billboardBounds.x >= static_cast<float>(framebuffer_.width()) ||
            item.billboardBounds.y >= static_cast<float>(framebuffer_.height())) {
            ++stats.culled;
            ++stats.frustumCulled;
            continue;
        }
        projected.push_back(item);
    }

    std::sort(projected.begin(), projected.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.depth > rhs.depth; });
    for (const auto& triangle : projected) {
        if (triangle.billboard) {
            const auto x = static_cast<int>(std::floor(triangle.billboardBounds.x));
            const auto y = static_cast<int>(std::floor(triangle.billboardBounds.y));
            const auto width = static_cast<int>(std::ceil(triangle.billboardBounds.width));
            const auto height = static_cast<int>(std::ceil(triangle.billboardBounds.height));
            if (triangle.blend == MaterialBlendMode::Alpha) {
                for (auto row = 0; row < height; ++row) {
                    for (auto column = 0; column < width; ++column) {
                        framebuffer_.blendPixel(x + column, y + row, triangle.color);
                    }
                }
            } else {
                framebuffer_.fillRect(x, y, width, height, triangle.color);
            }
            ++stats.billboards;
        } else if (triangle.texture.valid(settings.maximumTextureDimension)) {
            ++stats.texturedTriangles;
            const auto pixels = drawTexturedTriangle(framebuffer_, triangle);
            stats.texturedPixels =
                pixels > std::numeric_limits<std::uint32_t>::max() - stats.texturedPixels
                    ? std::numeric_limits<std::uint32_t>::max()
                    : stats.texturedPixels + pixels;
        } else if (triangle.blend == MaterialBlendMode::Alpha) {
            framebuffer_.fillTriangleBlended(triangle.points[0], triangle.points[1],
                                             triangle.points[2], triangle.color);
        } else {
            framebuffer_.fillTriangle(triangle.points[0], triangle.points[1], triangle.points[2],
                                      triangle.color);
        }
        ++stats.drawn;
    }
    return stats;
}

LowPolyMesh makeDemoCube() {
    LowPolyMesh mesh;
    mesh.vertices = {{{-1.0F, -1.0F, -1.0F}}, {{1.0F, -1.0F, -1.0F}}, {{1.0F, 1.0F, -1.0F}},
                     {{-1.0F, 1.0F, -1.0F}},  {{-1.0F, -1.0F, 1.0F}}, {{1.0F, -1.0F, 1.0F}},
                     {{1.0F, 1.0F, 1.0F}},    {{-1.0F, 1.0F, 1.0F}}};
    const Color red{210, 65, 60, 255};
    const Color green{65, 190, 100, 255};
    const Color blue{65, 110, 215, 255};
    const Color yellow{220, 190, 70, 255};
    const Color cyan{60, 195, 205, 255};
    const Color magenta{195, 70, 195, 255};
    mesh.triangles = {{0, 2, 1, red},  {0, 3, 2, red},  {4, 5, 6, green},   {4, 6, 7, green},
                      {0, 1, 5, blue}, {0, 5, 4, blue}, {3, 7, 6, yellow},  {3, 6, 2, yellow},
                      {1, 2, 6, cyan}, {1, 6, 5, cyan}, {0, 4, 7, magenta}, {0, 7, 3, magenta}};
    return mesh;
}

} // namespace fabgl::rendering
