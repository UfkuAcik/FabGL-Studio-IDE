#include "AssetBrowserImporters.h"

#include <fabgl/assets/audio_importer.h>
#include <fabgl/assets/font_importer.h>
#include <fabgl/assets/image_pipeline.h>
#include <fabgl/assets/mesh_importer.h>
#include <fabgl/assets/tilemap_importer.h>

#include <project_format.h>

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <limits>
#include <memory>
#include <string_view>
#include <utility>

namespace fgl::studio {
namespace {

using fabgl::assets::AssetImportRequest;
using fabgl::assets::AssetKind;
using fabgl::assets::AssetTarget;
using fabgl::assets::IAssetImporter;
using fabgl::assets::ImportedAsset;

[[nodiscard]] QString normalizedExtension(const QString& path) {
    return QFileInfo(path).suffix().trimmed().toLower();
}

[[nodiscard]] AssetKind kindForType(const QString& type) {
    const QString value = type.trimmed().toLower();
    if (value == QStringLiteral("image"))
        return AssetKind::Image;
    if (value == QStringLiteral("audio"))
        return AssetKind::Audio;
    if (value == QStringLiteral("font"))
        return AssetKind::Font;
    if (value == QStringLiteral("tilemap"))
        return AssetKind::Tilemap;
    if (value == QStringLiteral("tileset"))
        return AssetKind::Tileset;
    if (value == QStringLiteral("sprite.atlas"))
        return AssetKind::SpriteAtlas;
    if (value == QStringLiteral("animation.clip") ||
        value == QStringLiteral("animation.controller"))
        return AssetKind::Animation;
    if (value == QStringLiteral("material"))
        return AssetKind::Material;
    if (value == QStringLiteral("scene"))
        return AssetKind::Scene;
    if (value == QStringLiteral("prefab"))
        return AssetKind::Prefab;
    if (value == QStringLiteral("script"))
        return AssetKind::Script;
    if (value == QStringLiteral("visual.script"))
        return AssetKind::VisualScript;
    if (value == QStringLiteral("raycast.map"))
        return AssetKind::RaycastMap;
    if (value == QStringLiteral("racer.track"))
        return AssetKind::RacerTrack;
    if (value == QStringLiteral("mesh"))
        return AssetKind::LowPolyMesh;
    if (value == QStringLiteral("json"))
        return AssetKind::Json;
    return AssetKind::Binary;
}

[[nodiscard]] QJsonObject settingsObject(const AssetImportRequest& request) {
    const auto document = QJsonDocument::fromJson(
        QByteArray(request.normalizedSettings.data(),
                   static_cast<qsizetype>(request.normalizedSettings.size())));
    return document.isObject() ? document.object() : QJsonObject{};
}

[[nodiscard]] std::uint32_t boundedMicros(const std::size_t value) noexcept {
    return value > std::numeric_limits<std::uint32_t>::max()
               ? std::numeric_limits<std::uint32_t>::max()
               : static_cast<std::uint32_t>(value);
}

void applyStorageTarget(ImportedAsset& output, const AssetTarget target) {
    if (target == AssetTarget::Pc) {
        return;
    }
    output.flashBytes = 0U;
    output.psramBytes = 0U;
    output.sdBytes = 0U;
    switch (target) {
    case AssetTarget::Pc:
        break;
    case AssetTarget::Esp32Flash:
        output.flashBytes = output.payload.size();
        break;
    case AssetTarget::Esp32Psram:
        output.psramBytes = output.payload.size();
        break;
    case AssetTarget::Esp32Sd:
        output.sdBytes = output.payload.size();
        break;
    }
}

[[nodiscard]] fabgl::assets::Image previewImage(const fabgl::assets::IndexedImage& indexed) {
    fabgl::assets::Image output;
    output.width = indexed.width;
    output.height = indexed.height;
    output.pixels.reserve(indexed.indices.size());
    for (const auto paletteIndex : indexed.indices) {
        auto color = indexed.palette[static_cast<std::size_t>(paletteIndex)];
        if (paletteIndex == indexed.transparentIndex)
            color.a = 0U;
        output.pixels.push_back(color);
    }
    return output;
}

class ImageSourceImporter final : public IAssetImporter {
  public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return "fabgl.image.source";
    }
    [[nodiscard]] std::uint32_t version() const noexcept override {
        return 2U;
    }
    [[nodiscard]] AssetKind kind() const noexcept override {
        return AssetKind::Image;
    }
    [[nodiscard]] std::vector<std::string> extensions() const override {
        return {"bmp", "jpeg", "jpg", "png"};
    }
    [[nodiscard]] fabgl::Result<ImportedAsset>
    import(const AssetImportRequest& request) const override {
        auto loaded = fabgl::assets::loadImage(request.sourcePath);
        if (!loaded)
            return fabgl::Result<ImportedAsset>::failure(loaded.error());
        auto imageSettings =
            fabgl::project::decodeProjectImageImportSettings(request.normalizedSettings);
        if (!imageSettings)
            return fabgl::Result<ImportedAsset>::failure(imageSettings.error());
        auto compiled = fabgl::assets::compileImageAsset(loaded.value(), imageSettings.value());
        if (!compiled)
            return fabgl::Result<ImportedAsset>::failure(compiled.error());
        fabgl::assets::ThumbnailSettings thumbnailSettings;
        thumbnailSettings.maximumWidth = 96;
        thumbnailSettings.maximumHeight = 96;
        thumbnailSettings.paletteSize = std::min(imageSettings.value().paletteSize, 32);
        thumbnailSettings.alphaThreshold = imageSettings.value().alphaThreshold;
        auto thumbnail = fabgl::assets::createThumbnail(previewImage(compiled.value().preview),
                                                        thumbnailSettings);
        if (!thumbnail)
            return fabgl::Result<ImportedAsset>::failure(thumbnail.error());
        ImportedAsset output;
        output.payload = std::move(compiled.value().payload);
        output.thumbnail = fabgl::assets::encodeIndexedImage(thumbnail.value());
        output.flashBytes = compiled.value().cost.packedBytes;
        const bool boundedStreaming =
            request.target != AssetTarget::Pc &&
            imageSettings.value().residency == fabgl::assets::ImageResidency::Stream;
        output.internalRamBytes =
            boundedStreaming
                ? compiled.value().cost.paletteBytes +
                      std::min<std::size_t>(
                          compiled.value().cost.decodedBytes,
                          static_cast<std::size_t>(std::max(1, compiled.value().preview.width)))
                : compiled.value().cost.decodedBytes + compiled.value().cost.paletteBytes;
        output.estimatedDecodeMicros =
            boundedMicros(compiled.value().cost.estimatedPixelsPerFrame / 8U);
        output.estimatedRenderPixelsPerFrame = compiled.value().cost.estimatedPixelsPerFrame;
        return fabgl::Result<ImportedAsset>::success(std::move(output));
    }
};

class AudioSourceImporter final : public IAssetImporter {
  public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return "fabgl.audio.wav";
    }
    [[nodiscard]] std::uint32_t version() const noexcept override {
        return 1U;
    }
    [[nodiscard]] AssetKind kind() const noexcept override {
        return AssetKind::Audio;
    }
    [[nodiscard]] std::vector<std::string> extensions() const override {
        return {"wav"};
    }
    [[nodiscard]] fabgl::Result<ImportedAsset>
    import(const AssetImportRequest& request) const override {
        const auto settings = settingsObject(request);
        fabgl::assets::AudioImportSettings audioSettings;
        audioSettings.targetSampleRate = static_cast<std::uint32_t>(std::clamp(
            settings.value(QStringLiteral("targetSampleRate")).toInt(22'050), 1, 96'000));
        audioSettings.normalize = settings.value(QStringLiteral("normalize")).toBool(true);
        audioSettings.trimSilence = settings.value(QStringLiteral("trimSilence")).toBool(true);
        audioSettings.silenceThreshold =
            static_cast<float>(settings.value(QStringLiteral("silenceThreshold")).toDouble(0.01));
        audioSettings.streaming = settings.value(QStringLiteral("streaming")).toBool(false);
        audioSettings.loopStart = static_cast<std::uint32_t>(
            std::max(0, settings.value(QStringLiteral("loopStart")).toInt(0)));
        audioSettings.loopEnd = static_cast<std::uint32_t>(
            std::max(0, settings.value(QStringLiteral("loopEnd")).toInt(0)));
        auto clip = fabgl::assets::importWav(request.sourceBytes, audioSettings);
        if (!clip)
            return fabgl::Result<ImportedAsset>::failure(clip.error());
        const auto encoding =
            settings.value(QStringLiteral("encoding")).toString() == QStringLiteral("pcm16")
                ? fabgl::assets::AudioEncoding::Pcm16
                : fabgl::assets::AudioEncoding::Delta8;
        ImportedAsset output;
        output.payload = fabgl::assets::encodeAudioClip(clip.value(), encoding);
        output.flashBytes = output.payload.size();
        output.internalRamBytes =
            clip.value().streaming ? std::min<std::size_t>(clip.value().samples.size() * 2U, 4096U)
                                   : clip.value().samples.size() * sizeof(std::int16_t);
        output.estimatedDecodeMicros = boundedMicros(clip.value().samples.size() / 16U);
        return fabgl::Result<ImportedAsset>::success(std::move(output));
    }
};

class CompiledImageImporter final : public IAssetImporter {
  public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return "fabgl.image.compiled";
    }
    [[nodiscard]] std::uint32_t version() const noexcept override {
        return 1U;
    }
    [[nodiscard]] AssetKind kind() const noexcept override {
        return AssetKind::Image;
    }
    [[nodiscard]] std::vector<std::string> extensions() const override {
        return {"fgli"};
    }
    [[nodiscard]] fabgl::Result<ImportedAsset>
    import(const AssetImportRequest& request) const override {
        auto decoded = fabgl::assets::decodeIndexedImage(request.sourceBytes);
        if (!decoded)
            return fabgl::Result<ImportedAsset>::failure(decoded.error());
        fabgl::assets::Image image;
        image.width = decoded.value().width;
        image.height = decoded.value().height;
        image.pixels.reserve(decoded.value().indices.size());
        for (const auto index : decoded.value().indices)
            image.pixels.push_back(decoded.value().palette[index]);
        auto thumbnail = fabgl::assets::createThumbnail(image);
        if (!thumbnail)
            return fabgl::Result<ImportedAsset>::failure(thumbnail.error());
        const auto cost = fabgl::assets::estimateCost(decoded.value(), request.sourceBytes);
        ImportedAsset output;
        output.payload = request.sourceBytes;
        output.thumbnail = fabgl::assets::encodeIndexedImage(thumbnail.value());
        output.flashBytes = cost.packedBytes;
        output.internalRamBytes = cost.decodedBytes + cost.paletteBytes;
        output.estimatedDecodeMicros = boundedMicros(cost.estimatedPixelsPerFrame / 8U);
        return fabgl::Result<ImportedAsset>::success(std::move(output));
    }
};

class CompiledAudioImporter final : public IAssetImporter {
  public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return "fabgl.audio.compiled";
    }
    [[nodiscard]] std::uint32_t version() const noexcept override {
        return 1U;
    }
    [[nodiscard]] AssetKind kind() const noexcept override {
        return AssetKind::Audio;
    }
    [[nodiscard]] std::vector<std::string> extensions() const override {
        return {"fgla"};
    }
    [[nodiscard]] fabgl::Result<ImportedAsset>
    import(const AssetImportRequest& request) const override {
        auto decoded = fabgl::assets::decodeAudioClip(request.sourceBytes);
        if (!decoded)
            return fabgl::Result<ImportedAsset>::failure(decoded.error());
        ImportedAsset output;
        output.payload = request.sourceBytes;
        output.flashBytes = output.payload.size();
        output.internalRamBytes =
            decoded.value().streaming
                ? std::min<std::size_t>(decoded.value().samples.size() * 2U, 4096U)
                : decoded.value().samples.size() * sizeof(std::int16_t);
        output.estimatedDecodeMicros = boundedMicros(decoded.value().samples.size() / 16U);
        return fabgl::Result<ImportedAsset>::success(std::move(output));
    }
};

class CompiledTilemapImporter final : public IAssetImporter {
  public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return "fabgl.tilemap.compiled";
    }
    [[nodiscard]] std::uint32_t version() const noexcept override {
        return 2U;
    }
    [[nodiscard]] AssetKind kind() const noexcept override {
        return AssetKind::Tilemap;
    }
    [[nodiscard]] std::vector<std::string> extensions() const override {
        return {"fglt", "fgltilemap"};
    }
    [[nodiscard]] fabgl::Result<ImportedAsset>
    import(const AssetImportRequest& request) const override {
        auto decoded = fabgl::assets::inspectTilemap(request.sourceBytes);
        if (!decoded)
            return fabgl::Result<ImportedAsset>::failure(decoded.error());
        if (decoded.value().guid.isNil()) {
            decoded.value().guid = request.guid;
        } else if (decoded.value().guid != request.guid) {
            return fabgl::Result<ImportedAsset>::failure(
                fabgl::Error(fabgl::ErrorCode::TypeMismatch,
                             "compiled tilemap GUID does not match its project mapping")
                    .addContext("mapped_guid", request.guid.toString())
                    .addContext("payload_guid", decoded.value().guid.toString()));
        }
        auto canonical = fabgl::assets::encodeTilemap(decoded.value());
        if (!canonical)
            return fabgl::Result<ImportedAsset>::failure(canonical.error());
        std::size_t cells = decoded.value().tiles.size();
        for (const auto& layer : decoded.value().layers)
            cells += layer.cells.size();
        for (const auto& chunk : decoded.value().chunks)
            cells += chunk.cells.size();
        ImportedAsset output;
        output.payload = std::move(canonical.value());
        output.flashBytes = output.payload.size();
        output.internalRamBytes = cells * sizeof(std::uint32_t);
        output.estimatedDecodeMicros = boundedMicros(cells / 8U);
        for (const auto& reference : decoded.value().tilesets)
            output.dependencies.push_back(reference.tileset);
        for (const auto& object : decoded.value().objects) {
            if (!object.asset.isNil())
                output.dependencies.push_back(object.asset);
        }
        return fabgl::Result<ImportedAsset>::success(std::move(output));
    }
};

class CompiledTilesetImporter final : public IAssetImporter {
  public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return "fabgl.tileset.compiled";
    }
    [[nodiscard]] std::uint32_t version() const noexcept override {
        return 1U;
    }
    [[nodiscard]] AssetKind kind() const noexcept override {
        return AssetKind::Tileset;
    }
    [[nodiscard]] std::vector<std::string> extensions() const override {
        return {"fgltileset"};
    }
    [[nodiscard]] fabgl::Result<ImportedAsset>
    import(const AssetImportRequest& request) const override {
        auto decoded = fabgl::assets::inspectTileset(request.sourceBytes);
        if (!decoded)
            return fabgl::Result<ImportedAsset>::failure(decoded.error());
        if (decoded.value().guid.isNil()) {
            decoded.value().guid = request.guid;
        } else if (decoded.value().guid != request.guid) {
            return fabgl::Result<ImportedAsset>::failure(
                fabgl::Error(fabgl::ErrorCode::TypeMismatch,
                             "compiled tileset GUID does not match its project mapping")
                    .addContext("mapped_guid", request.guid.toString())
                    .addContext("payload_guid", decoded.value().guid.toString()));
        }
        ImportedAsset output;
        auto canonical = fabgl::assets::encodeTileset(decoded.value());
        if (!canonical)
            return fabgl::Result<ImportedAsset>::failure(canonical.error());
        output.payload = std::move(canonical.value());
        output.flashBytes = output.payload.size();
        output.internalRamBytes = decoded.value().collisionTiles.size() * sizeof(std::uint32_t);
        output.estimatedDecodeMicros = boundedMicros(decoded.value().tileCount / 8U);
        output.dependencies.push_back(decoded.value().sourceImage);
        return fabgl::Result<ImportedAsset>::success(std::move(output));
    }
};

class CompiledMeshImporter final : public IAssetImporter {
  public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return "fabgl.mesh.compiled";
    }
    [[nodiscard]] std::uint32_t version() const noexcept override {
        return 1U;
    }
    [[nodiscard]] AssetKind kind() const noexcept override {
        return AssetKind::LowPolyMesh;
    }
    [[nodiscard]] std::vector<std::string> extensions() const override {
        return {"fglm"};
    }
    [[nodiscard]] fabgl::Result<ImportedAsset>
    import(const AssetImportRequest& request) const override {
        auto decoded = fabgl::assets::inspectLowPolyMesh(request.sourceBytes);
        if (!decoded)
            return fabgl::Result<ImportedAsset>::failure(decoded.error());
        ImportedAsset output;
        output.payload = request.sourceBytes;
        output.flashBytes = output.payload.size();
        output.internalRamBytes =
            decoded.value().positions.size() * sizeof(fabgl::assets::MeshPosition) +
            decoded.value().indices.size() * sizeof(std::uint16_t);
        output.estimatedDecodeMicros = boundedMicros(decoded.value().indices.size() / 3U);
        return fabgl::Result<ImportedAsset>::success(std::move(output));
    }
};

class CompiledFontImporter final : public IAssetImporter {
  public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return "fabgl.font.compiled";
    }
    [[nodiscard]] std::uint32_t version() const noexcept override {
        return 1U;
    }
    [[nodiscard]] AssetKind kind() const noexcept override {
        return AssetKind::Font;
    }
    [[nodiscard]] std::vector<std::string> extensions() const override {
        return {"fglf"};
    }
    [[nodiscard]] fabgl::Result<ImportedAsset>
    import(const AssetImportRequest& request) const override {
        auto decoded = fabgl::assets::inspectBitmapFont(request.sourceBytes);
        if (!decoded)
            return fabgl::Result<ImportedAsset>::failure(decoded.error());
        ImportedAsset output;
        output.payload = request.sourceBytes;
        output.flashBytes = output.payload.size();
        output.internalRamBytes =
            decoded.value().atlasBits.size() +
            decoded.value().glyphs.size() * sizeof(fabgl::assets::BitmapGlyph);
        output.estimatedDecodeMicros = boundedMicros(decoded.value().glyphs.size());
        return fabgl::Result<ImportedAsset>::success(std::move(output));
    }
};

class SourceCopyImporter final : public IAssetImporter {
  public:
    SourceCopyImporter(std::string id, const AssetKind kind, std::string extension)
        : id_(std::move(id)), kind_(kind), extension_(std::move(extension)) {}

    [[nodiscard]] std::string_view id() const noexcept override {
        return id_;
    }
    [[nodiscard]] std::uint32_t version() const noexcept override {
        return 1U;
    }
    [[nodiscard]] AssetKind kind() const noexcept override {
        return kind_;
    }
    [[nodiscard]] std::vector<std::string> extensions() const override {
        return {extension_};
    }
    [[nodiscard]] fabgl::Result<ImportedAsset>
    import(const AssetImportRequest& request) const override {
        ImportedAsset output;
        output.payload = request.sourceBytes;
        output.flashBytes = output.payload.size();
        output.estimatedDecodeMicros = boundedMicros(output.payload.size() / 128U);
        return fabgl::Result<ImportedAsset>::success(std::move(output));
    }

  private:
    std::string id_;
    AssetKind kind_;
    std::string extension_;
};

[[nodiscard]] std::unique_ptr<IAssetImporter>
makeImporter(const AssetBrowserImporterDescriptor& descriptor, const QString& relativePath,
             const QString& type) {
    if (descriptor.id == "fabgl.image.source")
        return std::make_unique<ImageSourceImporter>();
    if (descriptor.id == "fabgl.audio.wav")
        return std::make_unique<AudioSourceImporter>();
    if (descriptor.id == "fabgl.image.compiled")
        return std::make_unique<CompiledImageImporter>();
    if (descriptor.id == "fabgl.audio.compiled")
        return std::make_unique<CompiledAudioImporter>();
    if (descriptor.id == "fabgl.tilemap.csv")
        return std::make_unique<fabgl::assets::CsvTilemapImporter>();
    if (descriptor.id == "fabgl.tilemap.json")
        return std::make_unique<fabgl::assets::JsonTilemapImporter>();
    if (descriptor.id == "fabgl.tilemap.compiled")
        return std::make_unique<CompiledTilemapImporter>();
    if (descriptor.id == "fabgl.tileset.compiled")
        return std::make_unique<CompiledTilesetImporter>();
    if (descriptor.id == "fabgl.mesh.obj")
        return std::make_unique<fabgl::assets::WavefrontObjImporter>();
    if (descriptor.id == "fabgl.mesh.compiled")
        return std::make_unique<CompiledMeshImporter>();
    if (descriptor.id == "fabgl.font.bdf")
        return std::make_unique<fabgl::assets::BdfFontImporter>();
    if (descriptor.id == "fabgl.font.compiled")
        return std::make_unique<CompiledFontImporter>();
    const auto extension = normalizedExtension(relativePath).toStdString();
    if (descriptor.id.rfind("fabgl.source.copy.", 0U) == 0U && !extension.empty()) {
        return std::make_unique<SourceCopyImporter>(descriptor.id, kindForType(type), extension);
    }
    return {};
}

} // namespace

AssetBrowserImporterDescriptor assetBrowserImporterFor(const QString& relativePath,
                                                       const QString& type) {
    const auto extension = normalizedExtension(relativePath);
    const auto normalizedType = type.trimmed().toLower();
    if (QStringList{QStringLiteral("bmp"), QStringLiteral("jpeg"), QStringLiteral("jpg"),
                    QStringLiteral("png")}
            .contains(extension))
        return {"fabgl.image.source", 2U, AssetKind::Image, true};
    if (extension == QStringLiteral("wav"))
        return {"fabgl.audio.wav", 1U, AssetKind::Audio, true};
    if (extension == QStringLiteral("fgli"))
        return {"fabgl.image.compiled", 1U, AssetKind::Image, true};
    if (extension == QStringLiteral("fgla"))
        return {"fabgl.audio.compiled", 1U, AssetKind::Audio, true};
    if (extension == QStringLiteral("csv") && normalizedType == QStringLiteral("tilemap"))
        return {"fabgl.tilemap.csv", 2U, AssetKind::Tilemap, true};
    if (extension == QStringLiteral("json") && normalizedType == QStringLiteral("tilemap"))
        return {"fabgl.tilemap.json", 2U, AssetKind::Tilemap, true};
    if (extension == QStringLiteral("fglt") || extension == QStringLiteral("fgltilemap"))
        return {"fabgl.tilemap.compiled", 2U, AssetKind::Tilemap, true};
    if (extension == QStringLiteral("fgltileset"))
        return {"fabgl.tileset.compiled", 1U, AssetKind::Tileset, true};
    if (extension == QStringLiteral("obj"))
        return {"fabgl.mesh.obj", 1U, AssetKind::LowPolyMesh, true};
    if (extension == QStringLiteral("fglm"))
        return {"fabgl.mesh.compiled", 1U, AssetKind::LowPolyMesh, true};
    if (extension == QStringLiteral("bdf"))
        return {"fabgl.font.bdf", 1U, AssetKind::Font, true};
    if (extension == QStringLiteral("fglf"))
        return {"fabgl.font.compiled", 1U, AssetKind::Font, true};
    if (!extension.isEmpty()) {
        return {"fabgl.source.copy." + normalizedType.toStdString(), 1U,
                kindForType(normalizedType), true};
    }
    return {};
}

fabgl::Result<ImportedAsset> importAssetForBrowser(const AssetBrowserImporterDescriptor& descriptor,
                                                   const AssetImportRequest& request,
                                                   const QString& type) {
    if (!descriptor.supported) {
        return fabgl::Result<ImportedAsset>::failure(
            fabgl::Error(fabgl::ErrorCode::NotFound, "asset has no supported importer")
                .addContext("path", request.relativePath));
    }
    if (descriptor.id == "fabgl.image.source") {
        auto settings =
            fabgl::project::decodeProjectImageImportSettings(request.normalizedSettings);
        if (!settings)
            return fabgl::Result<ImportedAsset>::failure(settings.error());
        const bool atlas =
            settings.value().outputKind == fabgl::assets::ImageOutputKind::SpriteAtlas;
        const auto normalizedType = type.trimmed().toLower();
        if ((atlas && normalizedType != QStringLiteral("sprite.atlas")) ||
            (!atlas && normalizedType != QStringLiteral("image"))) {
            return fabgl::Result<ImportedAsset>::failure(
                fabgl::Error(fabgl::ErrorCode::TypeMismatch,
                             "image source type does not match its canonical output")
                    .addContext("type", normalizedType.toStdString())
                    .addContext("output", atlas ? "sprite.atlas" : "image"));
        }
    }
    auto importer = makeImporter(descriptor, QString::fromStdString(request.relativePath), type);
    if (importer == nullptr) {
        return fabgl::Result<ImportedAsset>::failure(
            fabgl::Error(fabgl::ErrorCode::NotFound, "asset importer implementation is missing")
                .addContext("importer", descriptor.id));
    }
    fabgl::assets::AssetImporterRegistry registry;
    auto added = registry.add(std::move(importer));
    if (!added)
        return fabgl::Result<ImportedAsset>::failure(added.error());
    auto imported = registry.import(request);
    if (!imported)
        return imported;
    imported.value().kind = kindForType(type);
    applyStorageTarget(imported.value(), request.target);
    std::sort(imported.value().dependencies.begin(), imported.value().dependencies.end());
    imported.value().dependencies.erase(
        std::unique(imported.value().dependencies.begin(), imported.value().dependencies.end()),
        imported.value().dependencies.end());
    return imported;
}

} // namespace fgl::studio
