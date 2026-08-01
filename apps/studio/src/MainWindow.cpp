#include "MainWindow.h"

#include "CodeEditor.h"
#include "EntityCommands.h"

#include <fabgl/scene/entity.h>

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QSettings>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTime>
#include <QTimer>
#include <QToolBar>
#include <QTreeView>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

namespace fgl::studio {
namespace {

constexpr auto ProjectExtension = ".fglproject";

QString ensureProjectExtension(QString path) {
    if (!path.endsWith(QString::fromLatin1(ProjectExtension), Qt::CaseInsensitive)) {
        path += QString::fromLatin1(ProjectExtension);
    }
    return path;
}

QString projectDialogFilter() {
    return QObject::tr("FabGL Studio Projects (*.fglproject)");
}

QString quotedForDisplay(QString argument) {
    if (argument.isEmpty()) {
        return QStringLiteral("\"\"");
    }
    if (!argument.contains(QLatin1Char(' ')) && !argument.contains(QLatin1Char('\t')) &&
        !argument.contains(QLatin1Char('"'))) {
        return argument;
    }
    argument.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QStringLiteral("\"") + argument + QStringLiteral("\"");
}

QString commandForDisplay(const QString& program, const QStringList& arguments) {
    QStringList pieces;
    pieces.reserve(arguments.size() + 1);
    pieces.push_back(quotedForDisplay(program));
    for (const auto& argument : arguments) {
        pieces.push_back(quotedForDisplay(argument));
    }
    return pieces.join(QLatin1Char(' '));
}

QString argumentsForDisplay(const QStringList& arguments) {
    QStringList pieces;
    pieces.reserve(arguments.size());
    for (const auto& argument : arguments) {
        pieces.push_back(quotedForDisplay(argument));
    }
    return pieces.join(QLatin1Char(' '));
}

bool snapshotsEqual(const EntitySnapshot& left, const EntitySnapshot& right) {
    return left.id == right.id && left.name == right.name && left.active == right.active &&
           left.position == right.position && left.rotation == right.rotation &&
           left.scale == right.scale && left.parent == right.parent;
}

QString engineError(const fabgl::Error& error) {
    return QString::fromStdString(error.message());
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), m_sceneDocument(this), m_entities(this), m_undoStack(this),
      m_buildRunner(this), m_engineProfiler(), m_defaultPalette(qApp->palette()),
      m_defaultStyleSheet(qApp->styleSheet()) {
    setObjectName(QStringLiteral("FabGLStudioMainWindow"));
    setWindowTitle(tr("Untitled[*] — FabGL Studio"));
    setWindowModified(false);
    resize(1540, 940);
    setMinimumSize(1024, 680);
    setDockNestingEnabled(true);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks |
                   QMainWindow::AllowTabbedDocks);
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);

    m_entities.setDocument(&m_sceneDocument);
    m_sceneDocument.createDefault();
    (void)m_engineProfiler.setBudget("pc.frame", 16.67, fabgl::ProfilerUnit::Milliseconds);
    (void)m_engineProfiler.setBudget("esp32.frame.estimated", 16.67,
                                     fabgl::ProfilerUnit::Milliseconds);
    (void)m_engineProfiler.setBudget("esp32.draw_calls", 64.0, fabgl::ProfilerUnit::Count);

    createActions();
    createDockPanels();
    createMenusAndToolbars();
    m_playTimer = new QTimer(this);
    m_playTimer->setTimerType(Qt::PreciseTimer);
    m_playTimer->setInterval(16);
    connectEditorSignals();

    m_defaultLayout = saveState(LayoutVersion);
    restoreSettings();
    m_undoStack.clear();
    m_undoStack.setClean();
    m_sceneDocument.setModified(false);
    updateWindowTitle();
    updateProjectPanel();
    selectEntityRow(m_entities.rowCount() > 0 ? 0 : -1);
    updateRunActions();
    updateBuildActions(false);
    renderCurrentScene();
    appendConsoleMessage(tr("FabGL Studio is ready with a live engine scene."));
    statusBar()->showMessage(tr("Ready"), 3000);
}

void MainWindow::createActions() {
    const auto icon = [this](const QStyle::StandardPixmap standardPixmap) {
        return style()->standardIcon(standardPixmap);
    };

    m_newAction = new QAction(icon(QStyle::SP_FileIcon), tr("&New Project..."), this);
    m_newAction->setShortcut(QKeySequence::New);
    connect(m_newAction, &QAction::triggered, this, &MainWindow::newProject);

    m_openAction = new QAction(icon(QStyle::SP_DialogOpenButton), tr("&Open Project..."), this);
    m_openAction->setShortcut(QKeySequence::Open);
    connect(m_openAction, &QAction::triggered, this, &MainWindow::openProject);

    m_saveAction = new QAction(icon(QStyle::SP_DialogSaveButton), tr("&Save Project"), this);
    m_saveAction->setShortcut(QKeySequence::Save);
    connect(m_saveAction, &QAction::triggered, this, [this]() { (void)saveProject(); });
    m_saveAsAction = new QAction(tr("Save Project &As..."), this);
    m_saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(m_saveAsAction, &QAction::triggered, this, [this]() { (void)saveProjectAs(); });

    m_undoAction = m_undoStack.createUndoAction(this, tr("&Undo"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_redoAction = m_undoStack.createRedoAction(this, tr("&Redo"));
    m_redoAction->setShortcut(QKeySequence::Redo);

    m_addEntityAction = new QAction(tr("&Add Entity"), this);
    m_addEntityAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
    connect(m_addEntityAction, &QAction::triggered, this, &MainWindow::addEntity);
    m_deleteEntityAction = new QAction(tr("&Delete Entity"), this);
    m_deleteEntityAction->setShortcut(QKeySequence(Qt::Key_Delete));
    connect(m_deleteEntityAction, &QAction::triggered, this, &MainWindow::deleteSelectedEntity);
    m_snapAction = new QAction(tr("Snap to &Grid"), this);
    m_snapAction->setCheckable(true);
    m_snapAction->setChecked(true);

    m_playAction = new QAction(icon(QStyle::SP_MediaPlay), tr("&Play"), this);
    m_playAction->setShortcut(QKeySequence(Qt::Key_F6));
    connect(m_playAction, &QAction::triggered, this, &MainWindow::play);
    m_pauseAction = new QAction(icon(QStyle::SP_MediaPause), tr("P&ause"), this);
    m_pauseAction->setShortcut(QKeySequence(Qt::Key_F7));
    connect(m_pauseAction, &QAction::triggered, this, &MainWindow::pause);
    m_stepAction = new QAction(icon(QStyle::SP_MediaSkipForward), tr("&Step"), this);
    m_stepAction->setShortcut(QKeySequence(Qt::Key_F8));
    connect(m_stepAction, &QAction::triggered, this, &MainWindow::step);
    m_stopAction = new QAction(icon(QStyle::SP_MediaStop), tr("S&top"), this);
    m_stopAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F6));
    connect(m_stopAction, &QAction::triggered, this, &MainWindow::stop);

    m_buildAction = new QAction(tr("&Build Project"), this);
    m_buildAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    connect(m_buildAction, &QAction::triggered, this, &MainWindow::runBuild);
    m_cancelBuildAction = new QAction(tr("&Cancel Build"), this);
    connect(m_cancelBuildAction, &QAction::triggered, &m_buildRunner, &BuildRunner::stopBuild);
    m_buildSettingsAction = new QAction(tr("Build &Command..."), this);
    connect(m_buildSettingsAction, &QAction::triggered, this, &MainWindow::configureBuildCommand);

    m_themeActions = new QActionGroup(this);
    m_themeActions->setExclusive(true);
    m_darkThemeAction = new QAction(tr("&Dark Theme"), m_themeActions);
    m_darkThemeAction->setCheckable(true);
    m_lightThemeAction = new QAction(tr("&Light Theme"), m_themeActions);
    m_lightThemeAction->setCheckable(true);
    connect(m_darkThemeAction, &QAction::triggered, this, [this]() { applyTheme(Theme::Dark); });
    connect(m_lightThemeAction, &QAction::triggered, this, [this]() { applyTheme(Theme::Light); });
}

QDockWidget* MainWindow::createDock(const QString& title, const QString& objectName,
                                    QWidget* contents, const Qt::DockWidgetArea area) {
    auto* dock = new QDockWidget(title, this);
    dock->setObjectName(objectName);
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);
    dock->setWidget(contents);
    addDockWidget(area, dock);
    m_docks.push_back(dock);
    return dock;
}

void MainWindow::createDockPanels() {
    auto* central = new QFrame(this);
    central->setFrameShape(QFrame::StyledPanel);
    auto* centralLayout = new QVBoxLayout(central);
    auto* welcome = new QLabel(tr("FabGL Studio"), central);
    auto welcomeFont = welcome->font();
    welcomeFont.setPointSize(welcomeFont.pointSize() + 8);
    welcomeFont.setBold(true);
    welcome->setFont(welcomeFont);
    welcome->setAlignment(Qt::AlignCenter);
    m_centralProjectLabel = new QLabel(tr("No project is open"), central);
    m_centralProjectLabel->setAlignment(Qt::AlignCenter);
    m_centralProjectLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    centralLayout->addStretch();
    centralLayout->addWidget(welcome);
    centralLayout->addWidget(m_centralProjectLabel);
    centralLayout->addStretch();
    setCentralWidget(central);

    m_hierarchyView = new QListView(this);
    m_hierarchyView->setModel(&m_entities);
    m_hierarchyView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_hierarchyView->setUniformItemSizes(true);
    const auto hierarchyDock = createDock(tr("Hierarchy"), QStringLiteral("hierarchyDock"),
                                          m_hierarchyView, Qt::LeftDockWidgetArea);

    m_projectModel = new QFileSystemModel(this);
    m_projectModel->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_projectTree = new QTreeView(this);
    m_projectTree->setModel(m_projectModel);
    m_projectTree->setDragEnabled(true);
    m_projectTree->setSelectionMode(QAbstractItemView::SingleSelection);
    for (int column = 1; column < 4; ++column) {
        m_projectTree->hideColumn(column);
    }
    const auto projectDock = createDock(tr("Assets / Project"), QStringLiteral("projectDock"),
                                        m_projectTree, Qt::LeftDockWidgetArea);
    splitDockWidget(hierarchyDock, projectDock, Qt::Vertical);

    m_sceneView = new SceneView(this);
    m_sceneView->setDocument(&m_sceneDocument);
    const auto sceneDock =
        createDock(tr("Scene"), QStringLiteral("sceneDock"), m_sceneView, Qt::RightDockWidgetArea);

    m_gameView = new GameView(this);
    const auto gameDock =
        createDock(tr("Game"), QStringLiteral("gameDock"), m_gameView, Qt::RightDockWidgetArea);
    tabifyDockWidget(sceneDock, gameDock);
    sceneDock->raise();

    auto* inspector = new QWidget(this);
    auto* inspectorLayout = new QFormLayout(inspector);
    m_entityNameEdit = new QLineEdit(inspector);
    m_entityActiveCheck = new QCheckBox(tr("Enabled"), inspector);
    m_entityIdLabel = new QLabel(tr("No selection"), inspector);
    m_entityIdLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_entityIdLabel->setWordWrap(true);
    inspectorLayout->addRow(tr("Name"), m_entityNameEdit);
    inspectorLayout->addRow(tr("Active"), m_entityActiveCheck);
    inspectorLayout->addRow(tr("Entity GUID"), m_entityIdLabel);

    const auto addVectorEditor =
        [inspector, inspectorLayout](const QString& label, std::array<QDoubleSpinBox*, 3>& edits,
                                     const double minimum, const double maximum,
                                     const QString& suffix) {
            auto* container = new QWidget(inspector);
            auto* layout = new QHBoxLayout(container);
            layout->setContentsMargins(0, 0, 0, 0);
            constexpr std::array<const char*, 3> Axes = {"X", "Y", "Z"};
            for (std::size_t index = 0; index < edits.size(); ++index) {
                auto* spin = new QDoubleSpinBox(container);
                spin->setRange(minimum, maximum);
                spin->setDecimals(3);
                spin->setSingleStep(0.1);
                spin->setKeyboardTracking(false);
                spin->setPrefix(QString::fromLatin1(Axes.at(index)) + QStringLiteral(": "));
                spin->setSuffix(suffix);
                edits.at(index) = spin;
                layout->addWidget(spin);
            }
            inspectorLayout->addRow(label, container);
        };
    addVectorEditor(tr("Position"), m_positionEdits, -10000.0, 10000.0, {});
    addVectorEditor(tr("Rotation"), m_rotationEdits, -36000.0, 36000.0, tr("°"));
    addVectorEditor(tr("Scale"), m_scaleEdits, -1000.0, 1000.0, {});

    const auto inspectorDock = createDock(tr("Inspector"), QStringLiteral("inspectorDock"),
                                          inspector, Qt::RightDockWidgetArea);
    splitDockWidget(sceneDock, inspectorDock, Qt::Horizontal);

    m_profiler = new QTableWidget(6, 4, this);
    m_profiler->setHorizontalHeaderLabels({tr("Metric"), tr("Source"), tr("Value"), tr("Budget")});
    m_profiler->verticalHeader()->setVisible(false);
    m_profiler->horizontalHeader()->setStretchLastSection(true);
    m_profiler->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_profiler->setSelectionMode(QAbstractItemView::NoSelection);
    const auto profilerDock = createDock(tr("Profiler"), QStringLiteral("profilerDock"), m_profiler,
                                         Qt::RightDockWidgetArea);
    splitDockWidget(inspectorDock, profilerDock, Qt::Vertical);

    m_codeEditor = new CodeEditorWidget(this);
    const auto codeDock = createDock(tr("Code Editor"), QStringLiteral("codeEditorDock"),
                                     m_codeEditor, Qt::BottomDockWidgetArea);
    m_console = new QPlainTextEdit(this);
    m_console->setReadOnly(true);
    m_console->setMaximumBlockCount(5000);
    const auto consoleDock = createDock(tr("Console"), QStringLiteral("consoleDock"), m_console,
                                        Qt::BottomDockWidgetArea);
    m_buildOutput = new DiagnosticOutputEdit(this);
    m_buildOutput->setMaximumBlockCount(10000);
    const auto buildDock = createDock(tr("Build Output"), QStringLiteral("buildOutputDock"),
                                      m_buildOutput, Qt::BottomDockWidgetArea);
    tabifyDockWidget(codeDock, consoleDock);
    tabifyDockWidget(consoleDock, buildDock);
    consoleDock->raise();
}

void MainWindow::createMenusAndToolbars() {
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(m_newAction);
    fileMenu->addAction(m_openAction);
    m_recentProjectsMenu = fileMenu->addMenu(tr("Open &Recent"));
    fileMenu->addSeparator();
    fileMenu->addAction(m_saveAction);
    fileMenu->addAction(m_saveAsAction);
    fileMenu->addSeparator();
    auto* exitAction = fileMenu->addAction(tr("E&xit"));
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    auto* editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(m_undoAction);
    editMenu->addAction(m_redoAction);
    editMenu->addSeparator();
    editMenu->addAction(m_addEntityAction);
    editMenu->addAction(m_deleteEntityAction);
    editMenu->addAction(m_snapAction);

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    m_panelsMenu = viewMenu->addMenu(tr("&Panels"));
    for (auto* dock : std::as_const(m_docks)) {
        m_panelsMenu->addAction(dock->toggleViewAction());
    }
    auto* themeMenu = viewMenu->addMenu(tr("&Theme"));
    themeMenu->addAction(m_darkThemeAction);
    themeMenu->addAction(m_lightThemeAction);
    viewMenu->addSeparator();
    auto* resetLayoutAction = viewMenu->addAction(tr("Reset &Layout"));
    connect(resetLayoutAction, &QAction::triggered, this, &MainWindow::resetLayout);

    auto* playMenu = menuBar()->addMenu(tr("&Play"));
    playMenu->addAction(m_playAction);
    playMenu->addAction(m_pauseAction);
    playMenu->addAction(m_stepAction);
    playMenu->addAction(m_stopAction);

    auto* buildMenu = menuBar()->addMenu(tr("&Build"));
    buildMenu->addAction(m_buildAction);
    buildMenu->addAction(m_cancelBuildAction);
    buildMenu->addSeparator();
    buildMenu->addAction(m_buildSettingsAction);

    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    auto* aboutAction = helpMenu->addAction(tr("&About FabGL Studio"));
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, tr("About FabGL Studio"),
                           tr("FabGL Studio 0.1.0\n\nQt 6 authoring, native FabGL engine "
                              "scenes, portable framebuffer preview, and ESP32 budgeting."));
    });

    auto* fileToolBar = addToolBar(tr("Project"));
    fileToolBar->setObjectName(QStringLiteral("projectToolBar"));
    fileToolBar->addAction(m_newAction);
    fileToolBar->addAction(m_openAction);
    fileToolBar->addAction(m_saveAction);
    fileToolBar->addSeparator();
    fileToolBar->addAction(m_undoAction);
    fileToolBar->addAction(m_redoAction);
    fileToolBar->addSeparator();
    fileToolBar->addAction(m_addEntityAction);
    fileToolBar->addAction(m_deleteEntityAction);
    fileToolBar->addAction(m_snapAction);

    auto* playToolBar = addToolBar(tr("Simulation"));
    playToolBar->setObjectName(QStringLiteral("simulationToolBar"));
    playToolBar->addAction(m_playAction);
    playToolBar->addAction(m_pauseAction);
    playToolBar->addAction(m_stepAction);
    playToolBar->addAction(m_stopAction);
    playToolBar->addSeparator();
    playToolBar->addAction(m_buildAction);
    playToolBar->addAction(m_cancelBuildAction);
    rebuildRecentProjectsMenu();
}

void MainWindow::connectEditorSignals() {
    connect(m_hierarchyView->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current, const QModelIndex&) {
                updateInspector(current);
                const auto id =
                    current.isValid() ? m_entities.entityIdAt(current.row()) : std::nullopt;
                m_sceneView->setSelectedEntity(id ? SceneDocument::guidString(*id) : QString{});
            });
    connect(m_sceneView, &SceneView::entitySelected, this, &MainWindow::selectEntityGuid);
    connect(m_sceneView, &SceneView::entityMovePreview, this,
            [this](const QString& guid, const float x, const float y, const float z) {
                const auto id = SceneDocument::parseEntityGuid(guid);
                if (id && m_runState == RunState::Editing) {
                    m_sceneDocument.previewPosition(*id, {x, y, z});
                }
            });
    connect(m_sceneView, &SceneView::entityMoveCommitted, this,
            [this](const QString& guid, const float oldX, const float oldY, const float oldZ,
                   const float newX, const float newY, const float newZ) {
                const auto id = SceneDocument::parseEntityGuid(guid);
                const auto current = id ? m_sceneDocument.snapshot(*id) : std::nullopt;
                if (!current || m_runState != RunState::Editing) {
                    return;
                }
                auto before = *current;
                auto after = *current;
                before.position = {oldX, oldY, oldZ};
                after.position = {newX, newY, newZ};
                m_sceneDocument.previewPosition(*id, before.position);
                pushEntityEdit(before, after, tr("Move %1").arg(after.name));
            });
    connect(m_sceneView, &SceneView::assetDropped, this, &MainWindow::addEntityFromAsset);
    connect(m_snapAction, &QAction::toggled, m_sceneView, &SceneView::setSnapEnabled);

    connect(m_entityNameEdit, &QLineEdit::editingFinished, this, &MainWindow::renameSelectedEntity);
    connect(m_entityActiveCheck, &QCheckBox::toggled, this, &MainWindow::changeSelectedActive);
    for (auto* spin : m_positionEdits) {
        connect(spin, &QDoubleSpinBox::editingFinished, this,
                &MainWindow::commitInspectorTransform);
    }
    for (auto* spin : m_rotationEdits) {
        connect(spin, &QDoubleSpinBox::editingFinished, this,
                &MainWindow::commitInspectorTransform);
    }
    for (auto* spin : m_scaleEdits) {
        connect(spin, &QDoubleSpinBox::editingFinished, this,
                &MainWindow::commitInspectorTransform);
    }

    connect(&m_sceneDocument, &SceneDocument::entityChanged, this, [this](const QString& guid) {
        const auto selected = selectedEntityId();
        if (selected && SceneDocument::guidString(*selected) == guid) {
            updateInspector(m_entities.index(selectedEntityRow(), 0));
        }
        if (m_runState == RunState::Editing) {
            renderCurrentScene();
        }
    });
    connect(&m_sceneDocument, &SceneDocument::sceneReset, this, [this]() {
        selectEntityRow(m_entities.rowCount() > 0 ? 0 : -1);
        renderCurrentScene();
    });
    connect(&m_sceneDocument, &SceneDocument::modifiedChanged, this,
            [this]() { refreshModifiedState(); });
    connect(&m_entities, &QAbstractItemModel::rowsInserted, this,
            [this](const QModelIndex&, const int first, const int) {
                selectEntityRow(first);
                renderCurrentScene();
            });
    connect(&m_entities, &QAbstractItemModel::rowsRemoved, this,
            [this](const QModelIndex&, const int first, const int) {
                selectEntityRow(
                    m_entities.rowCount() == 0 ? -1 : std::min(first, m_entities.rowCount() - 1));
                renderCurrentScene();
            });
    connect(&m_entities, &QAbstractItemModel::modelReset, this, [this]() {
        selectEntityRow(m_entities.rowCount() > 0 ? 0 : -1);
        updateProfiler();
    });

    connect(&m_undoStack, &QUndoStack::cleanChanged, this, [this](const bool clean) {
        m_sceneDocument.setModified(!clean);
        refreshModifiedState();
    });
    connect(&m_undoStack, &QUndoStack::indexChanged, this, [this]() {
        updateRunActions();
        updateProfiler();
    });
    connect(&m_undoStack, &QUndoStack::canUndoChanged, this, [this]() { updateRunActions(); });
    connect(&m_undoStack, &QUndoStack::canRedoChanged, this, [this]() { updateRunActions(); });

    connect(m_projectTree, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
        const auto path = m_projectModel->filePath(index);
        const QFileInfo info(path);
        const QString suffix = info.suffix().toLower();
        if (info.isFile() &&
            QStringList{QStringLiteral("c"), QStringLiteral("cc"), QStringLiteral("cpp"),
                        QStringLiteral("cxx"), QStringLiteral("h"), QStringLiteral("hpp")}
                .contains(suffix)) {
            m_codeEditor->openFile(path);
            if (auto* dock = findChild<QDockWidget*>(QStringLiteral("codeEditorDock"))) {
                dock->show();
                dock->raise();
            }
        }
    });
    connect(m_codeEditor, &CodeEditorWidget::statusMessage, this, [this](const QString& message) {
        statusBar()->showMessage(message, 5000);
        appendConsoleMessage(message);
    });
    connect(m_buildOutput, &DiagnosticOutputEdit::diagnosticActivated, this,
            &MainWindow::navigateDiagnostic);

    connect(m_playTimer, &QTimer::timeout, this, &MainWindow::tickPlayMode);
    connect(&m_buildRunner, &BuildRunner::outputReady, this, &MainWindow::appendBuildOutput);
    connect(&m_buildRunner, &BuildRunner::buildStarted, this,
            [this](const QString& program, const QStringList& arguments,
                   const QString& workingDirectory) {
                appendBuildOutput(tr("\n> %1\nWorking directory: %2\n")
                                      .arg(commandForDisplay(program, arguments),
                                           QDir::toNativeSeparators(workingDirectory)),
                                  false);
                statusBar()->showMessage(tr("Build running..."));
            });
    connect(&m_buildRunner, &BuildRunner::buildFinished, this,
            [this](const int exitCode, const QProcess::ExitStatus exitStatus) {
                const bool failed = exitStatus == QProcess::CrashExit || exitCode != 0;
                appendBuildOutput(failed ? tr("\nBuild failed (exit code %1).\n").arg(exitCode)
                                         : tr("\nBuild finished successfully.\n"),
                                  failed);
                appendConsoleMessage(failed ? tr("Build failed with exit code %1.").arg(exitCode)
                                            : tr("Build completed successfully."));
                statusBar()->showMessage(failed ? tr("Build failed") : tr("Build succeeded"), 5000);
            });
    connect(&m_buildRunner, &BuildRunner::runningChanged, this, &MainWindow::updateBuildActions);
}

void MainWindow::newProject() {
    if (!maybeSave()) {
        return;
    }
    if (m_runState != RunState::Editing) {
        stop();
    }

    QSettings settings;
    const auto initialDirectory =
        settings.value(QStringLiteral("project/lastDirectory"), QDir::homePath()).toString();
    auto filePath = QFileDialog::getSaveFileName(
        this, tr("Create FabGL Studio Project"),
        QDir(initialDirectory).filePath(QStringLiteral("NewGame.fglproject")),
        projectDialogFilter());
    if (filePath.isEmpty()) {
        return;
    }
    filePath = ensureProjectExtension(filePath);

    ProjectData data;
    data.projectGuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    data.name = QFileInfo(filePath).completeBaseName();
    m_sceneDocument.createDefault();
    QString errorMessage;
    if (!m_sceneDocument.saveAs(ProjectDocument::absoluteScenePath(filePath, data), errorMessage) ||
        !ProjectDocument::save(filePath, data, errorMessage)) {
        QMessageBox::critical(this, tr("Project Creation Failed"), errorMessage);
        return;
    }
    applyLoadedProject(filePath, data);
    addRecentProject(filePath);
    settings.setValue(QStringLiteral("project/lastDirectory"), QFileInfo(filePath).absolutePath());
    appendConsoleMessage(tr("Created project and Scenes/Main.fglscene at %1.")
                             .arg(QDir::toNativeSeparators(filePath)));
}

void MainWindow::openProject() {
    QSettings settings;
    const auto initialDirectory =
        settings.value(QStringLiteral("project/lastDirectory"), QDir::homePath()).toString();
    const auto filePath = QFileDialog::getOpenFileName(this, tr("Open FabGL Studio Project"),
                                                       initialDirectory, projectDialogFilter());
    if (!filePath.isEmpty()) {
        (void)openProjectPath(filePath);
    }
}

bool MainWindow::openProjectPath(const QString& filePath) {
    if (!maybeSave()) {
        return false;
    }
    if (m_runState != RunState::Editing) {
        stop();
    }
    const auto absolutePath = QFileInfo(filePath).absoluteFilePath();
    ProjectData data;
    QString errorMessage;
    if (!ProjectDocument::load(absolutePath, data, errorMessage) ||
        !m_sceneDocument.load(ProjectDocument::absoluteScenePath(absolutePath, data),
                              errorMessage)) {
        QMessageBox::critical(this, tr("Open Project Failed"), errorMessage);
        return false;
    }
    applyLoadedProject(absolutePath, data);
    addRecentProject(absolutePath);
    QSettings settings;
    settings.setValue(QStringLiteral("project/lastDirectory"),
                      QFileInfo(absolutePath).absolutePath());
    appendConsoleMessage(
        tr("Opened engine scene %1.").arg(QDir::toNativeSeparators(m_sceneDocument.filePath())));
    return true;
}

bool MainWindow::saveProject() {
    return m_projectFilePath.isEmpty() ? saveProjectAs() : saveProjectTo(m_projectFilePath);
}

bool MainWindow::saveProjectAs() {
    QSettings settings;
    const auto initialPath =
        m_projectFilePath.isEmpty()
            ? settings.value(QStringLiteral("project/lastDirectory"), QDir::homePath()).toString()
            : m_projectFilePath;
    auto filePath = QFileDialog::getSaveFileName(this, tr("Save FabGL Studio Project As"),
                                                 initialPath, projectDialogFilter());
    if (filePath.isEmpty()) {
        return false;
    }
    return saveProjectTo(ensureProjectExtension(filePath));
}

bool MainWindow::saveProjectTo(const QString& filePath) {
    ProjectData data = m_projectData;
    data.name = m_projectName == QStringLiteral("Untitled") ? QFileInfo(filePath).completeBaseName()
                                                            : m_projectName;
    if (data.sceneFile.isEmpty()) {
        data.sceneFile = QStringLiteral("Scenes/Main.fglscene");
    }
    QString errorMessage;
    if (!m_sceneDocument.saveAs(ProjectDocument::absoluteScenePath(filePath, data), errorMessage) ||
        !ProjectDocument::save(filePath, data, errorMessage)) {
        QMessageBox::critical(this, tr("Save Project Failed"), errorMessage);
        return false;
    }

    m_projectFilePath = QFileInfo(filePath).absoluteFilePath();
    m_projectName = data.name;
    m_projectData = std::move(data);
    m_forceModified = false;
    m_undoStack.setClean();
    m_sceneDocument.setModified(false);
    refreshModifiedState();
    updateProjectPanel();
    addRecentProject(m_projectFilePath);
    QSettings settings;
    settings.setValue(QStringLiteral("project/lastDirectory"),
                      QFileInfo(m_projectFilePath).absolutePath());
    appendConsoleMessage(tr("Saved project and scene atomically to separate files."));
    statusBar()->showMessage(tr("Project saved"), 3000);
    return true;
}

bool MainWindow::maybeSave() {
    if (!isWindowModified()) {
        return true;
    }
    const auto answer = QMessageBox::warning(
        this, tr("Unsaved Project"),
        tr("The project '%1' has unsaved scene or project changes.").arg(m_projectName),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save) {
        return saveProject();
    }
    return answer == QMessageBox::Discard;
}

void MainWindow::applyLoadedProject(const QString& filePath, ProjectData data) {
    m_projectFilePath = QFileInfo(filePath).absoluteFilePath();
    m_projectName = data.name;
    m_projectData = std::move(data);
    m_undoStack.clear();
    m_undoStack.setClean();
    m_sceneDocument.setModified(false);
    m_forceModified = false;
    refreshModifiedState();
    updateProjectPanel();
    selectEntityRow(m_entities.rowCount() > 0 ? 0 : -1);
    renderCurrentScene();
}

void MainWindow::setDocumentModified(const bool forceModified) {
    m_forceModified = forceModified;
    refreshModifiedState();
}

void MainWindow::refreshModifiedState() {
    setWindowModified(m_forceModified || m_sceneDocument.isModified() || !m_undoStack.isClean());
    updateWindowTitle();
}

void MainWindow::updateWindowTitle() {
    setWindowTitle(tr("%1[*] — FabGL Studio").arg(m_projectName));
    if (m_centralProjectLabel != nullptr) {
        m_centralProjectLabel->setText(
            m_projectFilePath.isEmpty()
                ? tr("No project is open — editing an unsaved default scene")
                : tr("Project: %1\nScene: %2")
                      .arg(QDir::toNativeSeparators(m_projectFilePath),
                           QDir::toNativeSeparators(m_sceneDocument.filePath())));
    }
}

void MainWindow::updateProjectPanel() {
    if (m_projectTree == nullptr || m_projectModel == nullptr) {
        return;
    }
    const bool hasProject = !m_projectFilePath.isEmpty();
    m_projectTree->setEnabled(hasProject);
    if (!hasProject) {
        m_projectTree->setRootIndex({});
        updateWindowTitle();
        return;
    }
    const auto root = projectRoot();
    m_projectTree->setRootIndex(m_projectModel->setRootPath(root));
    m_projectTree->setToolTip(QDir::toNativeSeparators(root));
    updateWindowTitle();
}

QString MainWindow::projectRoot() const {
    return m_projectFilePath.isEmpty() ? QDir::currentPath()
                                       : ProjectDocument::absoluteProjectRoot(
                                             m_projectFilePath, m_projectData.relativeRoot);
}

QString MainWindow::uniqueEntityName(const QString& stem) const {
    const QString base = stem.trimmed().isEmpty() ? tr("Entity") : stem.trimmed();
    for (int number = 1;; ++number) {
        const auto candidate = QStringLiteral("%1 %2").arg(base).arg(number);
        const auto entities = m_sceneDocument.scene().entities();
        const bool duplicate =
            std::any_of(entities.cbegin(), entities.cend(), [&candidate](const auto* entity) {
                return QString::fromStdString(entity->name()) == candidate;
            });
        if (!duplicate) {
            return candidate;
        }
    }
}

void MainWindow::addEntity() {
    if (m_runState != RunState::Editing) {
        return;
    }
    EntitySnapshot entity;
    entity.id = fabgl::EntityGuid::generate();
    entity.name = uniqueEntityName();
    const auto id = entity.id;
    m_undoStack.push(new AddEntityCommand(&m_sceneDocument, entity));
    selectEntityRow(m_entities.rowForId(id));
    appendConsoleMessage(tr("Added %1 to the engine scene.").arg(entity.name));
}

void MainWindow::addEntityFromAsset(const QString& filePath, const float x, const float y) {
    if (m_runState != RunState::Editing) {
        return;
    }
    EntitySnapshot entity;
    entity.id = fabgl::EntityGuid::generate();
    entity.name = uniqueEntityName(QFileInfo(filePath).completeBaseName());
    entity.position = {x, y, 0.0F};
    const auto id = entity.id;
    m_undoStack.push(new AddEntityCommand(&m_sceneDocument, entity));
    selectEntityRow(m_entities.rowForId(id));
    appendConsoleMessage(tr("Created %1 from dropped asset %2.")
                             .arg(entity.name, QDir::toNativeSeparators(filePath)));
}

void MainWindow::deleteSelectedEntity() {
    if (m_runState != RunState::Editing) {
        return;
    }
    const auto id = selectedEntityId();
    const auto entity = id ? m_sceneDocument.snapshot(*id) : std::nullopt;
    if (!entity) {
        return;
    }
    m_undoStack.push(new DeleteEntityCommand(&m_sceneDocument, *id));
    appendConsoleMessage(tr("Deleted %1; Undo restores its GUID and transform.").arg(entity->name));
}

void MainWindow::renameSelectedEntity() {
    if (m_updatingInspector || m_runState != RunState::Editing) {
        return;
    }
    const auto id = selectedEntityId();
    const auto before = id ? m_sceneDocument.snapshot(*id) : std::nullopt;
    if (!before) {
        return;
    }
    const auto name = m_entityNameEdit->text().trimmed();
    if (name.isEmpty()) {
        m_entityNameEdit->setText(before->name);
        statusBar()->showMessage(tr("Entity names cannot be empty"), 3000);
        return;
    }
    auto after = *before;
    after.name = name;
    pushEntityEdit(*before, after, tr("Rename %1").arg(before->name));
}

void MainWindow::changeSelectedActive(const bool active) {
    if (m_updatingInspector || m_runState != RunState::Editing) {
        return;
    }
    const auto id = selectedEntityId();
    const auto before = id ? m_sceneDocument.snapshot(*id) : std::nullopt;
    if (!before) {
        return;
    }
    auto after = *before;
    after.active = active;
    pushEntityEdit(*before, after,
                   active ? tr("Activate %1").arg(before->name)
                          : tr("Deactivate %1").arg(before->name));
}

void MainWindow::commitInspectorTransform() {
    if (m_updatingInspector || m_runState != RunState::Editing) {
        return;
    }
    const auto id = selectedEntityId();
    const auto before = id ? m_sceneDocument.snapshot(*id) : std::nullopt;
    if (!before) {
        return;
    }
    auto after = *before;
    after.position = {static_cast<float>(m_positionEdits[0]->value()),
                      static_cast<float>(m_positionEdits[1]->value()),
                      static_cast<float>(m_positionEdits[2]->value())};
    constexpr float DegreesToRadians = 0.01745329251994329577F;
    after.rotation = {static_cast<float>(m_rotationEdits[0]->value()) * DegreesToRadians,
                      static_cast<float>(m_rotationEdits[1]->value()) * DegreesToRadians,
                      static_cast<float>(m_rotationEdits[2]->value()) * DegreesToRadians};
    after.scale = {static_cast<float>(m_scaleEdits[0]->value()),
                   static_cast<float>(m_scaleEdits[1]->value()),
                   static_cast<float>(m_scaleEdits[2]->value())};
    pushEntityEdit(*before, after, tr("Transform %1").arg(before->name));
}

int MainWindow::selectedEntityRow() const {
    return m_hierarchyView != nullptr && m_hierarchyView->currentIndex().isValid()
               ? m_hierarchyView->currentIndex().row()
               : -1;
}

std::optional<fabgl::EntityGuid> MainWindow::selectedEntityId() const {
    return m_entities.entityIdAt(selectedEntityRow());
}

void MainWindow::selectEntityRow(const int row) {
    const auto index =
        row >= 0 && row < m_entities.rowCount() ? m_entities.index(row, 0) : QModelIndex{};
    m_hierarchyView->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect |
                                                                  QItemSelectionModel::Rows);
    updateInspector(index);
    const auto id = index.isValid() ? m_entities.entityIdAt(index.row()) : std::nullopt;
    m_sceneView->setSelectedEntity(id ? SceneDocument::guidString(*id) : QString{});
}

void MainWindow::selectEntityGuid(const QString& guid) {
    const auto id = SceneDocument::parseEntityGuid(guid);
    selectEntityRow(id ? m_entities.rowForId(*id) : -1);
}

void MainWindow::updateInspector(const QModelIndex& index) {
    m_updatingInspector = true;
    const auto id = index.isValid() ? m_entities.entityIdAt(index.row()) : std::nullopt;
    const auto entity = id ? m_sceneDocument.snapshot(*id) : std::nullopt;
    const bool enabled = entity.has_value() && m_runState == RunState::Editing;
    m_entityNameEdit->setEnabled(enabled);
    m_entityActiveCheck->setEnabled(enabled);
    for (auto* spin : m_positionEdits) {
        spin->setEnabled(enabled);
    }
    for (auto* spin : m_rotationEdits) {
        spin->setEnabled(enabled);
    }
    for (auto* spin : m_scaleEdits) {
        spin->setEnabled(enabled);
    }
    m_deleteEntityAction->setEnabled(enabled);
    if (!entity) {
        m_entityNameEdit->clear();
        m_entityActiveCheck->setChecked(false);
        m_entityIdLabel->setText(tr("No selection"));
        for (auto* spin : m_positionEdits) {
            spin->setValue(0.0);
        }
        for (auto* spin : m_rotationEdits) {
            spin->setValue(0.0);
        }
        for (auto* spin : m_scaleEdits) {
            spin->setValue(1.0);
        }
        m_updatingInspector = false;
        return;
    }
    m_entityNameEdit->setText(entity->name);
    m_entityActiveCheck->setChecked(entity->active);
    m_entityIdLabel->setText(SceneDocument::guidString(entity->id));
    m_positionEdits[0]->setValue(entity->position.x);
    m_positionEdits[1]->setValue(entity->position.y);
    m_positionEdits[2]->setValue(entity->position.z);
    constexpr float RadiansToDegrees = 57.295779513082320876F;
    m_rotationEdits[0]->setValue(entity->rotation.x * RadiansToDegrees);
    m_rotationEdits[1]->setValue(entity->rotation.y * RadiansToDegrees);
    m_rotationEdits[2]->setValue(entity->rotation.z * RadiansToDegrees);
    m_scaleEdits[0]->setValue(entity->scale.x);
    m_scaleEdits[1]->setValue(entity->scale.y);
    m_scaleEdits[2]->setValue(entity->scale.z);
    m_updatingInspector = false;
}

void MainWindow::pushEntityEdit(const EntitySnapshot& before, const EntitySnapshot& after,
                                const QString& description) {
    if (snapshotsEqual(before, after)) {
        return;
    }
    m_undoStack.push(new EditEntityCommand(&m_sceneDocument, before, after, description));
}

void MainWindow::play() {
    if (m_runState == RunState::Paused) {
        setRunState(RunState::Playing);
        m_frameClock.restart();
        m_playTimer->start();
        appendConsoleMessage(tr("Play mode resumed."));
        return;
    }
    if (m_runState != RunState::Editing) {
        return;
    }
    QString errorMessage;
    m_playScene = m_sceneDocument.cloneScene(errorMessage);
    if (m_playScene == nullptr) {
        QMessageBox::critical(this, tr("Play Failed"), errorMessage);
        return;
    }
    const auto started = m_playScene->start();
    if (!started) {
        QMessageBox::critical(this, tr("Play Failed"), engineError(started.error()));
        m_playScene.reset();
        return;
    }
    m_simulationElapsed = 0.0;
    m_frameClock.start();
    setRunState(RunState::Playing);
    m_playTimer->start();
    renderCurrentScene();
    appendConsoleMessage(tr("Play started from a SceneSerializer clone; edit scene is isolated."));
}

void MainWindow::pause() {
    if (m_runState != RunState::Playing) {
        return;
    }
    m_playTimer->stop();
    setRunState(RunState::Paused);
    appendConsoleMessage(tr("Runtime scene paused."));
}

void MainWindow::step() {
    if (m_runState != RunState::Paused || m_playScene == nullptr) {
        return;
    }
    advancePlayScene(1.0F / 60.0F);
    renderCurrentScene();
    appendConsoleMessage(tr("Runtime scene advanced by one 1/60 s step."));
}

void MainWindow::stop() {
    if (m_runState == RunState::Editing) {
        return;
    }
    m_playTimer->stop();
    if (m_playScene != nullptr) {
        m_playScene->shutdown();
    }
    m_playScene.reset();
    setRunState(RunState::Editing);
    renderCurrentScene();
    appendConsoleMessage(tr("Play stopped; the authoring scene remained unchanged."));
}

void MainWindow::tickPlayMode() {
    if (m_runState != RunState::Playing || m_playScene == nullptr) {
        return;
    }
    const qint64 elapsedMs = m_frameClock.restart();
    const float deltaTime = std::clamp(static_cast<float>(elapsedMs) / 1000.0F, 0.0F, 0.1F);
    advancePlayScene(deltaTime);
    renderCurrentScene();
}

void MainWindow::advancePlayScene(const float deltaTime) {
    if (m_playScene == nullptr) {
        return;
    }
    m_simulationElapsed += static_cast<double>(deltaTime);
    const auto fixed = m_playScene->fixedUpdate(deltaTime);
    const auto updated = m_playScene->update(deltaTime);
    const auto late = m_playScene->lateUpdate(deltaTime);
    if (!fixed || !updated || !late) {
        const auto& error = !fixed ? fixed.error() : (!updated ? updated.error() : late.error());
        appendConsoleMessage(tr("Runtime update error: %1").arg(engineError(error)));
        pause();
    }
}

void MainWindow::renderCurrentScene() {
    if (m_gameView == nullptr) {
        return;
    }
    const auto& scene = m_playScene != nullptr ? *m_playScene : m_sceneDocument.scene();
    m_lastFrameStats = m_gameView->renderScene(scene, m_simulationElapsed);
    (void)m_engineProfiler.recordMeasured("pc.frame", m_lastFrameStats.pcFrameMilliseconds,
                                          fabgl::ProfilerUnit::Milliseconds,
                                          fabgl::ProfilerSampleSource::MeasuredPc);
    (void)m_engineProfiler.recordMeasured(
        "pc.draw_calls", static_cast<double>(m_lastFrameStats.drawCalls),
        fabgl::ProfilerUnit::Count, fabgl::ProfilerSampleSource::MeasuredPc);
    const double estimatedEsp32Frame = m_lastFrameStats.pcFrameMilliseconds * 4.0 +
                                       static_cast<double>(m_lastFrameStats.drawCalls) * 0.05;
    (void)m_engineProfiler.recordEstimated("esp32.frame.estimated", estimatedEsp32Frame,
                                           fabgl::ProfilerUnit::Milliseconds);
    (void)m_engineProfiler.recordEstimated("esp32.draw_calls",
                                           static_cast<double>(m_lastFrameStats.drawCalls),
                                           fabgl::ProfilerUnit::Count);
    updateProfiler();
}

void MainWindow::setRunState(const RunState state) {
    m_runState = state;
    switch (m_runState) {
    case RunState::Editing:
        m_gameView->setOverlayText(tr("EDIT MODE • authoring framebuffer"));
        break;
    case RunState::Playing:
        m_gameView->setOverlayText(tr("PLAYING • cloned runtime scene"));
        break;
    case RunState::Paused:
        m_gameView->setOverlayText(tr("PAUSED • Step advances 1/60 s"));
        break;
    }
    m_sceneView->setEditable(m_runState == RunState::Editing);
    updateRunActions();
    updateInspector(m_entities.index(selectedEntityRow(), 0));
    updateProfiler();
}

void MainWindow::updateRunActions() {
    const bool editing = m_runState == RunState::Editing;
    const bool playing = m_runState == RunState::Playing;
    const bool paused = m_runState == RunState::Paused;
    m_playAction->setEnabled(editing || paused);
    m_playAction->setText(paused ? tr("&Resume") : tr("&Play"));
    m_pauseAction->setEnabled(playing);
    m_stepAction->setEnabled(paused);
    m_stopAction->setEnabled(!editing);
    m_addEntityAction->setEnabled(editing);
    m_deleteEntityAction->setEnabled(editing && selectedEntityId().has_value());
    m_snapAction->setEnabled(editing);
    m_undoAction->setEnabled(editing && m_undoStack.canUndo());
    m_redoAction->setEnabled(editing && m_undoStack.canRedo());
}

void MainWindow::updateProfiler() {
    if (m_profiler == nullptr) {
        return;
    }
    const double estimatedEsp32Frame = m_lastFrameStats.pcFrameMilliseconds * 4.0 +
                                       static_cast<double>(m_lastFrameStats.drawCalls) * 0.05;
    const QString state = m_runState == RunState::Editing
                              ? tr("Editing")
                              : (m_runState == RunState::Playing ? tr("Playing") : tr("Paused"));
    const std::array<std::array<QString, 4>, 6> rows = {{
        {tr("Frame time"), tr("Measured PC"),
         tr("%1 ms").arg(m_lastFrameStats.pcFrameMilliseconds, 0, 'f', 3), tr("16.67 ms")},
        {tr("Draw calls"), tr("Measured PC"), QString::number(m_lastFrameStats.drawCalls), tr("—")},
        {tr("Frame time"), tr("Estimated ESP32"), tr("%1 ms").arg(estimatedEsp32Frame, 0, 'f', 3),
         tr("16.67 ms")},
        {tr("Draw calls"), tr("Estimated ESP32 budget"),
         QString::number(m_lastFrameStats.drawCalls), tr("64 calls")},
        {tr("Entities"), tr("Authoring scene"),
         QString::number(static_cast<qulonglong>(m_sceneDocument.scene().entityCount())), tr("—")},
        {tr("Runtime state"), tr("Editor"), state, tr("—")},
    }};
    for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
        for (int column = 0; column < 4; ++column) {
            auto* item = new QTableWidgetItem(
                rows.at(static_cast<std::size_t>(row)).at(static_cast<std::size_t>(column)));
            if ((row == 2 && estimatedEsp32Frame > 16.67) ||
                (row == 3 && m_lastFrameStats.drawCalls > 64U)) {
                item->setForeground(QColor(QStringLiteral("#ff6b6b")));
            }
            m_profiler->setItem(row, column, item);
        }
    }
}

void MainWindow::configureBuildCommand() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Build Command"));
    auto* layout = new QFormLayout(&dialog);
    auto* programEdit = new QLineEdit(m_projectData.buildProgram, &dialog);
    auto* argumentsEdit = new QLineEdit(argumentsForDisplay(m_projectData.buildArguments), &dialog);
    argumentsEdit->setPlaceholderText(tr("--build out/build/dev"));
    layout->addRow(tr("Program"), programEdit);
    layout->addRow(tr("Arguments"), argumentsEdit);
    auto* explanation =
        new QLabel(tr("Program and arguments are passed directly to QProcess; no command "
                      "shell is used."),
                   &dialog);
    explanation->setWordWrap(true);
    layout->addRow(explanation);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const auto program = programEdit->text().trimmed();
    if (program.isEmpty()) {
        QMessageBox::warning(this, tr("Invalid Build Command"),
                             tr("Build program cannot be empty."));
        return;
    }
    const auto arguments = QProcess::splitCommand(argumentsEdit->text());
    if (program == m_projectData.buildProgram && arguments == m_projectData.buildArguments) {
        return;
    }
    m_projectData.buildProgram = program;
    m_projectData.buildArguments = arguments;
    setDocumentModified(true);
    appendConsoleMessage(
        tr("Build command changed to %1.").arg(commandForDisplay(program, arguments)));
}

void MainWindow::runBuild() {
    if (m_buildRunner.isRunning()) {
        return;
    }
    if (m_projectFilePath.isEmpty() || isWindowModified()) {
        if (!saveProject()) {
            return;
        }
    }
    if (auto* dock = findChild<QDockWidget*>(QStringLiteral("buildOutputDock"))) {
        dock->show();
        dock->raise();
    }
    appendBuildOutput(tr("\n===== Build requested =====\n"), false);
    m_buildRunner.startBuild(m_projectData.buildProgram, m_projectData.buildArguments,
                             projectRoot());
}

void MainWindow::appendBuildOutput(const QString& text, const bool standardError) {
    QTextCursor cursor(m_buildOutput->document());
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat format;
    if (standardError) {
        format.setForeground(QColor(QStringLiteral("#ff6b6b")));
    }
    cursor.insertText(text, format);
    m_buildOutput->setTextCursor(cursor);
    m_buildOutput->ensureCursorVisible();
}

void MainWindow::updateBuildActions(const bool running) {
    m_buildAction->setEnabled(!running);
    m_buildSettingsAction->setEnabled(!running);
    m_cancelBuildAction->setEnabled(running);
}

void MainWindow::navigateDiagnostic(const QString& filePath, const int line) {
    QString resolved = filePath.trimmed();
    if ((resolved.startsWith(QLatin1Char('"')) && resolved.endsWith(QLatin1Char('"'))) ||
        (resolved.startsWith(QLatin1Char('\'')) && resolved.endsWith(QLatin1Char('\'')))) {
        resolved = resolved.mid(1, resolved.size() - 2);
    }
    if (QDir::isRelativePath(resolved)) {
        resolved = QDir(projectRoot()).absoluteFilePath(resolved);
    }
    if (!QFileInfo::exists(resolved)) {
        QDirIterator iterator(projectRoot(), QStringList{QFileInfo(resolved).fileName()},
                              QDir::Files, QDirIterator::Subdirectories);
        if (iterator.hasNext()) {
            resolved = iterator.next();
        }
    }
    if (!QFileInfo::exists(resolved)) {
        statusBar()->showMessage(
            tr("Diagnostic source was not found: %1").arg(QDir::toNativeSeparators(resolved)),
            5000);
        return;
    }
    m_codeEditor->openFile(resolved, line);
    if (auto* dock = findChild<QDockWidget*>(QStringLiteral("codeEditorDock"))) {
        dock->show();
        dock->raise();
    }
}

void MainWindow::applyTheme(const Theme theme) {
    m_theme = theme;
    if (m_theme == Theme::Dark) {
        QPalette palette;
        palette.setColor(QPalette::Window, QColor(37, 39, 43));
        palette.setColor(QPalette::WindowText, QColor(230, 230, 230));
        palette.setColor(QPalette::Base, QColor(27, 29, 32));
        palette.setColor(QPalette::AlternateBase, QColor(45, 48, 53));
        palette.setColor(QPalette::ToolTipBase, QColor(50, 52, 57));
        palette.setColor(QPalette::ToolTipText, Qt::white);
        palette.setColor(QPalette::Text, QColor(230, 230, 230));
        palette.setColor(QPalette::Button, QColor(45, 48, 53));
        palette.setColor(QPalette::ButtonText, QColor(230, 230, 230));
        palette.setColor(QPalette::BrightText, QColor(255, 90, 90));
        palette.setColor(QPalette::Highlight, QColor(48, 112, 180));
        palette.setColor(QPalette::HighlightedText, Qt::white);
        palette.setColor(QPalette::Disabled, QPalette::Text, QColor(125, 125, 125));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(125, 125, 125));
        qApp->setPalette(palette);
        qApp->setStyleSheet(
            QStringLiteral("QDockWidget::title { padding: 5px; background: #30343a; }"
                           "QToolBar { spacing: 3px; }"
                           "QPlainTextEdit, QListView, QTreeView, QTableWidget {"
                           " selection-background-color: #3070b4; }"));
        m_darkThemeAction->setChecked(true);
    } else {
        qApp->setPalette(m_defaultPalette);
        qApp->setStyleSheet(m_defaultStyleSheet);
        m_lightThemeAction->setChecked(true);
    }
    QSettings settings;
    settings.setValue(QStringLiteral("ui/theme"),
                      m_theme == Theme::Dark ? QStringLiteral("dark") : QStringLiteral("light"));
}

void MainWindow::restoreSettings() {
    QSettings settings;
    m_recentProjects = settings.value(QStringLiteral("project/recentFiles")).toStringList();
    applyTheme(settings.value(QStringLiteral("ui/theme"), QStringLiteral("dark")).toString() ==
                       QStringLiteral("light")
                   ? Theme::Light
                   : Theme::Dark);
    const auto geometry = settings.value(QStringLiteral("ui/geometry")).toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }
    const auto state = settings.value(QStringLiteral("ui/windowState")).toByteArray();
    if (!state.isEmpty()) {
        (void)restoreState(state, LayoutVersion);
    }
    rebuildRecentProjectsMenu();
}

void MainWindow::saveSettings() {
    QSettings settings;
    settings.setValue(QStringLiteral("ui/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("ui/windowState"), saveState(LayoutVersion));
    settings.setValue(QStringLiteral("ui/theme"),
                      m_theme == Theme::Dark ? QStringLiteral("dark") : QStringLiteral("light"));
    settings.setValue(QStringLiteral("project/recentFiles"), m_recentProjects);
}

void MainWindow::addRecentProject(const QString& filePath) {
    const auto cleanPath = QDir::cleanPath(QFileInfo(filePath).absoluteFilePath());
    for (auto iterator = m_recentProjects.begin(); iterator != m_recentProjects.end();) {
        if (QString::compare(*iterator, cleanPath,
#ifdef Q_OS_WIN
                             Qt::CaseInsensitive
#else
                             Qt::CaseSensitive
#endif
                             ) == 0) {
            iterator = m_recentProjects.erase(iterator);
        } else {
            ++iterator;
        }
    }
    m_recentProjects.prepend(cleanPath);
    while (m_recentProjects.size() > MaximumRecentProjects) {
        m_recentProjects.removeLast();
    }
    QSettings settings;
    settings.setValue(QStringLiteral("project/recentFiles"), m_recentProjects);
    rebuildRecentProjectsMenu();
}

void MainWindow::rebuildRecentProjectsMenu() {
    if (m_recentProjectsMenu == nullptr) {
        return;
    }
    m_recentProjectsMenu->clear();
    if (m_recentProjects.isEmpty()) {
        auto* emptyAction = m_recentProjectsMenu->addAction(tr("No Recent Projects"));
        emptyAction->setEnabled(false);
        return;
    }
    for (const auto& filePath : std::as_const(m_recentProjects)) {
        auto* action = m_recentProjectsMenu->addAction(QFileInfo(filePath).completeBaseName());
        action->setToolTip(QDir::toNativeSeparators(filePath));
        connect(action, &QAction::triggered, this, [this, filePath]() {
            if (!QFileInfo::exists(filePath)) {
                QMessageBox::warning(this, tr("Project Not Found"),
                                     tr("The recent project no longer exists:\n%1")
                                         .arg(QDir::toNativeSeparators(filePath)));
                m_recentProjects.removeAll(filePath);
                rebuildRecentProjectsMenu();
                return;
            }
            (void)openProjectPath(filePath);
        });
    }
    m_recentProjectsMenu->addSeparator();
    auto* clearAction = m_recentProjectsMenu->addAction(tr("Clear Recent Projects"));
    connect(clearAction, &QAction::triggered, this, [this]() {
        m_recentProjects.clear();
        QSettings settings;
        settings.setValue(QStringLiteral("project/recentFiles"), m_recentProjects);
        rebuildRecentProjectsMenu();
    });
}

void MainWindow::resetLayout() {
    for (auto* dock : std::as_const(m_docks)) {
        dock->show();
    }
    (void)restoreState(m_defaultLayout, LayoutVersion);
    statusBar()->showMessage(tr("Default panel layout restored"), 3000);
}

void MainWindow::appendConsoleMessage(const QString& message) {
    m_console->appendPlainText(
        tr("[%1] %2").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), message));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!maybeSave() || !m_codeEditor->maybeSaveAll()) {
        event->ignore();
        return;
    }
    if (m_buildRunner.isRunning()) {
        const auto answer = QMessageBox::question(
            this, tr("Build Is Running"), tr("A build process is still running. Stop it and exit?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        m_buildRunner.stopBuild();
    }
    if (m_runState != RunState::Editing) {
        stop();
    }
    saveSettings();
    event->accept();
}

} // namespace fgl::studio
