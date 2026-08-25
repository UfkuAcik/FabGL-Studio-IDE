#pragma once

#include <fabgl/assets/tilemap_importer.h>
#include <fabgl/audio/audio_mixer.h>
#include <fabgl/material/material.h>
#include <fabgl/profiling/profiler.h>
#include <fabgl/rendering/racer_track.h>
#include <fabgl/rendering/raycast_map_asset.h>
#include <fabgl/serialization/material_serializer.h>
#include <fabgl/ui/runtime_widgets.h>

#include <QString>
#include <QStringList>
#include <QWidget>

#include <cstddef>
#include <cstdint>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTableWidget;
class QTreeWidget;
class QPushButton;

namespace fgl::studio {

class MaterialEditorWidget final : public QWidget {
    Q_OBJECT

  public:
    explicit MaterialEditorWidget(QWidget* parent = nullptr);

    [[nodiscard]] const fabgl::MaterialAsset& materialAsset() const noexcept;
    [[nodiscard]] fabgl::RendererBackend rendererBackend() const noexcept;
    [[nodiscard]] const fabgl::MaterialCostEstimate& costEstimate() const noexcept;
    [[nodiscard]] std::uint64_t previewChecksum() const noexcept;
    [[nodiscard]] qsizetype validationIssueCount() const noexcept;
    [[nodiscard]] bool validForSelectedRenderer() const noexcept;
    [[nodiscard]] bool rendererCompatible() const noexcept;
    [[nodiscard]] QString filePath() const;
    [[nodiscard]] bool modified() const noexcept;

    [[nodiscard]] bool setMaterialAsset(fabgl::MaterialAsset asset, QString& errorMessage);
    [[nodiscard]] bool setFlatColor(fabgl::Color color, QString& errorMessage);
    void setRendererBackend(fabgl::RendererBackend backend);
    [[nodiscard]] bool openMaterialFile(const QString& filePath, QString& errorMessage);
    [[nodiscard]] bool saveMaterialFile(const QString& filePath, QString& errorMessage);

  public slots:
    void refreshPreview();

  signals:
    void statusMessage(const QString& message);

  private:
    void syncControlsFromAsset();
    void applyControlsToAsset();
    void setModified(bool modified);

    fabgl::MaterialAsset m_asset;
    fabgl::RendererBackend m_backend = fabgl::RendererBackend::Renderer2D;
    fabgl::MaterialCostEstimate m_cost;
    std::uint64_t m_previewChecksum = 0U;
    qsizetype m_validationIssues = 0;
    bool m_valid = false;
    bool m_compatible = false;
    bool m_modified = false;
    bool m_updating = false;
    QString m_filePath;
    QLineEdit* m_name = nullptr;
    QComboBox* m_backendCombo = nullptr;
    QComboBox* m_colorMode = nullptr;
    QComboBox* m_dither = nullptr;
    QComboBox* m_sampling = nullptr;
    QComboBox* m_lighting = nullptr;
    QComboBox* m_blend = nullptr;
    QSpinBox* m_flatRed = nullptr;
    QSpinBox* m_flatGreen = nullptr;
    QSpinBox* m_flatBlue = nullptr;
    QSpinBox* m_flatAlpha = nullptr;
    QSpinBox* m_emissiveStrength = nullptr;
    QCheckBox* m_fog = nullptr;
    QCheckBox* m_billboard = nullptr;
    QCheckBox* m_doubleSided = nullptr;
    QCheckBox* m_rendererChecks[4]{};
    QLabel* m_preview = nullptr;
    QLabel* m_costLabel = nullptr;
    QLabel* m_compatibilityLabel = nullptr;
    QTableWidget* m_validation = nullptr;
};

struct ParticleAuthoringSettings final {
    float spawnRate = 18.0F;
    std::size_t burstCount = 12U;
    float lifetimeSeconds = 1.5F;
    fabgl::Vec2 velocity{14.0F, -25.0F};
    fabgl::Vec2 gravity{0.0F, 18.0F};
    fabgl::Color startColor{255U, 220U, 96U, 255U};
    fabgl::Color endColor{255U, 80U, 48U, 0U};
    float startSize = 3.0F;
    float endSize = 0.5F;
    float startRotationDegrees = 0.0F;
    float endRotationDegrees = 180.0F;
    std::size_t maximumParticles = 128U;
};

class ParticleEditorWidget final : public QWidget {
    Q_OBJECT

  public:
    explicit ParticleEditorWidget(QWidget* parent = nullptr);

    [[nodiscard]] const ParticleAuthoringSettings& settings() const noexcept;
    [[nodiscard]] bool setSettings(ParticleAuthoringSettings settings, QString& errorMessage);
    [[nodiscard]] std::uint64_t previewChecksum() const noexcept;
    [[nodiscard]] std::size_t previewParticleCount() const noexcept;
    [[nodiscard]] std::size_t estimatedRuntimeBytes() const noexcept;
    [[nodiscard]] bool settingsValid() const noexcept;

  public slots:
    void refreshPreview();

  signals:
    void statusMessage(const QString& message);

  private:
    void syncControlsFromSettings();
    void applyControlsToSettings();

    ParticleAuthoringSettings m_settings;
    std::uint64_t m_previewChecksum = 0U;
    std::size_t m_previewParticleCount = 0U;
    std::size_t m_estimatedRuntimeBytes = 0U;
    bool m_valid = false;
    bool m_updating = false;
    QDoubleSpinBox* m_spawnRate = nullptr;
    QSpinBox* m_burst = nullptr;
    QDoubleSpinBox* m_lifetime = nullptr;
    QDoubleSpinBox* m_velocityX = nullptr;
    QDoubleSpinBox* m_velocityY = nullptr;
    QDoubleSpinBox* m_gravityX = nullptr;
    QDoubleSpinBox* m_gravityY = nullptr;
    QDoubleSpinBox* m_startSize = nullptr;
    QDoubleSpinBox* m_endSize = nullptr;
    QDoubleSpinBox* m_startRotation = nullptr;
    QDoubleSpinBox* m_endRotation = nullptr;
    QSpinBox* m_maximumParticles = nullptr;
    QSpinBox* m_startColor[4]{};
    QSpinBox* m_endColor[4]{};
    QLabel* m_preview = nullptr;
    QLabel* m_costLabel = nullptr;
    QLabel* m_status = nullptr;
};

struct TilemapLayerModel final {
    QString name;
    bool collision = false;
    std::vector<std::uint32_t> cells;
    fabgl::assets::TilemapLayerKind kind = fabgl::assets::TilemapLayerKind::Tiles;
    bool visible = true;
    std::uint8_t opacity = 255U;
    float parallaxX = 1.0F;
    float parallaxY = 1.0F;
};

struct TilemapObjectModel final {
    std::uint32_t id = 0U;
    QString type;
    fabgl::Rect bounds;
    std::uint16_t layer = 0U;
    fabgl::AssetGuid asset{};
};

struct TilemapChunkModel final {
    TilemapChunkModel() = default;
    TilemapChunkModel(const std::uint32_t xValue, const std::uint32_t yValue,
                      const std::uint32_t widthValue, const std::uint32_t heightValue)
        : x(xValue), y(yValue), width(widthValue), height(heightValue) {}

    std::uint32_t x = 0U;
    std::uint32_t y = 0U;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint16_t layer = 0U;
    std::vector<std::uint32_t> cells;
};

struct TileAnimationFrameModel final {
    std::uint32_t tile = 0U;
    std::uint32_t durationMilliseconds = 100U;
};

struct TileAnimationModel final {
    std::uint32_t outputTile = 0U;
    std::vector<TileAnimationFrameModel> frames;
};

class TilemapEditorWidget final : public QWidget {
    Q_OBJECT

  public:
    explicit TilemapEditorWidget(QWidget* parent = nullptr);

    [[nodiscard]] bool newMap(std::uint32_t width, std::uint32_t height, QString& errorMessage);
    [[nodiscard]] bool addLayer(const QString& name, bool collision, QString& errorMessage);
    [[nodiscard]] bool paintTile(std::size_t layer, std::uint32_t x, std::uint32_t y,
                                 std::uint32_t tile, QString& errorMessage);
    [[nodiscard]] bool addObject(QString type, fabgl::Rect bounds, QString& errorMessage);
    [[nodiscard]] bool addChunk(TilemapChunkModel chunk, QString& errorMessage);
    [[nodiscard]] bool addAnimation(TileAnimationModel animation, QString& errorMessage);
    [[nodiscard]] bool importTilemapFile(const QString& filePath, QString& errorMessage);
    [[nodiscard]] bool exportTilemapFile(const QString& filePath, std::size_t layer,
                                         QString& errorMessage) const;

    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] std::uint32_t height() const noexcept;
    [[nodiscard]] qsizetype layerCount() const noexcept;
    [[nodiscard]] qsizetype collisionLayerCount() const noexcept;
    [[nodiscard]] qsizetype objectCount() const noexcept;
    [[nodiscard]] qsizetype chunkCount() const noexcept;
    [[nodiscard]] qsizetype animationCount() const noexcept;
    [[nodiscard]] std::uint32_t tileAt(std::size_t layer, std::uint32_t x,
                                       std::uint32_t y) const noexcept;
    [[nodiscard]] std::size_t estimatedRuntimeBytes() const noexcept;
    [[nodiscard]] std::uint64_t previewChecksum() const noexcept;
    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;

  public slots:
    void refreshPreview();
    void undo();
    void redo();

  signals:
    void statusMessage(const QString& message);

  private:
    struct Snapshot final {
        fabgl::AssetGuid guid{};
        std::uint32_t width = 0U;
        std::uint32_t height = 0U;
        std::uint16_t tileWidth = 8U;
        std::uint16_t tileHeight = 8U;
        std::vector<TilemapLayerModel> layers;
        std::vector<TilemapObjectModel> objects;
        std::vector<TilemapChunkModel> chunks;
        std::vector<TileAnimationModel> animations;
        std::vector<fabgl::assets::TilemapTilesetReference> tilesets;
        std::uint32_t nextObjectId = 1U;
    };

    [[nodiscard]] Snapshot snapshot() const;
    void restoreSnapshot(Snapshot snapshot);
    void recordUndoPoint();
    void updateHistoryActions();
    void refreshAuthoringControls();
    void refreshTables();

    fabgl::AssetGuid m_guid{};
    std::uint32_t m_width = 0U;
    std::uint32_t m_height = 0U;
    std::uint16_t m_tileWidth = 8U;
    std::uint16_t m_tileHeight = 8U;
    std::vector<TilemapLayerModel> m_layers;
    std::vector<TilemapObjectModel> m_objects;
    std::vector<TilemapChunkModel> m_chunks;
    std::vector<TileAnimationModel> m_animations;
    std::vector<fabgl::assets::TilemapTilesetReference> m_tilesets;
    std::uint32_t m_nextObjectId = 1U;
    std::size_t m_estimatedRuntimeBytes = 0U;
    std::uint64_t m_previewChecksum = 0U;
    std::vector<Snapshot> m_undoHistory;
    std::vector<Snapshot> m_redoHistory;
    QPushButton* m_undoButton = nullptr;
    QPushButton* m_redoButton = nullptr;
    QLineEdit* m_layerName = nullptr;
    QCheckBox* m_layerCollision = nullptr;
    QComboBox* m_paintLayer = nullptr;
    QSpinBox* m_paintX = nullptr;
    QSpinBox* m_paintY = nullptr;
    QSpinBox* m_paintTile = nullptr;
    QLineEdit* m_objectType = nullptr;
    QDoubleSpinBox* m_objectX = nullptr;
    QDoubleSpinBox* m_objectY = nullptr;
    QDoubleSpinBox* m_objectWidth = nullptr;
    QDoubleSpinBox* m_objectHeight = nullptr;
    QSpinBox* m_chunkX = nullptr;
    QSpinBox* m_chunkY = nullptr;
    QSpinBox* m_chunkWidth = nullptr;
    QSpinBox* m_chunkHeight = nullptr;
    QSpinBox* m_animationOutput = nullptr;
    QSpinBox* m_animationFrame = nullptr;
    QSpinBox* m_animationDuration = nullptr;
    QTableWidget* m_layerTable = nullptr;
    QTableWidget* m_objectTable = nullptr;
    QTableWidget* m_animationTable = nullptr;
    QLabel* m_preview = nullptr;
    QLabel* m_costLabel = nullptr;
};

enum class RaycastCellType : std::uint8_t {
    Empty = 0U,
    Wall = 1U,
    Door = 2U,
    Secret = 3U,
    Light = 4U,
};

class RaycastMapEditorWidget final : public QWidget {
    Q_OBJECT

  public:
    explicit RaycastMapEditorWidget(QWidget* parent = nullptr);

    [[nodiscard]] bool newMap(int width, int height, QString& errorMessage);
    [[nodiscard]] bool paintCell(int x, int y, RaycastCellType type, QString& errorMessage);
    [[nodiscard]] RaycastCellType cellType(int x, int y) const noexcept;
    [[nodiscard]] bool openMapFile(const QString& filePath, QString& errorMessage);
    [[nodiscard]] bool saveMapFile(const QString& filePath, QString& errorMessage);
    [[nodiscard]] const fabgl::rendering::RaycastMapAsset& mapAsset() const noexcept;
    [[nodiscard]] bool mapValid() const noexcept;
    [[nodiscard]] qsizetype cellCount(RaycastCellType type) const noexcept;
    [[nodiscard]] std::uint64_t previewChecksum() const noexcept;
    [[nodiscard]] QString filePath() const;

  public slots:
    void refreshPreview();

  signals:
    void statusMessage(const QString& message);

  private:
    void refreshGrid();

    fabgl::rendering::RaycastMapAsset m_asset;
    std::uint64_t m_previewChecksum = 0U;
    bool m_valid = false;
    QString m_filePath;
    QTableWidget* m_grid = nullptr;
    QComboBox* m_brush = nullptr;
    QLabel* m_preview = nullptr;
    QLabel* m_status = nullptr;
};

class TrackEditorWidget final : public QWidget {
    Q_OBJECT

  public:
    explicit TrackEditorWidget(QWidget* parent = nullptr);

    [[nodiscard]] bool newTrack(const QString& name, QString& errorMessage);
    [[nodiscard]] bool appendSegment(fabgl::rendering::RoadSegment segment, QString& errorMessage);
    [[nodiscard]] bool updateSegment(std::size_t index, fabgl::rendering::RoadSegment segment,
                                     QString& errorMessage);
    [[nodiscard]] bool addCheckpoint(std::uint32_t segment, QString name, QString& errorMessage);
    [[nodiscard]] bool addScenery(std::uint32_t segment, float lateral, float scale,
                                  fabgl::AssetGuid sprite, QString& errorMessage);
    [[nodiscard]] bool addOpponent(std::uint32_t segment, float lateral, float speed, float skill,
                                   fabgl::AssetGuid sprite, QString& errorMessage);
    [[nodiscard]] bool setWeather(fabgl::rendering::RacerWeatherMetadata weather,
                                  QString& errorMessage);
    [[nodiscard]] bool openTrackFile(const QString& filePath, QString& errorMessage);
    [[nodiscard]] bool saveTrackFile(const QString& filePath, QString& errorMessage);

    [[nodiscard]] const fabgl::rendering::RacerTrackAsset& track() const noexcept;
    [[nodiscard]] bool trackValid() const noexcept;
    [[nodiscard]] qsizetype segmentCount() const noexcept;
    [[nodiscard]] qsizetype checkpointCount() const noexcept;
    [[nodiscard]] qsizetype sceneryCount() const noexcept;
    [[nodiscard]] qsizetype opponentCount() const noexcept;
    [[nodiscard]] std::uint64_t previewChecksum() const noexcept;
    [[nodiscard]] QString filePath() const;
    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;

  public slots:
    void refreshPreview();
    void undo();
    void redo();

  signals:
    void statusMessage(const QString& message);

  private:
    struct Snapshot final {
        fabgl::rendering::RacerTrackAsset track;
    };

    [[nodiscard]] Snapshot snapshot() const;
    void restoreSnapshot(Snapshot snapshot);
    void recordUndoPoint();
    void updateHistoryActions();
    void refreshAuthoringControls();
    void refreshTables();

    fabgl::rendering::RacerTrackAsset m_track;
    std::uint64_t m_previewChecksum = 0U;
    bool m_valid = false;
    bool m_updatingControls = false;
    QString m_filePath;
    std::vector<Snapshot> m_undoHistory;
    std::vector<Snapshot> m_redoHistory;
    QPushButton* m_undoButton = nullptr;
    QPushButton* m_redoButton = nullptr;
    QSpinBox* m_segmentIndex = nullptr;
    QDoubleSpinBox* m_segmentCurve = nullptr;
    QDoubleSpinBox* m_segmentHill = nullptr;
    QDoubleSpinBox* m_segmentWidth = nullptr;
    QSpinBox* m_checkpointSegment = nullptr;
    QLineEdit* m_checkpointName = nullptr;
    QSpinBox* m_scenerySegment = nullptr;
    QDoubleSpinBox* m_sceneryLateral = nullptr;
    QDoubleSpinBox* m_sceneryScale = nullptr;
    QLineEdit* m_scenerySprite = nullptr;
    QSpinBox* m_opponentSegment = nullptr;
    QDoubleSpinBox* m_opponentLateral = nullptr;
    QDoubleSpinBox* m_opponentSpeed = nullptr;
    QDoubleSpinBox* m_opponentSkill = nullptr;
    QLineEdit* m_opponentSprite = nullptr;
    QTableWidget* m_segmentTable = nullptr;
    QTableWidget* m_sceneryTable = nullptr;
    QTableWidget* m_opponentTable = nullptr;
    QComboBox* m_weather = nullptr;
    QLabel* m_preview = nullptr;
    QLabel* m_status = nullptr;
};

class UIEditorWidget final : public QWidget {
    Q_OBJECT

  public:
    explicit UIEditorWidget(QWidget* parent = nullptr);

    [[nodiscard]] std::uint32_t addWidget(fabgl::UIWidgetType type, std::uint32_t parentId,
                                          QString text, QString& errorMessage);
    [[nodiscard]] bool removeWidget(std::uint32_t id, QString& errorMessage);
    [[nodiscard]] bool reparentWidget(std::uint32_t id, std::uint32_t parentId,
                                      QString& errorMessage);
    [[nodiscard]] bool setWidgetText(std::uint32_t id, QString text, QString& errorMessage);
    [[nodiscard]] bool setWidgetLayout(std::uint32_t id, fabgl::UILayoutProperties properties,
                                       QString& errorMessage);
    [[nodiscard]] bool setEditorTheme(fabgl::UITheme theme, QString& errorMessage);
    [[nodiscard]] bool setEditorScale(float scale, QString& errorMessage);
    [[nodiscard]] qsizetype widgetCount() const noexcept;
    [[nodiscard]] std::uint32_t selectedWidgetId() const noexcept;
    [[nodiscard]] std::uint32_t parentOf(std::uint32_t id) const noexcept;
    [[nodiscard]] std::uint64_t previewChecksum() const noexcept;
    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    [[nodiscard]] const fabgl::RuntimeUI& runtimeUI() const noexcept;

  public slots:
    void undo();
    void redo();
    void refreshPreview();

  signals:
    void statusMessage(const QString& message);

  private:
    struct Node final {
        std::uint32_t id = 0U;
        std::uint32_t parentId = 0U;
        fabgl::UIWidgetType type = fabgl::UIWidgetType::Panel;
        QString text;
        fabgl::UILayoutProperties properties;
    };

    struct Snapshot final {
        std::vector<Node> nodes;
        std::uint32_t nextId = 1U;
        std::uint32_t selectedId = 0U;
        fabgl::UITheme theme;
        float scale = 1.0F;
    };

    [[nodiscard]] Snapshot snapshot() const;
    void restoreSnapshot(Snapshot snapshot);
    void commitUndoSnapshot(Snapshot snapshot);
    void updateHistoryActions();
    [[nodiscard]] bool rebuildRuntime(QString& errorMessage);
    void refreshHierarchy();
    void refreshParentChoices();
    void syncPropertiesFromSelection();
    [[nodiscard]] Node* node(std::uint32_t id) noexcept;
    [[nodiscard]] const Node* node(std::uint32_t id) const noexcept;

    fabgl::RuntimeUI m_runtime;
    std::vector<Node> m_nodes;
    std::vector<std::pair<std::uint32_t, fabgl::UIElementId>> m_runtimeIds;
    std::vector<Snapshot> m_undoHistory;
    std::vector<Snapshot> m_redoHistory;
    std::uint32_t m_nextId = 1U;
    std::uint32_t m_selectedId = 0U;
    fabgl::UITheme m_theme;
    float m_scale = 1.0F;
    std::uint64_t m_previewChecksum = 0U;
    bool m_updatingControls = false;
    QTreeWidget* m_hierarchy = nullptr;
    QComboBox* m_palette = nullptr;
    QComboBox* m_parent = nullptr;
    QPushButton* m_undoButton = nullptr;
    QPushButton* m_redoButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QLineEdit* m_text = nullptr;
    QDoubleSpinBox* m_anchorMinX = nullptr;
    QDoubleSpinBox* m_anchorMinY = nullptr;
    QDoubleSpinBox* m_anchorMaxX = nullptr;
    QDoubleSpinBox* m_anchorMaxY = nullptr;
    QDoubleSpinBox* m_offsetMinX = nullptr;
    QDoubleSpinBox* m_offsetMinY = nullptr;
    QDoubleSpinBox* m_offsetMaxX = nullptr;
    QDoubleSpinBox* m_offsetMaxY = nullptr;
    QCheckBox* m_visible = nullptr;
    QCheckBox* m_enabled = nullptr;
    QComboBox* m_themeCombo = nullptr;
    QDoubleSpinBox* m_scaleSpin = nullptr;
    QLabel* m_preview = nullptr;
    QLabel* m_status = nullptr;
};

struct PackageCliCommand final {
    QString program;
    QStringList arguments;
    bool executableApprovalIncluded = false;
};

class PackageManagerWidget final : public QWidget {
    Q_OBJECT

  public:
    explicit PackageManagerWidget(QWidget* parent = nullptr);

    void setProjectCliPath(QString path);
    void setProjectManifestPath(QString path);
    void setPackageSourcePath(QString path);
    void setPackageId(QString packageId);
    void setProjectTrusted(bool trusted);
    void setAllowExecutablePackage(bool allow);

    [[nodiscard]] bool projectTrusted() const noexcept;
    [[nodiscard]] QString trustStatus() const;
    [[nodiscard]] PackageCliCommand installCommand() const;
    [[nodiscard]] PackageCliCommand listCommand() const;
    [[nodiscard]] PackageCliCommand validateCommand() const;
    [[nodiscard]] PackageCliCommand removeCommand() const;
    [[nodiscard]] bool canInstallExecutablePackage() const noexcept;

  signals:
    void commandPrepared(const QString& program, const QStringList& arguments);

  private:
    void refreshTrustStatus();

    QLineEdit* m_cliPath = nullptr;
    QLineEdit* m_projectPath = nullptr;
    QLineEdit* m_sourcePath = nullptr;
    QLineEdit* m_packageId = nullptr;
    QCheckBox* m_allowExecutable = nullptr;
    QLabel* m_trustStatus = nullptr;
    QLabel* m_commandPreview = nullptr;
    bool m_projectTrusted = false;
};

class AudioMixerEditorWidget final : public QWidget {
    Q_OBJECT

  public:
    explicit AudioMixerEditorWidget(QWidget* parent = nullptr);

    [[nodiscard]] bool setBusVolume(fabgl::AudioBusId bus, float volume, QString& errorMessage);
    [[nodiscard]] bool setBusPan(fabgl::AudioBusId bus, float pan, QString& errorMessage);
    [[nodiscard]] bool setBusMuted(fabgl::AudioBusId bus, bool muted, QString& errorMessage);
    [[nodiscard]] const fabgl::AudioBusSettings* busSettings(fabgl::AudioBusId bus) const noexcept;
    [[nodiscard]] fabgl::AudioMixerStats mixerStats() const noexcept;
    [[nodiscard]] bool renderTestTone(fabgl::AudioBusId bus, std::size_t frames,
                                      QString& errorMessage);
    [[nodiscard]] std::uint64_t lastMixChecksum() const noexcept;
    [[nodiscard]] std::uint64_t lastNonZeroSamples() const noexcept;

  signals:
    void statusMessage(const QString& message);

  private:
    void refreshTable();

    fabgl::AudioMixer m_mixer;
    std::vector<float> m_testTone;
    std::uint64_t m_lastMixChecksum = 0U;
    std::uint64_t m_lastNonZeroSamples = 0U;
    QTableWidget* m_busTable = nullptr;
    QLabel* m_statsLabel = nullptr;
};

class ProfilerTimelineWidget final : public QWidget {
    Q_OBJECT

  public:
    explicit ProfilerTimelineWidget(QWidget* parent = nullptr);

    [[nodiscard]] bool recordMeasuredPc(QString metric, double value, fabgl::ProfilerUnit unit,
                                        QString& errorMessage);
    [[nodiscard]] bool recordMeasuredEsp32(QString metric, double value, fabgl::ProfilerUnit unit,
                                           QString& errorMessage);
    [[nodiscard]] bool recordEstimatedEsp32(QString metric, double value, fabgl::ProfilerUnit unit,
                                            QString& errorMessage);
    [[nodiscard]] bool setBudget(QString metric, double maximum, fabgl::ProfilerUnit unit,
                                 QString& errorMessage);
    [[nodiscard]] std::size_t sampleCount() const noexcept;
    [[nodiscard]] qsizetype measuredPcCount() const noexcept;
    [[nodiscard]] qsizetype measuredEsp32Count() const noexcept;
    [[nodiscard]] qsizetype estimatedEsp32Count() const noexcept;
    [[nodiscard]] fabgl::Result<fabgl::ProfilerSummary>
    summary(const QString& metric, fabgl::ProfilerSampleSource source) const;

  public slots:
    void refreshTimeline();

  signals:
    void statusMessage(const QString& message);

  private:
    fabgl::Profiler m_profiler;
    QTableWidget* m_timeline = nullptr;
    QLabel* m_legend = nullptr;
};

} // namespace fgl::studio
