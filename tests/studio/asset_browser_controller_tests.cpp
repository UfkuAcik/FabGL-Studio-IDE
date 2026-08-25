#include "AssetBrowserController.h"

#include <fabgl/assets/image_pipeline.h>
#include <fabgl/assets/tilemap_importer.h>

#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMap>
#include <QMimeData>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

[[nodiscard]] QString resultMessage(const fabgl::Result<void>& result) {
    if (result)
        return {};
    QString message = QString::fromStdString(result.error().message());
    for (const auto& context : result.error().context()) {
        message +=
            QStringLiteral(" [%1=%2]")
                .arg(QString::fromStdString(context.key), QString::fromStdString(context.value));
    }
    return message;
}

[[nodiscard]] bool writeFile(const QString& path, const QByteArray& contents) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.write(contents) == contents.size() && file.flush();
}

[[nodiscard]] QByteArray compiledImage() {
    fabgl::assets::Image image;
    image.width = 8;
    image.height = 4;
    image.pixels.resize(32U);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            image.pixels[static_cast<std::size_t>(y * image.width + x)] = {
                static_cast<std::uint8_t>(x * 24), static_cast<std::uint8_t>(y * 60),
                static_cast<std::uint8_t>((x + y) * 16), 255U};
        }
    }
    fabgl::assets::ImageImportSettings settings;
    settings.paletteSize = 8;
    const auto indexed = fabgl::assets::processImage(image, settings);
    if (!indexed)
        return {};
    const auto bytes = fabgl::assets::encodeIndexedImage(indexed.value());
    return QByteArray(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<qsizetype>(bytes.size()));
}

[[nodiscard]] int rowForGuid(const fgl::studio::AssetBrowserController& controller,
                             const fabgl::AssetGuid& guid) {
    const auto& entries = controller.model()->entries();
    for (qsizetype row = 0; row < entries.size(); ++row) {
        if (entries.at(row).guid == guid)
            return static_cast<int>(row);
    }
    return -1;
}

[[nodiscard]] bool createDirectoryLinkForTest(const QString& linkPath, const QString& targetPath) {
#ifdef Q_OS_WIN
    const QString nativeLink = QDir::toNativeSeparators(linkPath);
    const QString nativeTarget = QDir::toNativeSeparators(targetPath);
    if (nativeLink.contains(QLatin1Char('"')) || nativeTarget.contains(QLatin1Char('"')))
        return false;
    QProcess process;
    process.setProgram(
        qEnvironmentVariable("COMSPEC", QStringLiteral("C:\\Windows\\System32\\cmd.exe")));
    process.setNativeArguments(
        QStringLiteral("/d /c mklink /J \"%1\" \"%2\"").arg(nativeLink, nativeTarget));
    process.start();
    return process.waitForFinished(5'000) && process.exitStatus() == QProcess::NormalExit &&
           process.exitCode() == 0 && QFileInfo(linkPath).exists();
#else
    return QFile::link(targetPath, linkPath);
#endif
}

[[nodiscard]] fgl::studio::AssetBrowserProjectEntry
mapping(const fabgl::AssetGuid& guid, const QString& path, const QString& type) {
    fgl::studio::AssetBrowserProjectEntry result;
    result.guid = guid;
    result.relativePath = path;
    result.type = type;
    result.normalizedSettings = QStringLiteral("{}");
    return result;
}

} // namespace

class AssetBrowserControllerTests final : public QObject {
    Q_OBJECT

  private slots:
    void actualImportExposesCostsThumbnailsAndDependencyDirections();
    void watcherDebouncesAndAutoReimportsChangedSources();
    void settingsAndDependenciesInvalidateIncrementalCache();
    void compiledTilemapCanonicalizesAndBindsGuid();
    void traversalAndLinkLikeTreesFailClosed();
    void externalAndControlledMovesPreserveGuidMapping();
    void importDiagnosticsAndStorageBudgetsAreObservable();
    void advancedImageSettingsApplyCropAtlasStorageAndCostModel();
    void specialistExtensionsDiscoverCanonicalAssetTypes();
};

void AssetBrowserControllerTests::specialistExtensionsDiscoverCanonicalAssetTypes() {
    QTemporaryDir project;
    QVERIFY(project.isValid());
    QVERIFY(QDir(project.path()).mkpath(QStringLiteral("Assets")));
    const QMap<QString, QString> expected{
        {QStringLiteral("sample.fglmaterial"), QStringLiteral("material")},
        {QStringLiteral("sample.fglanim"), QStringLiteral("animation.clip")},
        {QStringLiteral("sample.fglcontroller"), QStringLiteral("animation.controller")},
        {QStringLiteral("sample.fgltrack"), QStringLiteral("racer.track")},
        {QStringLiteral("sample.fglray"), QStringLiteral("raycast.map")},
        {QStringLiteral("sample.fgls"), QStringLiteral("sprite.atlas")},
    };
    for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
        QVERIFY(writeFile(project.filePath(QStringLiteral("Assets/") + it.key()),
                          QByteArrayLiteral("fixture")));
    }
    fgl::studio::AssetBrowserController controller;
    auto result = controller.setProject(project.path(), {});
    QVERIFY2(result, qPrintable(resultMessage(result)));
    QCOMPARE(controller.model()->entries().size(), expected.size());
    for (const auto& entry : controller.model()->entries()) {
        const auto fileName = QFileInfo(entry.relativePath).fileName();
        QVERIFY2(expected.contains(fileName), qPrintable(fileName));
        QCOMPARE(entry.type, expected.value(fileName));
    }
}

void AssetBrowserControllerTests::advancedImageSettingsApplyCropAtlasStorageAndCostModel() {
#ifndef Q_OS_WIN
    QSKIP("The dependency-free source image decoder uses WIC on Windows");
#else
    QTemporaryDir project;
    QVERIFY(project.isValid());
    QVERIFY(QDir(project.path()).mkpath(QStringLiteral("Assets")));
    QImage source(8, 4, QImage::Format_RGBA8888);
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x)
            source.setPixelColor(x, y, QColor(x * 28, y * 60, (x + y) * 18, 255));
    }
    const auto sourcePath = project.filePath(QStringLiteral("Assets/frames.png"));
    QVERIFY(source.save(sourcePath, "PNG"));
    const auto guid = fabgl::AssetGuid::fromStableName("tests.asset-browser.advanced-image");
    auto image = mapping(guid, QStringLiteral("Assets/frames.png"), QStringLiteral("image"));
    fgl::studio::AssetBrowserController controller;
    auto result = controller.setProject(project.path(), {image});
    QVERIFY2(result, qPrintable(resultMessage(result)));

    result = controller.setImportSettings(
        guid,
        QStringLiteral(
            R"json({"crop":{"x":2,"y":1,"width":4,"height":2},"targetWidth":2,"targetHeight":2,"paletteSize":8,"compression":"rle","residency":"stream"})json"),
        fabgl::assets::AssetTarget::Esp32Sd);
    QVERIFY2(result, qPrintable(resultMessage(result)));
    result = controller.refreshNow();
    QVERIFY2(result, qPrintable(resultMessage(result)));
    const auto* cropped = controller.model()->entry(guid);
    QVERIFY(cropped != nullptr);
    QCOMPARE(cropped->state, fgl::studio::AssetBrowserState::Clean);
    QCOMPARE(cropped->type, QStringLiteral("image"));
    QCOMPARE(cropped->esp32Cost.flashBytes, 0U);
    QCOMPARE(cropped->esp32Cost.psramBytes, 0U);
    QVERIFY(cropped->esp32Cost.sdBytes > 0U);
    QCOMPARE(cropped->esp32Cost.estimatedRenderPixelsPerFrame, 4U);
    QVERIFY(cropped->esp32Cost.internalRamBytes < cropped->pcCost.internalRamBytes);
    QFile pcPayload(controller.cachedPayloadPath(guid, fabgl::assets::AssetTarget::Pc));
    QVERIFY(pcPayload.open(QIODevice::ReadOnly));
    const auto pcBytes = pcPayload.readAll();
    auto decoded = fabgl::assets::decodeIndexedImage(
        std::vector<std::uint8_t>(pcBytes.cbegin(), pcBytes.cend()));
    QVERIFY(decoded);
    QCOMPARE(decoded.value().width, 2);
    QCOMPARE(decoded.value().height, 2);

    result = controller.setImportSettings(
        guid,
        QStringLiteral(
            R"json({"slice":{"mode":"grid","frameWidth":2,"frameHeight":2,"margin":0,"spacing":0},"atlas":{"enabled":true,"maxWidth":8,"padding":1,"powerOfTwo":true},"pivot":{"x":0.5,"y":1},"pixelsPerUnit":16,"compression":"rle","residency":"preload"})json"),
        fabgl::assets::AssetTarget::Esp32Psram);
    QVERIFY2(result, qPrintable(resultMessage(result)));
    result = controller.refreshNow();
    QVERIFY2(result, qPrintable(resultMessage(result)));
    const auto* atlas = controller.model()->entry(guid);
    QVERIFY(atlas != nullptr);
    QCOMPARE(atlas->type, QStringLiteral("sprite.atlas"));
    QCOMPARE(atlas->esp32Cost.estimatedRenderPixelsPerFrame, 4U);
    QVERIFY(atlas->esp32Cost.psramBytes > 0U);
    QFile atlasPayload(controller.cachedPayloadPath(guid, fabgl::assets::AssetTarget::Pc));
    QVERIFY(atlasPayload.open(QIODevice::ReadOnly));
    const auto atlasBytes = atlasPayload.readAll();
    QVERIFY(atlasBytes.size() > 16);
    QCOMPARE(atlasBytes.left(4), QByteArrayLiteral("FGLS"));

    result = controller.setImportSettings(
        guid, QStringLiteral(R"json({"paletteSize":8,"unknown":true})json"),
        fabgl::assets::AssetTarget::Esp32Flash);
    QVERIFY(!result);
#endif
}

void AssetBrowserControllerTests::actualImportExposesCostsThumbnailsAndDependencyDirections() {
    QTemporaryDir project;
    QVERIFY(project.isValid());
    QVERIFY(QDir(project.path()).mkpath(QStringLiteral("Assets")));
    const auto imagePath = project.filePath(QStringLiteral("Assets/tiles.fgli"));
    const auto mapPath = project.filePath(QStringLiteral("Assets/level.csv"));
    QVERIFY(writeFile(imagePath, compiledImage()));
    QVERIFY(writeFile(mapPath, QByteArrayLiteral("1,2\n3,4\n")));

    const auto imageGuid = fabgl::AssetGuid::fromStableName("tests.asset-browser.image");
    const auto mapGuid = fabgl::AssetGuid::fromStableName("tests.asset-browser.map");
    auto imageMapping =
        mapping(imageGuid, QStringLiteral("Assets/tiles.fgli"), QStringLiteral("image"));
    auto mapMapping =
        mapping(mapGuid, QStringLiteral("Assets/level.csv"), QStringLiteral("tilemap"));
    mapMapping.dependencies.push_back(imageGuid);

    fgl::studio::AssetBrowserController controller;
    const auto opened = controller.setProject(project.path(), {imageMapping, mapMapping});
    QVERIFY2(opened, qPrintable(resultMessage(opened)));
    QCOMPARE(controller.model()->rowCount(), 2);
    const auto* image = controller.model()->entry(imageGuid);
    const auto* tilemap = controller.model()->entry(mapGuid);
    QVERIFY(image != nullptr);
    QVERIFY(tilemap != nullptr);
    QCOMPARE(image->state, fgl::studio::AssetBrowserState::Clean);
    QCOMPARE(tilemap->state, fgl::studio::AssetBrowserState::Clean);
    QCOMPARE(image->importer, QStringLiteral("fabgl.image.compiled"));
    QCOMPARE(tilemap->importer, QStringLiteral("fabgl.tilemap.csv"));
    QVERIFY(image->pcCost.payloadBytes > 0U);
    QVERIFY(image->esp32Cost.flashBytes > 0U);
    QVERIFY(image->esp32Cost.internalRamBytes > 0U);
    QVERIFY(!image->thumbnail.isEmpty());
    QVERIFY(!image->thumbnailPlaceholder);
    QVERIFY(tilemap->thumbnailPlaceholder);
    QVERIFY(tilemap->dependencies.contains(imageGuid));
    QVERIFY(image->dependents.contains(mapGuid));
    QVERIFY(
        QFileInfo::exists(controller.cachedPayloadPath(imageGuid, fabgl::assets::AssetTarget::Pc)));
    QVERIFY(QFileInfo::exists(
        controller.cachedPayloadPath(imageGuid, fabgl::assets::AssetTarget::Esp32Flash)));
    QVERIFY(QFileInfo::exists(controller.metadataPath()));

    const auto imageIndex = controller.model()->index(rowForGuid(controller, imageGuid),
                                                      fgl::studio::AssetBrowserModel::NameColumn);
    QVERIFY(imageIndex.data(Qt::DecorationRole).canConvert<QIcon>());
    QVERIFY(imageIndex.data(fgl::studio::AssetBrowserModel::PcCostRole)
                .toMap()
                .value(QStringLiteral("payloadBytes"))
                .toULongLong() > 0U);
    QCOMPARE(imageIndex.data(fgl::studio::AssetBrowserModel::ImportSettingsRole).toString(),
             QStringLiteral("{}"));
    QCOMPARE(imageIndex.data(fgl::studio::AssetBrowserModel::Esp32TargetRole).toString(),
             QStringLiteral("flash"));
    QCOMPARE(imageIndex.data(fgl::studio::AssetBrowserModel::SourceMetadataRole)
                 .toMap()
                 .value(QStringLiteral("bytes"))
                 .toULongLong(),
             static_cast<qulonglong>(QFileInfo(imagePath).size()));
    QVERIFY(controller.model()->flags(imageIndex).testFlag(Qt::ItemIsDragEnabled));
    const std::unique_ptr<QMimeData> dragged(
        controller.model()->mimeData(QModelIndexList{imageIndex}));
    QVERIFY(dragged->hasUrls());
    QCOMPARE(QFileInfo(dragged->urls().constFirst().toLocalFile()).canonicalFilePath(),
             QFileInfo(imagePath).canonicalFilePath());
    QCOMPARE(QString::fromUtf8(dragged->data(QStringLiteral("application/x-fabgl-asset-guid"))),
             QString::fromStdString(imageGuid.toString()));
}

void AssetBrowserControllerTests::watcherDebouncesAndAutoReimportsChangedSources() {
    QTemporaryDir project;
    QVERIFY(project.isValid());
    QVERIFY(QDir(project.path()).mkpath(QStringLiteral("Assets")));
    const auto sourcePath = project.filePath(QStringLiteral("Assets/live.bin"));
    QVERIFY(writeFile(sourcePath, QByteArrayLiteral("initial")));
    const auto guid = fabgl::AssetGuid::fromStableName("tests.asset-browser.watch");
    auto sourceMapping = mapping(guid, QStringLiteral("Assets/live.bin"), QStringLiteral("binary"));
    fgl::studio::AssetBrowserLimits limits;
    limits.debounceMilliseconds = 80;
    limits.maximumDebounceMilliseconds = 220;
    fgl::studio::AssetBrowserController controller;
    QSignalSpy scheduled(&controller, &fgl::studio::AssetBrowserController::refreshScheduled);
    QSignalSpy refreshed(&controller, &fgl::studio::AssetBrowserController::refreshed);
    const auto opened = controller.setProject(project.path(), {sourceMapping}, limits);
    QVERIFY2(opened, qPrintable(resultMessage(opened)));
    const auto initialRefreshCount = controller.refreshCount();
    const auto initialKey = controller.model()->entry(guid)->pcCacheKey;
    refreshed.clear();

    for (int index = 0; index < 5; ++index) {
        QVERIFY(writeFile(sourcePath, QByteArray("changed-") + QByteArray::number(index)));
        QTest::qWait(20);
    }
    QElapsedTimer refreshWait;
    refreshWait.start();
    while (controller.refreshCount() == initialRefreshCount && refreshWait.elapsed() < 3'000) {
        QTest::qWait(20);
    }
    QVERIFY(controller.refreshCount() > initialRefreshCount);
    QVERIFY(!refreshed.isEmpty());
    QVERIFY(!scheduled.isEmpty());
    QTest::qWait(350);
    QVERIFY(controller.refreshCount() <= initialRefreshCount + 2U);
    const auto* entry = controller.model()->entry(guid);
    QVERIFY(entry != nullptr);
    QCOMPARE(entry->state, fgl::studio::AssetBrowserState::Clean);
    QVERIFY(entry->pcCacheKey != initialKey);
    QVERIFY(controller.lastRefreshStats().imported >= 1U);
}

void AssetBrowserControllerTests::settingsAndDependenciesInvalidateIncrementalCache() {
    QTemporaryDir project;
    QVERIFY(project.isValid());
    QVERIFY(QDir(project.path()).mkpath(QStringLiteral("Assets")));
    QVERIFY(
        writeFile(project.filePath(QStringLiteral("Assets/base.bin")), QByteArrayLiteral("base")));
    QVERIFY(writeFile(project.filePath(QStringLiteral("Assets/consumer.bin")),
                      QByteArrayLiteral("consumer")));
    const auto baseGuid = fabgl::AssetGuid::fromStableName("tests.asset-browser.cache.base");
    const auto consumerGuid =
        fabgl::AssetGuid::fromStableName("tests.asset-browser.cache.consumer");
    auto base = mapping(baseGuid, QStringLiteral("Assets/base.bin"), QStringLiteral("binary"));
    auto consumer =
        mapping(consumerGuid, QStringLiteral("Assets/consumer.bin"), QStringLiteral("binary"));
    consumer.dependencies.push_back(baseGuid);

    fgl::studio::AssetBrowserController controller;
    auto result = controller.setProject(project.path(), {base, consumer});
    QVERIFY2(result, qPrintable(resultMessage(result)));
    const auto baseKey = controller.model()->entry(baseGuid)->pcCacheKey;
    const auto consumerKey = controller.model()->entry(consumerGuid)->pcCacheKey;
    const auto firstCachePath =
        controller.cachedPayloadPath(baseGuid, fabgl::assets::AssetTarget::Pc);
    result = controller.refreshNow();
    QVERIFY2(result, qPrintable(resultMessage(result)));
    QCOMPARE(controller.lastRefreshStats().cacheHits, 2U);
    QCOMPARE(controller.model()->entry(baseGuid)->pcCacheKey, baseKey);

    result = controller.setImportSettings(baseGuid, QStringLiteral("{\"z\":1,\"a\":2}"),
                                          fabgl::assets::AssetTarget::Esp32Psram);
    QVERIFY2(result, qPrintable(resultMessage(result)));
    result = controller.refreshNow();
    QVERIFY2(result, qPrintable(resultMessage(result)));
    const auto changedBaseKey = controller.model()->entry(baseGuid)->pcCacheKey;
    QVERIFY(changedBaseKey != baseKey);
    QVERIFY(controller.model()->entry(consumerGuid)->pcCacheKey != consumerKey);
    QVERIFY(controller.cachedPayloadPath(baseGuid, fabgl::assets::AssetTarget::Pc) !=
            firstCachePath);
    QVERIFY(controller.model()->entry(baseGuid)->esp32Cost.psramBytes > 0U);
    QCOMPARE(controller.model()->entry(baseGuid)->esp32Cost.flashBytes, 0U);
}

void AssetBrowserControllerTests::compiledTilemapCanonicalizesAndBindsGuid() {
    QTemporaryDir project;
    QVERIFY(project.isValid());
    QVERIFY(QDir(project.path()).mkpath(QStringLiteral("Assets")));
    fabgl::assets::Tilemap source;
    source.width = 2U;
    source.height = 1U;
    source.tiles = {1U, 2U};
    const auto encoded = fabgl::assets::encodeTilemap(source);
    QVERIFY(encoded);
    const auto sourcePath = project.filePath(QStringLiteral("Assets/map.fgltilemap"));
    QVERIFY(writeFile(sourcePath, QByteArray(reinterpret_cast<const char*>(encoded.value().data()),
                                             static_cast<qsizetype>(encoded.value().size()))));
    const auto mappedGuid =
        fabgl::AssetGuid::fromStableName("tests.asset-browser.compiled-tilemap");
    const auto map =
        mapping(mappedGuid, QStringLiteral("Assets/map.fgltilemap"), QStringLiteral("tilemap"));
    fgl::studio::AssetBrowserController controller;
    auto result = controller.setProject(project.path(), {map});
    QVERIFY2(result, qPrintable(resultMessage(result)));
    QFile cache(controller.cachedPayloadPath(mappedGuid, fabgl::assets::AssetTarget::Pc));
    QVERIFY(cache.open(QIODevice::ReadOnly));
    const auto cacheBytes = cache.readAll();
    const auto inspected = fabgl::assets::inspectTilemap(
        std::vector<std::uint8_t>(cacheBytes.cbegin(), cacheBytes.cend()));
    QVERIFY(inspected);
    QVERIFY(inspected.value().guid == mappedGuid);

    source.guid = fabgl::AssetGuid::fromStableName("tests.asset-browser.wrong-payload-guid");
    const auto mismatched = fabgl::assets::encodeTilemap(source);
    QVERIFY(mismatched);
    QVERIFY(
        writeFile(sourcePath, QByteArray(reinterpret_cast<const char*>(mismatched.value().data()),
                                         static_cast<qsizetype>(mismatched.value().size()))));
    result = controller.refreshNow();
    QVERIFY2(result, qPrintable(resultMessage(result)));
    const auto* failed = controller.model()->entry(mappedGuid);
    QVERIFY(failed != nullptr);
    QCOMPARE(failed->state, fgl::studio::AssetBrowserState::Error);
    QVERIFY(failed->diagnostic.contains(QStringLiteral("GUID"), Qt::CaseInsensitive));
}

void AssetBrowserControllerTests::traversalAndLinkLikeTreesFailClosed() {
    QTemporaryDir project;
    QTemporaryDir outside;
    QVERIFY(project.isValid());
    QVERIFY(outside.isValid());
    QVERIFY(QDir(project.path()).mkpath(QStringLiteral("Assets")));
    QVERIFY(
        writeFile(outside.filePath(QStringLiteral("escape.bin")), QByteArrayLiteral("outside")));
    const auto guid = fabgl::AssetGuid::fromStableName("tests.asset-browser.security");
    fgl::studio::AssetBrowserController controller;
    auto unsafe = mapping(guid, QStringLiteral("../escape.bin"), QStringLiteral("binary"));
    auto result = controller.setProject(project.path(), {unsafe});
    QVERIFY(!result);
    QVERIFY(resultMessage(result).contains(QStringLiteral("invalid"), Qt::CaseInsensitive) ||
            resultMessage(result).contains(QStringLiteral("unsafe"), Qt::CaseInsensitive));

    QVERIFY(
        writeFile(project.filePath(QStringLiteral("Assets/safe.bin")), QByteArrayLiteral("safe")));
    auto safe = mapping(guid, QStringLiteral("Assets/safe.bin"), QStringLiteral("binary"));
    result = controller.setProject(project.path(), {safe});
    QVERIFY2(result, qPrintable(resultMessage(result)));
    const auto linkPath = project.filePath(QStringLiteral("Assets/linked-outside"));
    QVERIFY2(createDirectoryLinkForTest(linkPath, outside.path()),
             "The link/junction fixture could not be created.");
    result = controller.refreshNow();
    QVERIFY(!result);
    const auto message = resultMessage(result);
    QVERIFY(message.contains(QStringLiteral("symlink"), Qt::CaseInsensitive) ||
            message.contains(QStringLiteral("junction"), Qt::CaseInsensitive) ||
            message.contains(QStringLiteral("reparse"), Qt::CaseInsensitive) ||
            message.contains(QStringLiteral("link-like"), Qt::CaseInsensitive));
    QVERIFY(!QFileInfo::exists(project.filePath(QStringLiteral(".fabglstudio/escape.bin"))));
}

void AssetBrowserControllerTests::externalAndControlledMovesPreserveGuidMapping() {
    QTemporaryDir project;
    QVERIFY(project.isValid());
    QVERIFY(QDir(project.path()).mkpath(QStringLiteral("Assets/Moved")));
    const auto original = project.filePath(QStringLiteral("Assets/original.bin"));
    const auto externallyMoved = project.filePath(QStringLiteral("Assets/external.bin"));
    QVERIFY(writeFile(original, QByteArrayLiteral("stable-identity")));
    fabgl::AssetGuid discoveredGuid;
    {
        fgl::studio::AssetBrowserController controller;
        QSignalSpy moved(&controller, &fgl::studio::AssetBrowserController::assetMappingMoved);
        const auto opened = controller.setProject(project.path(), {});
        QVERIFY2(opened, qPrintable(resultMessage(opened)));
        QCOMPARE(controller.model()->rowCount(), 1);
        discoveredGuid = controller.model()->entries().front().guid;
        QVERIFY(QFile::rename(original, externallyMoved));
        const auto refreshed = controller.refreshNow();
        QVERIFY2(refreshed, qPrintable(resultMessage(refreshed)));
        const auto* entry = controller.model()->entry(discoveredGuid);
        QVERIFY(entry != nullptr);
        QCOMPARE(entry->relativePath, QStringLiteral("Assets/external.bin"));
        QCOMPARE(moved.size(), 1);
    }

    fgl::studio::AssetBrowserController reopened;
    auto stale =
        mapping(discoveredGuid, QStringLiteral("Assets/original.bin"), QStringLiteral("binary"));
    auto result = reopened.setProject(project.path(), {stale});
    QVERIFY2(result, qPrintable(resultMessage(result)));
    QCOMPARE(reopened.model()->entry(discoveredGuid)->relativePath,
             QStringLiteral("Assets/external.bin"));
    result = reopened.relocateAsset(discoveredGuid, QStringLiteral("Assets/Moved/final.bin"));
    QVERIFY2(result, qPrintable(resultMessage(result)));
    result = reopened.refreshNow();
    QVERIFY2(result, qPrintable(resultMessage(result)));
    const auto* finalEntry = reopened.model()->entry(discoveredGuid);
    QVERIFY(finalEntry != nullptr);
    QCOMPARE(finalEntry->relativePath, QStringLiteral("Assets/Moved/final.bin"));
    QVERIFY(QFileInfo::exists(project.filePath(QStringLiteral("Assets/Moved/final.bin"))));
    QVERIFY(!QFileInfo::exists(externallyMoved));
}

void AssetBrowserControllerTests::importDiagnosticsAndStorageBudgetsAreObservable() {
    QTemporaryDir project;
    QVERIFY(project.isValid());
    QVERIFY(QDir(project.path()).mkpath(QStringLiteral("Assets")));
    QVERIFY(writeFile(project.filePath(QStringLiteral("Assets/broken.fgli")),
                      QByteArrayLiteral("not-an-indexed-image")));
    QVERIFY(writeFile(project.filePath(QStringLiteral("Assets/large.bin")), QByteArray(128, 'x')));
    const auto brokenGuid = fabgl::AssetGuid::fromStableName("tests.asset-browser.broken");
    const auto largeGuid = fabgl::AssetGuid::fromStableName("tests.asset-browser.large");
    auto broken =
        mapping(brokenGuid, QStringLiteral("Assets/broken.fgli"), QStringLiteral("image"));
    auto large = mapping(largeGuid, QStringLiteral("Assets/large.bin"), QStringLiteral("binary"));
    fgl::studio::AssetBrowserLimits limits;
    limits.flashBudgetBytes = 1U;
    fgl::studio::AssetBrowserController controller;
    QSignalSpy diagnostics(&controller, &fgl::studio::AssetBrowserController::diagnosticRaised);
    QSignalSpy budgets(&controller, &fgl::studio::AssetBrowserController::storageBudgetExceeded);
    const auto opened = controller.setProject(project.path(), {broken, large}, limits);
    QVERIFY2(opened, qPrintable(resultMessage(opened)));
    const auto* brokenEntry = controller.model()->entry(brokenGuid);
    QVERIFY(brokenEntry != nullptr);
    QCOMPARE(brokenEntry->state, fgl::studio::AssetBrowserState::Error);
    QVERIFY(!brokenEntry->diagnostic.isEmpty());
    QVERIFY(!diagnostics.isEmpty());
    QVERIFY(!budgets.isEmpty());
    QCOMPARE(budgets.front().at(0).toString(), QStringLiteral("Flash"));
    QVERIFY(budgets.front().at(1).toULongLong() > budgets.front().at(2).toULongLong());
}

QTEST_MAIN(AssetBrowserControllerTests)

#include "asset_browser_controller_tests.moc"
