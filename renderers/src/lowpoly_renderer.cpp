#include <fabgl/rendering/lowpoly_renderer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace fabgl::rendering {

namespace {

struct ProjectedTriangle final {
    std::array<Vec2, 3> points{};
    float depth = 0.0F;
    Color color{};
};

[[nodiscard]] Vec3 subtract(Vec3 lhs, Vec3 rhs) noexcept {
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

[[nodiscard]] Vec3 cross(Vec3 lhs, Vec3 rhs) noexcept {
    return {lhs.y * rhs.z - lhs.z * rhs.y, lhs.z * rhs.x - lhs.x * rhs.z,
            lhs.x * rhs.y - lhs.y * rhs.x};
}

} // namespace

LowPolyStats LowPolyRenderer::render(const LowPolyMesh& mesh, const Mat4& model,
                                     const LowPolyCamera& camera) noexcept {
    LowPolyStats stats{};
    std::vector<ProjectedTriangle> projected;
    projected.reserve(mesh.triangles.size());
    for (const auto& triangle : mesh.triangles) {
        ++stats.submitted;
        if (triangle.a >= mesh.vertices.size() || triangle.b >= mesh.vertices.size() ||
            triangle.c >= mesh.vertices.size()) {
            ++stats.culled;
            continue;
        }
        std::array<Vec3, 3> viewPoints{
            subtract(model.transformPoint(mesh.vertices[triangle.a].position), camera.position),
            subtract(model.transformPoint(mesh.vertices[triangle.b].position), camera.position),
            subtract(model.transformPoint(mesh.vertices[triangle.c].position), camera.position)};
        if (viewPoints[0].z <= camera.nearPlane || viewPoints[1].z <= camera.nearPlane ||
            viewPoints[2].z <= camera.nearPlane) {
            ++stats.culled;
            continue;
        }
        const auto normal =
            cross(subtract(viewPoints[1], viewPoints[0]), subtract(viewPoints[2], viewPoints[0]));
        if (normal.z >= 0.0F) {
            ++stats.culled;
            continue;
        }

        ProjectedTriangle item;
        item.depth = (viewPoints[0].z + viewPoints[1].z + viewPoints[2].z) / 3.0F;
        item.color = triangle.color;
        const auto light = std::clamp(
            (-normal.z) / std::max(0.0001F, std::sqrt(normal.x * normal.x + normal.y * normal.y +
                                                      normal.z * normal.z)),
            0.2F, 1.0F);
        item.color.r = static_cast<std::uint8_t>(static_cast<float>(item.color.r) * light);
        item.color.g = static_cast<std::uint8_t>(static_cast<float>(item.color.g) * light);
        item.color.b = static_cast<std::uint8_t>(static_cast<float>(item.color.b) * light);
        for (std::size_t index = 0; index < viewPoints.size(); ++index) {
            const auto& point = viewPoints[index];
            item.points[index] = {static_cast<float>(framebuffer_.width()) * 0.5F +
                                      point.x * camera.focalLength *
                                          static_cast<float>(framebuffer_.height()) / point.z,
                                  static_cast<float>(framebuffer_.height()) * 0.5F -
                                      point.y * camera.focalLength *
                                          static_cast<float>(framebuffer_.height()) / point.z};
        }
        projected.push_back(item);
    }

    std::sort(projected.begin(), projected.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.depth > rhs.depth; });
    for (const auto& triangle : projected) {
        framebuffer_.fillTriangle(triangle.points[0], triangle.points[1], triangle.points[2],
                                  triangle.color);
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
