#pragma once

#include "BuildRunner.h"
#include "EditorViews.h"
#include "EntityModel.h"
#include "PreviewRestartController.h"
#include "ProjectDocument.h"
#include "ProjectTrustStore.h"
#include "RecoveryManager.h"
#include "SceneDocument.h"

#include <fabgl/profiling/profiler.h>
#include <fabgl/runtime/engine_loop.h>

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QMainWindow>
#include <QPalette>
#include <QStringList>
#include <QUndoStack>
#include <QVector>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class QAction;
class QActionGroup;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDockWidget;
class QFileSystemModel;
class QLabel;
class QLineEdit;
class QListView;
class QMenu;
class QModelIndex;
class QPlainTextEdit;
class QProgressBar;
class QTableWidget;
class QTimer;
class QTreeView;

namespace fabgl {
class Scene;
#if defined(_WIN32)
namespace player {
class Win32AudioOutput;
}
#endif
namespace project {
class ProjectAssetLibrary;
class ProjectExtensionModules;
class ProjectExtensionServiceHost;
struct ProjectExtensionDispatchReport;
} // namespace project
} // namespace fabgl

namespace fgl::studio {

struct StudioLaunchOptions final {
    bool safeMode = false;
    bool pluginsEnabled = true;
    bool reopenLastProject = false;
    bool interactiveRecovery = false;
    QString recoveryRoot;
};

class CodeEditorWidget;
class ComponentInspector;
class DiagnosticOutputEdit;
class ExtensionServicePanel;
class VisualScriptEditorWidget;
class AnimatorEditorWidget;
class MemoryAnalyzerWidget;
class MaterialEditorWidget;
class ParticleEditorWidget;
class TilemapEditorWidget;
class RaycastMapEditorWidget;
class TrackEditorWidget;
class UIEditorWidget;
class PackageManagerWidget;
class PrefabEditorPanel;
class InputMapEditorWidget;
class AudioMixerEditorWidget;
class AssetBrowserController;
class ProfilerTimelineWidget;
class SerialConsoleWidget;
class StudioPlaySession;
class ToolchainSetupWidget;
struct ProcessCommand;
class MainWindow final : public QMainWindow {
    Q_OBJECT

  public:
    explicit MainWindow(QWidget* parent = nullptr, StudioLaunchOptions options = {});
    ~MainWindow() override;
    bool openProjectPath(const QString& filePath);
    bool openProjectPath(const QString& filePath, QString& errorMessage);
    [[nodiscard]] SceneDocument& sceneDocument() noexcept;
    [[nodiscard]] const SceneDocument& sceneDocument() const noexcept;
    [[nodiscard]] QStringList namedLayouts() const;
    bool saveNamedLayout(const QString& name, QString& errorMessage);
    bool loadNamedLayout(const QString& name, QString& errorMessage);
    bool deleteNamedLayout(const QString& name, QString& errorMessage);
    [[nodiscard]] bool currentProjectTrusted() const;
    bool setCurrentProjectTrusted(bool trusted, QString& errorMessage);
    [[nodiscard]] bool safeMode() const noexcept;
    [[nodiscard]] bool pluginsEnabled() const noexcept;
    [[nodiscard]] bool telemetryEnabled() const noexcept;
    [[nodiscard]] bool previousSessionWasUnclean() const noexcept;
    [[nodiscard]] QVector<RecoveryEntry> recoveryEntries() const;
    bool performAutosave(QString& errorMessage);
    bool restoreRecovery(const QString& recoveryId, const QString& destinationProjectPath,
                         QString& errorMessage);
    bool discardRecovery(const QString& recoveryId, QString& errorMessage);
    [[nodiscard]] QString lastProjectPath() const;

  protected:
    void closeEvent(QCloseEvent* event) override;

  private:
    enum class RunState { Editing, Playing, Paused };
    enum class Theme { Dark, Light };
    enum class BuildTarget { Pc, Esp32 };
    enum class WorkflowState {
        Idle,
        CustomBuild,
        PcBuild,
        Esp32ExportOnly,
        Esp32Build,
        Esp32Upload,
        Esp32DeployDiagnostics
    };

    static constexpr int LayoutVersion = 3;
    static constexpr qsizetype MaximumRecentProjects = 10;

    void createActions();
    void createMenusAndToolbars();
    void createDockPanels();
    void connectEditorSignals();
    QDockWidget* createDock(const QString& title, const QString& objectName, QWidget* contents,
                            Qt::DockWidgetArea area);

    void newProject();
    void openProject();
    bool saveProject();
    bool saveProjectAs();
    bool saveProjectTo(const QString& filePath);
    bool maybeSave();
    void applyLoadedProject(const QString& filePath, ProjectData projectData);
    [[nodiscard]] bool loadProjectExtensions(
        const QString& filePath, const ProjectData& projectData, const fabgl::Scene& scene,
        std::unique_ptr<fabgl::project::ProjectExtensionModules>& modules,
        std::unique_ptr<fabgl::project::ProjectExtensionServiceHost>& services,
        std::string& manifestPath, std::string& projectRoot, QString& errorMessage) const;
    [[nodiscard]] bool reloadProjectExtensions(QString& errorMessage);
    [[nodiscard]] bool deactivateProjectExtensions(QString& errorMessage);
    void refreshExtensionServices();
    void configureExtensionProductHooks();
    void showExtensionCustomWindow(const QString& qualifiedServiceId);
    void hideExtensionCustomWindow(const QString& qualifiedServiceId, bool notifyExtension);
    void closeExtensionCustomWindows(bool notifyExtensions);
    [[nodiscard]] bool installExtensionCustomWindowDescriptor(const QString& qualifiedServiceId,
                                                              const QByteArray& response,
                                                              QString& errorMessage);
    void invokeExtensionService(const QString& qualifiedServiceId, int kind);
    [[nodiscard]] bool
    reportExtensionServiceFailures(const QString& phase,
                                   const fabgl::project::ProjectExtensionDispatchReport& report,
                                   bool buildOutput = false);
    [[nodiscard]] bool dispatchBuildExtensionServices(WorkflowState state, const QString& phase,
                                                      bool processSucceeded, int exitCode);
    void setDocumentModified(bool forceModified);
    void refreshModifiedState();
    void updateWindowTitle();
    void updateProjectPanel();
    void reloadPresentationAssets();
    void updateProjectTargetProfileUi();
    void editSelectedAssetImportSettings();
    void renameSelectedAsset();
    void moveSelectedAsset();
    [[nodiscard]] QString selectedAssetPath() const;
    [[nodiscard]] bool relocateProjectAsset(const QString& sourcePath,
                                            const QString& destinationPath, QString& errorMessage);
    [[nodiscard]] QString projectRoot() const;
    [[nodiscard]] bool currentTargetProfileSupported() const;
    [[nodiscard]] bool pcTargetProfileSupported() const;
    [[nodiscard]] bool isNativeGameplaySource(const QString& filePath) const;

    void addEntity();
    void addEntityFromAsset(const QString& filePath, float x, float y, float z);
    void duplicateSelectedEntities();
    void deleteSelectedEntity();
    void setSelectedEntitiesParent();
    void clearSelectedEntitiesParent();
    void renameSelectedEntity();
    void changeSelectedActive(bool active);
    [[nodiscard]] QString uniqueEntityName(const QString& stem = {}) const;
    [[nodiscard]] int selectedEntityRow() const;
    [[nodiscard]] std::optional<fabgl::EntityGuid> selectedEntityId() const;
    [[nodiscard]] std::vector<fabgl::EntityGuid> selectedEntityIds() const;
    void selectEntityRow(int row);
    void selectEntityGuid(const QString& guid);
    void selectEntityGuids(const QStringList& guids);
    void updateInspector(const QModelIndex& index);
    void pushEntityEdit(const EntitySnapshot& before, const EntitySnapshot& after,
                        const QString& description);

    void play();
    void pause();
    void step();
    void stop();
    void tickPlayMode();
    void advancePlayScene(float deltaTime);
    void renderCurrentScene();
    void setRunState(RunState state);
    void updateRunActions();
    void updateProfiler();
    void recordSerialProfilerMetrics(const QString& text);

    void configureBuildCommand();
    void runBuild();
    void exportEsp32();
    void playPc();
    void stopPc();
    void nativeGameplaySourceChanged(const QString& filePath, bool localChangesKept);
    [[nodiscard]] PreviewKind activePreviewKind() const noexcept;
    void performPreviewRestartAction(PreviewRestartAction action);
    [[nodiscard]] bool startNativePreviewBuild();
    void finishNativePreviewBuild(bool succeeded, const QString& detail);
    [[nodiscard]] QString nativePreviewBuildConfiguration() const;
    void refreshSerialPorts();
    void uploadEsp32();
    void deployEsp32Diagnostics();
    void runHardwareDiagnostic(const QString& diagnosticCheck);
    void startSerialMonitor();
    void stopSerialMonitor();
    void cancelWorkflow();
    void updateTargetConfiguration();
    void updateWorkflowActions();
    void startWorkflow(const ProcessCommand& command, WorkflowState state);
    void workflowFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void portDetectionFinished(int exitCode, QProcess::ExitStatus exitStatus);
    [[nodiscard]] bool prepareProjectForExternalWorkflow();
    [[nodiscard]] QString studioRepositoryRoot() const;
    [[nodiscard]] QString findBuiltTool(const QString& executableName,
                                        const QString& buildSubdirectory) const;
    [[nodiscard]] QString selectedSerialPort() const;
    [[nodiscard]] bool selectedPortIsCandidate() const;
    [[nodiscard]] QString createEsp32ExportPath();
    void appendBuildOutput(const QString& text, bool standardError);
    void updateBuildActions(bool running);
    void navigateDiagnostic(const QString& filePath, int line);

    void applyTheme(Theme theme);
    void restoreSettings();
    void saveSettings();
    void addRecentProject(const QString& filePath);
    void rebuildRecentProjectsMenu();
    void resetLayout();
    void applyLayoutPreset(const QString& name);
    void rebuildCustomLayoutsMenu();
    void saveNamedLayoutDialog();
    void loadNamedLayoutDialog();
    void deleteNamedLayoutDialog();
    void updateProjectTrustUi();
    void showRecoveryDialog();
    void appendConsoleMessage(const QString& message);

    SceneDocument m_sceneDocument;
    EntityModel m_entities;
    QUndoStack m_undoStack;
    BuildRunner m_buildRunner;
    BuildRunner m_pcRunner;
    BuildRunner m_serialRunner;
    BuildRunner m_portDetector;
    fabgl::Profiler m_engineProfiler;
    StudioLaunchOptions m_launchOptions;
    RecoveryManager m_recoveryManager;
    ProjectTrustStore m_projectTrustStore;

    QListView* m_hierarchyView = nullptr;
    SceneView* m_sceneView = nullptr;
    GameView* m_gameView = nullptr;
    QTreeView* m_projectTree = nullptr;
    QTreeView* m_assetTree = nullptr;
    QFileSystemModel* m_projectModel = nullptr;
    AssetBrowserController* m_assetBrowserController = nullptr;
    ExtensionServicePanel* m_extensionServicePanel = nullptr;
    QLineEdit* m_entityNameEdit = nullptr;
    QCheckBox* m_entityActiveCheck = nullptr;
    QLabel* m_entityIdLabel = nullptr;
    ComponentInspector* m_componentInspector = nullptr;
    QPlainTextEdit* m_console = nullptr;
    DiagnosticOutputEdit* m_buildOutput = nullptr;
    CodeEditorWidget* m_codeEditor = nullptr;
    SerialConsoleWidget* m_serialConsole = nullptr;
    VisualScriptEditorWidget* m_visualScriptEditor = nullptr;
    AnimatorEditorWidget* m_animatorEditor = nullptr;
    MemoryAnalyzerWidget* m_memoryAnalyzer = nullptr;
    MaterialEditorWidget* m_materialEditor = nullptr;
    ParticleEditorWidget* m_particleEditor = nullptr;
    TilemapEditorWidget* m_tilemapEditor = nullptr;
    RaycastMapEditorWidget* m_raycastMapEditor = nullptr;
    TrackEditorWidget* m_trackEditor = nullptr;
    UIEditorWidget* m_uiEditor = nullptr;
    PackageManagerWidget* m_packageManager = nullptr;
    PrefabEditorPanel* m_prefabEditor = nullptr;
    InputMapEditorWidget* m_inputMapEditor = nullptr;
    ToolchainSetupWidget* m_toolchainSetup = nullptr;
    AudioMixerEditorWidget* m_audioMixerEditor = nullptr;
    ProfilerTimelineWidget* m_profilerTimeline = nullptr;
    QTableWidget* m_profiler = nullptr;
    QLabel* m_centralProjectLabel = nullptr;
    QTimer* m_playTimer = nullptr;
    QComboBox* m_targetCombo = nullptr;
    QComboBox* m_configurationCombo = nullptr;
    QComboBox* m_toolbarTargetCombo = nullptr;
    QComboBox* m_toolbarConfigurationCombo = nullptr;
    QComboBox* m_serialPortCombo = nullptr;
    QComboBox* m_baudCombo = nullptr;
    QCheckBox* m_uploadConfirmation = nullptr;
    QComboBox* m_gameResolutionCombo = nullptr;
    QComboBox* m_gameAspectCombo = nullptr;
    QComboBox* m_gamePaletteCombo = nullptr;
    QComboBox* m_gameFpsCombo = nullptr;
    QComboBox* m_gameSimulationSpeedCombo = nullptr;
    QCheckBox* m_gameIntegerScaling = nullptr;
    QCheckBox* m_gamePixelPerfect = nullptr;
    QCheckBox* m_gameShowFps = nullptr;
    QCheckBox* m_gameEsp32Simulation = nullptr;
    QComboBox* m_layoutPresetCombo = nullptr;
    QComboBox* m_sceneViewModeCombo = nullptr;
    QProgressBar* m_workflowProgress = nullptr;
    QLabel* m_workflowStatus = nullptr;
    QLabel* m_projectTrustStatus = nullptr;
    QLabel* m_projectTargetProfileStatus = nullptr;
    QLabel* m_securityModeStatus = nullptr;

    QMenu* m_recentProjectsMenu = nullptr;
    QMenu* m_panelsMenu = nullptr;
    QMenu* m_customLayoutsMenu = nullptr;
    QActionGroup* m_themeActions = nullptr;
    QActionGroup* m_sceneToolActions = nullptr;
    QActionGroup* m_transformSpaceActions = nullptr;
    QAction* m_newAction = nullptr;
    QAction* m_openAction = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_saveAsAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_addEntityAction = nullptr;
    QAction* m_duplicateEntityAction = nullptr;
    QAction* m_deleteEntityAction = nullptr;
    QAction* m_setParentAction = nullptr;
    QAction* m_clearParentAction = nullptr;
    QAction* m_snapAction = nullptr;
    QAction* m_selectToolAction = nullptr;
    QAction* m_moveToolAction = nullptr;
    QAction* m_rotateToolAction = nullptr;
    QAction* m_scaleToolAction = nullptr;
    QAction* m_localTransformAction = nullptr;
    QAction* m_worldTransformAction = nullptr;
    QAction* m_frameSelectedAction = nullptr;
    QAction* m_zoomInAction = nullptr;
    QAction* m_zoomOutAction = nullptr;
    QAction* m_playAction = nullptr;
    QAction* m_pauseAction = nullptr;
    QAction* m_stepAction = nullptr;
    QAction* m_stopAction = nullptr;
    QAction* m_buildAction = nullptr;
    QAction* m_cancelBuildAction = nullptr;
    QAction* m_buildSettingsAction = nullptr;
    QAction* m_pcPlayAction = nullptr;
    QAction* m_pcStopAction = nullptr;
    QAction* m_exportEsp32Action = nullptr;
    QAction* m_refreshPortsAction = nullptr;
    QAction* m_uploadEsp32Action = nullptr;
    QAction* m_deployEsp32Action = nullptr;
    QAction* m_serialMonitorAction = nullptr;
    QAction* m_stopSerialMonitorAction = nullptr;
    QVector<QAction*> m_hardwareDiagnosticActions;
    QAction* m_darkThemeAction = nullptr;
    QAction* m_lightThemeAction = nullptr;
    QAction* m_refreshAssetsAction = nullptr;
    QAction* m_assetImportSettingsAction = nullptr;
    QAction* m_renameAssetAction = nullptr;
    QAction* m_moveAssetAction = nullptr;
    QAction* m_showPrefabEditorAction = nullptr;
    QAction* m_showInputMapAction = nullptr;
    QAction* m_showToolchainSetupAction = nullptr;
    QAction* m_renameEntityAction = nullptr;
    QAction* m_focusInspectorAction = nullptr;
    QAction* m_validateVisualScriptAction = nullptr;
    QAction* m_validateAnimatorAction = nullptr;
    QAction* m_refreshMemoryAction = nullptr;
    QAction* m_defaultLayoutAction = nullptr;
    QAction* m_layout2DAction = nullptr;
    QAction* m_layout3DAction = nullptr;
    QAction* m_scriptingLayoutAction = nullptr;
    QAction* m_animationLayoutAction = nullptr;
    QAction* m_profilingLayoutAction = nullptr;
    QAction* m_debugLayoutAction = nullptr;
    QAction* m_saveNamedLayoutAction = nullptr;
    QAction* m_loadNamedLayoutAction = nullptr;
    QAction* m_deleteNamedLayoutAction = nullptr;
    QAction* m_trustProjectAction = nullptr;
    QAction* m_manageRecoveryAction = nullptr;

    QVector<QDockWidget*> m_docks;
#if defined(_WIN32)
    std::unique_ptr<fabgl::player::Win32AudioOutput> m_playAudioOutput;
#endif
    std::unique_ptr<StudioPlaySession> m_playSession;
    QByteArray m_defaultLayout;
    QPalette m_defaultPalette;
    QString m_defaultStyleSheet;
    QString m_projectFilePath;
    QString m_projectName = QStringLiteral("Untitled");
    ProjectData m_projectData;
    std::unique_ptr<fabgl::project::ProjectAssetLibrary> m_projectAssetLibrary;
    std::unique_ptr<fabgl::project::ProjectExtensionModules> m_projectExtensions;
    std::unique_ptr<fabgl::project::ProjectExtensionServiceHost> m_projectExtensionServices;
    QHash<QString, QDockWidget*> m_extensionCustomWindows;
    std::string m_extensionProjectManifestPath;
    std::string m_extensionProjectRoot;
    QStringList m_recentProjects;
    QString m_activeLayoutName = QStringLiteral("Default");
    QElapsedTimer m_frameClock;
    RunState m_runState = RunState::Editing;
    Theme m_theme = Theme::Dark;
    FrameRenderStats m_lastFrameStats{};
    fabgl::FrameMetrics m_lastRuntimeMetrics{};
    double m_simulationElapsed = 0.0;
    WorkflowState m_workflowState = WorkflowState::Idle;
    QString m_pendingEsp32Sketch;
    QString m_lastPcScriptModule;
    QString m_lastEsp32BuildResult;
    QString m_activeDiagnosticCheck;
    QString m_serialMetricBuffer;
    std::optional<double> m_lastMeasuredEsp32FrameMilliseconds;
    std::optional<std::uint64_t> m_lastMeasuredEsp32HeapFreeBytes;
    std::optional<double> m_lastMeasuredEsp32HeapFragmentationPercent;
    std::optional<std::uint64_t> m_lastMeasuredEsp32SdReadBytes;
    PreviewRestartController m_previewRestartController;
    QString m_previewRestartConfiguration;
    QString m_previewRestartSource;
    QString m_activeWorkflowTarget;
    QString m_activeWorkflowConfiguration;
    bool m_workflowCancelled = false;
    bool m_forceModified = false;
    bool m_updatingInspector = false;
    bool m_sessionStarted = false;
    bool m_closing = false;
    QTimer* m_autosaveTimer = nullptr;
};

} // namespace fgl::studio
