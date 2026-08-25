#pragma once

#include <fabgl/assets/asset_importer.h>
#include <fabgl/core/result.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace fabgl::assets {

struct MeshPosition final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct MeshTextureCoordinate final {
    float u = 0.0F;
    float v = 0.0F;
};

struct LowPolyMeshLimits final {
    std::uint32_t maximumVertices = 65'535;
    std::uint32_t maximumTriangles = 65'535;
    std::uint32_t maximumFaceVertices = 64;
};

struct LowPolyMesh final {
    std::vector<MeshPosition> positions;
    // Empty for untextured legacy meshes, otherwise one normalized/atlas UV per position.
    // OBJ import duplicates positions when one position is referenced with multiple UVs so the
    // runtime keeps a compact, single-index triangle stream.
    std::vector<MeshTextureCoordinate> textureCoordinates;
    std::vector<std::uint16_t> indices;
    MeshPosition boundsMinimum;
    MeshPosition boundsMaximum;

    [[nodiscard]] bool valid(const LowPolyMeshLimits& limits = {}) const noexcept;
};

[[nodiscard]] Result<LowPolyMesh> importWavefrontObj(
    std::string_view source, const LowPolyMeshLimits& limits = {});
[[nodiscard]] Result<std::vector<std::uint8_t>> encodeLowPolyMesh(const LowPolyMesh& mesh);
[[nodiscard]] Result<LowPolyMesh> inspectLowPolyMesh(
    const std::vector<std::uint8_t>& bytes, const LowPolyMeshLimits& limits = {});

class WavefrontObjImporter final : public IAssetImporter {
  public:
    [[nodiscard]] std::string_view id() const noexcept override;
    [[nodiscard]] std::uint32_t version() const noexcept override;
    [[nodiscard]] AssetKind kind() const noexcept override;
    [[nodiscard]] std::vector<std::string> extensions() const override;
    [[nodiscard]] Result<ImportedAsset> import(const AssetImportRequest& request) const override;
};

} // namespace fabgl::assets
