#include "project_prepare.h"

#include "project_format.h"

#include "esp32_capabilities.h"
#include "esp32_export.h"

#include <fabgl/animation/animation_authoring.h>
#include <fabgl/assets/asset_pack.h>
#include <fabgl/assets/audio_importer.h>
#include <fabgl/assets/file_io.h>
#include <fabgl/assets/font_importer.h>
#include <fabgl/assets/image_pipeline.h>
#include <fabgl/assets/mesh_importer.h>
#include <fabgl/assets/tilemap_importer.h>
#include <fabgl/material/material.h>
#include <fabgl/reflection/reflection.h>
#include <fabgl/rendering/racer_track.h>
#include <fabgl/rendering/raycast_map_asset.h>
#include <fabgl/scene/builtin_components.h>
#include <fabgl/scene/scene.h>
#include <fabgl/serialization/material_serializer.h>
#include <fabgl/serialization/prefab_serializer.h>
#include <fabgl/serialization/scene_serializer.h>
#include <fabgl/visual/visual_graph.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fabgl::project {
namespace {

namespace fs = std::filesystem;

constexpr std::size_t MaximumPreparedSourceBytes = 256U * 1024U * 1024U;
constexpr std::size_t MaximumPreparedPackBytes = 256U * 1024U * 1024U;

struct PreparedPayload final {
    std::vector<std::uint8_t> bytes;
    bool imported = false;
    bool validated = false;
    std::size_t visualPrograms = 0U;
};

[[nodiscard]] fs::path pathFromUtf8(const std::string& value) {
    const auto* begin = reinterpret_cast<const char8_t*>(value.data());
    return fs::path(std::u8string(begin, begin + value.size()));
}

[[nodiscard]] std::string pathText(const fs::path& path) {
    const auto value = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

[[nodiscard]] bool pathInside(const fs::path& path, const fs::path& root) {
    auto child = pathText(path.lexically_normal());
    auto parent = pathText(root.lexically_normal());
#ifdef _WIN32
    std::transform(child.begin(), child.end(), child.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    std::transform(parent.begin(), parent.end(), parent.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
#endif
    if (child == parent)
        return true;
    if (!parent.empty() && parent.back() != '/')
        parent.push_back('/');
    return child.rfind(parent, 0U) == 0U;
}

[[nodiscard]] bool isReparsePoint(const fs::path& path) {
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U;
#else
    std::error_code code;
    return fs::is_symlink(fs::symlink_status(path, code));
#endif
}

[[nodiscard]] Result<void> rejectReparseChain(const fs::path& path, std::string_view description) {
    auto current = path.root_path();
    for (const auto& component : path.relative_path()) {
        current /= component;
        std::error_code code;
        const auto status = fs::symlink_status(current, code);
        if (code) {
            if (code == std::errc::no_such_file_or_directory)
                break;
            return Result<void>::failure(
                Error(ErrorCode::IoError, "filesystem path cannot be inspected")
                    .addContext("kind", std::string(description))
                    .addContext("path", pathText(current))
                    .addContext("system", code.message()));
        }
        if (status.type() == fs::file_type::not_found)
            break;
        if (isReparsePoint(current)) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument,
                      "project preparation cannot traverse a symlink, junction, or reparse point")
                    .addContext("kind", std::string(description))
                    .addContext("path", pathText(current)));
        }
    }
    return Result<void>::success();
}

[[nodiscard]] Result<fs::path> absolutePath(const std::string& input,
                                            std::string_view description) {
    if (input.empty() || input.find('\0') != std::string::npos) {
        return Result<fs::path>::failure(
            Error(ErrorCode::InvalidArgument, "filesystem path is invalid")
                .addContext("kind", std::string(description)));
    }
    std::error_code code;
    auto result = fs::absolute(pathFromUtf8(input), code).lexically_normal();
    if (code) {
        return Result<fs::path>::failure(Error(ErrorCode::IoError, "path cannot be resolved")
                                             .addContext("kind", std::string(description))
                                             .addContext("path", input)
                                             .addContext("system", code.message()));
    }
    return Result<fs::path>::success(std::move(result));
}

[[nodiscard]] std::string parentPath(const std::string& path) {
    const auto separator = path.find_last_of("/\\");
    return separator == std::string::npos ? "." : path.substr(0U, separator);
}

[[nodiscard]] std::string joinPath(const std::string& left, const std::string& right) {
    if (left.empty() || left == ".")
        return left.empty() ? right : left + "/" + right;
    return left + (left.back() == '/' || left.back() == '\\' ? "" : "/") + right;
}

[[nodiscard]] std::string extensionOf(std::string_view path) {
    const auto separator = path.find_last_of("/\\");
    const auto dot = path.find_last_of('.');
    if (dot == std::string_view::npos || dot + 1U == path.size() ||
        (separator != std::string_view::npos && dot < separator))
        return {};
    std::string extension(path.substr(dot + 1U));
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension;
}

[[nodiscard]] std::uint32_t stableTypeId(std::string_view type) noexcept {
    std::uint32_t hash = 2166136261U;
    for (const auto character : type) {
        hash ^= static_cast<std::uint8_t>(character);
        hash *= 16777619U;
    }
    return hash == 0U ? 1U : hash;
}

[[nodiscard]] Result<std::string> safeAssetPath(const std::string& projectRoot,
                                                const std::string& relativePath) {
    if (!assets::isSafeRelativePath(relativePath)) {
        return Result<std::string>::failure(
            Error(ErrorCode::InvalidArgument, "project asset path is unsafe")
                .addContext("asset", relativePath));
    }
    auto absoluteRoot = absolutePath(projectRoot, "project root");
    if (!absoluteRoot)
        return Result<std::string>::failure(absoluteRoot.error());
    auto safeRoot = rejectReparseChain(absoluteRoot.value(), "project root");
    if (!safeRoot)
        return Result<std::string>::failure(safeRoot.error());
    std::error_code code;
    const auto root = fs::weakly_canonical(absoluteRoot.value(), code);
    if (code) {
        return Result<std::string>::failure(
            Error(ErrorCode::IoError, "project root cannot be canonicalized")
                .addContext("path", projectRoot));
    }
    const auto unresolved = (root / pathFromUtf8(relativePath)).lexically_normal();
    if (!pathInside(unresolved, root)) {
        return Result<std::string>::failure(
            Error(ErrorCode::InvalidArgument, "project asset escapes the project root")
                .addContext("asset", relativePath));
    }
    auto safeCandidate = rejectReparseChain(unresolved, "project asset");
    if (!safeCandidate)
        return Result<std::string>::failure(safeCandidate.error());
    const auto candidate = fs::weakly_canonical(unresolved, code);
    if (code || !fs::is_regular_file(candidate, code) || code) {
        return Result<std::string>::failure(Error(ErrorCode::NotFound, "project asset is missing")
                                                .addContext("asset", relativePath));
    }
    if (!pathInside(candidate, root)) {
        return Result<std::string>::failure(
            Error(ErrorCode::InvalidArgument, "project asset escapes the project root")
                .addContext("asset", relativePath));
    }
    const auto utf8 = candidate.u8string();
    return Result<std::string>::success(
        std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size()));
}

[[nodiscard]] Result<void> requireGuid(const AssetGuid expected, const AssetGuid actual,
                                       std::string_view type, std::string_view path) {
    if (expected == actual)
        return Result<void>::success();
    return Result<void>::failure(Error(ErrorCode::InvalidFormat, "asset content GUID mismatch")
                                     .addContext("type", std::string(type))
                                     .addContext("path", std::string(path))
                                     .addContext("expected", expected.toString())
                                     .addContext("actual", actual.toString()));
}

[[nodiscard]] Result<void> requireManifestReference(const Manifest& manifest,
                                                    const AssetGuid referenced,
                                                    std::string_view expectedType,
                                                    std::string_view relationship,
                                                    std::string_view ownerPath) {
    const auto found =
        std::find_if(manifest.assets.begin(), manifest.assets.end(),
                     [referenced](const auto& asset) { return asset.guid == referenced; });
    if (found == manifest.assets.end()) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidFormat, "asset content references an unknown GUID")
                .addContext("path", std::string(ownerPath))
                .addContext("relationship", std::string(relationship))
                .addContext("referenced", referenced.toString()));
    }
    if (!expectedType.empty() && found->type != expectedType) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidFormat, "asset content references the wrong asset type")
                .addContext("path", std::string(ownerPath))
                .addContext("relationship", std::string(relationship))
                .addContext("referenced", referenced.toString())
                .addContext("expectedType", std::string(expectedType))
                .addContext("actualType", found->type));
    }
    return Result<void>::success();
}

[[nodiscard]] Result<VisualHostCallbackTable> projectVisualBuildCallbacks() {
    VisualHostCallbackTable callbacks;
    const std::vector<std::pair<std::string, std::uint8_t>> signatures{
        {"time.delay", 1U},        {"vector.length3", 3U},   {"input.action", 0U},
        {"entity.action", 1U},     {"component.action", 1U}, {"audio.play", 1U},
        {"animation.play", 1U},    {"scene.load", 0U},       {"ui.action", 1U},
        {"function.identity", 1U}, {"math.abs", 1U},         {"math.negate", 1U},
        {"math.clamp01", 1U},      {"math.sign", 1U},
    };
    const auto validationOnly = [](const VisualHostCallDescriptor&,
                                   const std::vector<double>& arguments) {
        return Result<double>::success(arguments.empty() ? 0.0 : arguments.front());
    };
    for (const auto& [name, argumentCount] : signatures) {
        auto added = callbacks.add(name, argumentCount, validationOnly);
        if (!added)
            return Result<VisualHostCallbackTable>::failure(added.error());
    }
    return Result<VisualHostCallbackTable>::success(std::move(callbacks));
}

[[nodiscard]] Result<std::size_t> compileVisualPrograms(VisualGraph graph, const Manifest& manifest,
                                                        const Scene& scene,
                                                        const ReflectionRegistry& reflection) {
    const auto hasAsset = [&manifest](const AssetGuid guid) {
        return std::any_of(manifest.assets.begin(), manifest.assets.end(),
                           [guid](const auto& asset) { return asset.guid == guid; });
    };
    VisualReferenceResolver resolver(
        hasAsset, [&scene](const EntityGuid guid) { return scene.findEntity(guid) != nullptr; },
        [&reflection](const ComponentTypeGuid guid) { return reflection.find(guid) != nullptr; });
    auto callbacks = projectVisualBuildCallbacks();
    if (!callbacks)
        return Result<std::size_t>::failure(callbacks.error());
    const auto& registry = VisualNodeRegistry::builtins();
    std::size_t programs = 0U;
    for (const auto& [id, node] : graph.nodes()) {
        const auto* definition = node.builtinType == VisualBuiltinNodeType::Legacy
                                     ? registry.findLegacy(node.kind)
                                     : registry.find(node.builtinType);
        if (definition == nullptr || definition->execution != VisualExecutionKind::Entry)
            continue;
        auto eventGraph = graph;
        eventGraph.setEntryNode(id);
        auto bytecode =
            VisualGraphCompiler::compile(eventGraph, resolver, {}, registry, &callbacks.value());
        if (!bytecode) {
            return Result<std::size_t>::failure(
                bytecode.error().withContext("visual_entry", std::to_string(id)));
        }
        ++programs;
    }
    if (programs == 0U) {
        return Result<std::size_t>::failure(
            Error(ErrorCode::InvalidFormat, "visual graph contains no event entry"));
    }
    return Result<std::size_t>::success(programs);
}

[[nodiscard]] Result<PreparedPayload> preparePayload(const ProjectAssetEntry& entry,
                                                     const std::string& path,
                                                     const Manifest& manifest, const Scene& scene,
                                                     const ReflectionRegistry& reflection) {
    auto source = assets::readBinaryFile(path);
    if (!source)
        return Result<PreparedPayload>::failure(source.error());
    PreparedPayload output;
    output.bytes = std::move(source.value());
    const auto extension = extensionOf(entry.path);
    const std::string_view text(reinterpret_cast<const char*>(output.bytes.data()),
                                output.bytes.size());

    if (extension == "png" || extension == "jpg" || extension == "jpeg" || extension == "bmp") {
        auto image = assets::loadImage(path);
        if (!image)
            return Result<PreparedPayload>::failure(image.error());
        auto imageSettings = decodeProjectImageImportSettings(entry.importSettings);
        if (!imageSettings)
            return Result<PreparedPayload>::failure(imageSettings.error());
        auto compiled = assets::compileImageAsset(image.value(), imageSettings.value());
        if (!compiled)
            return Result<PreparedPayload>::failure(compiled.error());
        output.bytes = std::move(compiled.value().payload);
        output.imported = true;
        output.validated = true;
    } else if (extension == "wav") {
        auto audioSettings = decodeProjectAudioImportSettings(entry.importSettings);
        if (!audioSettings)
            return Result<PreparedPayload>::failure(audioSettings.error());
        auto clip = assets::importWav(output.bytes, audioSettings.value().settings);
        if (!clip)
            return Result<PreparedPayload>::failure(clip.error());
        output.bytes = assets::encodeAudioClip(clip.value(), audioSettings.value().encoding);
        output.imported = true;
        output.validated = true;
    } else if (extension == "csv" || (extension == "json" && entry.type == "tilemap")) {
        Result<assets::Tilemap> tilemap =
            extension == "csv" ? assets::importCsvTilemap(text) : assets::importJsonTilemap(text);
        if (!tilemap)
            return Result<PreparedPayload>::failure(tilemap.error());
        tilemap.value().guid = entry.guid;
        auto encoded = assets::encodeTilemap(tilemap.value());
        if (!encoded)
            return Result<PreparedPayload>::failure(encoded.error());
        output.bytes = std::move(encoded.value());
        output.imported = true;
        output.validated = true;
    } else if (extension == "obj") {
        auto mesh = assets::importWavefrontObj(text);
        if (!mesh)
            return Result<PreparedPayload>::failure(mesh.error());
        auto encoded = assets::encodeLowPolyMesh(mesh.value());
        if (!encoded)
            return Result<PreparedPayload>::failure(encoded.error());
        output.bytes = std::move(encoded.value());
        output.imported = true;
        output.validated = true;
    } else if (extension == "bdf") {
        auto font = assets::importBdfFont(text);
        if (!font)
            return Result<PreparedPayload>::failure(font.error());
        auto encoded = assets::encodeBitmapFont(font.value());
        if (!encoded)
            return Result<PreparedPayload>::failure(encoded.error());
        output.bytes = std::move(encoded.value());
        output.imported = true;
        output.validated = true;
    } else if (extension == "fgli") {
        auto decoded = assets::decodeIndexedImage(output.bytes);
        if (!decoded)
            return Result<PreparedPayload>::failure(decoded.error());
        output.validated = true;
    } else if (extension == "fgla") {
        auto decoded = assets::decodeAudioClip(output.bytes);
        if (!decoded)
            return Result<PreparedPayload>::failure(decoded.error());
        output.validated = true;
    } else if (extension == "fglt" || extension == "fgltilemap") {
        auto decoded = assets::inspectTilemap(output.bytes);
        if (!decoded)
            return Result<PreparedPayload>::failure(decoded.error());
        if (decoded.value().guid.isNil()) {
            decoded.value().guid = entry.guid;
        } else {
            auto identity = requireGuid(entry.guid, decoded.value().guid, entry.type, entry.path);
            if (!identity)
                return Result<PreparedPayload>::failure(identity.error());
        }
        for (const auto& reference : decoded.value().tilesets) {
            auto validReference = requireManifestReference(manifest, reference.tileset, "tileset",
                                                           "tilemap.tileset", entry.path);
            if (!validReference)
                return Result<PreparedPayload>::failure(validReference.error());
        }
        for (const auto& object : decoded.value().objects) {
            if (object.asset.isNil())
                continue;
            auto validReference = requireManifestReference(manifest, object.asset, {},
                                                           "tilemap.object.asset", entry.path);
            if (!validReference)
                return Result<PreparedPayload>::failure(validReference.error());
        }
        auto canonical = assets::encodeTilemap(decoded.value());
        if (!canonical)
            return Result<PreparedPayload>::failure(canonical.error());
        if (canonical.value() != output.bytes) {
            output.bytes = std::move(canonical.value());
            output.imported = true;
        }
        output.validated = true;
    } else if (extension == "fgltileset") {
        auto decoded = assets::inspectTileset(output.bytes);
        if (!decoded)
            return Result<PreparedPayload>::failure(decoded.error());
        auto identity = requireGuid(entry.guid, decoded.value().guid, entry.type, entry.path);
        if (!identity)
            return Result<PreparedPayload>::failure(identity.error());
        auto validReference = requireManifestReference(manifest, decoded.value().sourceImage,
                                                       "image", "tileset.sourceImage", entry.path);
        if (!validReference)
            return Result<PreparedPayload>::failure(validReference.error());
        output.validated = true;
    } else if (extension == "fglm") {
        auto decoded = assets::inspectLowPolyMesh(output.bytes);
        if (!decoded)
            return Result<PreparedPayload>::failure(decoded.error());
        output.validated = true;
    } else if (extension == "fglf") {
        auto decoded = assets::inspectBitmapFont(output.bytes);
        if (!decoded)
            return Result<PreparedPayload>::failure(decoded.error());
        output.validated = true;
    } else if (extension == "fglvisual") {
        auto graph = deserializeVisualGraph(text);
        if (!graph)
            return Result<PreparedPayload>::failure(graph.error());
        auto identity = requireGuid(entry.guid, graph.value().guid(), entry.type, entry.path);
        if (!identity)
            return Result<PreparedPayload>::failure(identity.error());
        auto programs = compileVisualPrograms(graph.value(), manifest, scene, reflection);
        if (!programs)
            return Result<PreparedPayload>::failure(programs.error());
        output.visualPrograms = programs.value();
        output.validated = true;
    } else if (extension == "fglmaterial") {
        auto material = MaterialSerializer::deserialize(text);
        if (!material)
            return Result<PreparedPayload>::failure(material.error());
        auto identity = requireGuid(entry.guid, material.value().id, entry.type, entry.path);
        if (!identity)
            return Result<PreparedPayload>::failure(identity.error());
        const bool supported =
            validateMaterial(material.value().material, RendererBackend::Renderer2D).valid() ||
            validateMaterial(material.value().material, RendererBackend::Raycast).valid() ||
            validateMaterial(material.value().material, RendererBackend::Racer).valid() ||
            validateMaterial(material.value().material, RendererBackend::LowPoly).valid();
        if (!supported) {
            return Result<PreparedPayload>::failure(
                Error(ErrorCode::InvalidFormat, "material validation failed"));
        }
        output.validated = true;
    } else if (extension == "fglprefab") {
        auto prefab = PrefabSerializer::deserialize(text);
        if (!prefab)
            return Result<PreparedPayload>::failure(prefab.error());
        auto identity = requireGuid(entry.guid, prefab.value().id, entry.type, entry.path);
        if (!identity)
            return Result<PreparedPayload>::failure(identity.error());
        output.validated = true;
    } else if (extension == "fglanim") {
        auto clip = deserializeAnimationClipAsset(text);
        if (!clip)
            return Result<PreparedPayload>::failure(clip.error());
        auto identity = requireGuid(entry.guid, clip.value().guid, entry.type, entry.path);
        if (!identity)
            return Result<PreparedPayload>::failure(identity.error());
        output.validated = true;
    } else if (extension == "fglcontroller") {
        auto controller = deserializeAnimatorControllerAsset(text);
        if (!controller)
            return Result<PreparedPayload>::failure(controller.error());
        auto identity = requireGuid(entry.guid, controller.value().guid, entry.type, entry.path);
        if (!identity)
            return Result<PreparedPayload>::failure(identity.error());
        output.validated = true;
    } else if (extension == "fglray") {
        auto map = rendering::deserializeRaycastMapAsset(text);
        if (!map)
            return Result<PreparedPayload>::failure(map.error());
        auto identity = requireGuid(entry.guid, map.value().guid, entry.type, entry.path);
        if (!identity)
            return Result<PreparedPayload>::failure(identity.error());
        output.validated = true;
    } else if (extension == "fgltrack") {
        auto track = rendering::deserializeRacerTrack(text);
        if (!track)
            return Result<PreparedPayload>::failure(track.error());
        auto identity = requireGuid(entry.guid, track.value().guid, entry.type, entry.path);
        if (!identity)
            return Result<PreparedPayload>::failure(identity.error());
        output.validated = true;
    } else if (extension == "fglscene") {
        auto loaded = SceneSerializer::deserialize(text);
        if (!loaded)
            return Result<PreparedPayload>::failure(loaded.error());
        output.validated = true;
    }
    return Result<PreparedPayload>::success(std::move(output));
}

} // namespace

Result<ProjectPrepareResult> prepareProjectInputs(const std::string& projectManifestPath,
                                                  const std::string& outputDirectory,
                                                  const ProjectPrepareTarget target) {
    auto manifestPath = absolutePath(projectManifestPath, "project manifest");
    if (!manifestPath)
        return Result<ProjectPrepareResult>::failure(manifestPath.error());
    auto safeManifest = rejectReparseChain(manifestPath.value(), "project manifest");
    if (!safeManifest)
        return Result<ProjectPrepareResult>::failure(safeManifest.error());
    std::error_code pathCode;
    if (!fs::is_regular_file(manifestPath.value(), pathCode) || pathCode) {
        return Result<ProjectPrepareResult>::failure(
            Error(ErrorCode::NotFound, "project manifest was not found")
                .addContext("path", pathText(manifestPath.value())));
    }
    auto source = assets::readTextFile(pathText(manifestPath.value()));
    if (!source)
        return Result<ProjectPrepareResult>::failure(source.error());
    auto manifest = parseManifest(source.value());
    if (!manifest)
        return Result<ProjectPrepareResult>::failure(manifest.error());
    const auto projectRootPath = manifestPath.value().parent_path();
    const auto projectRoot = pathText(projectRootPath);
    const auto legacyIndexRelative = std::string(".fabglstudio/asset-index-v1.json");
    const auto legacyIndexCandidate = projectRootPath / pathFromUtf8(legacyIndexRelative);
    std::error_code legacyCode;
    const auto legacyStatus = fs::symlink_status(legacyIndexCandidate, legacyCode);
    if (legacyCode && legacyCode != std::errc::no_such_file_or_directory) {
        return Result<ProjectPrepareResult>::failure(
            Error(ErrorCode::IoError, "legacy asset index cannot be inspected")
                .addContext("path", pathText(legacyIndexCandidate))
                .addContext("system", legacyCode.message()));
    }
    if (!legacyCode && legacyStatus.type() != fs::file_type::not_found) {
        auto legacyPath = safeAssetPath(projectRoot, legacyIndexRelative);
        if (!legacyPath)
            return Result<ProjectPrepareResult>::failure(legacyPath.error());
        auto legacySource = assets::readTextFile(legacyPath.value());
        if (!legacySource)
            return Result<ProjectPrepareResult>::failure(legacySource.error());
        auto migrated = mergeLegacyAssetIndex(legacySource.value(), manifest.value());
        if (!migrated)
            return Result<ProjectPrepareResult>::failure(
                migrated.error().withContext("path", legacyPath.value()));
    }
    auto scenePath = safeAssetPath(projectRoot, manifest.value().startupScene);
    if (!scenePath)
        return Result<ProjectPrepareResult>::failure(scenePath.error());
    auto sceneSource = assets::readTextFile(scenePath.value());
    if (!sceneSource)
        return Result<ProjectPrepareResult>::failure(sceneSource.error());
    auto scene = SceneSerializer::deserialize(sceneSource.value());
    if (!scene)
        return Result<ProjectPrepareResult>::failure(scene.error());
    std::vector<Esp32PortableScriptSource> portableScripts;
    if (target == ProjectPrepareTarget::Esp32) {
        auto capabilities = validateEsp32TargetCapabilities(manifest.value(), *scene.value());
        if (!capabilities) {
            return Result<ProjectPrepareResult>::failure(
                capabilities.error().withContext("scene_path", scenePath.value()));
        }
        auto scripts = collectEsp32PortableScriptSources(projectRoot);
        if (!scripts)
            return Result<ProjectPrepareResult>::failure(scripts.error());
        portableScripts = std::move(scripts.value());
    }

    ReflectionRegistry reflection;
    auto registered = registerBuiltinComponentTypes(reflection);
    if (!registered)
        return Result<ProjectPrepareResult>::failure(registered.error());
    auto outputPath = absolutePath(outputDirectory, "project preparation output");
    if (!outputPath)
        return Result<ProjectPrepareResult>::failure(outputPath.error());
    auto safeOutput = rejectReparseChain(outputPath.value(), "project preparation output");
    if (!safeOutput)
        return Result<ProjectPrepareResult>::failure(safeOutput.error());
    if (pathInside(outputPath.value(), projectRootPath) ||
        pathInside(projectRootPath, outputPath.value())) {
        return Result<ProjectPrepareResult>::failure(
            Error(ErrorCode::InvalidArgument,
                  "project preparation output cannot overlap the source project")
                .addContext("project", projectRoot)
                .addContext("output", pathText(outputPath.value())));
    }
    const auto outputPathText = pathText(outputPath.value());
    auto created = assets::createDirectories(outputPathText);
    if (!created)
        return Result<ProjectPrepareResult>::failure(created.error());
    safeOutput = rejectReparseChain(outputPath.value(), "project preparation output");
    if (!safeOutput)
        return Result<ProjectPrepareResult>::failure(safeOutput.error());

    ProjectPrepareResult result;
    const auto preparedRoot = joinPath(outputPathText, "project");
    created = assets::createDirectories(preparedRoot);
    if (!created)
        return Result<ProjectPrepareResult>::failure(created.error());
    safeOutput = rejectReparseChain(pathFromUtf8(preparedRoot), "prepared project directory");
    if (!safeOutput)
        return Result<ProjectPrepareResult>::failure(safeOutput.error());
    for (const auto& script : portableScripts) {
        const auto relativePath = std::string("Scripts/") + script.relativePath;
        if (!assets::isSafeRelativePath(relativePath)) {
            return Result<ProjectPrepareResult>::failure(
                Error(ErrorCode::InvalidArgument, "portable ESP32 script path is unsafe")
                    .addContext("path", relativePath));
        }
        const auto preparedScriptPath = joinPath(preparedRoot, relativePath);
        created = assets::createDirectories(parentPath(preparedScriptPath));
        if (!created)
            return Result<ProjectPrepareResult>::failure(created.error());
        safeOutput = rejectReparseChain(pathFromUtf8(preparedScriptPath), "prepared ESP32 script");
        if (!safeOutput)
            return Result<ProjectPrepareResult>::failure(safeOutput.error());
        if (script.bytes.size() > MaximumPreparedSourceBytes - result.sourceBytes) {
            return Result<ProjectPrepareResult>::failure(
                Error(ErrorCode::CapacityExceeded, "project sources exceed 256 MiB"));
        }
        auto scriptWritten = assets::writeBinaryFileAtomic(preparedScriptPath, script.bytes);
        if (!scriptWritten)
            return Result<ProjectPrepareResult>::failure(scriptWritten.error());
        result.sourceBytes += script.bytes.size();
    }
    result.portableScriptFileCount = portableScripts.size();
    const auto preparedScenePath = joinPath(preparedRoot, manifest.value().startupScene);
    created = assets::createDirectories(parentPath(preparedScenePath));
    if (!created)
        return Result<ProjectPrepareResult>::failure(created.error());
    safeOutput = rejectReparseChain(pathFromUtf8(preparedScenePath), "prepared scene");
    if (!safeOutput)
        return Result<ProjectPrepareResult>::failure(safeOutput.error());
    const std::vector<std::uint8_t> sceneBytes(sceneSource.value().begin(),
                                               sceneSource.value().end());
    auto sceneWritten = assets::writeBinaryFileAtomic(preparedScenePath, sceneBytes);
    if (!sceneWritten)
        return Result<ProjectPrepareResult>::failure(sceneWritten.error());

    std::vector<assets::PackInput> inputs;
    inputs.reserve(manifest.value().assets.size());
    std::set<AssetGuid> seen;
    for (const auto& entry : manifest.value().assets) {
        if (!seen.insert(entry.guid).second) {
            return Result<ProjectPrepareResult>::failure(
                Error(ErrorCode::AlreadyExists, "duplicate project asset GUID"));
        }
        auto path = safeAssetPath(projectRoot, entry.path);
        if (!path)
            return Result<ProjectPrepareResult>::failure(path.error());
        auto original = assets::readBinaryFile(path.value());
        if (!original)
            return Result<ProjectPrepareResult>::failure(original.error());
        if (original.value().size() > MaximumPreparedSourceBytes - result.sourceBytes) {
            return Result<ProjectPrepareResult>::failure(
                Error(ErrorCode::CapacityExceeded, "project asset sources exceed 256 MiB"));
        }
        result.sourceBytes += original.value().size();
        auto payload =
            preparePayload(entry, path.value(), manifest.value(), *scene.value(), reflection);
        if (!payload)
            return Result<ProjectPrepareResult>::failure(
                payload.error().withContext("asset", entry.path));
        result.importedAssetCount += payload.value().imported ? 1U : 0U;
        result.validatedAssetCount += payload.value().validated ? 1U : 0U;
        if (payload.value().visualPrograms != 0U) {
            ++result.visualGraphCount;
            result.visualProgramCount += payload.value().visualPrograms;
        }
        const auto preparedAssetPath = joinPath(preparedRoot, entry.path);
        created = assets::createDirectories(parentPath(preparedAssetPath));
        if (!created)
            return Result<ProjectPrepareResult>::failure(created.error());
        safeOutput = rejectReparseChain(pathFromUtf8(preparedAssetPath), "prepared asset");
        if (!safeOutput)
            return Result<ProjectPrepareResult>::failure(safeOutput.error());
        auto assetWritten = assets::writeBinaryFileAtomic(preparedAssetPath, payload.value().bytes);
        if (!assetWritten)
            return Result<ProjectPrepareResult>::failure(assetWritten.error());
        auto storage = assets::StorageClass::InternalRam;
        if (target == ProjectPrepareTarget::Esp32) {
            storage =
                entry.esp32Target == assets::AssetTarget::Esp32Psram ? assets::StorageClass::Psram
                : entry.esp32Target == assets::AssetTarget::Esp32Sd  ? assets::StorageClass::Sd
                                                                     : assets::StorageClass::Flash;
        }
        inputs.push_back(
            {entry.guid, stableTypeId(entry.type), storage, std::move(payload.value().bytes)});
    }
    result.assetCount = inputs.size();

    auto canonicalManifest = serializeManifest(manifest.value());
    if (!canonicalManifest)
        return Result<ProjectPrepareResult>::failure(canonicalManifest.error());
    result.preparedProjectPath = joinPath(preparedRoot, "Prepared.fglproject");
    safeOutput = rejectReparseChain(pathFromUtf8(result.preparedProjectPath), "prepared manifest");
    if (!safeOutput)
        return Result<ProjectPrepareResult>::failure(safeOutput.error());
    const std::vector<std::uint8_t> manifestBytes(canonicalManifest.value().begin(),
                                                  canonicalManifest.value().end());
    auto manifestWritten = assets::writeBinaryFileAtomic(result.preparedProjectPath, manifestBytes);
    if (!manifestWritten)
        return Result<ProjectPrepareResult>::failure(manifestWritten.error());

    auto pack = assets::buildPack(std::move(inputs));
    if (!pack)
        return Result<ProjectPrepareResult>::failure(pack.error());
    if (pack.value().bytes.size() > MaximumPreparedPackBytes) {
        return Result<ProjectPrepareResult>::failure(
            Error(ErrorCode::CapacityExceeded, "prepared project pack exceeds 256 MiB"));
    }
    result.packPath = joinPath(outputPathText, "project-assets.fglpack");
    safeOutput = rejectReparseChain(pathFromUtf8(result.packPath), "prepared asset pack");
    if (!safeOutput)
        return Result<ProjectPrepareResult>::failure(safeOutput.error());
    auto written = assets::writeBinaryFileAtomic(result.packPath, pack.value().bytes);
    if (!written)
        return Result<ProjectPrepareResult>::failure(written.error());
    result.packedBytes = pack.value().bytes.size();
    result.packChecksum = pack.value().buildChecksum;
    return Result<ProjectPrepareResult>::success(std::move(result));
}

} // namespace fabgl::project
