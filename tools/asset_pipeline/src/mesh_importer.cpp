#include <fabgl/assets/mesh_importer.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <string>
#include <utility>

namespace fabgl::assets {
namespace {

constexpr std::size_t MeshHeaderSize = 40U;

[[nodiscard]] Error formatError(std::string message) {
    return Error(ErrorCode::InvalidFormat, std::move(message));
}

[[nodiscard]] std::string_view trim(std::string_view value) noexcept {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' ||
                              value.front() == '\r')) {
        value.remove_prefix(1U);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' ||
                              value.back() == '\r')) {
        value.remove_suffix(1U);
    }
    return value;
}

[[nodiscard]] std::vector<std::string_view> words(std::string_view value) {
    std::vector<std::string_view> result;
    while (true) {
        value = trim(value);
        if (value.empty()) {
            break;
        }
        const auto end = value.find_first_of(" \t\r");
        result.push_back(value.substr(0U, end));
        if (end == std::string_view::npos) {
            break;
        }
        value.remove_prefix(end);
    }
    return result;
}

[[nodiscard]] bool parseFloat(const std::string_view token, float& value) noexcept {
    std::istringstream stream{std::string(token)};
    stream.imbue(std::locale::classic());
    if (!(stream >> value) || !std::isfinite(value)) {
        return false;
    }
    char trailing = '\0';
    if (stream.get(trailing)) {
        return false;
    }
    if (value == 0.0F) {
        value = 0.0F;
    }
    return true;
}

[[nodiscard]] bool parseSigned(const std::string_view token, std::int32_t& value) noexcept {
    if (token.empty() || token.front() == '+') {
        return false;
    }
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value, 10);
    return parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size() && value != 0;
}

[[nodiscard]] bool resolveIndex(const std::int32_t raw, const std::size_t count,
                                std::size_t& resolved) noexcept {
    const auto candidate = raw > 0 ? static_cast<std::int64_t>(raw) - 1
                                   : static_cast<std::int64_t>(count) + raw;
    if (candidate < 0 || candidate >= static_cast<std::int64_t>(count)) {
        return false;
    }
    resolved = static_cast<std::size_t>(candidate);
    return true;
}

struct FaceReference final {
    std::size_t position = 0;
    std::size_t textureCoordinate = 0;
    bool hasTextureCoordinate = false;
};

[[nodiscard]] bool parseFaceReference(const std::string_view token, const std::size_t positions,
                                      const std::size_t textureCoordinates,
                                      const std::size_t normals, FaceReference& output) noexcept {
    std::string_view parts[3];
    auto partCount = std::size_t{0};
    auto begin = std::size_t{0};
    while (begin <= token.size()) {
        if (partCount == 3U) {
            return false;
        }
        const auto slash = token.find('/', begin);
        parts[partCount++] = token.substr(
            begin, slash == std::string_view::npos ? token.size() - begin : slash - begin);
        if (slash == std::string_view::npos) {
            break;
        }
        begin = slash + 1U;
    }
    std::int32_t raw = 0;
    if (partCount == 0U || !parseSigned(parts[0], raw) ||
        !resolveIndex(raw, positions, output.position)) {
        return false;
    }
    if (partCount == 2U) {
        output.hasTextureCoordinate = true;
        return parseSigned(parts[1], raw) &&
               resolveIndex(raw, textureCoordinates, output.textureCoordinate);
    }
    if (partCount == 3U) {
        std::size_t ignored = 0;
        if (!parts[1].empty()) {
            output.hasTextureCoordinate = true;
            if (!parseSigned(parts[1], raw) ||
                !resolveIndex(raw, textureCoordinates, output.textureCoordinate)) {
                return false;
            }
        }
        return !parts[2].empty() && parseSigned(parts[2], raw) &&
               resolveIndex(raw, normals, ignored);
    }
    return true;
}

[[nodiscard]] bool nonDegenerate(const MeshPosition& a, const MeshPosition& b,
                                 const MeshPosition& c) noexcept {
    const auto abx = static_cast<double>(b.x) - a.x;
    const auto aby = static_cast<double>(b.y) - a.y;
    const auto abz = static_cast<double>(b.z) - a.z;
    const auto acx = static_cast<double>(c.x) - a.x;
    const auto acy = static_cast<double>(c.y) - a.y;
    const auto acz = static_cast<double>(c.z) - a.z;
    const auto cx = aby * acz - abz * acy;
    const auto cy = abz * acx - abx * acz;
    const auto cz = abx * acy - aby * acx;
    const auto squared = cx * cx + cy * cy + cz * cz;
    return std::isfinite(squared) && squared > 0.0;
}

void calculateBounds(LowPolyMesh& mesh) noexcept {
    mesh.boundsMinimum = mesh.positions.front();
    mesh.boundsMaximum = mesh.positions.front();
    for (const auto& position : mesh.positions) {
        mesh.boundsMinimum.x = std::min(mesh.boundsMinimum.x, position.x);
        mesh.boundsMinimum.y = std::min(mesh.boundsMinimum.y, position.y);
        mesh.boundsMinimum.z = std::min(mesh.boundsMinimum.z, position.z);
        mesh.boundsMaximum.x = std::max(mesh.boundsMaximum.x, position.x);
        mesh.boundsMaximum.y = std::max(mesh.boundsMaximum.y, position.y);
        mesh.boundsMaximum.z = std::max(mesh.boundsMaximum.z, position.z);
    }
}

[[nodiscard]] bool samePosition(const MeshPosition& left, const MeshPosition& right) noexcept {
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

void appendU16(std::vector<std::uint8_t>& output, const std::uint16_t value) {
    const auto wide = static_cast<std::uint32_t>(value);
    output.push_back(static_cast<std::uint8_t>(wide & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((wide >> 8U) & 0xFFU));
}

void appendU32(std::vector<std::uint8_t>& output, const std::uint32_t value) {
    for (unsigned int shift = 0; shift < 32U; shift += 8U) {
        output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void appendFloat(std::vector<std::uint8_t>& output, const float value) {
    auto bits = std::uint32_t{0};
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    appendU32(output, bits);
}

[[nodiscard]] std::uint16_t readU16(const std::vector<std::uint8_t>& bytes,
                                    const std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset]) |
                                      (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U));
}

[[nodiscard]] std::uint32_t readU32(const std::vector<std::uint8_t>& bytes,
                                    const std::size_t offset) noexcept {
    auto value = std::uint32_t{0};
    for (unsigned int shift = 0; shift < 32U; shift += 8U) {
        value |= static_cast<std::uint32_t>(bytes[offset + shift / 8U]) << shift;
    }
    return value;
}

[[nodiscard]] float readFloat(const std::vector<std::uint8_t>& bytes,
                              const std::size_t offset) noexcept {
    const auto bits = readU32(bytes, offset);
    auto value = 0.0F;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

} // namespace

bool LowPolyMesh::valid(const LowPolyMeshLimits& limits) const noexcept {
    if (positions.size() < 3U || positions.size() > limits.maximumVertices ||
        positions.size() > std::numeric_limits<std::uint16_t>::max() || indices.empty() ||
        indices.size() % 3U != 0U || indices.size() / 3U > limits.maximumTriangles ||
        (!textureCoordinates.empty() && textureCoordinates.size() != positions.size())) {
        return false;
    }
    for (const auto& position : positions) {
        if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
            !std::isfinite(position.z)) {
            return false;
        }
    }
    for (const auto& coordinate : textureCoordinates) {
        if (!std::isfinite(coordinate.u) || !std::isfinite(coordinate.v)) {
            return false;
        }
    }
    for (auto index = std::size_t{0}; index < indices.size(); index += 3U) {
        const auto a = indices[index];
        const auto b = indices[index + 1U];
        const auto c = indices[index + 2U];
        if (a >= positions.size() || b >= positions.size() || c >= positions.size() ||
            !nonDegenerate(positions[a], positions[b], positions[c])) {
            return false;
        }
    }
    LowPolyMesh checked;
    checked.positions = positions;
    calculateBounds(checked);
    return samePosition(boundsMinimum, checked.boundsMinimum) &&
           samePosition(boundsMaximum, checked.boundsMaximum);
}

Result<LowPolyMesh> importWavefrontObj(const std::string_view source,
                                       const LowPolyMeshLimits& limits) {
    if (source.empty() || source.size() > 32U * 1024U * 1024U || limits.maximumVertices == 0U ||
        limits.maximumTriangles == 0U || limits.maximumFaceVertices < 3U) {
        return Result<LowPolyMesh>::failure(
            Error(ErrorCode::InvalidArgument, "OBJ source or import limits are invalid"));
    }
    LowPolyMesh result;
    std::vector<MeshPosition> sourcePositions;
    std::vector<MeshTextureCoordinate> sourceTextureCoordinates;
    std::vector<MeshTextureCoordinate> outputTextureCoordinates;
    std::map<std::pair<std::size_t, std::size_t>, std::uint16_t> outputVertices;
    constexpr std::size_t MissingTextureCoordinate = std::numeric_limits<std::size_t>::max();
    bool hasReferencedTextureCoordinate = false;
    auto normalCount = std::size_t{0};
    auto position = std::size_t{0};
    auto lineNumber = std::uint32_t{0};
    while (position <= source.size()) {
        const auto end = source.find('\n', position);
        auto line = source.substr(position, end == std::string_view::npos ? source.size() - position
                                                                         : end - position);
        ++lineNumber;
        if (line.size() > 4096U) {
            return Result<LowPolyMesh>::failure(formatError("OBJ line exceeds 4096 bytes"));
        }
        const auto comment = line.find('#');
        if (comment != std::string_view::npos) {
            line = line.substr(0U, comment);
        }
        line = trim(line);
        if (!line.empty()) {
            const auto separator = line.find_first_of(" \t\r");
            const auto directive = line.substr(0U, separator);
            const auto fields = words(separator == std::string_view::npos
                                          ? std::string_view{}
                                          : line.substr(separator));
            if (directive == "v") {
                MeshPosition vertex;
                if (fields.size() != 3U || !parseFloat(fields[0], vertex.x) ||
                    !parseFloat(fields[1], vertex.y) || !parseFloat(fields[2], vertex.z)) {
                    return Result<LowPolyMesh>::failure(formatError("OBJ vertex must contain finite XYZ values")
                                                            .addContext("line", std::to_string(lineNumber)));
                }
                if (sourcePositions.size() >= limits.maximumVertices ||
                    sourcePositions.size() >= std::numeric_limits<std::uint16_t>::max()) {
                    return Result<LowPolyMesh>::failure(
                        Error(ErrorCode::CapacityExceeded, "OBJ exceeds the vertex limit"));
                }
                sourcePositions.push_back(vertex);
            } else if (directive == "vt") {
                if (fields.empty() || fields.size() > 3U) {
                    return Result<LowPolyMesh>::failure(formatError("OBJ texture coordinate is invalid"));
                }
                MeshTextureCoordinate coordinate;
                if (!parseFloat(fields[0], coordinate.u) ||
                    (fields.size() >= 2U && !parseFloat(fields[1], coordinate.v))) {
                    return Result<LowPolyMesh>::failure(
                        formatError("OBJ texture coordinate is invalid"));
                }
                for (std::size_t field = 2U; field < fields.size(); ++field) {
                    float ignored = 0.0F;
                    if (!parseFloat(fields[field], ignored)) {
                        return Result<LowPolyMesh>::failure(formatError("OBJ texture coordinate is invalid"));
                    }
                }
                sourceTextureCoordinates.push_back(coordinate);
            } else if (directive == "vn") {
                if (fields.size() != 3U) {
                    return Result<LowPolyMesh>::failure(formatError("OBJ normal is invalid"));
                }
                for (const auto field : fields) {
                    float ignored = 0.0F;
                    if (!parseFloat(field, ignored)) {
                        return Result<LowPolyMesh>::failure(formatError("OBJ normal is invalid"));
                    }
                }
                ++normalCount;
            } else if (directive == "f") {
                if (fields.size() < 3U || fields.size() > limits.maximumFaceVertices) {
                    return Result<LowPolyMesh>::failure(formatError("OBJ face size is outside limits"));
                }
                std::vector<FaceReference> references(fields.size());
                for (auto index = std::size_t{0}; index < fields.size(); ++index) {
                    if (!parseFaceReference(fields[index], sourcePositions.size(),
                                            sourceTextureCoordinates.size(), normalCount,
                                            references[index])) {
                        return Result<LowPolyMesh>::failure(formatError("OBJ face reference is invalid")
                                                                .addContext("line", std::to_string(lineNumber)));
                    }
                }
                for (auto index = std::size_t{1}; index + 1U < references.size(); ++index) {
                    if (result.indices.size() / 3U >= limits.maximumTriangles) {
                        return Result<LowPolyMesh>::failure(
                            Error(ErrorCode::CapacityExceeded, "OBJ exceeds the triangle limit"));
                    }
                    const auto a = references[0].position;
                    const auto b = references[index].position;
                    const auto c = references[index + 1U].position;
                    if (!nonDegenerate(sourcePositions[a], sourcePositions[b], sourcePositions[c])) {
                        return Result<LowPolyMesh>::failure(formatError("OBJ face creates a degenerate triangle")
                                                                .addContext("line", std::to_string(lineNumber)));
                    }
                    const std::array triangleReferences{references[0], references[index],
                                                        references[index + 1U]};
                    for (const auto& reference : triangleReferences) {
                        const auto textureCoordinate = reference.hasTextureCoordinate
                                                           ? reference.textureCoordinate
                                                           : MissingTextureCoordinate;
                        const auto key = std::pair{reference.position, textureCoordinate};
                        auto found = outputVertices.find(key);
                        if (found == outputVertices.end()) {
                            if (result.positions.size() >= limits.maximumVertices ||
                                result.positions.size() >=
                                    std::numeric_limits<std::uint16_t>::max()) {
                                return Result<LowPolyMesh>::failure(Error(
                                    ErrorCode::CapacityExceeded,
                                    "OBJ position/UV pairs exceed the vertex limit"));
                            }
                            const auto outputIndex =
                                static_cast<std::uint16_t>(result.positions.size());
                            result.positions.push_back(sourcePositions[reference.position]);
                            outputTextureCoordinates.push_back(
                                reference.hasTextureCoordinate
                                    ? sourceTextureCoordinates[reference.textureCoordinate]
                                    : MeshTextureCoordinate{});
                            found = outputVertices.emplace(key, outputIndex).first;
                        }
                        result.indices.push_back(found->second);
                        hasReferencedTextureCoordinate =
                            hasReferencedTextureCoordinate || reference.hasTextureCoordinate;
                    }
                }
            } else if (directive != "o" && directive != "g" && directive != "s" &&
                       directive != "usemtl" && directive != "mtllib") {
                return Result<LowPolyMesh>::failure(formatError("OBJ directive is unsupported")
                                                        .addContext("directive", std::string(directive))
                                                        .addContext("line", std::to_string(lineNumber)));
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        position = end + 1U;
    }
    if (result.positions.empty() || result.indices.empty()) {
        return Result<LowPolyMesh>::failure(formatError("OBJ contains no triangle mesh"));
    }
    if (hasReferencedTextureCoordinate) {
        result.textureCoordinates = std::move(outputTextureCoordinates);
    }
    calculateBounds(result);
    if (!result.valid(limits)) {
        return Result<LowPolyMesh>::failure(formatError("OBJ mesh failed validation"));
    }
    return Result<LowPolyMesh>::success(std::move(result));
}

Result<std::vector<std::uint8_t>> encodeLowPolyMesh(const LowPolyMesh& mesh) {
    if (!mesh.valid()) {
        return Result<std::vector<std::uint8_t>>::failure(
            Error(ErrorCode::InvalidArgument, "low-poly mesh cannot be encoded"));
    }
    const bool hasTextureCoordinates = !mesh.textureCoordinates.empty();
    std::vector<std::uint8_t> output;
    output.reserve(MeshHeaderSize + mesh.positions.size() * 12U +
                   mesh.textureCoordinates.size() * 8U + mesh.indices.size() * 2U);
    output.insert(output.end(), {'F', 'G', 'L', 'M'});
    appendU16(output, 2U);
    appendU16(output, hasTextureCoordinates ? 1U : 0U);
    appendU32(output, static_cast<std::uint32_t>(mesh.positions.size()));
    appendU32(output, static_cast<std::uint32_t>(mesh.indices.size()));
    appendFloat(output, mesh.boundsMinimum.x);
    appendFloat(output, mesh.boundsMinimum.y);
    appendFloat(output, mesh.boundsMinimum.z);
    appendFloat(output, mesh.boundsMaximum.x);
    appendFloat(output, mesh.boundsMaximum.y);
    appendFloat(output, mesh.boundsMaximum.z);
    for (const auto& position : mesh.positions) {
        appendFloat(output, position.x);
        appendFloat(output, position.y);
        appendFloat(output, position.z);
    }
    for (const auto& coordinate : mesh.textureCoordinates) {
        appendFloat(output, coordinate.u);
        appendFloat(output, coordinate.v);
    }
    for (const auto index : mesh.indices) {
        appendU16(output, index);
    }
    return Result<std::vector<std::uint8_t>>::success(std::move(output));
}

Result<LowPolyMesh> inspectLowPolyMesh(const std::vector<std::uint8_t>& bytes,
                                       const LowPolyMeshLimits& limits) {
    if (bytes.size() < MeshHeaderSize || bytes[0] != 'F' || bytes[1] != 'G' ||
        bytes[2] != 'L' || bytes[3] != 'M') {
        return Result<LowPolyMesh>::failure(formatError("mesh magic is invalid"));
    }
    const auto version = readU16(bytes, 4U);
    if (version != 1U && version != 2U) {
        return Result<LowPolyMesh>::failure(
            Error(ErrorCode::UnsupportedVersion, "mesh version is unsupported"));
    }
    const auto flags = readU16(bytes, 6U);
    if ((version == 1U && flags != 0U) || (version == 2U && (flags & ~1U) != 0U)) {
        return Result<LowPolyMesh>::failure(formatError("mesh flags are invalid"));
    }
    const bool hasTextureCoordinates = version == 2U && (flags & 1U) != 0U;
    const auto vertexCount = readU32(bytes, 8U);
    const auto indexCount = readU32(bytes, 12U);
    const auto expectedSize = static_cast<std::uint64_t>(MeshHeaderSize) +
                              static_cast<std::uint64_t>(vertexCount) * 12U +
                              (hasTextureCoordinates
                                   ? static_cast<std::uint64_t>(vertexCount) * 8U
                                   : 0U) +
                              static_cast<std::uint64_t>(indexCount) * 2U;
    if (expectedSize != bytes.size() || vertexCount > limits.maximumVertices ||
        indexCount / 3U > limits.maximumTriangles) {
        return Result<LowPolyMesh>::failure(formatError("mesh length or declared counts are invalid"));
    }
    LowPolyMesh result;
    result.boundsMinimum = {readFloat(bytes, 16U), readFloat(bytes, 20U), readFloat(bytes, 24U)};
    result.boundsMaximum = {readFloat(bytes, 28U), readFloat(bytes, 32U), readFloat(bytes, 36U)};
    result.positions.reserve(vertexCount);
    auto offset = MeshHeaderSize;
    for (std::uint32_t index = 0; index < vertexCount; ++index) {
        result.positions.push_back(
            {readFloat(bytes, offset), readFloat(bytes, offset + 4U), readFloat(bytes, offset + 8U)});
        offset += 12U;
    }
    if (hasTextureCoordinates) {
        result.textureCoordinates.reserve(vertexCount);
        for (std::uint32_t index = 0; index < vertexCount; ++index) {
            result.textureCoordinates.push_back(
                {readFloat(bytes, offset), readFloat(bytes, offset + 4U)});
            offset += 8U;
        }
    }
    result.indices.reserve(indexCount);
    for (std::uint32_t index = 0; index < indexCount; ++index) {
        result.indices.push_back(readU16(bytes, offset));
        offset += 2U;
    }
    if (!result.valid(limits)) {
        return Result<LowPolyMesh>::failure(formatError("mesh payload failed validation"));
    }
    return Result<LowPolyMesh>::success(std::move(result));
}

std::string_view WavefrontObjImporter::id() const noexcept { return "fabgl.mesh.obj"; }
std::uint32_t WavefrontObjImporter::version() const noexcept { return 2U; }
AssetKind WavefrontObjImporter::kind() const noexcept { return AssetKind::LowPolyMesh; }
std::vector<std::string> WavefrontObjImporter::extensions() const { return {"obj"}; }
Result<ImportedAsset> WavefrontObjImporter::import(const AssetImportRequest& request) const {
    const auto source = std::string_view(
        reinterpret_cast<const char*>(request.sourceBytes.data()), request.sourceBytes.size());
    auto mesh = importWavefrontObj(source);
    if (!mesh) {
        return Result<ImportedAsset>::failure(mesh.error());
    }
    auto encoded = encodeLowPolyMesh(mesh.value());
    if (!encoded) {
        return Result<ImportedAsset>::failure(encoded.error());
    }
    ImportedAsset output;
    output.payload = std::move(encoded.value());
    output.flashBytes = output.payload.size();
    output.internalRamBytes = mesh.value().positions.size() * sizeof(MeshPosition) +
                              mesh.value().textureCoordinates.size() *
                                  sizeof(MeshTextureCoordinate) +
                              mesh.value().indices.size() * sizeof(std::uint16_t);
    output.estimatedDecodeMicros = static_cast<std::uint32_t>(mesh.value().indices.size() / 3U);
    return Result<ImportedAsset>::success(std::move(output));
}

} // namespace fabgl::assets
