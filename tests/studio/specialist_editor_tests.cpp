#include "SpecialistEditorPanels.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest>

#include <cstddef>
#include <utility>

class SpecialistEditorTests final : public QObject {
    Q_OBJECT

  private slots:
    void materialSerializerRoundTrip();
    void particlePreviewUsesRuntimePool();
    void tilemapPipelineRoundTrip();
    void tilemapRichCanonicalModelIsLossless();
    void tilemapVisibleAuthoringAndTransactions();
    void raycastSerializerRoundTrip();
    void racerTrackSerializerRoundTrip();
    void racerTrackVisibleAuthoringAndTransactions();
    void runtimeUIAuthoringAndTransactions();
    void packageCommandsPreserveTrustBoundary();
    void audioMixerRendersBusPreview();
    void profilerSeparatesMeasuredAndEstimatedSamples();
};

void SpecialistEditorTests::materialSerializerRoundTrip() {
    fgl::studio::MaterialEditorWidget editor;
    QString error;
    QVERIFY(editor.setFlatColor({32U, 96U, 180U, 220U}, error));
    editor.setRendererBackend(fabgl::RendererBackend::Renderer2D);
    QVERIFY(editor.validForSelectedRenderer());
    QVERIFY(editor.rendererCompatible());
    QVERIFY(editor.previewChecksum() != 0U);
    QVERIFY(editor.modified());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("roundtrip.fglmaterial"));
    QVERIFY(editor.saveMaterialFile(path, error));
    QVERIFY(QFileInfo::exists(path));
    QVERIFY(!editor.modified());

    fgl::studio::MaterialEditorWidget loaded;
    QVERIFY(loaded.openMaterialFile(path, error));
    QCOMPARE(loaded.materialAsset().material.flatColor.r, std::uint8_t{32U});
    QCOMPARE(loaded.materialAsset().material.flatColor.g, std::uint8_t{96U});
    QCOMPARE(loaded.materialAsset().material.flatColor.b, std::uint8_t{180U});
    QCOMPARE(loaded.materialAsset().material.flatColor.a, std::uint8_t{220U});
    QCOMPARE(loaded.filePath(), QFileInfo(path).absoluteFilePath());
    QVERIFY(!loaded.modified());
}

void SpecialistEditorTests::particlePreviewUsesRuntimePool() {
    fgl::studio::ParticleEditorWidget editor;
    auto settings = editor.settings();
    settings.spawnRate = 24.0F;
    settings.burstCount = 16U;
    settings.maximumParticles = 64U;
    settings.lifetimeSeconds = 2.0F;
    settings.velocity = {8.0F, -18.0F};
    QString error;
    QVERIFY(editor.setSettings(settings, error));
    QVERIFY(editor.settingsValid());
    QVERIFY(editor.previewParticleCount() >= settings.burstCount);
    QVERIFY(editor.previewParticleCount() <= settings.maximumParticles);
    QVERIFY(editor.estimatedRuntimeBytes() >= settings.maximumParticles * sizeof(float));
    QVERIFY(editor.previewChecksum() != 0U);

    settings.maximumParticles = 0U;
    QVERIFY(!editor.setSettings(settings, error));
    QVERIFY(!error.isEmpty());
}

void SpecialistEditorTests::tilemapPipelineRoundTrip() {
    fgl::studio::TilemapEditorWidget editor;
    QString error;
    QVERIFY(editor.newMap(8U, 6U, error));
    QVERIFY(editor.addLayer(QStringLiteral("Collision"), true, error));
    QVERIFY(editor.paintTile(1U, 3U, 2U, 41U, error));
    QVERIFY(editor.addObject(QStringLiteral("Spawn"), {1.0F, 1.0F, 2.0F, 1.0F}, error));
    QVERIFY(editor.addChunk({0U, 0U, 4U, 3U}, error));
    fgl::studio::TileAnimationModel animation;
    animation.outputTile = 50U;
    animation.frames = {{51U, 80U}, {52U, 120U}};
    QVERIFY(editor.addAnimation(std::move(animation), error));
    QCOMPARE(editor.layerCount(), qsizetype{2});
    QCOMPARE(editor.collisionLayerCount(), qsizetype{1});
    QCOMPARE(editor.objectCount(), qsizetype{1});
    QCOMPARE(editor.chunkCount(), qsizetype{1});
    QCOMPARE(editor.animationCount(), qsizetype{1});
    QCOMPARE(editor.tileAt(1U, 3U, 2U), std::uint32_t{41U});
    QVERIFY(editor.previewChecksum() != 0U);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("collision.fgltilemap"));
    QVERIFY(editor.exportTilemapFile(path, 1U, error));
    fgl::studio::TilemapEditorWidget loaded;
    QVERIFY(loaded.importTilemapFile(path, error));
    QCOMPARE(loaded.width(), std::uint32_t{8U});
    QCOMPARE(loaded.height(), std::uint32_t{6U});
    QCOMPARE(loaded.layerCount(), qsizetype{2});
    QCOMPARE(loaded.collisionLayerCount(), qsizetype{1});
    QCOMPARE(loaded.objectCount(), qsizetype{1});
    QCOMPARE(loaded.chunkCount(), qsizetype{1});
    QCOMPARE(loaded.animationCount(), qsizetype{1});
    QCOMPARE(loaded.tileAt(1U, 3U, 2U), std::uint32_t{41U});
    QVERIFY(loaded.estimatedRuntimeBytes() > 0U);

    QVERIFY(!loaded.newMap(0U, 6U, error));
    QVERIFY(!error.isEmpty());
}

void SpecialistEditorTests::tilemapRichCanonicalModelIsLossless() {
    fabgl::assets::Tilemap source;
    source.guid = fabgl::AssetGuid::fromStableName("specialist.tilemap.rich");
    source.width = 2U;
    source.height = 2U;
    source.tileWidth = 16U;
    source.tileHeight = 16U;
    fabgl::assets::TilemapLayer ground;
    ground.name = "Ground";
    ground.cells = {1U, 2U, 3U, 0U};
    ground.parallaxX = 0.5F;
    ground.parallaxY = 0.75F;
    ground.opacity = 173U;
    ground.visible = false;
    fabgl::assets::TilemapLayer objects;
    objects.name = "Objects";
    objects.kind = fabgl::assets::TilemapLayerKind::Objects;
    objects.cells.resize(4U);
    source.layers = {ground, objects};
    source.tiles = ground.cells;
    fabgl::assets::TilemapTilesetReference tileset;
    tileset.tileset = fabgl::AssetGuid::fromStableName("specialist.tilemap.tileset");
    tileset.firstTile = 1U;
    tileset.tileCount = 3U;
    source.tilesets.push_back(tileset);
    fabgl::assets::TilemapObject object;
    object.id = 42U;
    object.layer = 1U;
    object.type = "Portal";
    object.bounds = {0.25F, 0.5F, 1.0F, 1.25F};
    object.asset = fabgl::AssetGuid::fromStableName("specialist.tilemap.portal-prefab");
    source.objects.push_back(object);
    fabgl::assets::TilemapChunk chunk;
    chunk.layer = 0U;
    chunk.width = 2U;
    chunk.height = 1U;
    chunk.cells = {1U, 2U};
    source.chunks.push_back(chunk);
    fabgl::assets::TileAnimation animation;
    animation.outputTile = 1U;
    animation.frames = {{2U, 70U}, {3U, 130U}};
    source.animations.push_back(animation);
    const auto encoded = fabgl::assets::encodeTilemap(source);
    QVERIFY(encoded);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto inputPath = directory.filePath(QStringLiteral("rich-input.fgltilemap"));
    QFile input(inputPath);
    QVERIFY(input.open(QIODevice::WriteOnly));
    QCOMPARE(input.write(reinterpret_cast<const char*>(encoded.value().data()),
                         static_cast<qint64>(encoded.value().size())),
             static_cast<qint64>(encoded.value().size()));
    input.close();

    fgl::studio::TilemapEditorWidget editor;
    QString error;
    QVERIFY(editor.importTilemapFile(inputPath, error));
    QVERIFY(!editor.exportTilemapFile(directory.filePath(QStringLiteral("legacy-output.fglt")), 0U,
                                      error));
    const auto outputPath = directory.filePath(QStringLiteral("rich-output.fgltilemap"));
    QVERIFY(editor.exportTilemapFile(outputPath, 0U, error));
    QFile output(outputPath);
    QVERIFY(output.open(QIODevice::ReadOnly));
    const auto outputBytes = output.readAll();
    const std::vector<std::uint8_t> serialized(outputBytes.begin(), outputBytes.end());
    QCOMPARE(serialized, encoded.value());
}

void SpecialistEditorTests::tilemapVisibleAuthoringAndTransactions() {
    fgl::studio::TilemapEditorWidget editor;
    auto* layerName = editor.findChild<QLineEdit*>(QStringLiteral("tilemapLayerNameEdit"));
    auto* collision = editor.findChild<QCheckBox*>(QStringLiteral("tilemapLayerCollisionCheck"));
    auto* addLayer = editor.findChild<QPushButton*>(QStringLiteral("tilemapAddLayerButton"));
    auto* layer = editor.findChild<QComboBox*>(QStringLiteral("tilemapPaintLayerCombo"));
    auto* paintX = editor.findChild<QSpinBox*>(QStringLiteral("tilemapPaintXSpin"));
    auto* paintY = editor.findChild<QSpinBox*>(QStringLiteral("tilemapPaintYSpin"));
    auto* brush = editor.findChild<QSpinBox*>(QStringLiteral("tilemapBrushTileSpin"));
    auto* paint = editor.findChild<QPushButton*>(QStringLiteral("tilemapPaintButton"));
    auto* addObject = editor.findChild<QPushButton*>(QStringLiteral("tilemapAddObjectButton"));
    auto* addChunk = editor.findChild<QPushButton*>(QStringLiteral("tilemapAddChunkButton"));
    auto* output = editor.findChild<QSpinBox*>(QStringLiteral("tilemapAnimationOutputSpin"));
    auto* frame = editor.findChild<QSpinBox*>(QStringLiteral("tilemapAnimationFrameSpin"));
    auto* duration = editor.findChild<QSpinBox*>(QStringLiteral("tilemapAnimationDurationSpin"));
    auto* addAnimation =
        editor.findChild<QPushButton*>(QStringLiteral("tilemapAddAnimationButton"));
    QVERIFY(layerName != nullptr);
    QVERIFY(collision != nullptr);
    QVERIFY(addLayer != nullptr);
    QVERIFY(layer != nullptr);
    QVERIFY(paintX != nullptr);
    QVERIFY(paintY != nullptr);
    QVERIFY(brush != nullptr);
    QVERIFY(paint != nullptr);
    QVERIFY(addObject != nullptr);
    QVERIFY(addChunk != nullptr);
    QVERIFY(output != nullptr);
    QVERIFY(frame != nullptr);
    QVERIFY(duration != nullptr);
    QVERIFY(addAnimation != nullptr);

    layerName->setText(QStringLiteral("Gameplay collision"));
    collision->setChecked(true);
    addLayer->click();
    QCOMPARE(editor.layerCount(), qsizetype{2});
    QCOMPARE(editor.collisionLayerCount(), qsizetype{1});
    QVERIFY(editor.canUndo());

    layer->setCurrentIndex(1);
    paintX->setValue(2);
    paintY->setValue(3);
    brush->setValue(73);
    paint->click();
    QCOMPARE(editor.tileAt(1U, 2U, 3U), std::uint32_t{73U});

    addObject->click();
    addChunk->click();
    output->setValue(80);
    frame->setValue(81);
    duration->setValue(125);
    addAnimation->click();
    QCOMPARE(editor.objectCount(), qsizetype{1});
    QCOMPARE(editor.chunkCount(), qsizetype{1});
    QCOMPARE(editor.animationCount(), qsizetype{1});

    editor.undo();
    QCOMPARE(editor.animationCount(), qsizetype{0});
    QVERIFY(editor.canRedo());
    editor.redo();
    QCOMPARE(editor.animationCount(), qsizetype{1});
    QVERIFY(editor.previewChecksum() != 0U);
}

void SpecialistEditorTests::raycastSerializerRoundTrip() {
    fgl::studio::RaycastMapEditorWidget editor;
    QString error;
    QVERIFY(editor.newMap(10, 8, error));
    QVERIFY(editor.paintCell(3, 3, fgl::studio::RaycastCellType::Door, error));
    QVERIFY(editor.paintCell(4, 3, fgl::studio::RaycastCellType::Secret, error));
    QVERIFY(editor.paintCell(5, 3, fgl::studio::RaycastCellType::Light, error));
    QVERIFY(!editor.paintCell(0, 0, fgl::studio::RaycastCellType::Empty, error));
    QVERIFY(editor.mapValid());
    QCOMPARE(editor.cellCount(fgl::studio::RaycastCellType::Door), qsizetype{1});
    QVERIFY(editor.previewChecksum() != 0U);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("room.fglray"));
    QVERIFY(editor.saveMapFile(path, error));
    fgl::studio::RaycastMapEditorWidget loaded;
    QVERIFY(loaded.openMapFile(path, error));
    QCOMPARE(loaded.mapAsset().guid, editor.mapAsset().guid);
    QCOMPARE(loaded.cellType(3, 3), fgl::studio::RaycastCellType::Door);
    QCOMPARE(loaded.cellType(4, 3), fgl::studio::RaycastCellType::Secret);
    QCOMPARE(loaded.cellType(5, 3), fgl::studio::RaycastCellType::Light);
    QVERIFY(loaded.mapValid());
}

void SpecialistEditorTests::racerTrackSerializerRoundTrip() {
    fgl::studio::TrackEditorWidget editor;
    QString error;
    QVERIFY(editor.newTrack(QStringLiteral("Test Circuit"), error));
    fabgl::rendering::RoadSegment segment;
    segment.curve = -0.12F;
    segment.hill = 0.04F;
    segment.width = 1.25F;
    QVERIFY(editor.appendSegment(segment, error));
    const auto last = static_cast<std::uint32_t>(editor.segmentCount() - 1);
    QVERIFY(editor.addCheckpoint(last, QStringLiteral("Bonus"), error));
    const auto sprite = fabgl::AssetGuid::fromStableName("test.track.sprite");
    QVERIFY(editor.addScenery(last, -0.5F, 1.2F, sprite, error));
    QVERIFY(editor.addOpponent(last, 0.3F, 52.0F, 0.75F, sprite, error));
    fabgl::rendering::RacerWeatherMetadata weather;
    weather.kind = fabgl::rendering::RacerWeatherKind::Rain;
    weather.intensity = 0.65F;
    weather.visibility = 0.8F;
    weather.seed = 42U;
    QVERIFY(editor.setWeather(weather, error));
    QVERIFY(editor.trackValid());
    QCOMPARE(editor.segmentCount(), qsizetype{17});
    QCOMPARE(editor.checkpointCount(), qsizetype{3});
    QCOMPARE(editor.sceneryCount(), qsizetype{1});
    QCOMPARE(editor.opponentCount(), qsizetype{1});

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("circuit.fgltrack"));
    QVERIFY(editor.saveTrackFile(path, error));
    fgl::studio::TrackEditorWidget loaded;
    QVERIFY(loaded.openTrackFile(path, error));
    QCOMPARE(loaded.track().guid, editor.track().guid);
    QCOMPARE(loaded.segmentCount(), editor.segmentCount());
    QCOMPARE(loaded.sceneryCount(), editor.sceneryCount());
    QCOMPARE(loaded.opponentCount(), editor.opponentCount());
    QVERIFY(loaded.previewChecksum() != 0U);
}

void SpecialistEditorTests::racerTrackVisibleAuthoringAndTransactions() {
    fgl::studio::TrackEditorWidget editor;
    auto* segmentIndex = editor.findChild<QSpinBox*>(QStringLiteral("trackSegmentIndexSpin"));
    auto* curve = editor.findChild<QDoubleSpinBox*>(QStringLiteral("trackSegmentCurveSpin"));
    auto* hill = editor.findChild<QDoubleSpinBox*>(QStringLiteral("trackSegmentHillSpin"));
    auto* width = editor.findChild<QDoubleSpinBox*>(QStringLiteral("trackSegmentWidthSpin"));
    auto* addSegment = editor.findChild<QPushButton*>(QStringLiteral("trackAddSegmentButton"));
    auto* updateSegment =
        editor.findChild<QPushButton*>(QStringLiteral("trackUpdateSegmentButton"));
    auto* checkpointSegment =
        editor.findChild<QSpinBox*>(QStringLiteral("trackCheckpointSegmentSpin"));
    auto* checkpointName = editor.findChild<QLineEdit*>(QStringLiteral("trackCheckpointNameEdit"));
    auto* addCheckpoint =
        editor.findChild<QPushButton*>(QStringLiteral("trackAddCheckpointButton"));
    auto* scenerySegment = editor.findChild<QSpinBox*>(QStringLiteral("trackScenerySegmentSpin"));
    auto* addScenery = editor.findChild<QPushButton*>(QStringLiteral("trackAddSceneryButton"));
    auto* opponentSegment = editor.findChild<QSpinBox*>(QStringLiteral("trackOpponentSegmentSpin"));
    auto* addOpponent = editor.findChild<QPushButton*>(QStringLiteral("trackAddOpponentButton"));
    QVERIFY(segmentIndex != nullptr);
    QVERIFY(curve != nullptr);
    QVERIFY(hill != nullptr);
    QVERIFY(width != nullptr);
    QVERIFY(addSegment != nullptr);
    QVERIFY(updateSegment != nullptr);
    QVERIFY(checkpointSegment != nullptr);
    QVERIFY(checkpointName != nullptr);
    QVERIFY(addCheckpoint != nullptr);
    QVERIFY(scenerySegment != nullptr);
    QVERIFY(addScenery != nullptr);
    QVERIFY(opponentSegment != nullptr);
    QVERIFY(addOpponent != nullptr);

    curve->setValue(0.22);
    hill->setValue(-0.08);
    width->setValue(1.4);
    addSegment->click();
    QCOMPARE(editor.segmentCount(), qsizetype{17});
    QVERIFY(editor.canUndo());

    segmentIndex->setValue(16);
    curve->setValue(-0.31);
    hill->setValue(0.12);
    width->setValue(1.6);
    updateSegment->click();
    QCOMPARE(editor.track().segments[16].curve, -0.31F);
    QCOMPARE(editor.track().segments[16].hill, 0.12F);
    QCOMPARE(editor.track().segments[16].width, 1.6F);

    checkpointSegment->setValue(16);
    checkpointName->setText(QStringLiteral("Hairpin"));
    addCheckpoint->click();
    scenerySegment->setValue(16);
    addScenery->click();
    opponentSegment->setValue(16);
    addOpponent->click();
    QCOMPARE(editor.checkpointCount(), qsizetype{3});
    QCOMPARE(editor.sceneryCount(), qsizetype{1});
    QCOMPARE(editor.opponentCount(), qsizetype{1});

    editor.undo();
    QCOMPARE(editor.opponentCount(), qsizetype{0});
    QVERIFY(editor.canRedo());
    editor.redo();
    QCOMPARE(editor.opponentCount(), qsizetype{1});
    QVERIFY(editor.trackValid());
}

void SpecialistEditorTests::runtimeUIAuthoringAndTransactions() {
    fgl::studio::UIEditorWidget editor;
    QCOMPARE(editor.widgetCount(), qsizetype{2});
    QVERIFY(editor.previewChecksum() != 0U);
    auto* hierarchy = editor.findChild<QTreeWidget*>(QStringLiteral("uiHierarchyTree"));
    auto* palette = editor.findChild<QComboBox*>(QStringLiteral("uiWidgetPaletteCombo"));
    auto* add = editor.findChild<QPushButton*>(QStringLiteral("uiAddWidgetButton"));
    auto* remove = editor.findChild<QPushButton*>(QStringLiteral("uiRemoveWidgetButton"));
    auto* apply = editor.findChild<QPushButton*>(QStringLiteral("uiApplyPropertiesButton"));
    QVERIFY(hierarchy != nullptr);
    QVERIFY(palette != nullptr);
    QVERIFY(add != nullptr);
    QVERIFY(remove != nullptr);
    QVERIFY(apply != nullptr);
    QCOMPARE(hierarchy->topLevelItemCount(), 1);

    const auto rootId = editor.selectedWidgetId();
    const auto sliderIndex = palette->findData(static_cast<int>(fabgl::UIWidgetType::Slider));
    QVERIFY(sliderIndex >= 0);
    palette->setCurrentIndex(sliderIndex);
    add->click();
    QCOMPARE(editor.widgetCount(), qsizetype{3});
    const auto sliderId = editor.selectedWidgetId();
    QVERIFY(sliderId != rootId);
    QCOMPARE(editor.parentOf(sliderId), rootId);
    QVERIFY(editor.canUndo());

    QString error;
    QVERIFY(editor.setWidgetText(sliderId, QStringLiteral("Music volume"), error));
    fabgl::UILayoutProperties layout;
    layout.minimumOffset = {24.0F, 70.0F};
    layout.maximumOffset = {220.0F, 104.0F};
    QVERIFY(editor.setWidgetLayout(sliderId, layout, error));
    const auto panelId =
        editor.addWidget(fabgl::UIWidgetType::Panel, rootId, QStringLiteral("Settings"), error);
    QVERIFY(panelId != 0U);
    QVERIFY(editor.reparentWidget(sliderId, panelId, error));
    QCOMPARE(editor.parentOf(sliderId), panelId);
    QVERIFY(!editor.reparentWidget(panelId, sliderId, error));
    QVERIFY(error.contains(QStringLiteral("cycle"), Qt::CaseInsensitive));

    const auto darkChecksum = editor.previewChecksum();
    fabgl::UITheme light;
    light.panel = {236U, 239U, 245U, 255U};
    light.foreground = {24U, 28U, 36U, 255U};
    light.accent = {25U, 105U, 215U, 255U};
    light.disabled = {145U, 150U, 160U, 255U};
    QVERIFY(editor.setEditorTheme(light, error));
    QVERIFY(editor.previewChecksum() != darkChecksum);
    QVERIFY(editor.setEditorScale(1.5F, error));
    QCOMPARE(editor.runtimeUI().scale(), 1.5F);
    editor.undo();
    QCOMPARE(editor.runtimeUI().scale(), 1.0F);
    QVERIFY(editor.canRedo());
    editor.redo();
    QCOMPARE(editor.runtimeUI().scale(), 1.5F);

    QCOMPARE(editor.widgetCount(), qsizetype{4});
    QVERIFY(editor.removeWidget(panelId, error));
    QCOMPARE(editor.widgetCount(), qsizetype{2});
    editor.undo();
    QCOMPARE(editor.widgetCount(), qsizetype{4});
    editor.redo();
    QCOMPARE(editor.widgetCount(), qsizetype{2});
}

void SpecialistEditorTests::packageCommandsPreserveTrustBoundary() {
    fgl::studio::PackageManagerWidget manager;
    manager.setProjectCliPath(QStringLiteral("C:/FabGL/bin/fabgl_project_cli.exe"));
    manager.setProjectManifestPath(QStringLiteral("C:/Games/Test/Test.fglproject"));
    manager.setPackageSourcePath(QStringLiteral("C:/Packages/org.example.tools"));
    manager.setPackageId(QStringLiteral("org.example.tools"));
    manager.setAllowExecutablePackage(true);
    QVERIFY(!manager.projectTrusted());
    QVERIFY(!manager.canInstallExecutablePackage());
    auto install = manager.installCommand();
    QVERIFY(!install.executableApprovalIncluded);
    QVERIFY(!install.arguments.contains(QStringLiteral("--allow-executable")));
    QVERIFY(manager.trustStatus().contains(QStringLiteral("untrusted"), Qt::CaseInsensitive));

    manager.setProjectTrusted(true);
    QVERIFY(manager.canInstallExecutablePackage());
    install = manager.installCommand();
    QVERIFY(install.executableApprovalIncluded);
    QCOMPARE(install.arguments.last(), QStringLiteral("--allow-executable"));
    QCOMPARE(manager.listCommand().arguments.first(), QStringLiteral("package"));
    QCOMPARE(manager.validateCommand().arguments.at(1), QStringLiteral("validate"));
    QCOMPARE(manager.removeCommand().arguments.last(), QStringLiteral("org.example.tools"));
}

void SpecialistEditorTests::audioMixerRendersBusPreview() {
    fgl::studio::AudioMixerEditorWidget editor;
    QString error;
    const fabgl::AudioBusId music{1U};
    QVERIFY(editor.busSettings(music) != nullptr);
    QVERIFY(editor.setBusVolume(music, 0.5F, error));
    QVERIFY(editor.setBusPan(music, -0.25F, error));
    QVERIFY(editor.setBusMuted(music, false, error));
    auto* volume = editor.findChild<QDoubleSpinBox*>(QStringLiteral("audioBus1VolumeSpin"));
    auto* muted = editor.findChild<QCheckBox*>(QStringLiteral("audioBus1MutedCheck"));
    QVERIFY(volume != nullptr);
    QVERIFY(muted != nullptr);
    volume->setValue(0.6);
    muted->setChecked(true);
    QCOMPARE(editor.busSettings(music)->volume, 0.6F);
    QVERIFY(editor.busSettings(music)->muted);
    muted->setChecked(false);
    QVERIFY(editor.renderTestTone(music, 2048U, error));
    QVERIFY(editor.lastMixChecksum() != 0U);
    QVERIFY(editor.lastNonZeroSamples() > 0U);
    QCOMPARE(editor.mixerStats().mixedFrames, std::uint64_t{2048U});

    QVERIFY(!editor.setBusPan(music, 2.0F, error));
    QVERIFY(!editor.renderTestTone({99U}, 64U, error));
    QVERIFY(!editor.renderTestTone(music, 0U, error));
}

void SpecialistEditorTests::profilerSeparatesMeasuredAndEstimatedSamples() {
    fgl::studio::ProfilerTimelineWidget timeline;
    QString error;
    QVERIFY(timeline.setBudget(QStringLiteral("frame"), 16.0, fabgl::ProfilerUnit::Milliseconds,
                               error));
    QVERIFY(timeline.recordMeasuredPc(QStringLiteral("frame"), 8.0,
                                      fabgl::ProfilerUnit::Milliseconds, error));
    QVERIFY(timeline.recordMeasuredEsp32(QStringLiteral("frame"), 17.0,
                                         fabgl::ProfilerUnit::Milliseconds, error));
    QVERIFY(timeline.recordEstimatedEsp32(QStringLiteral("frame"), 12.0,
                                          fabgl::ProfilerUnit::Milliseconds, error));
    QCOMPARE(timeline.sampleCount(), std::size_t{3U});
    QCOMPARE(timeline.measuredPcCount(), qsizetype{1});
    QCOMPARE(timeline.measuredEsp32Count(), qsizetype{1});
    QCOMPARE(timeline.estimatedEsp32Count(), qsizetype{1});

    const auto pc =
        timeline.summary(QStringLiteral("frame"), fabgl::ProfilerSampleSource::MeasuredPc);
    QVERIFY(pc);
    QCOMPARE(pc.value().sampleCount, std::size_t{1U});
    QVERIFY(!pc.value().budgetExceeded);
    const auto esp32 =
        timeline.summary(QStringLiteral("frame"), fabgl::ProfilerSampleSource::MeasuredEsp32);
    QVERIFY(esp32);
    QVERIFY(esp32.value().budgetExceeded);
    QVERIFY(!timeline.recordMeasuredPc(QStringLiteral("frame"), 100.0, fabgl::ProfilerUnit::Bytes,
                                       error));
    auto* table = timeline.findChild<QTableWidget*>(QStringLiteral("profilerTimelineTable"));
    QVERIFY(table != nullptr);
    QCOMPARE(table->rowCount(), 3);
}

QTEST_MAIN(SpecialistEditorTests)

#include "specialist_editor_tests.moc"
