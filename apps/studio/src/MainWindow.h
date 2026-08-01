#pragma once

#include "BuildRunner.h"
#include "EditorViews.h"
#include "EntityModel.h"
#include "ProjectDocument.h"
#include "SceneDocument.h"

#include <fabgl/profiling/profiler.h>

#include <QByteArray>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QPalette>
#include <QStringList>
#include <QUndoStack>
#include <QVector>

#include <array>
#include <memory>
#include <optional>

class QAction;
class QActionGroup;
class QCheckBox;
class QCloseEvent;
class QDockWidget;
class QDoubleSpinBox;
class QFileSystemModel;
class QLabel;
class QLineEdit;
class QListView;
class QMenu;
class QModelIndex;
class QPlainTextEdit;
class QTableWidget;
class QTimer;
class QTreeView;

namespace fabgl {
class Scene;
}

namespace fgl::studio {

class CodeEditorWidget;
class DiagnosticOutputEdit;
class MainWindow final : public QMainWindow {
    Q_OBJECT

  public:
    explicit MainWindow(QWidget* parent = nullptr);
    bool openProjectPath(const QString& filePath);

  protected:
    void closeEvent(QCloseEvent* event) override;

  private:
    enum class RunState { Editing, Playing, Paused };
    enum class Theme { Dark, Light };

    static constexpr int LayoutVersion = 2;
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
    void applyLoadedProject(const QString& filePath, ProjectData data);
    void setDocumentModified(bool forceModified);
    void refreshModifiedState();
    void updateWindowTitle();
    void updateProjectPanel();
    [[nodiscard]] QString projectRoot() const;

    void addEntity();
    void addEntityFromAsset(const QString& filePath, float x, float y);
    void deleteSelectedEntity();
    void renameSelectedEntity();
    void changeSelectedActive(bool active);
    void commitInspectorTransform();
    [[nodiscard]] QString uniqueEntityName(const QString& stem = {}) const;
    [[nodiscard]] int selectedEntityRow() const;
    [[nodiscard]] std::optional<fabgl::EntityGuid> selectedEntityId() const;
    void selectEntityRow(int row);
    void selectEntityGuid(const QString& guid);
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

    void configureBuildCommand();
    void runBuild();
    void appendBuildOutput(const QString& text, bool standardError);
    void updateBuildActions(bool running);
    void navigateDiagnostic(const QString& filePath, int line);

    void applyTheme(Theme theme);
    void restoreSettings();
    void saveSettings();
    void addRecentProject(const QString& filePath);
    void rebuildRecentProjectsMenu();
    void resetLayout();
    void appendConsoleMessage(const QString& message);

    SceneDocument m_sceneDocument;
    EntityModel m_entities;
    QUndoStack m_undoStack;
    BuildRunner m_buildRunner;
    fabgl::Profiler m_engineProfiler;

    QListView* m_hierarchyView = nullptr;
    SceneView* m_sceneView = nullptr;
    GameView* m_gameView = nullptr;
    QTreeView* m_projectTree = nullptr;
    QFileSystemModel* m_projectModel = nullptr;
    QLineEdit* m_entityNameEdit = nullptr;
    QCheckBox* m_entityActiveCheck = nullptr;
    QLabel* m_entityIdLabel = nullptr;
    std::array<QDoubleSpinBox*, 3> m_positionEdits{};
    std::array<QDoubleSpinBox*, 3> m_rotationEdits{};
    std::array<QDoubleSpinBox*, 3> m_scaleEdits{};
    QPlainTextEdit* m_console = nullptr;
    DiagnosticOutputEdit* m_buildOutput = nullptr;
    CodeEditorWidget* m_codeEditor = nullptr;
    QTableWidget* m_profiler = nullptr;
    QLabel* m_centralProjectLabel = nullptr;
    QTimer* m_playTimer = nullptr;

    QMenu* m_recentProjectsMenu = nullptr;
    QMenu* m_panelsMenu = nullptr;
    QActionGroup* m_themeActions = nullptr;
    QAction* m_newAction = nullptr;
    QAction* m_openAction = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_saveAsAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_addEntityAction = nullptr;
    QAction* m_deleteEntityAction = nullptr;
    QAction* m_snapAction = nullptr;
    QAction* m_playAction = nullptr;
    QAction* m_pauseAction = nullptr;
    QAction* m_stepAction = nullptr;
    QAction* m_stopAction = nullptr;
    QAction* m_buildAction = nullptr;
    QAction* m_cancelBuildAction = nullptr;
    QAction* m_buildSettingsAction = nullptr;
    QAction* m_darkThemeAction = nullptr;
    QAction* m_lightThemeAction = nullptr;

    QVector<QDockWidget*> m_docks;
    std::unique_ptr<fabgl::Scene> m_playScene;
    QByteArray m_defaultLayout;
    QPalette m_defaultPalette;
    QString m_defaultStyleSheet;
    QString m_projectFilePath;
    QString m_projectName = QStringLiteral("Untitled");
    ProjectData m_projectData;
    QStringList m_recentProjects;
    QElapsedTimer m_frameClock;
    RunState m_runState = RunState::Editing;
    Theme m_theme = Theme::Dark;
    FrameRenderStats m_lastFrameStats{};
    double m_simulationElapsed = 0.0;
    bool m_forceModified = false;
    bool m_updatingInspector = false;
};

} // namespace fgl::studio
