#include <fabgl/project/project_asset_library.h>
#include <fabgl/project/project_extension_modules.h>
#include <fabgl/project/project_extension_service_host.h>
#include <fabgl/runtime/scene_runtime.h>

#include <project_format.h>

#if defined(_WIN32)
#include "win32_audio.h"
#endif

#include "AssetBrowserController.h"
#include "MainWindow.h"
#include "StudioPlaySession.h"
#include "ToolchainSetupWidget.h"

#include "AdvancedEditorPanels.h"
#include "CodeEditor.h"
#include "ComponentInspector.h"
#include "EntityCommands.h"
#include "ExtensionServicePanel.h"
#include "ImageImportSettingsWidget.h"
#include "InputMapEditorWidget.h"
#include "PrefabEditorPanel.h"
#include "ProjectCreationDialog.h"
#include "SerialConsoleWidget.h"
#include "SpecialistEditorPanels.h"
#include "WorkflowCommands.h"

#include <fabgl/scene/entity.h>

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTime>
#include <QTimer>
#include <QToolBar>
#include <QTreeView>
#include <QUndoCommand>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <string_view>
#include <system_error>
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

fabgl::Error extensionSchemaError(const QString& service, const QString& message) {
    return fabgl::Error(fabgl::ErrorCode::InvalidFormat, message.toStdString())
        .addContext("service", service.toStdString());
}

QString assetTargetName(const fabgl::assets::AssetTarget target) {
    switch (target) {
    case fabgl::assets::AssetTarget::Pc:
        return QStringLiteral("pc");
    case fabgl::assets::AssetTarget::Esp32Flash:
        return QStringLiteral("esp32-flash");
    case fabgl::assets::AssetTarget::Esp32Psram:
        return QStringLiteral("esp32-psram");
    case fabgl::assets::AssetTarget::Esp32Sd:
        return QStringLiteral("esp32-sd");
    }
    return QStringLiteral("unknown");
}

std::optional<fabgl::assets::AssetKind> extensionAssetKind(const QString& text) {
    static const std::array kinds{
        std::pair{QStringLiteral("binary"), fabgl::assets::AssetKind::Binary},
        std::pair{QStringLiteral("image"), fabgl::assets::AssetKind::Image},
        std::pair{QStringLiteral("audio"), fabgl::assets::AssetKind::Audio},
        std::pair{QStringLiteral("font"), fabgl::assets::AssetKind::Font},
        std::pair{QStringLiteral("tilemap"), fabgl::assets::AssetKind::Tilemap},
        std::pair{QStringLiteral("tileset"), fabgl::assets::AssetKind::Tileset},
        std::pair{QStringLiteral("sprite-atlas"), fabgl::assets::AssetKind::SpriteAtlas},
        std::pair{QStringLiteral("animation"), fabgl::assets::AssetKind::Animation},
        std::pair{QStringLiteral("material"), fabgl::assets::AssetKind::Material},
        std::pair{QStringLiteral("scene"), fabgl::assets::AssetKind::Scene},
        std::pair{QStringLiteral("prefab"), fabgl::assets::AssetKind::Prefab},
        std::pair{QStringLiteral("script"), fabgl::assets::AssetKind::Script},
        std::pair{QStringLiteral("visual-script"), fabgl::assets::AssetKind::VisualScript},
        std::pair{QStringLiteral("raycast-map"), fabgl::assets::AssetKind::RaycastMap},
        std::pair{QStringLiteral("racer-track"), fabgl::assets::AssetKind::RacerTrack},
        std::pair{QStringLiteral("low-poly-mesh"), fabgl::assets::AssetKind::LowPolyMesh},
        std::pair{QStringLiteral("json"), fabgl::assets::AssetKind::Json},
    };
    const auto normalized = text.trimmed().toLower();
    const auto found = std::find_if(kinds.cbegin(), kinds.cend(), [&normalized](const auto& item) {
        return item.first == normalized;
    });
    return found == kinds.cend() ? std::nullopt
                                 : std::optional<fabgl::assets::AssetKind>(found->second);
}

QString propertyTypeName(const fabgl::PropertyType type) {
    switch (type) {
    case fabgl::PropertyType::Boolean:
        return QStringLiteral("boolean");
    case fabgl::PropertyType::SignedInteger:
        return QStringLiteral("signed-integer");
    case fabgl::PropertyType::UnsignedInteger:
        return QStringLiteral("unsigned-integer");
    case fabgl::PropertyType::Float:
        return QStringLiteral("float");
    case fabgl::PropertyType::Fixed:
        return QStringLiteral("fixed");
    case fabgl::PropertyType::String:
        return QStringLiteral("string");
    case fabgl::PropertyType::Enumeration:
        return QStringLiteral("enumeration");
    case fabgl::PropertyType::BitFlags:
        return QStringLiteral("bit-flags");
    case fabgl::PropertyType::Vec2:
        return QStringLiteral("vec2");
    case fabgl::PropertyType::Vec3:
        return QStringLiteral("vec3");
    case fabgl::PropertyType::EulerAngles:
        return QStringLiteral("euler-angles");
    case fabgl::PropertyType::Quaternion:
        return QStringLiteral("quaternion");
    case fabgl::PropertyType::Rect:
        return QStringLiteral("rect");
    case fabgl::PropertyType::Color:
        return QStringLiteral("color");
    case fabgl::PropertyType::AssetReference:
        return QStringLiteral("asset-reference");
    case fabgl::PropertyType::EntityReference:
        return QStringLiteral("entity-reference");
    case fabgl::PropertyType::ComponentReference:
        return QStringLiteral("component-reference");
    case fabgl::PropertyType::List:
        return QStringLiteral("list");
    case fabgl::PropertyType::Curve:
        return QStringLiteral("curve");
    case fabgl::PropertyType::AnimationCurve:
        return QStringLiteral("animation-curve");
    case fabgl::PropertyType::ActionReference:
        return QStringLiteral("action-reference");
    case fabgl::PropertyType::EventReference:
        return QStringLiteral("event-reference");
    }
    return QStringLiteral("unknown");
}

QJsonValue extensionPropertyValue(const fabgl::PropertyValue& value) {
    return std::visit(
        [](const auto& typed) -> QJsonValue {
            using Value = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<Value, bool>)
                return typed;
            else if constexpr (std::is_same_v<Value, std::int64_t> ||
                               std::is_same_v<Value, std::uint64_t>)
                return QString::number(typed);
            else if constexpr (std::is_same_v<Value, double>)
                return std::isfinite(typed) ? QJsonValue(typed) : QJsonValue();
            else if constexpr (std::is_same_v<Value, fabgl::Fixed>)
                return static_cast<double>(typed.toFloat());
            else if constexpr (std::is_same_v<Value, std::string>)
                return QString::fromStdString(typed);
            else if constexpr (std::is_same_v<Value, fabgl::Vec2>)
                return QJsonArray{typed.x, typed.y};
            else if constexpr (std::is_same_v<Value, fabgl::Vec3> ||
                               std::is_same_v<Value, fabgl::EulerAngles>)
                return QJsonArray{typed.x, typed.y, typed.z};
            else if constexpr (std::is_same_v<Value, fabgl::Quaternion>)
                return QJsonArray{typed.x, typed.y, typed.z, typed.w};
            else if constexpr (std::is_same_v<Value, fabgl::Rect>)
                return QJsonArray{typed.x, typed.y, typed.width, typed.height};
            else if constexpr (std::is_same_v<Value, fabgl::Color>)
                return QJsonArray{typed.r, typed.g, typed.b, typed.a};
            else if constexpr (std::is_same_v<Value, fabgl::AssetGuid> ||
                               std::is_same_v<Value, fabgl::EntityGuid>)
                return QString::fromStdString(typed.toString());
            else if constexpr (std::is_same_v<Value, fabgl::ComponentReference>)
                return QJsonObject{
                    {QStringLiteral("entity"), QString::fromStdString(typed.entity.toString())},
                    {QStringLiteral("component"),
                     QString::fromStdString(typed.component.toString())}};
            else if constexpr (std::is_same_v<Value, fabgl::ActionReference> ||
                               std::is_same_v<Value, fabgl::EventReference>)
                return QString::fromStdString(typed.name);
            else
                return QJsonValue();
        },
        value);
}

fabgl::Result<fabgl::PropertyValue>
extensionDecodedPropertyValue(const fabgl::PropertyMetadata& metadata, const QJsonValue& json) {
    const auto invalid = [&metadata]() {
        return fabgl::Result<fabgl::PropertyValue>::failure(
            fabgl::Error(fabgl::ErrorCode::InvalidFormat,
                         "custom inspector returned a value with the wrong schema")
                .addContext("property", metadata.name));
    };
    switch (metadata.type) {
    case fabgl::PropertyType::Boolean:
        return json.isBool() ? fabgl::Result<fabgl::PropertyValue>::success(json.toBool())
                             : invalid();
    case fabgl::PropertyType::SignedInteger:
    case fabgl::PropertyType::Enumeration: {
        bool ok = false;
        const auto number = json.toString().toLongLong(&ok);
        return ok ? fabgl::Result<fabgl::PropertyValue>::success(static_cast<std::int64_t>(number))
                  : invalid();
    }
    case fabgl::PropertyType::UnsignedInteger:
    case fabgl::PropertyType::BitFlags: {
        bool ok = false;
        const auto number = json.toString().toULongLong(&ok);
        return ok ? fabgl::Result<fabgl::PropertyValue>::success(static_cast<std::uint64_t>(number))
                  : invalid();
    }
    case fabgl::PropertyType::Float:
        return json.isDouble() && std::isfinite(json.toDouble())
                   ? fabgl::Result<fabgl::PropertyValue>::success(json.toDouble())
                   : invalid();
    case fabgl::PropertyType::Fixed:
        return json.isDouble() && std::isfinite(json.toDouble())
                   ? fabgl::Result<fabgl::PropertyValue>::success(
                         fabgl::Fixed::fromFloat(static_cast<float>(json.toDouble())))
                   : invalid();
    case fabgl::PropertyType::String:
        return json.isString() && static_cast<std::size_t>(json.toString().toUtf8().size()) <=
                                      fabgl::MaximumPropertyStringLength
                   ? fabgl::Result<fabgl::PropertyValue>::success(json.toString().toStdString())
                   : invalid();
    case fabgl::PropertyType::ActionReference:
        return json.isString() ? fabgl::Result<fabgl::PropertyValue>::success(
                                     fabgl::ActionReference{json.toString().toStdString()})
                               : invalid();
    case fabgl::PropertyType::EventReference:
        return json.isString() ? fabgl::Result<fabgl::PropertyValue>::success(
                                     fabgl::EventReference{json.toString().toStdString()})
                               : invalid();
    default:
        return invalid();
    }
}

bool pathInsideRoot(const QString& rootPath, const QString& candidatePath) {
    const QString root =
        QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(rootPath).absoluteFilePath()));
    const QString candidate =
        QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(candidatePath).absoluteFilePath()));
    const auto sensitivity =
#ifdef Q_OS_WIN
        Qt::CaseInsensitive;
#else
        Qt::CaseSensitive;
#endif
    const QString prefix = root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/');
    return candidate.compare(root, sensitivity) == 0 || candidate.startsWith(prefix, sensitivity);
}

bool pathCrossesLink(const QString& rootPath, const QString& candidatePath) {
    const QString root = QDir::cleanPath(QFileInfo(rootPath).absoluteFilePath());
    QString cursor = QDir::cleanPath(QFileInfo(candidatePath).absoluteFilePath());
    while (pathInsideRoot(root, cursor)) {
        const QFileInfo info(cursor);
        if (info.exists() && info.isSymLink()) {
            return true;
        }
        if (cursor.compare(root,
#ifdef Q_OS_WIN
                           Qt::CaseInsensitive
#else
                           Qt::CaseSensitive
#endif
                           ) == 0) {
            break;
        }
        const QString parent = QFileInfo(cursor).absolutePath();
        if (parent == cursor) {
            break;
        }
        cursor = parent;
    }
    return false;
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
    QString description = QString::fromStdString(error.message());
    for (const auto& context : error.context()) {
        description +=
            QStringLiteral(" [%1=%2]")
                .arg(QString::fromStdString(context.key), QString::fromStdString(context.value));
    }
    return description;
}

fabgl::project::ProjectExtensionHostContext
extensionHostContext(const std::string& manifestPath, const std::string& projectRoot,
                     const fabgl::Scene& scene, const fabgl::SceneRuntime* sceneRuntime = nullptr) {
    fabgl::project::ProjectExtensionHostContext context;
    context.hostKind = fabgl::project::ProjectExtensionHostKind::Studio;
    context.projectManifestPath = manifestPath.data();
    context.projectManifestPathBytes = manifestPath.size();
    context.projectRoot = projectRoot.data();
    context.projectRootBytes = projectRoot.size();
    context.scene = &scene;
    context.sceneRuntime = sceneRuntime;
    return context;
}

} // namespace

MainWindow::MainWindow(QWidget* parent, StudioLaunchOptions options)
    : QMainWindow(parent), m_sceneDocument(this), m_entities(this), m_undoStack(this),
      m_buildRunner(this), m_pcRunner(this), m_serialRunner(this), m_portDetector(this),
      m_engineProfiler(), m_launchOptions(std::move(options)),
      m_recoveryManager(m_launchOptions.recoveryRoot), m_defaultPalette(qApp->palette()),
      m_defaultStyleSheet(qApp->styleSheet()) {
    m_buildRunner.setObjectName(QStringLiteral("workflowBuildRunner"));
    m_pcRunner.setObjectName(QStringLiteral("pcPlayerRunner"));
    m_serialRunner.setObjectName(QStringLiteral("serialMonitorRunner"));
    m_portDetector.setObjectName(QStringLiteral("serialPortDetector"));
    if (m_launchOptions.safeMode) {
        m_launchOptions.pluginsEnabled = false;
        m_launchOptions.reopenLastProject = false;
    }
    QString sessionError;
    m_sessionStarted = m_recoveryManager.beginSession(sessionError);
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
    configureExtensionProductHooks();
    createMenusAndToolbars();
    m_playTimer = new QTimer(this);
    m_playTimer->setTimerType(Qt::PreciseTimer);
    m_playTimer->setInterval(16);
    m_autosaveTimer = new QTimer(this);
    m_autosaveTimer->setObjectName(QStringLiteral("autosaveTimer"));
    m_autosaveTimer->setInterval(30000);
    connect(m_autosaveTimer, &QTimer::timeout, this, [this]() {
        QString errorMessage;
        if (!performAutosave(errorMessage) && !errorMessage.isEmpty()) {
            appendConsoleMessage(tr("Autosave failed: %1").arg(errorMessage));
            statusBar()->showMessage(tr("Autosave failed"), 5000);
        }
    });
    m_autosaveTimer->start();
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
    if (!sessionError.isEmpty()) {
        appendConsoleMessage(tr("Recovery session marker warning: %1").arg(sessionError));
    }
    if (m_recoveryManager.previousSessionWasUnclean()) {
        appendConsoleMessage(tr("The previous Studio session did not close cleanly; recovery data "
                                "is available from File > Recovery Sessions."));
        if (m_launchOptions.interactiveRecovery && !m_recoveryManager.entries().isEmpty()) {
            QTimer::singleShot(0, this, &MainWindow::showRecoveryDialog);
        }
    }
    if (m_launchOptions.safeMode || !m_launchOptions.pluginsEnabled) {
        appendConsoleMessage(
            m_launchOptions.safeMode
                ? tr("Safe mode is active; plugins and last-project reopening are disabled.")
                : tr("Editor plugins are disabled for this session."));
    }
    statusBar()->showMessage(tr("Ready"), 3000);
}

MainWindow::~MainWindow() {
    QString extensionError;
    if (!deactivateProjectExtensions(extensionError) && !extensionError.isEmpty() &&
        m_console != nullptr) {
        appendConsoleMessage(tr("Project extension shutdown failed: %1").arg(extensionError));
    }

    // Child widgets may emit status signals while QMainWindow's base classes destroy them. At
    // that point calling back into MainWindow (for example, CodeEditorWidget forwarding clangd's
    // final status) is no longer safe, so sever child-to-window connections while the full object
    // is still alive.
    const auto childObjects = findChildren<QObject*>();
    for (auto* child : childObjects) {
        child->disconnect(this);
    }

    // QUndoStack::clear() emits availability signals from its destructor. Disconnect while all
    // MainWindow state is still alive so those signals cannot call slots after QString members
    // such as m_projectFilePath have already been destroyed.
    disconnect(&m_undoStack, nullptr, this, nullptr);
    if (m_sessionStarted) {
        QString ignoredError;
        (void)m_recoveryManager.endSession(ignoredError);
        m_sessionStarted = false;
    }
}

SceneDocument& MainWindow::sceneDocument() noexcept {
    return m_sceneDocument;
}

const SceneDocument& MainWindow::sceneDocument() const noexcept {
    return m_sceneDocument;
}

bool MainWindow::currentProjectTrusted() const {
    return m_projectFilePath.isEmpty() || m_projectTrustStore.isTrusted(m_projectFilePath);
}

bool MainWindow::setCurrentProjectTrusted(const bool trusted, QString& errorMessage) {
    if (m_projectFilePath.isEmpty()) {
        errorMessage = tr("Save or open a project before recording a trust decision.");
        return false;
    }
    if (!trusted) {
        m_previewRestartController.clear();
    }
    if (!trusted && m_runState != RunState::Editing) {
        stop();
    }
    if (!trusted && m_pcRunner.isRunning()) {
        stopPc();
    }
    if (!trusted && m_buildRunner.isRunning()) {
        m_workflowCancelled = true;
        m_buildRunner.stopBuild();
    }
    if (!m_projectTrustStore.setTrusted(m_projectFilePath, trusted, errorMessage)) {
        return false;
    }
    if (trusted) {
        if (!reloadProjectExtensions(errorMessage)) {
            updateProjectTrustUi();
            appendConsoleMessage(tr("Project was trusted, but its extensions remain disabled: %1")
                                     .arg(errorMessage));
            return false;
        }
    } else {
        QString shutdownError;
        if (!deactivateProjectExtensions(shutdownError) && !shutdownError.isEmpty()) {
            appendConsoleMessage(tr("Project trust was revoked; extension shutdown reported: %1")
                                     .arg(shutdownError));
        }
    }
    updateProjectTrustUi();
    appendConsoleMessage(trusted ? tr("Project trusted for code execution at this exact path.")
                                 : tr("Project trust revoked; code execution is blocked."));
    return true;
}

bool MainWindow::safeMode() const noexcept {
    return m_launchOptions.safeMode;
}

bool MainWindow::pluginsEnabled() const noexcept {
    return m_launchOptions.pluginsEnabled;
}

bool MainWindow::telemetryEnabled() const noexcept {
    return false;
}

bool MainWindow::previousSessionWasUnclean() const noexcept {
    return m_recoveryManager.previousSessionWasUnclean();
}

QVector<RecoveryEntry> MainWindow::recoveryEntries() const {
    return m_recoveryManager.entries();
}

bool MainWindow::performAutosave(QString& errorMessage) {
    if (!isWindowModified()) {
        errorMessage.clear();
        return true;
    }
    ProjectData recoveryProject = m_projectData;
    if (recoveryProject.name.trimmed().isEmpty()) {
        recoveryProject.name = m_projectName;
    }
    if (recoveryProject.projectGuid.trimmed().isEmpty()) {
        m_projectData.projectGuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        recoveryProject.projectGuid = m_projectData.projectGuid;
    }
    if (recoveryProject.sceneFile.trimmed().isEmpty()) {
        recoveryProject.sceneFile = QStringLiteral("Scenes/Main.fglscene");
    }
    const QByteArray projectBytes = ProjectDocument::serialized(recoveryProject, errorMessage);
    if (projectBytes.isEmpty()) {
        return false;
    }
    const QByteArray sceneBytes = m_sceneDocument.serialized(errorMessage);
    if (sceneBytes.isEmpty()) {
        return false;
    }
    if (!m_recoveryManager.writeAutosave(m_projectFilePath, recoveryProject.sceneFile, projectBytes,
                                         sceneBytes, errorMessage)) {
        return false;
    }
    statusBar()->showMessage(tr("Recovery autosave written atomically"), 2500);
    return true;
}

bool MainWindow::restoreRecovery(const QString& recoveryId, const QString& destinationProjectPath,
                                 QString& errorMessage) {
    const auto recoveries = m_recoveryManager.entries();
    const auto iterator =
        std::find_if(recoveries.cbegin(), recoveries.cend(),
                     [&recoveryId](const RecoveryEntry& entry) { return entry.id == recoveryId; });
    if (iterator == recoveries.cend()) {
        errorMessage = tr("Recovery entry '%1' does not exist.").arg(recoveryId);
        return false;
    }
    return m_recoveryManager.restore(*iterator, destinationProjectPath, errorMessage);
}

bool MainWindow::discardRecovery(const QString& recoveryId, QString& errorMessage) {
    const auto recoveries = m_recoveryManager.entries();
    const auto iterator =
        std::find_if(recoveries.cbegin(), recoveries.cend(),
                     [&recoveryId](const RecoveryEntry& entry) { return entry.id == recoveryId; });
    if (iterator == recoveries.cend()) {
        errorMessage = tr("Recovery entry '%1' does not exist.").arg(recoveryId);
        return false;
    }
    return m_recoveryManager.discard(*iterator, errorMessage);
}

QString MainWindow::lastProjectPath() const {
    return m_recoveryManager.lastProjectPath();
}

void MainWindow::createActions() {
    const auto icon = [this](const QStyle::StandardPixmap standardPixmap) {
        return style()->standardIcon(standardPixmap);
    };

    m_newAction = new QAction(icon(QStyle::SP_FileIcon), tr("&New Project..."), this);
    m_newAction->setObjectName(QStringLiteral("newProjectAction"));
    m_newAction->setShortcut(QKeySequence::New);
    connect(m_newAction, &QAction::triggered, this, &MainWindow::newProject);

    m_openAction = new QAction(icon(QStyle::SP_DialogOpenButton), tr("&Open Project..."), this);
    m_openAction->setObjectName(QStringLiteral("openProjectAction"));
    m_openAction->setShortcut(QKeySequence::Open);
    connect(m_openAction, &QAction::triggered, this, &MainWindow::openProject);

    m_saveAction = new QAction(icon(QStyle::SP_DialogSaveButton), tr("&Save Project"), this);
    m_saveAction->setObjectName(QStringLiteral("saveProjectAction"));
    m_saveAction->setShortcut(QKeySequence::Save);
    connect(m_saveAction, &QAction::triggered, this, [this]() { (void)saveProject(); });
    m_saveAsAction = new QAction(tr("Save Project &As..."), this);
    m_saveAsAction->setObjectName(QStringLiteral("saveProjectAsAction"));
    m_saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(m_saveAsAction, &QAction::triggered, this, [this]() { (void)saveProjectAs(); });

    m_undoAction = m_undoStack.createUndoAction(this, tr("&Undo"));
    m_undoAction->setObjectName(QStringLiteral("undoAction"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_redoAction = m_undoStack.createRedoAction(this, tr("&Redo"));
    m_redoAction->setObjectName(QStringLiteral("redoAction"));
    m_redoAction->setShortcut(QKeySequence::Redo);

    m_addEntityAction = new QAction(tr("&Add Entity"), this);
    m_addEntityAction->setObjectName(QStringLiteral("addEntityAction"));
    m_addEntityAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
    connect(m_addEntityAction, &QAction::triggered, this, &MainWindow::addEntity);
    m_duplicateEntityAction = new QAction(tr("D&uplicate Selected Entities"), this);
    m_duplicateEntityAction->setObjectName(QStringLiteral("duplicateEntityAction"));
    m_duplicateEntityAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    connect(m_duplicateEntityAction, &QAction::triggered, this,
            &MainWindow::duplicateSelectedEntities);
    m_deleteEntityAction = new QAction(tr("&Delete Entity"), this);
    m_deleteEntityAction->setObjectName(QStringLiteral("deleteEntityAction"));
    m_deleteEntityAction->setShortcut(QKeySequence(Qt::Key_Delete));
    connect(m_deleteEntityAction, &QAction::triggered, this, &MainWindow::deleteSelectedEntity);
    m_setParentAction = new QAction(tr("Set &Parent..."), this);
    m_setParentAction->setObjectName(QStringLiteral("setEntityParentAction"));
    connect(m_setParentAction, &QAction::triggered, this, &MainWindow::setSelectedEntitiesParent);
    m_clearParentAction = new QAction(tr("Move to Scene &Root"), this);
    m_clearParentAction->setObjectName(QStringLiteral("clearEntityParentAction"));
    connect(m_clearParentAction, &QAction::triggered, this,
            &MainWindow::clearSelectedEntitiesParent);
    m_snapAction = new QAction(tr("Snap to &Grid"), this);
    m_snapAction->setObjectName(QStringLiteral("snapToGridAction"));
    m_snapAction->setCheckable(true);
    m_snapAction->setChecked(true);

    m_sceneToolActions = new QActionGroup(this);
    m_sceneToolActions->setExclusive(true);
    const auto createSceneTool = [this](const QString& text, const QString& objectName,
                                        const QKeySequence& shortcut, const SceneView::Tool tool) {
        auto* action = new QAction(text, m_sceneToolActions);
        action->setObjectName(objectName);
        action->setCheckable(true);
        action->setShortcut(shortcut);
        connect(action, &QAction::triggered, this, [this, tool]() {
            if (m_sceneView != nullptr) {
                m_sceneView->setTool(tool);
            }
        });
        return action;
    };
    m_selectToolAction = createSceneTool(tr("&Select Tool"), QStringLiteral("selectToolAction"),
                                         QKeySequence(Qt::Key_Q), SceneView::Tool::Select);
    m_moveToolAction = createSceneTool(tr("&Move Tool"), QStringLiteral("moveToolAction"),
                                       QKeySequence(Qt::Key_W), SceneView::Tool::Move);
    m_rotateToolAction = createSceneTool(tr("&Rotate Tool"), QStringLiteral("rotateToolAction"),
                                         QKeySequence(Qt::Key_E), SceneView::Tool::Rotate);
    m_scaleToolAction = createSceneTool(tr("&Scale Tool"), QStringLiteral("scaleToolAction"),
                                        QKeySequence(Qt::Key_R), SceneView::Tool::Scale);
    m_selectToolAction->setChecked(true);
    m_transformSpaceActions = new QActionGroup(this);
    m_transformSpaceActions->setExclusive(true);
    m_localTransformAction = new QAction(tr("&Local Transform Space"), m_transformSpaceActions);
    m_localTransformAction->setObjectName(QStringLiteral("localTransformSpaceAction"));
    m_localTransformAction->setCheckable(true);
    m_localTransformAction->setChecked(true);
    m_worldTransformAction = new QAction(tr("&World Transform Space"), m_transformSpaceActions);
    m_worldTransformAction->setObjectName(QStringLiteral("worldTransformSpaceAction"));
    m_worldTransformAction->setCheckable(true);
    connect(m_localTransformAction, &QAction::triggered, this, [this]() {
        if (m_sceneView != nullptr) {
            m_sceneView->setTransformSpace(SceneView::TransformSpace::Local);
        }
    });
    connect(m_worldTransformAction, &QAction::triggered, this, [this]() {
        if (m_sceneView != nullptr) {
            m_sceneView->setTransformSpace(SceneView::TransformSpace::World);
        }
    });
    m_frameSelectedAction = new QAction(tr("&Frame Selected"), this);
    m_frameSelectedAction->setObjectName(QStringLiteral("frameSelectedAction"));
    m_frameSelectedAction->setShortcut(QKeySequence(Qt::Key_F));
    connect(m_frameSelectedAction, &QAction::triggered, this, [this]() {
        if (m_sceneView != nullptr) {
            m_sceneView->frameSelected();
        }
    });
    m_zoomInAction = new QAction(tr("Zoom &In"), this);
    m_zoomInAction->setObjectName(QStringLiteral("sceneZoomInAction"));
    m_zoomInAction->setShortcut(QKeySequence::ZoomIn);
    connect(m_zoomInAction, &QAction::triggered, this, [this]() {
        if (m_sceneView != nullptr) {
            m_sceneView->zoomIn();
        }
    });
    m_zoomOutAction = new QAction(tr("Zoom &Out"), this);
    m_zoomOutAction->setObjectName(QStringLiteral("sceneZoomOutAction"));
    m_zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    connect(m_zoomOutAction, &QAction::triggered, this, [this]() {
        if (m_sceneView != nullptr) {
            m_sceneView->zoomOut();
        }
    });

    m_playAction = new QAction(icon(QStyle::SP_MediaPlay), tr("&Play"), this);
    m_playAction->setObjectName(QStringLiteral("playAction"));
    m_playAction->setShortcut(QKeySequence(Qt::Key_F6));
    connect(m_playAction, &QAction::triggered, this, &MainWindow::play);
    m_pauseAction = new QAction(icon(QStyle::SP_MediaPause), tr("P&ause"), this);
    m_pauseAction->setObjectName(QStringLiteral("pauseAction"));
    m_pauseAction->setShortcut(QKeySequence(Qt::Key_F7));
    connect(m_pauseAction, &QAction::triggered, this, &MainWindow::pause);
    m_stepAction = new QAction(icon(QStyle::SP_MediaSkipForward), tr("&Step"), this);
    m_stepAction->setObjectName(QStringLiteral("stepAction"));
    m_stepAction->setShortcut(QKeySequence(Qt::Key_F8));
    connect(m_stepAction, &QAction::triggered, this, &MainWindow::step);
    m_stopAction = new QAction(icon(QStyle::SP_MediaStop), tr("S&top"), this);
    m_stopAction->setObjectName(QStringLiteral("stopAction"));
    m_stopAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F6));
    connect(m_stopAction, &QAction::triggered, this, &MainWindow::stop);

    m_buildAction = new QAction(tr("&Build Project"), this);
    m_buildAction->setObjectName(QStringLiteral("buildProjectAction"));
    m_buildAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    connect(m_buildAction, &QAction::triggered, this, &MainWindow::runBuild);
    m_cancelBuildAction = new QAction(tr("&Cancel Build"), this);
    m_cancelBuildAction->setObjectName(QStringLiteral("cancelBuildAction"));
    connect(m_cancelBuildAction, &QAction::triggered, this, &MainWindow::cancelWorkflow);
    m_buildSettingsAction = new QAction(tr("Project &Settings..."), this);
    m_buildSettingsAction->setObjectName(QStringLiteral("buildCommandAction"));
    connect(m_buildSettingsAction, &QAction::triggered, this, &MainWindow::configureBuildCommand);

    m_pcPlayAction = new QAction(icon(QStyle::SP_MediaPlay), tr("Play on &PC"), this);
    m_pcPlayAction->setObjectName(QStringLiteral("pcPlayAction"));
    m_pcPlayAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F6));
    connect(m_pcPlayAction, &QAction::triggered, this, &MainWindow::playPc);
    m_pcStopAction = new QAction(icon(QStyle::SP_MediaStop), tr("Stop PC Player"), this);
    m_pcStopAction->setObjectName(QStringLiteral("pcStopAction"));
    connect(m_pcStopAction, &QAction::triggered, this, &MainWindow::stopPc);

    m_exportEsp32Action = new QAction(tr("&Export ESP32 Sketch"), this);
    m_exportEsp32Action->setObjectName(QStringLiteral("exportEsp32Action"));
    connect(m_exportEsp32Action, &QAction::triggered, this, &MainWindow::exportEsp32);
    m_refreshPortsAction = new QAction(tr("&Refresh Serial Ports"), this);
    m_refreshPortsAction->setObjectName(QStringLiteral("refreshSerialPortsAction"));
    connect(m_refreshPortsAction, &QAction::triggered, this, &MainWindow::refreshSerialPorts);
    m_uploadEsp32Action = new QAction(tr("&Upload to Selected ESP32"), this);
    m_uploadEsp32Action->setObjectName(QStringLiteral("uploadEsp32Action"));
    connect(m_uploadEsp32Action, &QAction::triggered, this, &MainWindow::uploadEsp32);
    m_deployEsp32Action = new QAction(tr("Build, Upload && Diagnose &All"), this);
    m_deployEsp32Action->setObjectName(QStringLiteral("deployEsp32DiagnosticsAction"));
    connect(m_deployEsp32Action, &QAction::triggered, this, &MainWindow::deployEsp32Diagnostics);
    const auto addHardwareDiagnosticAction = [this](const QString& label, const QString& objectName,
                                                    const QString& check) {
        auto* action = new QAction(label, this);
        action->setObjectName(objectName);
        connect(action, &QAction::triggered, this,
                [this, check]() { runHardwareDiagnostic(check); });
        m_hardwareDiagnosticActions.push_back(action);
    };
    addHardwareDiagnosticAction(tr("VGA Output Test"),
                                QStringLiteral("vgaHardwareDiagnosticAction"),
                                QStringLiteral("vga"));
    addHardwareDiagnosticAction(tr("Keyboard Test"),
                                QStringLiteral("keyboardHardwareDiagnosticAction"),
                                QStringLiteral("keyboard"));
    addHardwareDiagnosticAction(tr("Mouse Test"), QStringLiteral("mouseHardwareDiagnosticAction"),
                                QStringLiteral("mouse"));
    addHardwareDiagnosticAction(tr("Audio Test"), QStringLiteral("audioHardwareDiagnosticAction"),
                                QStringLiteral("audio"));
    addHardwareDiagnosticAction(tr("SD Card Test"), QStringLiteral("sdHardwareDiagnosticAction"),
                                QStringLiteral("sd"));
    addHardwareDiagnosticAction(tr("PSRAM Test"), QStringLiteral("psramHardwareDiagnosticAction"),
                                QStringLiteral("psram"));
    addHardwareDiagnosticAction(tr("Frame-rate Test"),
                                QStringLiteral("frameRateHardwareDiagnosticAction"),
                                QStringLiteral("frame-rate"));
    m_serialMonitorAction = new QAction(tr("Open Serial &Monitor"), this);
    m_serialMonitorAction->setObjectName(QStringLiteral("serialMonitorAction"));
    connect(m_serialMonitorAction, &QAction::triggered, this, &MainWindow::startSerialMonitor);
    m_stopSerialMonitorAction = new QAction(tr("Stop Serial Monitor"), this);
    m_stopSerialMonitorAction->setObjectName(QStringLiteral("stopSerialMonitorAction"));
    connect(m_stopSerialMonitorAction, &QAction::triggered, this, &MainWindow::stopSerialMonitor);

    m_themeActions = new QActionGroup(this);
    m_themeActions->setExclusive(true);
    m_darkThemeAction = new QAction(tr("&Dark Theme"), m_themeActions);
    m_darkThemeAction->setObjectName(QStringLiteral("darkThemeAction"));
    m_darkThemeAction->setCheckable(true);
    m_lightThemeAction = new QAction(tr("&Light Theme"), m_themeActions);
    m_lightThemeAction->setObjectName(QStringLiteral("lightThemeAction"));
    m_lightThemeAction->setCheckable(true);
    connect(m_darkThemeAction, &QAction::triggered, this, [this]() { applyTheme(Theme::Dark); });
    connect(m_lightThemeAction, &QAction::triggered, this, [this]() { applyTheme(Theme::Light); });

    m_refreshAssetsAction = new QAction(tr("&Refresh Asset Browser"), this);
    m_refreshAssetsAction->setObjectName(QStringLiteral("refreshAssetsAction"));
    connect(m_refreshAssetsAction, &QAction::triggered, this, [this]() {
        if (m_projectModel != nullptr && !m_projectFilePath.isEmpty()) {
            m_projectModel->setRootPath(QString{});
        }
        updateProjectPanel();
        reloadPresentationAssets();
        renderCurrentScene();
        statusBar()->showMessage(tr("Asset browser refreshed"), 3000);
    });
    m_assetImportSettingsAction = new QAction(tr("Edit &Import Settings..."), this);
    m_assetImportSettingsAction->setObjectName(QStringLiteral("assetImportSettingsAction"));
    connect(m_assetImportSettingsAction, &QAction::triggered, this,
            &MainWindow::editSelectedAssetImportSettings);
    m_renameAssetAction = new QAction(tr("&Rename Selected Asset..."), this);
    m_renameAssetAction->setObjectName(QStringLiteral("renameAssetAction"));
    connect(m_renameAssetAction, &QAction::triggered, this, &MainWindow::renameSelectedAsset);
    m_moveAssetAction = new QAction(tr("&Move Selected Asset..."), this);
    m_moveAssetAction->setObjectName(QStringLiteral("moveAssetAction"));
    connect(m_moveAssetAction, &QAction::triggered, this, &MainWindow::moveSelectedAsset);
    m_showPrefabEditorAction = new QAction(tr("Open &Prefab Editor"), this);
    m_showPrefabEditorAction->setObjectName(QStringLiteral("showPrefabEditorAction"));
    connect(m_showPrefabEditorAction, &QAction::triggered, this, [this]() {
        if (auto* dock = findChild<QDockWidget*>(QStringLiteral("prefabEditorDock"))) {
            dock->show();
            dock->raise();
        }
    });
    m_showInputMapAction = new QAction(tr("Open &Input Map Editor"), this);
    m_showInputMapAction->setObjectName(QStringLiteral("showInputMapEditorAction"));
    connect(m_showInputMapAction, &QAction::triggered, this, [this]() {
        if (auto* dock = findChild<QDockWidget*>(QStringLiteral("inputMapEditorDock"))) {
            dock->show();
            dock->raise();
        }
    });
    m_showToolchainSetupAction = new QAction(tr("Toolchain Setup / &Repair"), this);
    m_showToolchainSetupAction->setObjectName(QStringLiteral("showToolchainSetupAction"));
    connect(m_showToolchainSetupAction, &QAction::triggered, this, [this]() {
        if (m_toolchainSetup != nullptr) {
            m_toolchainSetup->setRepositoryRoot(studioRepositoryRoot());
        }
        if (auto* dock = findChild<QDockWidget*>(QStringLiteral("toolchainSetupDock"))) {
            dock->show();
            dock->raise();
        }
    });
    m_renameEntityAction = new QAction(tr("&Rename Selected Entity"), this);
    m_renameEntityAction->setObjectName(QStringLiteral("renameEntityAction"));
    m_renameEntityAction->setShortcut(QKeySequence(Qt::Key_F2));
    connect(m_renameEntityAction, &QAction::triggered, this, [this]() {
        if (m_entityNameEdit != nullptr && m_entityNameEdit->isEnabled()) {
            m_entityNameEdit->setFocus();
            m_entityNameEdit->selectAll();
        }
    });
    m_focusInspectorAction = new QAction(tr("Add / Edit &Components"), this);
    m_focusInspectorAction->setObjectName(QStringLiteral("focusComponentInspectorAction"));
    connect(m_focusInspectorAction, &QAction::triggered, this, [this]() {
        if (auto* dock = findChild<QDockWidget*>(QStringLiteral("inspectorDock"))) {
            dock->show();
            dock->raise();
        }
        if (m_componentInspector != nullptr) {
            m_componentInspector->setFocus(Qt::ShortcutFocusReason);
        }
    });
    m_validateVisualScriptAction = new QAction(tr("Validate Visual &Script"), this);
    m_validateVisualScriptAction->setObjectName(QStringLiteral("validateVisualScriptAction"));
    connect(m_validateVisualScriptAction, &QAction::triggered, this, [this]() {
        if (m_visualScriptEditor != nullptr) {
            m_visualScriptEditor->validateGraph();
        }
        if (auto* dock = findChild<QDockWidget*>(QStringLiteral("visualScriptDock"))) {
            dock->show();
            dock->raise();
        }
    });
    m_validateAnimatorAction = new QAction(tr("Validate &Animator"), this);
    m_validateAnimatorAction->setObjectName(QStringLiteral("validateAnimatorAction"));
    connect(m_validateAnimatorAction, &QAction::triggered, this, [this]() {
        if (m_animatorEditor != nullptr) {
            m_animatorEditor->validateController();
        }
        if (auto* dock = findChild<QDockWidget*>(QStringLiteral("animatorDock"))) {
            dock->show();
            dock->raise();
        }
    });
    m_refreshMemoryAction = new QAction(tr("Refresh &Memory Analysis"), this);
    m_refreshMemoryAction->setObjectName(QStringLiteral("refreshMemoryAnalysisAction"));
    connect(m_refreshMemoryAction, &QAction::triggered, this, [this]() {
        if (m_memoryAnalyzer != nullptr) {
            m_memoryAnalyzer->refresh();
        }
        if (auto* dock = findChild<QDockWidget*>(QStringLiteral("memoryAnalyzerDock"))) {
            dock->show();
            dock->raise();
        }
    });
    m_trustProjectAction = new QAction(tr("Trust Project for Code &Execution"), this);
    m_trustProjectAction->setObjectName(QStringLiteral("trustProjectAction"));
    m_trustProjectAction->setCheckable(true);
    connect(m_trustProjectAction, &QAction::triggered, this, [this](const bool trusted) {
        if (trusted) {
            const auto answer = QMessageBox::warning(
                this, tr("Trust Project"),
                tr("Trusted projects may compile and execute C++, scripts, package hooks, and "
                   "external build commands. Trust this project path only if you know its source."),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes) {
                const QSignalBlocker blocker(m_trustProjectAction);
                m_trustProjectAction->setChecked(false);
                return;
            }
        }
        QString errorMessage;
        if (!setCurrentProjectTrusted(trusted, errorMessage)) {
            QMessageBox::warning(this, tr("Trust Decision Failed"), errorMessage);
        }
    });
    m_manageRecoveryAction = new QAction(tr("&Recovery Sessions..."), this);
    m_manageRecoveryAction->setObjectName(QStringLiteral("manageRecoveryAction"));
    connect(m_manageRecoveryAction, &QAction::triggered, this, &MainWindow::showRecoveryDialog);

    const auto createLayoutAction = [this](const QString& text, const QString& objectName,
                                           const QString& preset) {
        auto* action = new QAction(text, this);
        action->setObjectName(objectName);
        connect(action, &QAction::triggered, this, [this, preset]() { applyLayoutPreset(preset); });
        return action;
    };
    m_defaultLayoutAction = createLayoutAction(
        tr("&Default"), QStringLiteral("defaultLayoutAction"), QStringLiteral("Default"));
    m_layout2DAction =
        createLayoutAction(tr("&2D"), QStringLiteral("layout2DAction"), QStringLiteral("2D"));
    m_layout3DAction =
        createLayoutAction(tr("&3D"), QStringLiteral("layout3DAction"), QStringLiteral("3D"));
    m_scriptingLayoutAction = createLayoutAction(
        tr("&Scripting"), QStringLiteral("scriptingLayoutAction"), QStringLiteral("Scripting"));
    m_animationLayoutAction = createLayoutAction(
        tr("&Animation"), QStringLiteral("animationLayoutAction"), QStringLiteral("Animation"));
    m_profilingLayoutAction = createLayoutAction(
        tr("&Profiling"), QStringLiteral("profilingLayoutAction"), QStringLiteral("Profiling"));
    m_debugLayoutAction = createLayoutAction(tr("De&bug"), QStringLiteral("debugLayoutAction"),
                                             QStringLiteral("Debug"));
    m_saveNamedLayoutAction = new QAction(tr("&Save Current Layout..."), this);
    m_saveNamedLayoutAction->setObjectName(QStringLiteral("saveNamedLayoutAction"));
    connect(m_saveNamedLayoutAction, &QAction::triggered, this, &MainWindow::saveNamedLayoutDialog);
    m_loadNamedLayoutAction = new QAction(tr("&Load Named Layout..."), this);
    m_loadNamedLayoutAction->setObjectName(QStringLiteral("loadNamedLayoutAction"));
    connect(m_loadNamedLayoutAction, &QAction::triggered, this, &MainWindow::loadNamedLayoutDialog);
    m_deleteNamedLayoutAction = new QAction(tr("&Delete Named Layout..."), this);
    m_deleteNamedLayoutAction->setObjectName(QStringLiteral("deleteNamedLayoutAction"));
    connect(m_deleteNamedLayoutAction, &QAction::triggered, this,
            &MainWindow::deleteNamedLayoutDialog);
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
    m_hierarchyView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_hierarchyView->setContextMenuPolicy(Qt::CustomContextMenu);
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
    const auto projectDock = createDock(tr("Project Browser"), QStringLiteral("projectDock"),
                                        m_projectTree, Qt::LeftDockWidgetArea);
    splitDockWidget(hierarchyDock, projectDock, Qt::Vertical);
    m_assetBrowserController = new AssetBrowserController(this);
    m_assetBrowserController->setObjectName(QStringLiteral("assetBrowserController"));
    m_assetTree = new QTreeView(this);
    m_assetTree->setObjectName(QStringLiteral("assetBrowserTree"));
    m_assetTree->setModel(m_assetBrowserController->model());
    m_assetTree->setRootIsDecorated(false);
    m_assetTree->setItemsExpandable(false);
    m_assetTree->setAlternatingRowColors(true);
    m_assetTree->setUniformRowHeights(true);
    m_assetTree->setDragEnabled(true);
    m_assetTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_assetTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_assetTree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_assetTree->header()->setSectionResizeMode(AssetBrowserModel::NameColumn,
                                                QHeaderView::Stretch);
    const auto assetDock = createDock(tr("Asset Browser"), QStringLiteral("assetBrowserDock"),
                                      m_assetTree, Qt::LeftDockWidgetArea);
    tabifyDockWidget(projectDock, assetDock);
    projectDock->raise();

    m_sceneView = new SceneView(this);
    m_sceneView->setObjectName(QStringLiteral("sceneView"));
    m_sceneView->setDocument(&m_sceneDocument);
    m_sceneView->setContextMenuPolicy(Qt::CustomContextMenu);
    const auto sceneDock =
        createDock(tr("Scene"), QStringLiteral("sceneDock"), m_sceneView, Qt::RightDockWidgetArea);

    auto* gamePanel = new QWidget(this);
    auto* gameLayout = new QVBoxLayout(gamePanel);
    gameLayout->setContentsMargins(4, 4, 4, 4);
    auto* gameControls = new QWidget(gamePanel);
    auto* gameControlsLayout = new QGridLayout(gameControls);
    gameControlsLayout->setContentsMargins(0, 0, 0, 0);
    gameControlsLayout->setHorizontalSpacing(6);
    gameControlsLayout->setVerticalSpacing(3);
    m_gameResolutionCombo = new QComboBox(gameControls);
    m_gameResolutionCombo->setObjectName(QStringLiteral("gameResolutionCombo"));
    for (const QSize resolution :
         {QSize(320, 180), QSize(320, 200), QSize(640, 360), QSize(640, 480)}) {
        m_gameResolutionCombo->addItem(
            QStringLiteral("%1×%2").arg(resolution.width()).arg(resolution.height()), resolution);
    }
    m_gameAspectCombo = new QComboBox(gameControls);
    m_gameAspectCombo->setObjectName(QStringLiteral("gameAspectCombo"));
    m_gameAspectCombo->addItem(tr("Preserve"), static_cast<int>(GameView::AspectMode::Preserve));
    m_gameAspectCombo->addItem(tr("Stretch"), static_cast<int>(GameView::AspectMode::Stretch));
    m_gameAspectCombo->addItem(QStringLiteral("4:3"),
                               static_cast<int>(GameView::AspectMode::FourThree));
    m_gameAspectCombo->addItem(QStringLiteral("16:9"),
                               static_cast<int>(GameView::AspectMode::SixteenNine));
    m_gamePaletteCombo = new QComboBox(gameControls);
    m_gamePaletteCombo->setObjectName(QStringLiteral("gamePaletteCombo"));
    m_gamePaletteCombo->addItem(tr("True color"),
                                static_cast<int>(GameView::PaletteMode::TrueColor));
    m_gamePaletteCombo->addItem(tr("ESP32 RGB222"),
                                static_cast<int>(GameView::PaletteMode::Esp32Rgb222));
    m_gamePaletteCombo->addItem(tr("Monochrome"),
                                static_cast<int>(GameView::PaletteMode::Monochrome));
    m_gameFpsCombo = new QComboBox(gameControls);
    m_gameFpsCombo->setObjectName(QStringLiteral("gameFpsCombo"));
    for (const int fps : {30, 60, 120}) {
        m_gameFpsCombo->addItem(tr("%1 FPS").arg(fps), fps);
    }
    m_gameFpsCombo->setCurrentIndex(1);
    m_gameSimulationSpeedCombo = new QComboBox(gameControls);
    m_gameSimulationSpeedCombo->setObjectName(QStringLiteral("gameSimulationSpeedCombo"));
    for (const double speed : {0.25, 0.5, 1.0, 2.0}) {
        m_gameSimulationSpeedCombo->addItem(tr("%1×").arg(speed), speed);
    }
    m_gameSimulationSpeedCombo->setCurrentIndex(2);
    m_gameIntegerScaling = new QCheckBox(tr("Integer scaling"), gameControls);
    m_gameIntegerScaling->setObjectName(QStringLiteral("gameIntegerScalingCheck"));
    m_gameIntegerScaling->setChecked(true);
    m_gamePixelPerfect = new QCheckBox(tr("Pixel-perfect"), gameControls);
    m_gamePixelPerfect->setObjectName(QStringLiteral("gamePixelPerfectCheck"));
    m_gamePixelPerfect->setChecked(true);
    m_gameShowFps = new QCheckBox(tr("FPS overlay"), gameControls);
    m_gameShowFps->setObjectName(QStringLiteral("gameShowFpsCheck"));
    m_gameShowFps->setChecked(true);
    m_gameEsp32Simulation = new QCheckBox(tr("ESP32 simulation"), gameControls);
    m_gameEsp32Simulation->setObjectName(QStringLiteral("gameEsp32SimulationCheck"));
    auto* fullscreenButton = new QPushButton(tr("Fullscreen"), gameControls);
    fullscreenButton->setObjectName(QStringLiteral("gameFullscreenButton"));
    gameControlsLayout->addWidget(new QLabel(tr("Resolution"), gameControls), 0, 0);
    gameControlsLayout->addWidget(m_gameResolutionCombo, 0, 1);
    gameControlsLayout->addWidget(new QLabel(tr("Aspect"), gameControls), 0, 2);
    gameControlsLayout->addWidget(m_gameAspectCombo, 0, 3);
    gameControlsLayout->addWidget(new QLabel(tr("Palette"), gameControls), 0, 4);
    gameControlsLayout->addWidget(m_gamePaletteCombo, 0, 5);
    gameControlsLayout->addWidget(new QLabel(tr("Simulation"), gameControls), 1, 0);
    gameControlsLayout->addWidget(m_gameSimulationSpeedCombo, 1, 1);
    gameControlsLayout->addWidget(m_gameFpsCombo, 1, 2);
    gameControlsLayout->addWidget(m_gameIntegerScaling, 1, 3);
    gameControlsLayout->addWidget(m_gamePixelPerfect, 1, 4);
    gameControlsLayout->addWidget(m_gameShowFps, 1, 5);
    gameControlsLayout->addWidget(m_gameEsp32Simulation, 0, 6);
    gameControlsLayout->addWidget(fullscreenButton, 1, 6);
    m_gameView = new GameView(gamePanel);
    m_gameView->setObjectName(QStringLiteral("gameView"));
    connect(m_gameView, &GameView::runtimeControlChanged, this,
            [this](const QString& control, const float value) {
                if (m_playSession == nullptr) {
                    return;
                }
                const QByteArray encoded = control.toUtf8();
                auto changed = m_playSession->setControlValue(
                    std::string(encoded.constData(), static_cast<std::size_t>(encoded.size())),
                    value);
                if (!changed) {
                    appendConsoleMessage(
                        tr("Runtime input error: %1").arg(engineError(changed.error())));
                    pause();
                }
            });
    gameLayout->addWidget(gameControls);
    gameLayout->addWidget(m_gameView, 1);
    const auto gameDock =
        createDock(tr("Game"), QStringLiteral("gameDock"), gamePanel, Qt::RightDockWidgetArea);
    connect(m_gameResolutionCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) {
                m_gameView->setTargetResolution(m_gameResolutionCombo->currentData().toSize());
                renderCurrentScene();
            });
    connect(m_gameAspectCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        m_gameView->setAspectMode(
            static_cast<GameView::AspectMode>(m_gameAspectCombo->currentData().toInt()));
    });
    connect(m_gamePaletteCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        m_gameView->setPaletteMode(
            static_cast<GameView::PaletteMode>(m_gamePaletteCombo->currentData().toInt()));
        renderCurrentScene();
    });
    connect(m_gameFpsCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        const int fps = m_gameFpsCombo->currentData().toInt();
        m_gameView->setTargetFps(fps);
        if (m_playTimer != nullptr) {
            m_playTimer->setInterval(std::max(1, qRound(1000.0 / static_cast<double>(fps))));
        }
    });
    connect(m_gameSimulationSpeedCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) {
                m_gameView->setSimulationSpeed(
                    m_gameSimulationSpeedCombo->currentData().toDouble());
            });
    connect(m_gameIntegerScaling, &QCheckBox::toggled, m_gameView, &GameView::setIntegerScaling);
    connect(m_gamePixelPerfect, &QCheckBox::toggled, m_gameView, &GameView::setPixelPerfect);
    connect(m_gameShowFps, &QCheckBox::toggled, m_gameView, &GameView::setFpsOverlayVisible);
    connect(m_gameEsp32Simulation, &QCheckBox::toggled, m_gameView,
            &GameView::setEsp32SimulationMode);
    connect(fullscreenButton, &QPushButton::clicked, this, [gameDock, fullscreenButton]() {
        if (gameDock->isFullScreen()) {
            gameDock->showNormal();
            gameDock->setFloating(false);
            fullscreenButton->setText(QObject::tr("Fullscreen"));
        } else {
            gameDock->setFloating(true);
            gameDock->showFullScreen();
            fullscreenButton->setText(QObject::tr("Exit Fullscreen"));
        }
    });
    tabifyDockWidget(sceneDock, gameDock);
    sceneDock->raise();

    auto* inspector = new QWidget(this);
    auto* inspectorLayout = new QVBoxLayout(inspector);
    auto* entityGroup = new QGroupBox(tr("Entity"), inspector);
    auto* entityLayout = new QFormLayout(entityGroup);
    m_entityNameEdit = new QLineEdit(entityGroup);
    m_entityNameEdit->setObjectName(QStringLiteral("entityNameEditor"));
    m_entityActiveCheck = new QCheckBox(tr("Enabled"), entityGroup);
    m_entityActiveCheck->setObjectName(QStringLiteral("entityActiveEditor"));
    m_entityIdLabel = new QLabel(tr("No selection"), entityGroup);
    m_entityIdLabel->setObjectName(QStringLiteral("entityGuidLabel"));
    m_entityIdLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_entityIdLabel->setWordWrap(true);
    entityLayout->addRow(tr("Name"), m_entityNameEdit);
    entityLayout->addRow(tr("Active"), m_entityActiveCheck);
    entityLayout->addRow(tr("Entity GUID"), m_entityIdLabel);
    inspectorLayout->addWidget(entityGroup);

    m_componentInspector = new ComponentInspector(&m_sceneDocument, &m_undoStack, inspector);
    m_componentInspector->setObjectName(QStringLiteral("componentInspector"));
    inspectorLayout->addWidget(m_componentInspector);

    auto* inspectorScroll = new QScrollArea(this);
    inspectorScroll->setObjectName(QStringLiteral("inspectorScrollArea"));
    inspectorScroll->setWidgetResizable(true);
    inspectorScroll->setFrameShape(QFrame::NoFrame);
    inspectorScroll->setWidget(inspector);

    const auto inspectorDock = createDock(tr("Inspector"), QStringLiteral("inspectorDock"),
                                          inspectorScroll, Qt::RightDockWidgetArea);
    splitDockWidget(sceneDock, inspectorDock, Qt::Horizontal);

    m_profiler = new QTableWidget(14, 4, this);
    m_profiler->setObjectName(QStringLiteral("profilerTable"));
    m_profiler->setHorizontalHeaderLabels({tr("Metric"), tr("Source"), tr("Value"), tr("Budget")});
    m_profiler->verticalHeader()->setVisible(false);
    m_profiler->horizontalHeader()->setStretchLastSection(true);
    m_profiler->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_profiler->setSelectionMode(QAbstractItemView::NoSelection);
    const auto profilerDock = createDock(tr("Profiler"), QStringLiteral("profilerDock"), m_profiler,
                                         Qt::RightDockWidgetArea);
    splitDockWidget(inspectorDock, profilerDock, Qt::Vertical);

    auto* devicePanel = new QWidget(this);
    auto* deviceLayout = new QFormLayout(devicePanel);
    m_targetCombo = new QComboBox(devicePanel);
    m_targetCombo->setObjectName(QStringLiteral("buildTargetCombo"));
    m_targetCombo->addItem(tr("PC"), static_cast<int>(BuildTarget::Pc));
    m_targetCombo->addItem(tr("ESP32"), static_cast<int>(BuildTarget::Esp32));
    m_configurationCombo = new QComboBox(devicePanel);
    m_configurationCombo->setObjectName(QStringLiteral("buildConfigurationCombo"));
    m_serialPortCombo = new QComboBox(devicePanel);
    m_serialPortCombo->setObjectName(QStringLiteral("serialPortCombo"));
    m_serialPortCombo->setEditable(true);
    m_serialPortCombo->setInsertPolicy(QComboBox::NoInsert);
    m_serialPortCombo->lineEdit()->setPlaceholderText(tr("COM5 or detected serial port"));
    m_serialPortCombo->addItem(tr("Refresh, then select a port explicitly"));
    m_baudCombo = new QComboBox(devicePanel);
    m_baudCombo->setObjectName(QStringLiteral("serialBaudCombo"));
    m_baudCombo->setEditable(true);
    for (const int baud : {115200, 230400, 460800, 921600}) {
        m_baudCombo->addItem(QString::number(baud), baud);
    }
    m_uploadConfirmation = new QCheckBox(
        tr("I confirm the selected port is an Olimex ESP32-SBC-FabGL Rev.B"), devicePanel);
    m_uploadConfirmation->setObjectName(QStringLiteral("uploadBoardConfirmation"));
    m_workflowProgress = new QProgressBar(devicePanel);
    m_workflowProgress->setObjectName(QStringLiteral("workflowProgress"));
    m_workflowProgress->setRange(0, 1);
    m_workflowProgress->setValue(0);
    m_workflowStatus = new QLabel(tr("Idle"), devicePanel);
    m_workflowStatus->setObjectName(QStringLiteral("workflowStatus"));
    m_workflowStatus->setWordWrap(true);
    m_projectTrustStatus = new QLabel(tr("No project"), devicePanel);
    m_projectTrustStatus->setObjectName(QStringLiteral("projectTrustStatus"));
    m_projectTrustStatus->setWordWrap(true);
    m_projectTargetProfileStatus = new QLabel(tr("No project"), devicePanel);
    m_projectTargetProfileStatus->setObjectName(QStringLiteral("projectTargetProfileStatus"));
    m_projectTargetProfileStatus->setWordWrap(true);
    m_securityModeStatus = new QLabel(devicePanel);
    m_securityModeStatus->setObjectName(QStringLiteral("securityModeStatus"));
    m_securityModeStatus->setText(m_launchOptions.safeMode
                                      ? tr("Safe mode — plugins disabled; telemetry off")
                                      : (m_launchOptions.pluginsEnabled
                                             ? tr("Normal mode — telemetry off")
                                             : tr("Plugins disabled — telemetry off")));
    m_securityModeStatus->setWordWrap(true);

    auto makeActionButton = [devicePanel](QAction* action, const QString& objectName) {
        auto* button = new QPushButton(action->text().remove(QLatin1Char('&')), devicePanel);
        button->setObjectName(objectName);
        QObject::connect(button, &QPushButton::clicked, action, &QAction::trigger);
        QObject::connect(action, &QAction::changed, button, [button, action]() {
            button->setEnabled(action->isEnabled());
            button->setText(action->text().remove(QLatin1Char('&')));
        });
        button->setEnabled(action->isEnabled());
        return button;
    };
    auto* portButtons = new QWidget(devicePanel);
    auto* portButtonsLayout = new QHBoxLayout(portButtons);
    portButtonsLayout->setContentsMargins(0, 0, 0, 0);
    portButtonsLayout->addWidget(
        makeActionButton(m_refreshPortsAction, QStringLiteral("refreshSerialPortsButton")));
    portButtonsLayout->addWidget(
        makeActionButton(m_serialMonitorAction, QStringLiteral("serialMonitorButton")));
    portButtonsLayout->addWidget(
        makeActionButton(m_stopSerialMonitorAction, QStringLiteral("stopSerialMonitorButton")));
    auto* uploadButton = makeActionButton(m_uploadEsp32Action, QStringLiteral("uploadEsp32Button"));
    auto* deployButton =
        makeActionButton(m_deployEsp32Action, QStringLiteral("deployEsp32DiagnosticsButton"));
    auto* trustButton =
        makeActionButton(m_trustProjectAction, QStringLiteral("trustProjectButton"));

    deviceLayout->addRow(tr("Target"), m_targetCombo);
    deviceLayout->addRow(tr("Profile"), m_configurationCombo);
    deviceLayout->addRow(tr("Serial port"), m_serialPortCombo);
    deviceLayout->addRow(tr("Baud"), m_baudCombo);
    deviceLayout->addRow(portButtons);
    deviceLayout->addRow(m_uploadConfirmation);
    deviceLayout->addRow(uploadButton);
    deviceLayout->addRow(deployButton);
    deviceLayout->addRow(tr("Operation"), m_workflowStatus);
    deviceLayout->addRow(m_workflowProgress);
    deviceLayout->addRow(tr("Manifest profile"), m_projectTargetProfileStatus);
    deviceLayout->addRow(tr("Project trust"), m_projectTrustStatus);
    deviceLayout->addRow(trustButton);
    deviceLayout->addRow(tr("Security"), m_securityModeStatus);
    const auto targetDock = createDock(tr("Targets / Device"), QStringLiteral("targetDeviceDock"),
                                       devicePanel, Qt::RightDockWidgetArea);
    tabifyDockWidget(profilerDock, targetDock);

    m_codeEditor = new CodeEditorWidget(this);
    const auto codeDock = createDock(tr("Code Editor"), QStringLiteral("codeEditorDock"),
                                     m_codeEditor, Qt::BottomDockWidgetArea);
    m_console = new QPlainTextEdit(this);
    m_console->setObjectName(QStringLiteral("consoleOutput"));
    m_console->setReadOnly(true);
    m_console->setMaximumBlockCount(5000);
    const auto consoleDock = createDock(tr("Console"), QStringLiteral("consoleDock"), m_console,
                                        Qt::BottomDockWidgetArea);
    m_buildOutput = new DiagnosticOutputEdit(this);
    m_buildOutput->setMaximumBlockCount(10000);
    const auto buildDock = createDock(tr("Build Output"), QStringLiteral("buildOutputDock"),
                                      m_buildOutput, Qt::BottomDockWidgetArea);
    m_serialConsole = new SerialConsoleWidget(this);
    m_serialConsole->setObjectName(QStringLiteral("serialConsole"));
    const auto serialDock = createDock(tr("Serial Monitor"), QStringLiteral("serialMonitorDock"),
                                       m_serialConsole, Qt::BottomDockWidgetArea);
    tabifyDockWidget(codeDock, consoleDock);
    tabifyDockWidget(consoleDock, buildDock);
    tabifyDockWidget(buildDock, serialDock);

    m_visualScriptEditor = new VisualScriptEditorWidget(this);
    const auto visualScriptDock =
        createDock(tr("Visual Script"), QStringLiteral("visualScriptDock"), m_visualScriptEditor,
                   Qt::BottomDockWidgetArea);
    tabifyDockWidget(codeDock, visualScriptDock);
    m_animatorEditor = new AnimatorEditorWidget(this);
    const auto animatorDock = createDock(tr("Animator"), QStringLiteral("animatorDock"),
                                         m_animatorEditor, Qt::RightDockWidgetArea);
    tabifyDockWidget(gameDock, animatorDock);
    m_prefabEditor = new PrefabEditorPanel(&m_sceneDocument, &m_undoStack, this);
    const auto prefabDock = createDock(tr("Prefab Editor"), QStringLiteral("prefabEditorDock"),
                                       m_prefabEditor, Qt::RightDockWidgetArea);
    tabifyDockWidget(animatorDock, prefabDock);
    connect(m_prefabEditor, &PrefabEditorPanel::projectAssetsChanged, this,
            [this](const QVector<ProjectAssetEntry>& assets) {
                m_projectData.assets = assets;
                updateProjectPanel();
                reloadPresentationAssets();
            });
    connect(m_prefabEditor, &PrefabEditorPanel::sceneSelectionRequested, this,
            &MainWindow::selectEntityGuids);
    connect(m_prefabEditor, &PrefabEditorPanel::statusMessage, this,
            [this](const QString& message) {
                statusBar()->showMessage(message, 5000);
                appendConsoleMessage(message);
            });
    m_inputMapEditor = new InputMapEditorWidget(this);
    const auto inputMapDock =
        createDock(tr("Input Map Editor"), QStringLiteral("inputMapEditorDock"), m_inputMapEditor,
                   Qt::RightDockWidgetArea);
    tabifyDockWidget(prefabDock, inputMapDock);
    connect(m_inputMapEditor, &InputMapEditorWidget::projectDataChanged, this,
            [this](const ProjectData& project) {
                if (project.projectGuid != m_projectData.projectGuid) {
                    return;
                }
                m_projectData.inputContexts = project.inputContexts;
                setDocumentModified(true);
            });
    connect(m_inputMapEditor, &InputMapEditorWidget::projectSaved, this,
            [this](const ProjectData& project) {
                if (project.projectGuid != m_projectData.projectGuid) {
                    return;
                }
                m_projectData.inputContexts = project.inputContexts;
            });
    connect(m_inputMapEditor, &InputMapEditorWidget::statusMessage, this,
            [this](const QString& message) {
                statusBar()->showMessage(message, 5000);
                appendConsoleMessage(message);
            });
    m_toolchainSetup = new ToolchainSetupWidget(this);
    m_toolchainSetup->setRepositoryRoot(studioRepositoryRoot());
    const auto toolchainDock =
        createDock(tr("Toolchain Setup"), QStringLiteral("toolchainSetupDock"), m_toolchainSetup,
                   Qt::RightDockWidgetArea);
    tabifyDockWidget(targetDock, toolchainDock);
    connect(m_toolchainSetup, &ToolchainSetupWidget::statusMessage, this,
            [this](const QString& message) {
                statusBar()->showMessage(message, 5000);
                appendConsoleMessage(message);
            });
    m_extensionServicePanel = new ExtensionServicePanel(this);
    const auto extensionDock = createDock(tr("Extensions"), QStringLiteral("extensionServicesDock"),
                                          m_extensionServicePanel, Qt::RightDockWidgetArea);
    tabifyDockWidget(targetDock, extensionDock);
    connect(m_extensionServicePanel, &ExtensionServicePanel::serviceInvocationRequested, this,
            &MainWindow::invokeExtensionService);
    refreshExtensionServices();
    m_memoryAnalyzer = new MemoryAnalyzerWidget(&m_sceneDocument, this);
    const auto memoryDock = createDock(tr("Memory Analyzer"), QStringLiteral("memoryAnalyzerDock"),
                                       m_memoryAnalyzer, Qt::RightDockWidgetArea);
    tabifyDockWidget(profilerDock, memoryDock);

    auto* specialistTabs = new QTabWidget(this);
    specialistTabs->setObjectName(QStringLiteral("specialistEditorTabs"));
    specialistTabs->setDocumentMode(true);
    m_materialEditor = new MaterialEditorWidget(specialistTabs);
    m_particleEditor = new ParticleEditorWidget(specialistTabs);
    m_tilemapEditor = new TilemapEditorWidget(specialistTabs);
    m_raycastMapEditor = new RaycastMapEditorWidget(specialistTabs);
    m_trackEditor = new TrackEditorWidget(specialistTabs);
    m_uiEditor = new UIEditorWidget(specialistTabs);
    m_packageManager = new PackageManagerWidget(specialistTabs);
    m_audioMixerEditor = new AudioMixerEditorWidget(specialistTabs);
    m_profilerTimeline = new ProfilerTimelineWidget(specialistTabs);
    specialistTabs->addTab(m_materialEditor, tr("Material"));
    specialistTabs->addTab(m_particleEditor, tr("Particles"));
    specialistTabs->addTab(m_tilemapEditor, tr("Tilemap"));
    specialistTabs->addTab(m_raycastMapEditor, tr("Raycast Map"));
    specialistTabs->addTab(m_trackEditor, tr("Racer Track"));
    specialistTabs->addTab(m_uiEditor, tr("UI Editor"));
    specialistTabs->addTab(m_packageManager, tr("Packages"));
    specialistTabs->addTab(m_audioMixerEditor, tr("Audio Mixer"));
    specialistTabs->addTab(m_profilerTimeline, tr("Profiler Timeline"));
    const auto specialistDock =
        createDock(tr("Specialist Editors"), QStringLiteral("specialistEditorsDock"),
                   specialistTabs, Qt::BottomDockWidgetArea);
    tabifyDockWidget(visualScriptDock, specialistDock);

    QString profilerError;
    if (!m_profilerTimeline->setBudget(QStringLiteral("pc.frame"), 16.67,
                                       fabgl::ProfilerUnit::Milliseconds, profilerError) ||
        !m_profilerTimeline->setBudget(QStringLiteral("pc.ai"), 1.0,
                                       fabgl::ProfilerUnit::Milliseconds, profilerError) ||
        !m_profilerTimeline->setBudget(QStringLiteral("esp32.frame"), 16.67,
                                       fabgl::ProfilerUnit::Milliseconds, profilerError)) {
        appendConsoleMessage(tr("Profiler timeline budget setup failed: %1").arg(profilerError));
    }
    const auto forwardStatus = [this](const QString& message) {
        statusBar()->showMessage(message, 5000);
        appendConsoleMessage(message);
    };
    connect(m_materialEditor, &MaterialEditorWidget::statusMessage, this, forwardStatus);
    connect(m_particleEditor, &ParticleEditorWidget::statusMessage, this, forwardStatus);
    connect(m_tilemapEditor, &TilemapEditorWidget::statusMessage, this, forwardStatus);
    connect(m_raycastMapEditor, &RaycastMapEditorWidget::statusMessage, this, forwardStatus);
    connect(m_trackEditor, &TrackEditorWidget::statusMessage, this, forwardStatus);
    connect(m_uiEditor, &UIEditorWidget::statusMessage, this, forwardStatus);
    connect(m_audioMixerEditor, &AudioMixerEditorWidget::statusMessage, this, forwardStatus);
    connect(m_profilerTimeline, &ProfilerTimelineWidget::statusMessage, this, forwardStatus);
    connect(m_packageManager, &PackageManagerWidget::commandPrepared, this,
            [this](const QString& program, const QStringList& arguments) {
                if (program.trimmed().isEmpty() || arguments.isEmpty()) {
                    appendConsoleMessage(tr("Package command is incomplete; select a project and "
                                            "provide the required package path or ID."));
                    return;
                }
                startWorkflow({tr("Package Manager"), program, arguments, projectRoot()},
                              WorkflowState::CustomBuild);
            });
    connect(m_visualScriptEditor, &VisualScriptEditorWidget::statusMessage, this,
            [this](const QString& message) {
                statusBar()->showMessage(message, 5000);
                appendConsoleMessage(message);
            });
    connect(m_animatorEditor, &AnimatorEditorWidget::statusMessage, this,
            [this](const QString& message) { statusBar()->showMessage(message, 5000); });
    connect(m_memoryAnalyzer, &MemoryAnalyzerWidget::statusMessage, this,
            [this](const QString& message) { statusBar()->showMessage(message, 5000); });
    connect(m_memoryAnalyzer, &MemoryAnalyzerWidget::performanceProfilesChanged, this,
            [this](const int pcProfile, const int esp32Profile) {
                const auto pc = static_cast<fabgl::project::PerformanceBudgetProfile>(pcProfile);
                const auto esp32 =
                    static_cast<fabgl::project::PerformanceBudgetProfile>(esp32Profile);
                if (m_projectData.performance.pcProfile == pc &&
                    m_projectData.performance.esp32Profile == esp32) {
                    return;
                }
                m_projectData.performance.pcProfile = pc;
                m_projectData.performance.esp32Profile = esp32;
                setDocumentModified(true);
                updateProjectPanel();
                updateProfiler();
            });
    visualScriptDock->hide();
    animatorDock->hide();
    prefabDock->hide();
    inputMapDock->hide();
    toolchainDock->hide();
    if (!m_toolchainSetup->selectedProfileInstalled()) {
        QTimer::singleShot(0, toolchainDock, [toolchainDock]() {
            toolchainDock->show();
            toolchainDock->raise();
        });
    }
    memoryDock->hide();
    specialistDock->hide();
    consoleDock->raise();
}

void MainWindow::createMenusAndToolbars() {
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->setObjectName(QStringLiteral("fileMenu"));
    fileMenu->addAction(m_newAction);
    fileMenu->addAction(m_openAction);
    m_recentProjectsMenu = fileMenu->addMenu(tr("Open &Recent"));
    fileMenu->addSeparator();
    fileMenu->addAction(m_saveAction);
    fileMenu->addAction(m_saveAsAction);
    fileMenu->addAction(m_manageRecoveryAction);
    fileMenu->addSeparator();
    auto* exitAction = fileMenu->addAction(tr("E&xit"));
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    auto* editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->setObjectName(QStringLiteral("editMenu"));
    editMenu->addAction(m_undoAction);
    editMenu->addAction(m_redoAction);
    editMenu->addSeparator();
    editMenu->addAction(m_addEntityAction);
    editMenu->addAction(m_duplicateEntityAction);
    editMenu->addAction(m_deleteEntityAction);
    editMenu->addSeparator();
    editMenu->addActions(m_sceneToolActions->actions());
    editMenu->addActions(m_transformSpaceActions->actions());
    editMenu->addAction(m_frameSelectedAction);
    editMenu->addAction(m_zoomInAction);
    editMenu->addAction(m_zoomOutAction);
    editMenu->addAction(m_snapAction);

    auto* assetsMenu = menuBar()->addMenu(tr("&Assets"));
    assetsMenu->setObjectName(QStringLiteral("assetsMenu"));
    assetsMenu->addAction(m_refreshAssetsAction);
    assetsMenu->addAction(m_assetImportSettingsAction);
    assetsMenu->addAction(m_renameAssetAction);
    assetsMenu->addAction(m_moveAssetAction);
    assetsMenu->addAction(m_showPrefabEditorAction);
    assetsMenu->addSeparator();
    assetsMenu->addAction(
        findChild<QDockWidget*>(QStringLiteral("projectDock"))->toggleViewAction());
    assetsMenu->addAction(
        findChild<QDockWidget*>(QStringLiteral("assetBrowserDock"))->toggleViewAction());

    auto* entityMenu = menuBar()->addMenu(tr("&Entity"));
    entityMenu->setObjectName(QStringLiteral("entityMenu"));
    entityMenu->addAction(m_addEntityAction);
    entityMenu->addAction(m_renameEntityAction);
    entityMenu->addAction(m_duplicateEntityAction);
    entityMenu->addAction(m_deleteEntityAction);
    entityMenu->addSeparator();
    entityMenu->addAction(m_setParentAction);
    entityMenu->addAction(m_clearParentAction);
    entityMenu->addSeparator();
    entityMenu->addActions(m_sceneToolActions->actions());
    entityMenu->addAction(m_frameSelectedAction);

    auto* componentMenu = menuBar()->addMenu(tr("&Component"));
    componentMenu->setObjectName(QStringLiteral("componentMenu"));
    componentMenu->addAction(m_focusInspectorAction);
    componentMenu->addAction(
        findChild<QDockWidget*>(QStringLiteral("inspectorDock"))->toggleViewAction());

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->setObjectName(QStringLiteral("viewMenu"));
    viewMenu->addActions(m_sceneToolActions->actions());
    viewMenu->addAction(m_frameSelectedAction);
    viewMenu->addAction(m_zoomInAction);
    viewMenu->addAction(m_zoomOutAction);
    viewMenu->addAction(m_snapAction);

    auto* playMenu = menuBar()->addMenu(tr("&Play"));
    playMenu->setObjectName(QStringLiteral("playMenu"));
    playMenu->addAction(m_playAction);
    playMenu->addAction(m_pauseAction);
    playMenu->addAction(m_stepAction);
    playMenu->addAction(m_stopAction);
    playMenu->addSeparator();
    playMenu->addAction(m_pcPlayAction);
    playMenu->addAction(m_pcStopAction);

    auto* debugMenu = menuBar()->addMenu(tr("&Debug"));
    debugMenu->setObjectName(QStringLiteral("debugMenu"));
    debugMenu->addAction(m_playAction);
    debugMenu->addAction(m_pauseAction);
    debugMenu->addAction(m_stepAction);
    debugMenu->addAction(m_stopAction);
    debugMenu->addSeparator();
    debugMenu->addAction(
        findChild<QDockWidget*>(QStringLiteral("profilerDock"))->toggleViewAction());
    debugMenu->addAction(
        findChild<QDockWidget*>(QStringLiteral("memoryAnalyzerDock"))->toggleViewAction());
    debugMenu->addAction(m_debugLayoutAction);

    auto* buildMenu = menuBar()->addMenu(tr("&Build"));
    buildMenu->setObjectName(QStringLiteral("buildMenu"));
    buildMenu->addAction(m_buildAction);
    buildMenu->addAction(m_cancelBuildAction);
    buildMenu->addSeparator();
    buildMenu->addAction(m_exportEsp32Action);
    buildMenu->addAction(m_refreshPortsAction);
    buildMenu->addAction(m_uploadEsp32Action);
    buildMenu->addAction(m_deployEsp32Action);
    buildMenu->addAction(m_serialMonitorAction);
    buildMenu->addAction(m_stopSerialMonitorAction);
    buildMenu->addSeparator();
    buildMenu->addAction(m_buildSettingsAction);
    buildMenu->addAction(m_showToolchainSetupAction);

    auto* hardwareDiagnosticsMenu = menuBar()->addMenu(tr("&Hardware Diagnostics"));
    hardwareDiagnosticsMenu->setObjectName(QStringLiteral("hardwareDiagnosticsMenu"));
    hardwareDiagnosticsMenu->addAction(m_deployEsp32Action);
    hardwareDiagnosticsMenu->addSeparator();
    for (auto* action : std::as_const(m_hardwareDiagnosticActions)) {
        hardwareDiagnosticsMenu->addAction(action);
    }

    auto* toolsMenu = menuBar()->addMenu(tr("&Tools"));
    toolsMenu->setObjectName(QStringLiteral("toolsMenu"));
    toolsMenu->addAction(m_validateVisualScriptAction);
    toolsMenu->addAction(m_validateAnimatorAction);
    toolsMenu->addAction(m_refreshMemoryAction);
    toolsMenu->addAction(m_showInputMapAction);
    toolsMenu->addAction(m_showToolchainSetupAction);
    toolsMenu->addSeparator();
    toolsMenu->addAction(m_trustProjectAction);
    toolsMenu->addAction(m_manageRecoveryAction);
    toolsMenu->addSeparator();
    toolsMenu->addAction(m_buildSettingsAction);
    toolsMenu->addAction(m_serialMonitorAction);
    toolsMenu->addAction(m_stopSerialMonitorAction);

    auto* specialistMenu = toolsMenu->addMenu(tr("Specialist &Editors"));
    specialistMenu->setObjectName(QStringLiteral("specialistEditorsMenu"));
    const auto showSpecialist = [this](QWidget* editor) {
        auto* dock = findChild<QDockWidget*>(QStringLiteral("specialistEditorsDock"));
        auto* tabs = findChild<QTabWidget*>(QStringLiteral("specialistEditorTabs"));
        if (dock != nullptr && tabs != nullptr && editor != nullptr) {
            tabs->setCurrentWidget(editor);
            dock->show();
            dock->raise();
        }
    };
    const auto reportEditorFailure = [this](const QString& title, const QString& message) {
        appendConsoleMessage(title + QStringLiteral(": ") + message);
        QMessageBox::warning(this, title, message);
    };
    const auto allowProjectWrite = [this, reportEditorFailure](const QString& path) {
        if (pathInsideRoot(projectRoot(), path)) {
            return true;
        }
        reportEditorFailure(
            tr("Unsafe Output Path"),
            tr("FabGL Studio refuses to write editor assets outside the active project root."));
        return false;
    };
    auto* materialOpen = specialistMenu->addAction(tr("Open &Material..."));
    materialOpen->setObjectName(QStringLiteral("openMaterialEditorAction"));
    connect(materialOpen, &QAction::triggered, this, [this, showSpecialist, reportEditorFailure]() {
        showSpecialist(m_materialEditor);
        const auto path = QFileDialog::getOpenFileName(this, tr("Open Material"), projectRoot(),
                                                       tr("FabGL Materials (*.fglmaterial)"));
        if (path.isEmpty()) {
            return;
        }
        QString errorMessage;
        if (!m_materialEditor->openMaterialFile(path, errorMessage)) {
            reportEditorFailure(tr("Material Open Failed"), errorMessage);
        }
    });
    auto* materialSave = specialistMenu->addAction(tr("Save &Material As..."));
    materialSave->setObjectName(QStringLiteral("saveMaterialEditorAction"));
    connect(materialSave, &QAction::triggered, this,
            [this, showSpecialist, reportEditorFailure, allowProjectWrite]() {
                showSpecialist(m_materialEditor);
                auto path = QFileDialog::getSaveFileName(
                    this, tr("Save Material"),
                    QDir(projectRoot()).filePath(QStringLiteral("Assets/Material.fglmaterial")),
                    tr("FabGL Materials (*.fglmaterial)"));
                if (path.isEmpty()) {
                    return;
                }
                if (!path.endsWith(QStringLiteral(".fglmaterial"), Qt::CaseInsensitive)) {
                    path += QStringLiteral(".fglmaterial");
                }
                if (!allowProjectWrite(path)) {
                    return;
                }
                QString errorMessage;
                if (!m_materialEditor->saveMaterialFile(path, errorMessage)) {
                    reportEditorFailure(tr("Material Save Failed"), errorMessage);
                }
            });
    specialistMenu->addSeparator();
    auto* showParticles = specialistMenu->addAction(tr("&Particle Editor"));
    showParticles->setObjectName(QStringLiteral("showParticleEditorAction"));
    connect(showParticles, &QAction::triggered, this,
            [this, showSpecialist]() { showSpecialist(m_particleEditor); });
    auto* tilemapNew = specialistMenu->addAction(tr("New &Tilemap..."));
    tilemapNew->setObjectName(QStringLiteral("newTilemapEditorAction"));
    connect(tilemapNew, &QAction::triggered, this, [this, showSpecialist, reportEditorFailure]() {
        showSpecialist(m_tilemapEditor);
        bool accepted = false;
        const int width =
            QInputDialog::getInt(this, tr("New Tilemap"), tr("Width"), 32, 1, 512, 1, &accepted);
        if (!accepted) {
            return;
        }
        const int height =
            QInputDialog::getInt(this, tr("New Tilemap"), tr("Height"), 24, 1, 512, 1, &accepted);
        if (!accepted) {
            return;
        }
        QString errorMessage;
        if (!m_tilemapEditor->newMap(static_cast<std::uint32_t>(width),
                                     static_cast<std::uint32_t>(height), errorMessage)) {
            reportEditorFailure(tr("Tilemap Creation Failed"), errorMessage);
        }
    });
    auto* tilemapImport = specialistMenu->addAction(tr("&Import Tilemap..."));
    tilemapImport->setObjectName(QStringLiteral("importTilemapEditorAction"));
    connect(tilemapImport, &QAction::triggered, this,
            [this, showSpecialist, reportEditorFailure]() {
                showSpecialist(m_tilemapEditor);
                const auto path = QFileDialog::getOpenFileName(
                    this, tr("Import Tilemap"), projectRoot(),
                    tr("Tilemaps (*.csv *.json *.fglt *.fgltilemap);;All Files (*)"));
                if (path.isEmpty()) {
                    return;
                }
                QString errorMessage;
                if (!m_tilemapEditor->importTilemapFile(path, errorMessage)) {
                    reportEditorFailure(tr("Tilemap Import Failed"), errorMessage);
                }
            });
    auto* tilemapExport = specialistMenu->addAction(tr("E&xport Tilemap..."));
    tilemapExport->setObjectName(QStringLiteral("exportTilemapEditorAction"));
    connect(tilemapExport, &QAction::triggered, this,
            [this, showSpecialist, reportEditorFailure, allowProjectWrite]() {
                showSpecialist(m_tilemapEditor);
                auto path = QFileDialog::getSaveFileName(
                    this, tr("Export Tilemap"),
                    QDir(projectRoot()).filePath(QStringLiteral("Assets/Tilemap.fgltilemap")),
                    tr("Compiled FabGL Tilemaps (*.fgltilemap)"));
                if (path.isEmpty()) {
                    return;
                }
                if (!path.endsWith(QStringLiteral(".fgltilemap"), Qt::CaseInsensitive)) {
                    path += QStringLiteral(".fgltilemap");
                }
                if (!allowProjectWrite(path)) {
                    return;
                }
                bool accepted = false;
                const int maximumLayer =
                    std::max(1, static_cast<int>(m_tilemapEditor->layerCount()));
                const int layer =
                    QInputDialog::getInt(this, tr("Export Tilemap"), tr("Layer (1-based)"), 1, 1,
                                         maximumLayer, 1, &accepted);
                if (!accepted) {
                    return;
                }
                QString errorMessage;
                if (!m_tilemapEditor->exportTilemapFile(path, static_cast<std::size_t>(layer - 1),
                                                        errorMessage)) {
                    reportEditorFailure(tr("Tilemap Export Failed"), errorMessage);
                }
            });
    specialistMenu->addSeparator();
    auto* raycastOpen = specialistMenu->addAction(tr("Open &Raycast Map..."));
    raycastOpen->setObjectName(QStringLiteral("openRaycastMapEditorAction"));
    connect(raycastOpen, &QAction::triggered, this, [this, showSpecialist, reportEditorFailure]() {
        showSpecialist(m_raycastMapEditor);
        const auto path = QFileDialog::getOpenFileName(this, tr("Open Raycast Map"), projectRoot(),
                                                       tr("FabGL Raycast Maps (*.fglray)"));
        if (path.isEmpty()) {
            return;
        }
        QString errorMessage;
        if (!m_raycastMapEditor->openMapFile(path, errorMessage)) {
            reportEditorFailure(tr("Raycast Map Open Failed"), errorMessage);
        }
    });
    auto* raycastSave = specialistMenu->addAction(tr("Save Raycast Map &As..."));
    raycastSave->setObjectName(QStringLiteral("saveRaycastMapEditorAction"));
    connect(raycastSave, &QAction::triggered, this,
            [this, showSpecialist, reportEditorFailure, allowProjectWrite]() {
                showSpecialist(m_raycastMapEditor);
                auto path = QFileDialog::getSaveFileName(
                    this, tr("Save Raycast Map"),
                    QDir(projectRoot()).filePath(QStringLiteral("Maps/Main.fglray")),
                    tr("FabGL Raycast Maps (*.fglray)"));
                if (path.isEmpty()) {
                    return;
                }
                if (!path.endsWith(QStringLiteral(".fglray"), Qt::CaseInsensitive)) {
                    path += QStringLiteral(".fglray");
                }
                if (!allowProjectWrite(path)) {
                    return;
                }
                QString errorMessage;
                if (!m_raycastMapEditor->saveMapFile(path, errorMessage)) {
                    reportEditorFailure(tr("Raycast Map Save Failed"), errorMessage);
                }
            });
    auto* trackOpen = specialistMenu->addAction(tr("Open Racer &Track..."));
    trackOpen->setObjectName(QStringLiteral("openTrackEditorAction"));
    connect(trackOpen, &QAction::triggered, this, [this, showSpecialist, reportEditorFailure]() {
        showSpecialist(m_trackEditor);
        const auto path = QFileDialog::getOpenFileName(this, tr("Open Racer Track"), projectRoot(),
                                                       tr("FabGL Racer Tracks (*.fgltrack)"));
        if (path.isEmpty()) {
            return;
        }
        QString errorMessage;
        if (!m_trackEditor->openTrackFile(path, errorMessage)) {
            reportEditorFailure(tr("Track Open Failed"), errorMessage);
        }
    });
    auto* trackSave = specialistMenu->addAction(tr("Save Racer Track &As..."));
    trackSave->setObjectName(QStringLiteral("saveTrackEditorAction"));
    connect(trackSave, &QAction::triggered, this,
            [this, showSpecialist, reportEditorFailure, allowProjectWrite]() {
                showSpecialist(m_trackEditor);
                auto path = QFileDialog::getSaveFileName(
                    this, tr("Save Racer Track"),
                    QDir(projectRoot()).filePath(QStringLiteral("Tracks/Main.fgltrack")),
                    tr("FabGL Racer Tracks (*.fgltrack)"));
                if (path.isEmpty()) {
                    return;
                }
                if (!path.endsWith(QStringLiteral(".fgltrack"), Qt::CaseInsensitive)) {
                    path += QStringLiteral(".fgltrack");
                }
                if (!allowProjectWrite(path)) {
                    return;
                }
                QString errorMessage;
                if (!m_trackEditor->saveTrackFile(path, errorMessage)) {
                    reportEditorFailure(tr("Track Save Failed"), errorMessage);
                }
            });
    specialistMenu->addSeparator();
    const auto addShowAction = [specialistMenu, this, showSpecialist](const QString& text,
                                                                      const QString& objectName,
                                                                      QWidget* editor) {
        auto* action = specialistMenu->addAction(text);
        action->setObjectName(objectName);
        connect(action, &QAction::triggered, this,
                [showSpecialist, editor]() { showSpecialist(editor); });
    };
    addShowAction(tr("Package &Manager"), QStringLiteral("showPackageManagerAction"),
                  m_packageManager);
    addShowAction(tr("&UI Editor"), QStringLiteral("showUiEditorAction"), m_uiEditor);
    addShowAction(tr("&Audio Mixer"), QStringLiteral("showAudioMixerEditorAction"),
                  m_audioMixerEditor);
    addShowAction(tr("Profiler &Timeline"), QStringLiteral("showProfilerTimelineAction"),
                  m_profilerTimeline);

    auto* windowMenu = menuBar()->addMenu(tr("&Window"));
    windowMenu->setObjectName(QStringLiteral("windowMenu"));
    m_panelsMenu = windowMenu->addMenu(tr("&Panels"));
    m_panelsMenu->setObjectName(QStringLiteral("panelsMenu"));
    for (auto* dock : std::as_const(m_docks)) {
        m_panelsMenu->addAction(dock->toggleViewAction());
    }
    auto* layoutsMenu = windowMenu->addMenu(tr("&Layouts"));
    layoutsMenu->setObjectName(QStringLiteral("layoutsMenu"));
    layoutsMenu->addAction(m_defaultLayoutAction);
    layoutsMenu->addAction(m_layout2DAction);
    layoutsMenu->addAction(m_layout3DAction);
    layoutsMenu->addAction(m_scriptingLayoutAction);
    layoutsMenu->addAction(m_animationLayoutAction);
    layoutsMenu->addAction(m_profilingLayoutAction);
    layoutsMenu->addAction(m_debugLayoutAction);
    layoutsMenu->addSeparator();
    m_customLayoutsMenu = layoutsMenu->addMenu(tr("Named Layouts"));
    m_customLayoutsMenu->setObjectName(QStringLiteral("customLayoutsMenu"));
    layoutsMenu->addAction(m_loadNamedLayoutAction);
    layoutsMenu->addAction(m_saveNamedLayoutAction);
    layoutsMenu->addAction(m_deleteNamedLayoutAction);
    auto* themeMenu = windowMenu->addMenu(tr("&Theme"));
    themeMenu->setObjectName(QStringLiteral("themeMenu"));
    themeMenu->addAction(m_darkThemeAction);
    themeMenu->addAction(m_lightThemeAction);

    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->setObjectName(QStringLiteral("helpMenu"));
    auto* aboutAction = helpMenu->addAction(tr("&About FabGL Studio"));
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(
            this, tr("About FabGL Studio"),
            tr("FabGL Studio %1\n\nQt 6 authoring, native FabGL engine scenes, portable "
               "framebuffer preview, and ESP32 budgeting.")
                .arg(QStringLiteral(FABGL_STUDIO_VERSION)));
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

    auto* sceneToolBar = addToolBar(tr("Scene Tools"));
    sceneToolBar->setObjectName(QStringLiteral("sceneToolBar"));
    m_sceneViewModeCombo = new QComboBox(sceneToolBar);
    m_sceneViewModeCombo->setObjectName(QStringLiteral("sceneViewModeCombo"));
    m_sceneViewModeCombo->addItem(tr("2D"), static_cast<int>(SceneView::ViewMode::TwoDimensional));
    m_sceneViewModeCombo->addItem(tr("Raycast Map"),
                                  static_cast<int>(SceneView::ViewMode::RaycastMap));
    m_sceneViewModeCombo->addItem(tr("3D"),
                                  static_cast<int>(SceneView::ViewMode::ThreeDimensional));
    sceneToolBar->addWidget(m_sceneViewModeCombo);
    sceneToolBar->addActions(m_sceneToolActions->actions());
    sceneToolBar->addActions(m_transformSpaceActions->actions());
    sceneToolBar->addSeparator();
    sceneToolBar->addAction(m_frameSelectedAction);
    sceneToolBar->addAction(m_zoomInAction);
    sceneToolBar->addAction(m_zoomOutAction);
    sceneToolBar->addAction(m_snapAction);
    connect(m_sceneViewModeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](const int) {
                m_sceneView->setViewMode(
                    static_cast<SceneView::ViewMode>(m_sceneViewModeCombo->currentData().toInt()));
            });

    auto* playToolBar = addToolBar(tr("Simulation"));
    playToolBar->setObjectName(QStringLiteral("simulationToolBar"));
    playToolBar->addAction(m_playAction);
    playToolBar->addAction(m_pauseAction);
    playToolBar->addAction(m_stepAction);
    playToolBar->addAction(m_stopAction);
    playToolBar->addSeparator();
    playToolBar->addAction(m_buildAction);
    playToolBar->addAction(m_cancelBuildAction);

    auto* targetToolBar = addToolBar(tr("Target"));
    targetToolBar->setObjectName(QStringLiteral("targetToolBar"));
    m_toolbarTargetCombo = new QComboBox(targetToolBar);
    m_toolbarTargetCombo->setObjectName(QStringLiteral("toolbarBuildTargetCombo"));
    m_toolbarTargetCombo->addItem(tr("PC"), static_cast<int>(BuildTarget::Pc));
    m_toolbarTargetCombo->addItem(tr("ESP32"), static_cast<int>(BuildTarget::Esp32));
    m_toolbarConfigurationCombo = new QComboBox(targetToolBar);
    m_toolbarConfigurationCombo->setObjectName(QStringLiteral("toolbarBuildConfigurationCombo"));
    targetToolBar->addWidget(new QLabel(tr("Target:"), targetToolBar));
    targetToolBar->addWidget(m_toolbarTargetCombo);
    targetToolBar->addWidget(new QLabel(tr("Profile:"), targetToolBar));
    targetToolBar->addWidget(m_toolbarConfigurationCombo);
    targetToolBar->addSeparator();
    targetToolBar->addAction(m_pcPlayAction);
    targetToolBar->addAction(m_pcStopAction);
    targetToolBar->addAction(m_exportEsp32Action);
    targetToolBar->addAction(m_uploadEsp32Action);
    targetToolBar->addAction(m_serialMonitorAction);

    auto* layoutToolBar = addToolBar(tr("Layout"));
    layoutToolBar->setObjectName(QStringLiteral("layoutToolBar"));
    layoutToolBar->addWidget(new QLabel(tr("Layout:"), layoutToolBar));
    m_layoutPresetCombo = new QComboBox(layoutToolBar);
    m_layoutPresetCombo->setObjectName(QStringLiteral("layoutPresetCombo"));
    m_layoutPresetCombo->addItems({QStringLiteral("Default"), QStringLiteral("2D"),
                                   QStringLiteral("3D"), QStringLiteral("Scripting"),
                                   QStringLiteral("Animation"), QStringLiteral("Profiling"),
                                   QStringLiteral("Debug")});
    layoutToolBar->addWidget(m_layoutPresetCombo);
    layoutToolBar->addAction(m_saveNamedLayoutAction);
    connect(m_layoutPresetCombo, &QComboBox::currentTextChanged, this,
            &MainWindow::applyLayoutPreset);
    updateTargetConfiguration();
    rebuildRecentProjectsMenu();
    rebuildCustomLayoutsMenu();
}

void MainWindow::connectEditorSignals() {
    connect(m_hierarchyView->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current, const QModelIndex&) { updateInspector(current); });
    connect(m_hierarchyView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this]() {
                QStringList guids;
                const auto rows = m_hierarchyView->selectionModel()->selectedRows();
                guids.reserve(rows.size());
                for (const auto& row : rows) {
                    const auto id = m_entities.entityIdAt(row.row());
                    if (id) {
                        guids.push_back(SceneDocument::guidString(*id));
                    }
                }
                const auto current = m_hierarchyView->currentIndex();
                const auto currentId =
                    current.isValid() ? m_entities.entityIdAt(current.row()) : std::nullopt;
                if (currentId) {
                    const auto primary = SceneDocument::guidString(*currentId);
                    guids.removeAll(primary);
                    guids.push_back(primary);
                }
                m_sceneView->setSelectedEntities(guids);
                if (m_prefabEditor != nullptr) {
                    m_prefabEditor->setSelectedEntity(currentId);
                    m_prefabEditor->setSelectedInstanceRoot(currentId);
                }
                updateInspector(current);
                updateRunActions();
            });
    connect(m_sceneView, &SceneView::entitiesSelected, this, &MainWindow::selectEntityGuids);
    connect(m_hierarchyView, &QWidget::customContextMenuRequested, this,
            [this](const QPoint& position) {
                const auto index = m_hierarchyView->indexAt(position);
                if (index.isValid() && !m_hierarchyView->selectionModel()->isSelected(index)) {
                    selectEntityRow(index.row());
                }
                QMenu menu(m_hierarchyView);
                menu.addAction(m_addEntityAction);
                menu.addSeparator();
                menu.addAction(m_duplicateEntityAction);
                menu.addAction(m_renameEntityAction);
                menu.addAction(m_deleteEntityAction);
                menu.addSeparator();
                menu.addAction(m_setParentAction);
                menu.addAction(m_clearParentAction);
                menu.addSeparator();
                menu.addAction(m_showPrefabEditorAction);
                menu.exec(m_hierarchyView->viewport()->mapToGlobal(position));
            });
    connect(m_sceneView, &QWidget::customContextMenuRequested, this,
            [this](const QPoint& position) {
                QMenu menu(m_sceneView);
                menu.addAction(m_addEntityAction);
                menu.addSeparator();
                menu.addAction(m_duplicateEntityAction);
                menu.addAction(m_deleteEntityAction);
                menu.addSeparator();
                menu.addAction(m_setParentAction);
                menu.addAction(m_clearParentAction);
                menu.addSeparator();
                menu.addActions(m_sceneToolActions->actions());
                menu.exec(m_sceneView->mapToGlobal(position));
            });
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
    connect(m_sceneView, &SceneView::entityRotationPreview, this,
            [this](const QString& guid, const float x, const float y, const float z) {
                const auto id = SceneDocument::parseEntityGuid(guid);
                if (id && m_runState == RunState::Editing) {
                    m_sceneDocument.previewRotation(*id, {x, y, z});
                }
            });
    connect(m_sceneView, &SceneView::entityRotationCommitted, this,
            [this](const QString& guid, const float oldX, const float oldY, const float oldZ,
                   const float newX, const float newY, const float newZ) {
                const auto id = SceneDocument::parseEntityGuid(guid);
                const auto current = id ? m_sceneDocument.snapshot(*id) : std::nullopt;
                if (!current || m_runState != RunState::Editing) {
                    return;
                }
                auto before = *current;
                auto after = *current;
                before.rotation = {oldX, oldY, oldZ};
                after.rotation = {newX, newY, newZ};
                m_sceneDocument.previewRotation(*id, before.rotation);
                pushEntityEdit(before, after, tr("Rotate %1").arg(after.name));
            });
    connect(m_sceneView, &SceneView::entityScalePreview, this,
            [this](const QString& guid, const float x, const float y, const float z) {
                const auto id = SceneDocument::parseEntityGuid(guid);
                if (id && m_runState == RunState::Editing) {
                    m_sceneDocument.previewScale(*id, {x, y, z});
                }
            });
    connect(m_sceneView, &SceneView::entityScaleCommitted, this,
            [this](const QString& guid, const float oldX, const float oldY, const float oldZ,
                   const float newX, const float newY, const float newZ) {
                const auto id = SceneDocument::parseEntityGuid(guid);
                const auto current = id ? m_sceneDocument.snapshot(*id) : std::nullopt;
                if (!current || m_runState != RunState::Editing) {
                    return;
                }
                auto before = *current;
                auto after = *current;
                before.scale = {oldX, oldY, oldZ};
                after.scale = {newX, newY, newZ};
                m_sceneDocument.previewScale(*id, before.scale);
                pushEntityEdit(before, after, tr("Scale %1").arg(after.name));
            });
    connect(m_sceneView, &SceneView::assetDropped, this, &MainWindow::addEntityFromAsset);
    connect(m_snapAction, &QAction::toggled, m_sceneView, &SceneView::setSnapEnabled);

    connect(m_entityNameEdit, &QLineEdit::editingFinished, this, &MainWindow::renameSelectedEntity);
    connect(m_entityActiveCheck, &QCheckBox::toggled, this, &MainWindow::changeSelectedActive);
    connect(m_componentInspector, &ComponentInspector::statusMessage, this,
            [this](const QString& message) { statusBar()->showMessage(message, 5000); });

    connect(&m_sceneDocument, &SceneDocument::entityChanged, this, [this](const QString& guid) {
        const auto selected = selectedEntityIds();
        if (std::any_of(selected.cbegin(), selected.cend(), [&guid](const auto entityId) {
                return SceneDocument::guidString(entityId) == guid;
            })) {
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
    connect(m_assetBrowserController, &AssetBrowserController::diagnosticRaised, this,
            [this](const QString& relativePath, const QString& message) {
                const QString diagnostic =
                    relativePath.isEmpty()
                        ? tr("Asset Browser: %1").arg(message)
                        : tr("Asset Browser [%1]: %2").arg(relativePath, message);
                appendConsoleMessage(diagnostic);
                statusBar()->showMessage(diagnostic, 6000);
            });
    connect(m_assetBrowserController, &AssetBrowserController::assetDiscovered, this,
            [this](const QString& guid, const QString& relativePath, const QString& type) {
                appendConsoleMessage(
                    tr("Asset Browser discovered %1 (%2, GUID %3); its stable mapping is stored "
                       "in the project asset index.")
                        .arg(relativePath, type, guid));
            });
    connect(m_assetBrowserController, &AssetBrowserController::assetMappingMoved, this,
            [this](const QString& guid, const QString& oldRelativePath,
                   const QString& newRelativePath) {
                bool manifestChanged = false;
                for (auto& asset : m_projectData.assets) {
                    if (asset.guid.compare(guid, Qt::CaseInsensitive) == 0 &&
                        asset.path.compare(newRelativePath, Qt::CaseInsensitive) != 0) {
                        asset.path = newRelativePath;
                        manifestChanged = true;
                    }
                }
                if (manifestChanged) {
                    setDocumentModified(true);
                }
                appendConsoleMessage(tr("Asset Browser preserved GUID %1 while mapping %2 to %3.")
                                         .arg(guid, oldRelativePath, newRelativePath));
            });
    connect(
        m_assetBrowserController, &AssetBrowserController::storageBudgetExceeded, this,
        [this](const QString& storage, const qulonglong usedBytes, const qulonglong budgetBytes) {
            const bool error = budgetBytes == 0U || usedBytes > budgetBytes + budgetBytes / 4U;
            appendConsoleMessage(
                tr("Asset Browser %1: %2 estimate exceeds budget (%3 / %4 bytes). "
                   "Downscale/compress the asset or move eligible payloads to streaming "
                   "storage.")
                    .arg(error ? tr("ERROR") : tr("WARNING"))
                    .arg(storage)
                    .arg(usedBytes)
                    .arg(budgetBytes));
        });
    connect(m_assetBrowserController, &AssetBrowserController::refreshed, this,
            [this](const qulonglong, const int imported, const int cacheHits, const int errors) {
                statusBar()->showMessage(
                    tr("Asset Browser refreshed: %1 imported, %2 cache hits, %3 errors")
                        .arg(imported)
                        .arg(cacheHits)
                        .arg(errors),
                    4000);
                if (m_memoryAnalyzer != nullptr) {
                    std::uint64_t flash = 0U;
                    std::uint64_t internalRam = 0U;
                    std::uint64_t psram = 0U;
                    std::uint64_t sd = 0U;
                    for (const auto& entry : m_assetBrowserController->model()->entries()) {
                        flash += entry.esp32Cost.flashBytes;
                        internalRam += entry.esp32Cost.internalRamBytes;
                        psram += entry.esp32Cost.psramBytes;
                        sd += entry.esp32Cost.sdBytes;
                    }
                    m_memoryAnalyzer->setEsp32StorageEstimates(flash, internalRam, psram, sd);
                }
                updateProfiler();
            });
    connect(m_assetTree, &QTreeView::customContextMenuRequested, this,
            [this](const QPoint& position) {
                const auto index = m_assetTree->indexAt(position);
                if (index.isValid()) {
                    m_assetTree->setCurrentIndex(index);
                }
                const bool hasAsset = !selectedAssetPath().isEmpty();
                m_assetImportSettingsAction->setEnabled(hasAsset);
                m_renameAssetAction->setEnabled(hasAsset);
                m_moveAssetAction->setEnabled(hasAsset);
                QMenu menu(m_assetTree);
                menu.addAction(m_assetImportSettingsAction);
                menu.addSeparator();
                menu.addAction(m_renameAssetAction);
                menu.addAction(m_moveAssetAction);
                menu.addSeparator();
                menu.addAction(m_refreshAssetsAction);
                menu.exec(m_assetTree->viewport()->mapToGlobal(position));
            });
    connect(m_assetTree->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this]() { updateRunActions(); });
    connect(m_codeEditor, &CodeEditorWidget::statusMessage, this, [this](const QString& message) {
        statusBar()->showMessage(message, 5000);
        appendConsoleMessage(message);
    });
    connect(m_codeEditor, &CodeEditorWidget::fileSaved, this,
            [this](const QString& filePath) { nativeGameplaySourceChanged(filePath, false); });
    connect(m_codeEditor, &CodeEditorWidget::externalFileChanged, this,
            [this](const QString& filePath, const bool localChangesKept) {
                nativeGameplaySourceChanged(filePath, localChangesKept);
            });
    connect(m_buildOutput, &DiagnosticOutputEdit::diagnosticActivated, this,
            &MainWindow::navigateDiagnostic);

    connect(m_playTimer, &QTimer::timeout, this, &MainWindow::tickPlayMode);
    connect(&m_buildRunner, &BuildRunner::classifiedOutputReady, this,
            [this](const QString& text, const BuildOutputSeverity severity) {
                m_buildOutput->appendOutput(text, severity);
                if (text.contains(QStringLiteral("FABGLSTUDIO|1|"))) {
                    m_serialConsole->appendChunk(text, severity == BuildOutputSeverity::Error);
                    recordSerialProfilerMetrics(text);
                }
            });
    connect(&m_buildRunner, &BuildRunner::buildStarted, this,
            [this](const QString& program, const QStringList& arguments,
                   const QString& workingDirectory) {
                appendBuildOutput(tr("\n> %1\nWorking directory: %2\n")
                                      .arg(commandForDisplay(program, arguments),
                                           QDir::toNativeSeparators(workingDirectory)),
                                  false);
                statusBar()->showMessage(tr("Workflow running..."));
            });
    connect(&m_buildRunner, &BuildRunner::buildFinished, this, &MainWindow::workflowFinished);
    connect(&m_buildRunner, &BuildRunner::runningChanged, this, [this](const bool running) {
        updateBuildActions(running);
        updateWorkflowActions();
    });

    connect(&m_pcRunner, &BuildRunner::outputReady, this, &MainWindow::appendBuildOutput);
    connect(&m_pcRunner, &BuildRunner::buildStarted, this,
            [this](const QString& program, const QStringList& arguments,
                   const QString& workingDirectory) {
                appendBuildOutput(tr("\n===== PC player =====\n> %1\nWorking directory: %2\n")
                                      .arg(commandForDisplay(program, arguments),
                                           QDir::toNativeSeparators(workingDirectory)),
                                  false);
                appendConsoleMessage(tr("PC player started."));
            });
    connect(&m_pcRunner, &BuildRunner::buildFinished, this,
            [this](const int exitCode, const QProcess::ExitStatus exitStatus) {
                const bool stoppedForNativeRestart =
                    m_previewRestartController.phase() ==
                        PreviewRestartController::Phase::StoppingPreview &&
                    m_previewRestartController.target() == PreviewKind::ExternalPlayer;
                const bool failed = exitStatus == QProcess::CrashExit || exitCode != 0;
                if (stoppedForNativeRestart) {
                    appendBuildOutput(
                        tr("PC player stopped for native gameplay rebuild (exit code %1).\n")
                            .arg(exitCode),
                        false);
                    appendConsoleMessage(
                        tr("PC player stopped; rebuilding the verified native gameplay module."));
                    performPreviewRestartAction(m_previewRestartController.previewStopped());
                } else {
                    appendBuildOutput(failed
                                          ? tr("PC player failed (exit code %1).\n").arg(exitCode)
                                          : tr("PC player stopped.\n"),
                                      failed);
                    appendConsoleMessage(
                        failed ? tr("PC player failed with exit code %1.").arg(exitCode)
                               : tr("PC player stopped."));
                }
                updateWorkflowActions();
            });
    connect(&m_pcRunner, &BuildRunner::runningChanged, this, [this](const bool) {
        updateRunActions();
        updateWorkflowActions();
    });

    connect(&m_serialRunner, &BuildRunner::outputReady, this, &MainWindow::appendBuildOutput);
    connect(&m_serialRunner, &BuildRunner::outputReady, m_serialConsole,
            &SerialConsoleWidget::appendChunk);
    connect(&m_serialRunner, &BuildRunner::outputReady, this,
            [this](const QString& text, const bool standardError) {
                if (!standardError) {
                    recordSerialProfilerMetrics(text);
                }
            });
    connect(&m_serialRunner, &BuildRunner::buildStarted, this,
            [this](const QString& program, const QStringList& arguments,
                   const QString& workingDirectory) {
                appendBuildOutput(tr("\n===== Serial monitor (read/write serial, never upload) "
                                     "=====\n> %1\nWorking directory: %2\n")
                                      .arg(commandForDisplay(program, arguments),
                                           QDir::toNativeSeparators(workingDirectory)),
                                  false);
                m_workflowStatus->setText(tr("Serial monitor: %1").arg(selectedSerialPort()));
            });
    connect(&m_serialRunner, &BuildRunner::buildFinished, this,
            [this](const int exitCode, const QProcess::ExitStatus exitStatus) {
                const bool failed = exitStatus == QProcess::CrashExit || exitCode != 0;
                appendBuildOutput(failed
                                      ? tr("Serial monitor failed (exit code %1).\n").arg(exitCode)
                                      : tr("Serial monitor stopped.\n"),
                                  failed);
                m_serialConsole->flushPending();
                m_workflowStatus->setText(failed ? tr("Serial monitor failed") : tr("Idle"));
                updateWorkflowActions();
            });
    connect(&m_serialRunner, &BuildRunner::runningChanged, this, [this](const bool running) {
        m_serialConsole->setConnected(running);
        updateWorkflowActions();
    });
    connect(m_serialConsole, &SerialConsoleWidget::sendRequested, this,
            [this](const QByteArray& bytes) {
                const auto written = m_serialRunner.writeInput(bytes);
                if (written != bytes.size()) {
                    const auto message =
                        tr("Serial write was rejected or incomplete (%1/%2 bytes).")
                            .arg(written)
                            .arg(bytes.size());
                    appendBuildOutput(message + QLatin1Char('\n'), true);
                    statusBar()->showMessage(message, 5000);
                }
            });
    connect(m_serialConsole, &SerialConsoleWidget::statusMessage, this,
            [this](const QString& message) {
                statusBar()->showMessage(message, 5000);
                appendConsoleMessage(message);
            });

    connect(&m_portDetector, &BuildRunner::outputReady, this,
            [this](const QString& text, const bool standardError) {
                if (standardError) {
                    appendBuildOutput(text, true);
                }
            });
    connect(&m_portDetector, &BuildRunner::buildFinished, this, &MainWindow::portDetectionFinished);
    connect(&m_portDetector, &BuildRunner::runningChanged, this,
            [this](const bool) { updateWorkflowActions(); });

    connect(m_targetCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](const int) { updateTargetConfiguration(); });
    connect(m_toolbarTargetCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](const int index) { m_targetCombo->setCurrentIndex(index); });
    connect(m_configurationCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](const int index) {
                const QSignalBlocker blocker(m_toolbarConfigurationCombo);
                m_toolbarConfigurationCombo->setCurrentIndex(index);
                updateWorkflowActions();
            });
    connect(m_toolbarConfigurationCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](const int index) { m_configurationCombo->setCurrentIndex(index); });
    connect(m_serialPortCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](const int) {
                m_uploadConfirmation->setChecked(false);
                updateWorkflowActions();
            });
    connect(m_serialPortCombo, &QComboBox::editTextChanged, this, [this](const QString&) {
        m_uploadConfirmation->setChecked(false);
        updateWorkflowActions();
    });
    connect(m_uploadConfirmation, &QCheckBox::toggled, this,
            [this](const bool) { updateWorkflowActions(); });
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
    ProjectCreationDialog dialog(this);
    dialog.setInitialParentDirectory(initialDirectory);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const auto filePath = dialog.createdProjectPath();
    ProjectData projectData;
    QString errorMessage;
    if (!ProjectDocument::load(filePath, projectData, errorMessage)) {
        QMessageBox::critical(this, tr("Created Project Validation Failed"), errorMessage);
        return;
    }
    const auto scenePath = ProjectDocument::absoluteScenePath(filePath, projectData);
    SceneDocument stagedScene;
    if (!stagedScene.load(scenePath, errorMessage)) {
        QMessageBox::critical(this, tr("Created Project Validation Failed"), errorMessage);
        return;
    }
    QString extensionError;
    if (!deactivateProjectExtensions(extensionError)) {
        QMessageBox::critical(
            this, tr("Created Project Activation Failed"),
            tr("The previous project's extensions could not close: %1").arg(extensionError));
        return;
    }
    if (!m_sceneDocument.load(scenePath, errorMessage)) {
        QMessageBox::critical(this, tr("Created Project Validation Failed"), errorMessage);
        return;
    }
    applyLoadedProject(filePath, projectData);
    QString trustError;
    if (!setCurrentProjectTrusted(true, trustError)) {
        appendConsoleMessage(tr("Could not trust the newly created project: %1").arg(trustError));
    }
    addRecentProject(filePath);
    settings.setValue(QStringLiteral("project/lastDirectory"), QFileInfo(filePath).absolutePath());
    appendConsoleMessage(
        tr("Created template project with manifest v2, scene, input defaults and C++ build glue "
           "at %1.")
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
    QString errorMessage;
    if (openProjectPath(filePath, errorMessage)) {
        return true;
    }
    if (!errorMessage.isEmpty()) {
        QMessageBox::critical(this, tr("Open Project Failed"), errorMessage);
    }
    return false;
}

bool MainWindow::openProjectPath(const QString& filePath, QString& errorMessage) {
    if (!maybeSave()) {
        errorMessage =
            tr("Opening the project was cancelled because the current document was not saved.");
        return false;
    }
    if (m_runState != RunState::Editing) {
        stop();
    }
    const auto absolutePath = QFileInfo(filePath).absoluteFilePath();
    ProjectData projectData;
    if (!ProjectDocument::load(absolutePath, projectData, errorMessage)) {
        return false;
    }

    // Validate the incoming scene without mutating the current document. Native extension
    // activation and project-open hooks therefore cannot leave a half-switched editor behind.
    SceneDocument stagedScene;
    const auto incomingScenePath = ProjectDocument::absoluteScenePath(absolutePath, projectData);
    if (!stagedScene.load(incomingScenePath, errorMessage)) {
        return false;
    }

    QString previousExtensionError;
    if (!deactivateProjectExtensions(previousExtensionError)) {
        errorMessage = tr("The current project's extensions could not close cleanly: %1")
                           .arg(previousExtensionError);
        return false;
    }

    std::unique_ptr<fabgl::project::ProjectExtensionModules> incomingExtensions;
    std::unique_ptr<fabgl::project::ProjectExtensionServiceHost> incomingExtensionServices;
    std::string incomingManifestPath;
    std::string incomingProjectRoot;
    if (!loadProjectExtensions(absolutePath, projectData, stagedScene.scene(), incomingExtensions,
                               incomingExtensionServices, incomingManifestPath, incomingProjectRoot,
                               errorMessage)) {
        return false;
    }

    if (!m_sceneDocument.load(incomingScenePath, errorMessage)) {
        auto context =
            extensionHostContext(incomingManifestPath, incomingProjectRoot, stagedScene.scene());
        static_cast<void>(
            incomingExtensions->invokeAll(fabgl::project::ProjectCloseExtensionCapability,
                                          {"close", "scene-commit-failed", &context}));
        incomingExtensions->deactivate();
        return false;
    }
    applyLoadedProject(absolutePath, projectData);
    m_extensionProjectManifestPath = std::move(incomingManifestPath);
    m_extensionProjectRoot = std::move(incomingProjectRoot);
    m_projectExtensions = std::move(incomingExtensions);
    m_projectExtensionServices = std::move(incomingExtensionServices);
    refreshExtensionServices();
    if (m_assetBrowserController != nullptr)
        m_assetBrowserController->requestRefresh();
    const auto& extensionStats = m_projectExtensions->stats();
    appendConsoleMessage(
        tr("Project extension host activated %1 extension(s) from %2 native module(s); "
           "skipped %3 source/non-native and %4 disabled entry point(s).")
            .arg(static_cast<qulonglong>(m_projectExtensions->activeExtensionIds().size()))
            .arg(extensionStats.loadedModules)
            .arg(extensionStats.skippedSourceEntries)
            .arg(extensionStats.skippedDisabledEntries));
    addRecentProject(absolutePath);
    QSettings settings;
    settings.setValue(QStringLiteral("project/lastDirectory"),
                      QFileInfo(absolutePath).absolutePath());
    appendConsoleMessage(
        tr("Opened engine scene %1.").arg(QDir::toNativeSeparators(m_sceneDocument.filePath())));
    errorMessage.clear();
    return true;
}

bool MainWindow::loadProjectExtensions(
    const QString& filePath, const ProjectData& projectData, const fabgl::Scene& scene,
    std::unique_ptr<fabgl::project::ProjectExtensionModules>& modules,
    std::unique_ptr<fabgl::project::ProjectExtensionServiceHost>& services,
    std::string& manifestPath, std::string& rootPath, QString& errorMessage) const {
    manifestPath = QFileInfo(filePath).absoluteFilePath().toUtf8().toStdString();
    rootPath = ProjectDocument::absoluteProjectRoot(filePath, projectData.relativeRoot)
                   .toUtf8()
                   .toStdString();

    fabgl::project::ProjectExtensionLoadOptions options;
    options.safeMode = m_launchOptions.safeMode;
    options.extensionsEnabled =
        m_launchOptions.pluginsEnabled && m_projectTrustStore.isTrusted(filePath);
    auto loaded = fabgl::project::ProjectExtensionModules::load(manifestPath, options);
    if (!loaded) {
        errorMessage =
            tr("Project package lock/trust validation or native module loading failed: %1")
                .arg(engineError(loaded.error()));
        return false;
    }
    modules = std::make_unique<fabgl::project::ProjectExtensionModules>(std::move(loaded.value()));
    auto activated = modules->activate();
    if (!activated) {
        errorMessage =
            tr("Project extension activation failed: %1").arg(engineError(activated.error()));
        modules.reset();
        return false;
    }
    auto context = extensionHostContext(manifestPath, rootPath, scene);
    auto serviceHost = fabgl::project::ProjectExtensionServiceHost::create(*modules, context);
    if (!serviceHost) {
        errorMessage = tr("Project extension service registration failed: %1")
                           .arg(engineError(serviceHost.error()));
        modules->deactivate();
        modules.reset();
        return false;
    }
    services = std::make_unique<fabgl::project::ProjectExtensionServiceHost>(
        std::move(serviceHost.value()));
    auto opened = modules->invokeAll(fabgl::project::ProjectOpenExtensionCapability,
                                     {"open", "studio", &context});
    if (!opened) {
        errorMessage =
            tr("Project extension open hook failed: %1").arg(engineError(opened.error()));
        services.reset();
        modules->deactivate();
        modules.reset();
        return false;
    }
    const QByteArray activationPayload =
        QJsonDocument(QJsonObject{{QStringLiteral("schema"), 1},
                                  {QStringLiteral("projectManifest"), filePath}})
            .toJson(QJsonDocument::Compact);
    static_cast<void>(
        services->invokeKind(fabgl::PackageEntryPointKind::EditorPlugin, "activate",
                             std::string(activationPayload.constData(),
                                         static_cast<std::size_t>(activationPayload.size())),
                             context));
    errorMessage.clear();
    return true;
}

bool MainWindow::reloadProjectExtensions(QString& errorMessage) {
    if (m_projectFilePath.isEmpty()) {
        errorMessage.clear();
        return true;
    }
    QString shutdownError;
    if (!deactivateProjectExtensions(shutdownError)) {
        errorMessage = shutdownError;
        return false;
    }
    std::unique_ptr<fabgl::project::ProjectExtensionModules> modules;
    std::unique_ptr<fabgl::project::ProjectExtensionServiceHost> services;
    std::string manifestPath;
    std::string rootPath;
    if (!loadProjectExtensions(m_projectFilePath, m_projectData, m_sceneDocument.scene(), modules,
                               services, manifestPath, rootPath, errorMessage)) {
        return false;
    }
    m_extensionProjectManifestPath = std::move(manifestPath);
    m_extensionProjectRoot = std::move(rootPath);
    m_projectExtensions = std::move(modules);
    m_projectExtensionServices = std::move(services);
    refreshExtensionServices();
    if (m_assetBrowserController != nullptr)
        m_assetBrowserController->requestRefresh();
    updateInspector(m_hierarchyView != nullptr ? m_hierarchyView->currentIndex()
                                               : QModelIndex{});
    return true;
}

bool MainWindow::deactivateProjectExtensions(QString& errorMessage) {
    if (m_projectExtensions == nullptr) {
        closeExtensionCustomWindows(false);
        m_projectExtensionServices.reset();
        m_extensionProjectManifestPath.clear();
        m_extensionProjectRoot.clear();
        refreshExtensionServices();
        errorMessage.clear();
        return true;
    }
    auto context = extensionHostContext(m_extensionProjectManifestPath, m_extensionProjectRoot,
                                        m_sceneDocument.scene());
    if (m_projectExtensionServices != nullptr) {
        closeExtensionCustomWindows(true);
        const auto report = m_projectExtensionServices->invokeKind(
            fabgl::PackageEntryPointKind::EditorPlugin, "deactivate", std::string("{\"schema\":1}"),
            context);
        static_cast<void>(reportExtensionServiceFailures(tr("editor deactivation"), report, false));
        m_projectExtensionServices.reset();
    }
    auto closed = m_projectExtensions->invokeAll(fabgl::project::ProjectCloseExtensionCapability,
                                                 {"close", "studio", &context});
    m_projectExtensions->deactivate();
    m_projectExtensions.reset();
    m_extensionProjectManifestPath.clear();
    m_extensionProjectRoot.clear();
    refreshExtensionServices();
    if (m_componentInspector != nullptr)
        updateInspector(m_hierarchyView != nullptr ? m_hierarchyView->currentIndex()
                                                   : QModelIndex{});
    if (!closed) {
        errorMessage = engineError(closed.error());
        return false;
    }
    errorMessage.clear();
    return true;
}

void MainWindow::refreshExtensionServices() {
    if (m_extensionServicePanel == nullptr) {
        return;
    }
    const bool executionEnabled =
        currentProjectTrusted() && m_launchOptions.pluginsEnabled && !m_launchOptions.safeMode;
    if (m_projectExtensionServices == nullptr) {
        m_extensionServicePanel->setServices({}, executionEnabled);
        return;
    }
    m_extensionServicePanel->setServices(m_projectExtensionServices->services(), executionEnabled);
}

void MainWindow::configureExtensionProductHooks() {
    if (m_assetBrowserController != nullptr) {
        AssetBrowserExtensionImporterHooks hooks;
        hooks.probe = [this](const QString& relativePath, const QString& type,
                             const QString& settings)
            -> fabgl::Result<std::optional<AssetBrowserImporterDescriptor>> {
            if (m_projectExtensionServices == nullptr || !currentProjectTrusted() ||
                m_launchOptions.safeMode || !m_launchOptions.pluginsEnabled) {
                return fabgl::Result<std::optional<AssetBrowserImporterDescriptor>>::success(
                    std::nullopt);
            }
            std::vector<std::string> services;
            for (const auto& state : m_projectExtensionServices->services()) {
                if (state.enabled() &&
                    state.service.kind == fabgl::PackageEntryPointKind::AssetImporter) {
                    services.push_back(state.service.qualifiedId());
                }
            }
            const auto suffix = QFileInfo(relativePath).suffix().toLower();
            for (const auto& service : services) {
                QJsonParseError settingsError;
                const auto settingsDocument =
                    QJsonDocument::fromJson(settings.toUtf8(), &settingsError);
                if (settingsError.error != QJsonParseError::NoError ||
                    !settingsDocument.isObject()) {
                    return fabgl::Result<std::optional<AssetBrowserImporterDescriptor>>::failure(
                        fabgl::Error(
                            fabgl::ErrorCode::InvalidFormat,
                            "asset settings were not canonical JSON before extension probe"));
                }
                const QJsonObject request{{QStringLiteral("schema"), 1},
                                          {QStringLiteral("relativePath"), relativePath},
                                          {QStringLiteral("type"), type},
                                          {QStringLiteral("extension"), suffix},
                                          {QStringLiteral("settings"), settingsDocument.object()}};
                const auto encoded = QJsonDocument(request).toJson(QJsonDocument::Compact);
                const auto& scene =
                    m_playSession != nullptr ? m_playSession->scene() : m_sceneDocument.scene();
                const auto* runtime =
                    m_playSession != nullptr ? &m_playSession->runtime() : nullptr;
                auto context = extensionHostContext(m_extensionProjectManifestPath,
                                                    m_extensionProjectRoot, scene, runtime);
                auto response = m_projectExtensionServices->invoke(
                    service, "probe",
                    std::string(encoded.constData(), static_cast<std::size_t>(encoded.size())),
                    context);
                if (!response)
                    return fabgl::Result<std::optional<AssetBrowserImporterDescriptor>>::failure(
                        response.error());
                QJsonParseError parseError;
                const auto document = QJsonDocument::fromJson(
                    QByteArray(response.value().data(),
                               static_cast<qsizetype>(response.value().size())),
                    &parseError);
                const auto reject = [this, &service](QString message)
                    -> fabgl::Result<std::optional<AssetBrowserImporterDescriptor>> {
                    auto error = extensionSchemaError(QString::fromStdString(service), message);
                    static_cast<void>(m_projectExtensionServices->rejectResponse(service, error));
                    refreshExtensionServices();
                    return fabgl::Result<std::optional<AssetBrowserImporterDescriptor>>::failure(
                        std::move(error));
                };
                if (parseError.error != QJsonParseError::NoError || !document.isObject())
                    return reject(tr("Asset importer probe response is not a JSON object."));
                const auto object = document.object();
                if (object.value(QStringLiteral("schema")).toInt(-1) != 1 ||
                    !object.value(QStringLiteral("accepted")).isBool()) {
                    return reject(tr("Asset importer probe response has the wrong schema."));
                }
                if (!object.value(QStringLiteral("accepted")).toBool())
                    continue;
                const auto importerId = object.value(QStringLiteral("importerId")).toString();
                const auto version = object.value(QStringLiteral("version")).toInt(0);
                const auto kind =
                    extensionAssetKind(object.value(QStringLiteral("kind")).toString());
                static const QRegularExpression SafeId(
                    QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]{0,95}$"));
                if (!SafeId.match(importerId).hasMatch() || version <= 0 || version > 65535 ||
                    !kind || !object.value(QStringLiteral("settings")).isObject()) {
                    return reject(
                        tr("Asset importer probe returned invalid deterministic metadata."));
                }
                const auto canonicalSettings =
                    QJsonDocument(object.value(QStringLiteral("settings")).toObject())
                        .toJson(QJsonDocument::Compact);
                AssetBrowserImporterDescriptor descriptor{
                    service + "/" + importerId.toStdString(),
                    static_cast<std::uint32_t>(version),
                    *kind,
                    true,
                    service,
                    std::string(canonicalSettings.constData(),
                                static_cast<std::size_t>(canonicalSettings.size()))};
                return fabgl::Result<std::optional<AssetBrowserImporterDescriptor>>::success(
                    std::move(descriptor));
            }
            return fabgl::Result<std::optional<AssetBrowserImporterDescriptor>>::success(
                std::nullopt);
        };
        hooks.import = [this](const AssetBrowserImporterDescriptor& descriptor,
                              const fabgl::assets::AssetImportRequest& request,
                              const QString&) -> fabgl::Result<fabgl::assets::ImportedAsset> {
            if (m_projectExtensionServices == nullptr || !currentProjectTrusted() ||
                m_launchOptions.safeMode || !m_launchOptions.pluginsEnabled ||
                descriptor.extensionServiceId.empty()) {
                return fabgl::Result<fabgl::assets::ImportedAsset>::failure(
                    fabgl::Error(fabgl::ErrorCode::InvalidState,
                                 "extension importer execution is not authorized"));
            }
            QJsonArray dependencyKeys;
            for (const auto key : request.dependencyCacheKeys)
                dependencyKeys.push_back(QString::number(key));
            const QJsonObject payload{
                {QStringLiteral("schema"), 1},
                {QStringLiteral("guid"), QString::fromStdString(request.guid.toString())},
                {QStringLiteral("sourcePath"), QString::fromStdString(request.sourcePath)},
                {QStringLiteral("relativePath"), QString::fromStdString(request.relativePath)},
                {QStringLiteral("settings"), QString::fromStdString(request.normalizedSettings)},
                {QStringLiteral("target"), assetTargetName(request.target)},
                {QStringLiteral("pipelineVersion"), static_cast<qint64>(request.pipelineVersion)},
                {QStringLiteral("dependencyCacheKeys"), dependencyKeys}};
            const auto encoded = QJsonDocument(payload).toJson(QJsonDocument::Compact);
            const auto& scene =
                m_playSession != nullptr ? m_playSession->scene() : m_sceneDocument.scene();
            const auto* runtime = m_playSession != nullptr ? &m_playSession->runtime() : nullptr;
            auto context = extensionHostContext(m_extensionProjectManifestPath,
                                                m_extensionProjectRoot, scene, runtime);
            auto response = m_projectExtensionServices->invoke(
                descriptor.extensionServiceId, "import",
                std::string(encoded.constData(), static_cast<std::size_t>(encoded.size())),
                context);
            if (!response)
                return fabgl::Result<fabgl::assets::ImportedAsset>::failure(response.error());
            const auto reject =
                [this,
                 &descriptor](QString message) -> fabgl::Result<fabgl::assets::ImportedAsset> {
                auto error = extensionSchemaError(
                    QString::fromStdString(descriptor.extensionServiceId), message);
                static_cast<void>(m_projectExtensionServices->rejectResponse(
                    descriptor.extensionServiceId, error));
                refreshExtensionServices();
                return fabgl::Result<fabgl::assets::ImportedAsset>::failure(std::move(error));
            };
            QJsonParseError parseError;
            const auto document =
                QJsonDocument::fromJson(QByteArray(response.value().data(),
                                                   static_cast<qsizetype>(response.value().size())),
                                        &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject())
                return reject(tr("Asset importer result is not a JSON object."));
            const auto object = document.object();
            if (object.value(QStringLiteral("schema")).toInt(-1) != 1 ||
                !object.value(QStringLiteral("payloadBase64")).isString()) {
                return reject(tr("Asset importer result has the wrong schema."));
            }
            const auto encodedPayload =
                object.value(QStringLiteral("payloadBase64")).toString().toLatin1();
            static const QRegularExpression Base64(
                QStringLiteral("^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$"));
            if (!Base64.match(QString::fromLatin1(encodedPayload)).hasMatch())
                return reject(tr("Asset importer payload is not canonical base64."));
            const auto bytes = QByteArray::fromBase64(encodedPayload);
            if (bytes.isEmpty() || bytes.size() > 192 * 1024)
                return reject(tr("Asset importer payload is empty or exceeds 192 KiB."));
            QByteArray thumbnail;
            if (object.contains(QStringLiteral("thumbnailBase64"))) {
                const auto encodedThumbnail =
                    object.value(QStringLiteral("thumbnailBase64")).toString().toLatin1();
                if (!Base64.match(QString::fromLatin1(encodedThumbnail)).hasMatch())
                    return reject(tr("Asset importer thumbnail is not canonical base64."));
                thumbnail = QByteArray::fromBase64(encodedThumbnail);
                if (thumbnail.size() > 64 * 1024)
                    return reject(tr("Asset importer thumbnail exceeds 64 KiB."));
            }
            const auto costs = object.value(QStringLiteral("costs")).toObject();
            const auto boundedCost = [&costs](const QString& key,
                                              const quint64 maximum) -> std::optional<quint64> {
                const auto value = costs.value(key);
                if (!value.isDouble() || !std::isfinite(value.toDouble()) ||
                    value.toDouble() < 0.0 || std::floor(value.toDouble()) != value.toDouble() ||
                    value.toDouble() > static_cast<double>(maximum))
                    return std::nullopt;
                return static_cast<quint64>(value.toDouble());
            };
            const auto flash = boundedCost(QStringLiteral("flashBytes"), 1ULL << 32U);
            const auto ram = boundedCost(QStringLiteral("internalRamBytes"), 1ULL << 32U);
            const auto psram = boundedCost(QStringLiteral("psramBytes"), 1ULL << 32U);
            const auto sd = boundedCost(QStringLiteral("sdBytes"), 1ULL << 36U);
            const auto decode = boundedCost(QStringLiteral("decodeMicros"), 1ULL << 32U);
            const auto render = boundedCost(QStringLiteral("renderPixelsPerFrame"), 1ULL << 40U);
            if (!flash || !ram || !psram || !sd || !decode || !render)
                return reject(tr("Asset importer cost metadata is missing or out of bounds."));
            std::vector<fabgl::AssetGuid> dependencies;
            const auto dependencyValues = object.value(QStringLiteral("dependencies")).toArray();
            if (dependencyValues.size() > 256)
                return reject(tr("Asset importer returned too many dependencies."));
            for (const auto& dependency : dependencyValues) {
                if (!dependency.isString())
                    return reject(tr("Asset importer dependency is not a GUID string."));
                auto parsed = fabgl::AssetGuid::parse(dependency.toString().toStdString());
                if (!parsed)
                    return reject(tr("Asset importer returned an invalid dependency GUID."));
                dependencies.push_back(parsed.value());
            }
            fabgl::assets::ImportedAsset imported;
            imported.guid = request.guid;
            imported.kind = descriptor.kind;
            imported.payload.assign(bytes.cbegin(), bytes.cend());
            imported.thumbnail.assign(thumbnail.cbegin(), thumbnail.cend());
            imported.dependencies = std::move(dependencies);
            imported.flashBytes = static_cast<std::size_t>(*flash);
            imported.internalRamBytes = static_cast<std::size_t>(*ram);
            imported.psramBytes = static_cast<std::size_t>(*psram);
            imported.sdBytes = static_cast<std::size_t>(*sd);
            imported.estimatedDecodeMicros = static_cast<std::uint32_t>(*decode);
            imported.estimatedRenderPixelsPerFrame = *render;
            return fabgl::Result<fabgl::assets::ImportedAsset>::success(std::move(imported));
        };
        m_assetBrowserController->setExtensionImporterHooks(std::move(hooks));
    }

    if (m_componentInspector == nullptr)
        return;
    m_componentInspector->setExtensionHooks(
        [this](const std::vector<fabgl::EntityGuid>& entities,
               const fabgl::ComponentTypeGuid componentType,
               const fabgl::PropertyMetadata& metadata, const fabgl::PropertyValue& value,
               const bool mixed) -> fabgl::Result<CustomInspectorPresentation> {
            CustomInspectorPresentation none;
            if (m_projectExtensionServices == nullptr || !currentProjectTrusted() ||
                m_launchOptions.safeMode || !m_launchOptions.pluginsEnabled)
                return fabgl::Result<CustomInspectorPresentation>::success(std::move(none));
            QJsonArray entityIds;
            for (const auto id : entities)
                entityIds.push_back(SceneDocument::guidString(id));
            const QJsonObject payload{
                {QStringLiteral("schema"), 1},
                {QStringLiteral("entities"), entityIds},
                {QStringLiteral("componentType"), QString::fromStdString(componentType.toString())},
                {QStringLiteral("property"), QString::fromStdString(metadata.name)},
                {QStringLiteral("propertyType"), propertyTypeName(metadata.type)},
                {QStringLiteral("mixed"), mixed},
                {QStringLiteral("value"), extensionPropertyValue(value)}};
            const auto encoded = QJsonDocument(payload).toJson(QJsonDocument::Compact);
            std::vector<std::string> services;
            for (const auto& state : m_projectExtensionServices->services()) {
                if (state.enabled() &&
                    state.service.kind == fabgl::PackageEntryPointKind::CustomInspector)
                    services.push_back(state.service.qualifiedId());
            }
            for (const auto& service : services) {
                auto context =
                    extensionHostContext(m_extensionProjectManifestPath, m_extensionProjectRoot,
                                         m_sceneDocument.scene());
                auto response = m_projectExtensionServices->invoke(
                    service, "inspect",
                    std::string(encoded.constData(), static_cast<std::size_t>(encoded.size())),
                    context);
                if (!response)
                    return fabgl::Result<CustomInspectorPresentation>::failure(response.error());
                QJsonParseError parseError;
                const auto document = QJsonDocument::fromJson(
                    QByteArray(response.value().data(),
                               static_cast<qsizetype>(response.value().size())),
                    &parseError);
                const auto reject =
                    [this,
                     &service](QString message) -> fabgl::Result<CustomInspectorPresentation> {
                    auto error = extensionSchemaError(QString::fromStdString(service), message);
                    static_cast<void>(m_projectExtensionServices->rejectResponse(service, error));
                    refreshExtensionServices();
                    return fabgl::Result<CustomInspectorPresentation>::failure(std::move(error));
                };
                if (parseError.error != QJsonParseError::NoError || !document.isObject())
                    return reject(tr("Custom inspector response is not a JSON object."));
                const auto object = document.object();
                if (object.value(QStringLiteral("schema")).toInt(-1) != 1 ||
                    !object.value(QStringLiteral("handled")).isBool())
                    return reject(tr("Custom inspector response has the wrong schema."));
                if (!object.value(QStringLiteral("handled")).toBool())
                    continue;
                CustomInspectorPresentation presentation;
                presentation.handled = true;
                presentation.serviceId = QString::fromStdString(service);
                presentation.displayName = object.value(QStringLiteral("displayName")).toString();
                presentation.tooltip = object.value(QStringLiteral("tooltip")).toString();
                presentation.readOnly = object.value(QStringLiteral("readOnly")).toBool(false);
                presentation.hidden = object.value(QStringLiteral("hidden")).toBool(false);
                if (presentation.displayName.toUtf8().size() > 128 ||
                    presentation.tooltip.toUtf8().size() > 1024)
                    return reject(tr("Custom inspector text metadata exceeds its limit."));
                return fabgl::Result<CustomInspectorPresentation>::success(std::move(presentation));
            }
            return fabgl::Result<CustomInspectorPresentation>::success(std::move(none));
        },
        [this](const QString& service, const std::vector<fabgl::EntityGuid>& entities,
               const fabgl::ComponentTypeGuid componentType,
               const fabgl::PropertyMetadata& metadata,
               const fabgl::PropertyValue& proposed) -> fabgl::Result<fabgl::PropertyValue> {
            if (m_projectExtensionServices == nullptr || !currentProjectTrusted() ||
                m_launchOptions.safeMode || !m_launchOptions.pluginsEnabled)
                return fabgl::Result<fabgl::PropertyValue>::failure(fabgl::Error(
                    fabgl::ErrorCode::InvalidState, "custom inspector mutation is not authorized"));
            QJsonArray entityIds;
            for (const auto id : entities)
                entityIds.push_back(SceneDocument::guidString(id));
            const QJsonObject payload{
                {QStringLiteral("schema"), 1},
                {QStringLiteral("entities"), entityIds},
                {QStringLiteral("componentType"), QString::fromStdString(componentType.toString())},
                {QStringLiteral("property"), QString::fromStdString(metadata.name)},
                {QStringLiteral("propertyType"), propertyTypeName(metadata.type)},
                {QStringLiteral("proposed"), extensionPropertyValue(proposed)}};
            const auto encoded = QJsonDocument(payload).toJson(QJsonDocument::Compact);
            auto context = extensionHostContext(m_extensionProjectManifestPath,
                                                m_extensionProjectRoot, m_sceneDocument.scene());
            auto response = m_projectExtensionServices->invoke(
                service.toStdString(), "apply",
                std::string(encoded.constData(), static_cast<std::size_t>(encoded.size())),
                context);
            if (!response)
                return fabgl::Result<fabgl::PropertyValue>::failure(response.error());
            QJsonParseError parseError;
            const auto document =
                QJsonDocument::fromJson(QByteArray(response.value().data(),
                                                   static_cast<qsizetype>(response.value().size())),
                                        &parseError);
            const auto reject = [this,
                                 &service](QString message) -> fabgl::Result<fabgl::PropertyValue> {
                auto error = extensionSchemaError(service, message);
                static_cast<void>(
                    m_projectExtensionServices->rejectResponse(service.toStdString(), error));
                refreshExtensionServices();
                return fabgl::Result<fabgl::PropertyValue>::failure(std::move(error));
            };
            if (parseError.error != QJsonParseError::NoError || !document.isObject())
                return reject(tr("Custom inspector apply response is not a JSON object."));
            const auto object = document.object();
            if (object.value(QStringLiteral("schema")).toInt(-1) != 1 ||
                !object.value(QStringLiteral("accepted")).isBool())
                return reject(tr("Custom inspector apply response has the wrong schema."));
            if (!object.value(QStringLiteral("accepted")).toBool()) {
                return fabgl::Result<fabgl::PropertyValue>::failure(
                    fabgl::Error(fabgl::ErrorCode::InvalidArgument,
                                 object.value(QStringLiteral("message"))
                                     .toString(tr("The extension rejected this value."))
                                     .toStdString()));
            }
            if (!object.contains(QStringLiteral("value")))
                return fabgl::Result<fabgl::PropertyValue>::success(proposed);
            auto transformed =
                extensionDecodedPropertyValue(metadata, object.value(QStringLiteral("value")));
            if (!transformed)
                return reject(QString::fromStdString(transformed.error().message()));
            auto valid = fabgl::validatePropertyValue(metadata, transformed.value());
            if (!valid)
                return reject(QString::fromStdString(valid.error().message()));
            return transformed;
        });
}

bool MainWindow::installExtensionCustomWindowDescriptor(const QString& qualifiedServiceId,
                                                        const QByteArray& response,
                                                        QString& errorMessage) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(response, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        errorMessage = tr("Custom window response is not a JSON object.");
        return false;
    }
    const auto descriptor = document.object();
    const auto title = descriptor.value(QStringLiteral("title")).toString();
    const auto fields = descriptor.value(QStringLiteral("fields"));
    if (descriptor.value(QStringLiteral("schema")).toInt(-1) != 1 || title.isEmpty() ||
        title.toUtf8().size() > 128 || !fields.isArray() || fields.toArray().size() > 64) {
        errorMessage = tr("Custom window descriptor has the wrong schema or exceeds its bounds.");
        return false;
    }

    auto* contents = new QWidget(this);
    auto* outer = new QVBoxLayout(contents);
    auto* formHost = new QWidget(contents);
    auto* form = new QFormLayout(formHost);
    outer->addWidget(formHost);
    struct FieldReader final {
        QString id;
        std::function<QJsonValue()> read;
    };
    std::vector<FieldReader> readers;
    QSet<QString> ids;
    static const QRegularExpression SafeFieldId(QStringLiteral("^[A-Za-z][A-Za-z0-9_.-]{0,63}$"));
    for (const auto& value : fields.toArray()) {
        if (!value.isObject()) {
            errorMessage = tr("Custom window contains a non-object field descriptor.");
            contents->deleteLater();
            return false;
        }
        const auto field = value.toObject();
        const auto id = field.value(QStringLiteral("id")).toString();
        const auto label = field.value(QStringLiteral("label")).toString();
        const auto kind = field.value(QStringLiteral("kind")).toString();
        const bool readOnly = field.value(QStringLiteral("readOnly")).toBool(false);
        if (!SafeFieldId.match(id).hasMatch() || ids.contains(id) || label.isEmpty() ||
            label.toUtf8().size() > 128) {
            errorMessage = tr("Custom window field identity is invalid or duplicated.");
            contents->deleteLater();
            return false;
        }
        ids.insert(id);
        if (kind == QStringLiteral("label")) {
            const auto text = field.value(QStringLiteral("value")).toString();
            if (text.toUtf8().size() > 4096) {
                errorMessage = tr("Custom window label exceeds its text limit.");
                contents->deleteLater();
                return false;
            }
            auto* widget = new QLabel(text, formHost);
            widget->setWordWrap(true);
            form->addRow(label, widget);
            continue;
        }
        if (kind == QStringLiteral("text")) {
            const auto text = field.value(QStringLiteral("value")).toString();
            const int maximumLength =
                std::clamp(field.value(QStringLiteral("maximumLength")).toInt(4096), 1, 4096);
            if (text.toUtf8().size() > maximumLength) {
                errorMessage = tr("Custom window text field exceeds its declared limit.");
                contents->deleteLater();
                return false;
            }
            auto* editor = new QLineEdit(text, formHost);
            editor->setObjectName(QStringLiteral("extensionField.%1").arg(id));
            editor->setMaxLength(maximumLength);
            editor->setReadOnly(readOnly);
            form->addRow(label, editor);
            readers.push_back({id, [editor]() { return QJsonValue(editor->text()); }});
            continue;
        }
        if (kind == QStringLiteral("boolean")) {
            if (!field.value(QStringLiteral("value")).isBool()) {
                errorMessage = tr("Custom window boolean field has a non-boolean value.");
                contents->deleteLater();
                return false;
            }
            auto* editor = new QCheckBox(formHost);
            editor->setObjectName(QStringLiteral("extensionField.%1").arg(id));
            editor->setChecked(field.value(QStringLiteral("value")).toBool());
            editor->setEnabled(!readOnly);
            form->addRow(label, editor);
            readers.push_back({id, [editor]() { return QJsonValue(editor->isChecked()); }});
            continue;
        }
        if (kind == QStringLiteral("number")) {
            const double minimum = field.value(QStringLiteral("minimum")).toDouble(-1.0e9);
            const double maximum = field.value(QStringLiteral("maximum")).toDouble(1.0e9);
            const double number = field.value(QStringLiteral("value"))
                                      .toDouble(std::numeric_limits<double>::quiet_NaN());
            if (!std::isfinite(minimum) || !std::isfinite(maximum) || !std::isfinite(number) ||
                minimum > maximum || number < minimum || number > maximum) {
                errorMessage = tr("Custom window number field is out of bounds.");
                contents->deleteLater();
                return false;
            }
            auto* editor = new QDoubleSpinBox(formHost);
            editor->setObjectName(QStringLiteral("extensionField.%1").arg(id));
            editor->setRange(minimum, maximum);
            editor->setDecimals(6);
            editor->setValue(number);
            editor->setReadOnly(readOnly);
            form->addRow(label, editor);
            readers.push_back({id, [editor]() { return QJsonValue(editor->value()); }});
            continue;
        }
        if (kind == QStringLiteral("choice")) {
            const auto options = field.value(QStringLiteral("options")).toArray();
            const auto selected = field.value(QStringLiteral("value")).toString();
            if (options.isEmpty() || options.size() > 64) {
                errorMessage = tr("Custom window choice field has no bounded option list.");
                contents->deleteLater();
                return false;
            }
            auto* editor = new QComboBox(formHost);
            editor->setObjectName(QStringLiteral("extensionField.%1").arg(id));
            for (const auto& option : options) {
                if (!option.isString() || option.toString().toUtf8().size() > 128) {
                    errorMessage = tr("Custom window choice option is invalid.");
                    contents->deleteLater();
                    return false;
                }
                editor->addItem(option.toString());
            }
            const int selectedIndex = editor->findText(selected);
            if (selectedIndex < 0) {
                errorMessage = tr("Custom window selected choice is not in its option list.");
                contents->deleteLater();
                return false;
            }
            editor->setCurrentIndex(selectedIndex);
            editor->setEnabled(!readOnly);
            form->addRow(label, editor);
            readers.push_back({id, [editor]() { return QJsonValue(editor->currentText()); }});
            continue;
        }
        errorMessage = tr("Custom window field kind '%1' is unsupported.").arg(kind);
        contents->deleteLater();
        return false;
    }

    auto* controls = new QWidget(contents);
    auto* controlsLayout = new QHBoxLayout(controls);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->addStretch();
    auto* refresh = new QPushButton(tr("Apply / Refresh"), controls);
    refresh->setObjectName(QStringLiteral("extensionWindowRefresh"));
    auto* hide = new QPushButton(tr("Hide"), controls);
    hide->setObjectName(QStringLiteral("extensionWindowHide"));
    controlsLayout->addWidget(refresh);
    controlsLayout->addWidget(hide);
    outer->addWidget(controls);

    connect(refresh, &QPushButton::clicked, this,
            [this, qualifiedServiceId, readers = std::move(readers)]() {
                if (m_projectExtensionServices == nullptr || !currentProjectTrusted())
                    return;
                QJsonObject values;
                for (const auto& reader : readers)
                    values.insert(reader.id, reader.read());
                const auto payload = QJsonDocument(QJsonObject{{QStringLiteral("schema"), 1},
                                                               {QStringLiteral("values"), values}})
                                         .toJson(QJsonDocument::Compact);
                auto context =
                    extensionHostContext(m_extensionProjectManifestPath, m_extensionProjectRoot,
                                         m_sceneDocument.scene());
                auto result = m_projectExtensionServices->invoke(
                    qualifiedServiceId.toStdString(), "refresh",
                    std::string(payload.constData(), static_cast<std::size_t>(payload.size())),
                    context);
                if (!result) {
                    appendConsoleMessage(tr("Custom window %1 refresh failed: %2")
                                             .arg(qualifiedServiceId, engineError(result.error())));
                    refreshExtensionServices();
                    return;
                }
                QString schemaError;
                const QByteArray refreshResponse(result.value().data(),
                                                 static_cast<qsizetype>(result.value().size()));
                if (!installExtensionCustomWindowDescriptor(qualifiedServiceId, refreshResponse,
                                                            schemaError)) {
                    auto error = extensionSchemaError(qualifiedServiceId, schemaError);
                    static_cast<void>(m_projectExtensionServices->rejectResponse(
                        qualifiedServiceId.toStdString(), error));
                    appendConsoleMessage(schemaError);
                    refreshExtensionServices();
                }
            });
    connect(hide, &QPushButton::clicked, this,
            [this, qualifiedServiceId]() { hideExtensionCustomWindow(qualifiedServiceId, true); });

    auto* dock = m_extensionCustomWindows.value(qualifiedServiceId, nullptr);
    if (dock == nullptr) {
        const auto digest =
            QCryptographicHash::hash(qualifiedServiceId.toUtf8(), QCryptographicHash::Sha256)
                .toHex()
                .left(16);
        dock = createDock(
            title, QStringLiteral("extensionCustomWindow.%1").arg(QString::fromLatin1(digest)),
            contents, Qt::RightDockWidgetArea);
        m_extensionCustomWindows.insert(qualifiedServiceId, dock);
        if (m_panelsMenu != nullptr)
            m_panelsMenu->addAction(dock->toggleViewAction());
        connect(dock->toggleViewAction(), &QAction::toggled, this,
                [this, qualifiedServiceId](const bool visible) {
                    if (!visible && m_extensionCustomWindows.contains(qualifiedServiceId))
                        hideExtensionCustomWindow(qualifiedServiceId, true);
                });
    } else {
        dock->setWindowTitle(title);
        auto* previous = dock->widget();
        dock->setWidget(contents);
        if (previous != nullptr)
            previous->deleteLater();
    }
    dock->show();
    dock->raise();
    errorMessage.clear();
    return true;
}

void MainWindow::showExtensionCustomWindow(const QString& qualifiedServiceId) {
    if (m_projectExtensionServices == nullptr || !currentProjectTrusted() ||
        m_launchOptions.safeMode || !m_launchOptions.pluginsEnabled)
        return;
    const auto payload =
        QJsonDocument(QJsonObject{{QStringLiteral("schema"), 1},
                                  {QStringLiteral("projectManifest"), m_projectFilePath}})
            .toJson(QJsonDocument::Compact);
    auto context = extensionHostContext(m_extensionProjectManifestPath, m_extensionProjectRoot,
                                        m_sceneDocument.scene());
    auto result = m_projectExtensionServices->invoke(
        qualifiedServiceId.toStdString(), "show",
        std::string(payload.constData(), static_cast<std::size_t>(payload.size())), context);
    if (!result) {
        const auto message = engineError(result.error());
        appendConsoleMessage(tr("Custom window %1 failed: %2").arg(qualifiedServiceId, message));
        if (m_extensionServicePanel != nullptr)
            m_extensionServicePanel->setDispatchResult(qualifiedServiceId, false, message);
        refreshExtensionServices();
        return;
    }
    QString errorMessage;
    const QByteArray response(result.value().data(), static_cast<qsizetype>(result.value().size()));
    if (!installExtensionCustomWindowDescriptor(qualifiedServiceId, response, errorMessage)) {
        auto error = extensionSchemaError(qualifiedServiceId, errorMessage);
        static_cast<void>(
            m_projectExtensionServices->rejectResponse(qualifiedServiceId.toStdString(), error));
        appendConsoleMessage(
            tr("Custom window %1 was disabled: %2").arg(qualifiedServiceId, errorMessage));
        if (m_extensionServicePanel != nullptr)
            m_extensionServicePanel->setDispatchResult(qualifiedServiceId, false, errorMessage);
        refreshExtensionServices();
        return;
    }
    if (m_extensionServicePanel != nullptr)
        m_extensionServicePanel->setDispatchResult(qualifiedServiceId, true, tr("Window is open"));
}

void MainWindow::hideExtensionCustomWindow(const QString& qualifiedServiceId,
                                           const bool notifyExtension) {
    auto* dock = m_extensionCustomWindows.take(qualifiedServiceId);
    if (notifyExtension && m_projectExtensionServices != nullptr && currentProjectTrusted()) {
        auto context = extensionHostContext(m_extensionProjectManifestPath, m_extensionProjectRoot,
                                            m_sceneDocument.scene());
        auto result = m_projectExtensionServices->invoke(qualifiedServiceId.toStdString(), "hide",
                                                         std::string("{\"schema\":1}"), context);
        if (!result)
            appendConsoleMessage(tr("Custom window %1 hide hook failed: %2")
                                     .arg(qualifiedServiceId, engineError(result.error())));
    }
    if (dock == nullptr)
        return;
    if (m_panelsMenu != nullptr)
        m_panelsMenu->removeAction(dock->toggleViewAction());
    m_docks.removeOne(dock);
    removeDockWidget(dock);
    dock->deleteLater();
}

void MainWindow::closeExtensionCustomWindows(const bool notifyExtensions) {
    const auto services = m_extensionCustomWindows.keys();
    for (const auto& service : services)
        hideExtensionCustomWindow(service, notifyExtensions);
}

void MainWindow::invokeExtensionService(const QString& qualifiedServiceId, const int kindValue) {
    if (m_projectExtensionServices == nullptr || !currentProjectTrusted() ||
        m_launchOptions.safeMode || !m_launchOptions.pluginsEnabled) {
        const auto message = tr("Extension service execution is disabled by project trust, Safe "
                                "Mode, or plugin settings.");
        appendConsoleMessage(message);
        if (m_extensionServicePanel != nullptr) {
            m_extensionServicePanel->setDispatchResult(qualifiedServiceId, false, message);
        }
        return;
    }

    const auto kind = static_cast<fabgl::PackageEntryPointKind>(kindValue);
    const char* operation = nullptr;
    QJsonObject payload{{QStringLiteral("schema"), 1},
                        {QStringLiteral("projectManifest"), m_projectFilePath}};
    switch (kind) {
    case fabgl::PackageEntryPointKind::EditorPlugin:
        operation = "execute";
        break;
    case fabgl::PackageEntryPointKind::AssetImporter:
        if (m_assetBrowserController != nullptr)
            m_assetBrowserController->requestRefresh();
        if (m_extensionServicePanel != nullptr)
            m_extensionServicePanel->setDispatchResult(
                qualifiedServiceId, true, tr("Importer refresh was scheduled."));
        return;
    case fabgl::PackageEntryPointKind::CustomInspector:
        updateInspector(m_hierarchyView != nullptr ? m_hierarchyView->currentIndex()
                                                   : QModelIndex{});
        if (m_extensionServicePanel != nullptr)
            m_extensionServicePanel->setDispatchResult(
                qualifiedServiceId, true, tr("Inspector hooks were refreshed."));
        return;
    case fabgl::PackageEntryPointKind::CustomWindow:
        showExtensionCustomWindow(qualifiedServiceId);
        return;
    case fabgl::PackageEntryPointKind::RuntimeModule:
    case fabgl::PackageEntryPointKind::RendererExtension:
    case fabgl::PackageEntryPointKind::Framework:
    case fabgl::PackageEntryPointKind::BuildStep:
        break;
    }
    if (operation == nullptr) {
        const auto message = tr("This service is driven automatically by its product lifecycle.");
        if (m_extensionServicePanel != nullptr) {
            m_extensionServicePanel->setDispatchResult(qualifiedServiceId, false, message);
        }
        return;
    }

    const auto& scene = m_playSession != nullptr ? m_playSession->scene() : m_sceneDocument.scene();
    const auto* runtime = m_playSession != nullptr ? &m_playSession->runtime() : nullptr;
    auto context = extensionHostContext(m_extensionProjectManifestPath, m_extensionProjectRoot,
                                        scene, runtime);
    const auto encodedPayload = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    auto result = m_projectExtensionServices->invoke(
        qualifiedServiceId.toStdString(), operation,
        std::string(encodedPayload.constData(), static_cast<std::size_t>(encodedPayload.size())),
        context);
    if (!result) {
        const auto message = engineError(result.error());
        appendConsoleMessage(tr("Extension service %1 failed and was disabled: %2")
                                 .arg(qualifiedServiceId, message));
        if (m_extensionServicePanel != nullptr) {
            m_extensionServicePanel->setDispatchResult(qualifiedServiceId, false, message);
        }
        refreshExtensionServices();
        return;
    }
    const auto response =
        QString::fromUtf8(result.value().data(), static_cast<qsizetype>(result.value().size()));
    const auto message = response.trimmed().isEmpty() ? tr("completed successfully") : response;
    appendConsoleMessage(tr("Extension service %1 completed: %2").arg(qualifiedServiceId, message));
    if (m_extensionServicePanel != nullptr) {
        m_extensionServicePanel->setDispatchResult(qualifiedServiceId, true, message);
    }
    refreshExtensionServices();
}

bool MainWindow::reportExtensionServiceFailures(
    const QString& phase, const fabgl::project::ProjectExtensionDispatchReport& report,
    const bool buildOutput) {
    for (const auto& failure : report.failures) {
        const auto service = QString::fromStdString(failure.qualifiedServiceId);
        const auto detail = engineError(failure.error);
        const auto message = tr("Extension service %1 failed during %2 and was disabled: %3")
                                 .arg(service, phase, detail);
        appendConsoleMessage(message);
        if (buildOutput) {
            appendBuildOutput(message + QLatin1Char('\n'), true);
        }
        if (m_extensionServicePanel != nullptr) {
            m_extensionServicePanel->setDispatchResult(service, false, detail);
        }
    }
    refreshExtensionServices();
    return report.ok();
}

bool MainWindow::dispatchBuildExtensionServices(const WorkflowState state, const QString& phase,
                                                const bool processSucceeded, const int exitCode) {
    if (m_projectExtensionServices == nullptr || m_projectExtensions == nullptr) {
        return true;
    }
    QString target = m_activeWorkflowTarget;
    QString configuration = m_activeWorkflowConfiguration;
    if (target.isEmpty()) {
        switch (state) {
        case WorkflowState::PcBuild:
            target = QStringLiteral("Pc");
            break;
        case WorkflowState::Esp32ExportOnly:
        case WorkflowState::Esp32Build:
        case WorkflowState::Esp32Upload:
        case WorkflowState::Esp32DeployDiagnostics:
            target = QStringLiteral("Esp32");
            break;
        case WorkflowState::CustomBuild:
            target = QStringLiteral("Custom");
            break;
        case WorkflowState::Idle:
            target = QStringLiteral("Idle");
            break;
        }
    }
    if (configuration.isEmpty()) {
        configuration = m_configurationCombo != nullptr
                            ? m_configurationCombo->currentData().toString()
                            : QStringLiteral("unknown");
    }
    const auto& scene = m_playSession != nullptr ? m_playSession->scene() : m_sceneDocument.scene();
    const auto* runtime = m_playSession != nullptr ? &m_playSession->runtime() : nullptr;
    auto context = extensionHostContext(m_extensionProjectManifestPath, m_extensionProjectRoot,
                                        scene, runtime);
    const auto report = m_projectExtensionServices->buildStep(
        context, phase.toStdString(), target.toStdString(), configuration.toStdString(),
        processSucceeded, exitCode);
    return reportExtensionServiceFailures(phase, report, true);
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
    const QString previousProjectPath = m_projectFilePath;
    const bool inheritTrust = m_projectFilePath.isEmpty() || currentProjectTrusted();
    ProjectData projectData = m_projectData;
    projectData.name = m_projectName == QStringLiteral("Untitled")
                           ? QFileInfo(filePath).completeBaseName()
                           : m_projectName;
    if (projectData.sceneFile.isEmpty()) {
        projectData.sceneFile = QStringLiteral("Scenes/Main.fglscene");
    }
    QString errorMessage;
    if (!m_sceneDocument.saveAs(ProjectDocument::absoluteScenePath(filePath, projectData),
                                errorMessage) ||
        !ProjectDocument::save(filePath, projectData, errorMessage)) {
        QMessageBox::critical(this, tr("Save Project Failed"), errorMessage);
        return false;
    }

    m_projectFilePath = QFileInfo(filePath).absoluteFilePath();
    m_projectName = projectData.name;
    m_projectData = std::move(projectData);
    m_forceModified = false;
    m_undoStack.setClean();
    m_sceneDocument.setModified(false);
    refreshModifiedState();
    updateProjectPanel();
    reloadPresentationAssets();
    renderCurrentScene();
    addRecentProject(m_projectFilePath);
    updateWorkflowActions();
    QSettings settings;
    settings.setValue(QStringLiteral("project/lastDirectory"),
                      QFileInfo(m_projectFilePath).absolutePath());
    QString trustError;
    if (!m_projectTrustStore.setTrusted(m_projectFilePath, inheritTrust, trustError)) {
        appendConsoleMessage(tr("Could not persist project trust decision: %1").arg(trustError));
    }
    if (QFileInfo(previousProjectPath).absoluteFilePath() !=
        QFileInfo(m_projectFilePath).absoluteFilePath()) {
        QString extensionError;
        if (!reloadProjectExtensions(extensionError)) {
            appendConsoleMessage(
                tr("Project was saved at its new path, but extensions remain disabled: %1")
                    .arg(extensionError));
        }
    }
    m_recoveryManager.recordLastProject(m_projectFilePath);
    QString recoveryError;
    if (!m_recoveryManager.discardProject(m_projectFilePath, recoveryError) &&
        !recoveryError.isEmpty()) {
        appendConsoleMessage(tr("Saved, but old recovery cleanup failed: %1").arg(recoveryError));
    }
    updateProjectTrustUi();
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

void MainWindow::applyLoadedProject(const QString& filePath, ProjectData projectData) {
    m_lastPcScriptModule.clear();
    m_lastMeasuredEsp32FrameMilliseconds.reset();
    m_lastMeasuredEsp32HeapFreeBytes.reset();
    m_lastMeasuredEsp32HeapFragmentationPercent.reset();
    m_lastMeasuredEsp32SdReadBytes.reset();
    m_projectFilePath = QFileInfo(filePath).absoluteFilePath();
    m_projectName = projectData.name;
    m_projectData = std::move(projectData);
    m_undoStack.clear();
    m_undoStack.setClean();
    m_sceneDocument.setModified(false);
    m_forceModified = false;
    m_recoveryManager.recordLastProject(m_projectFilePath);
    refreshModifiedState();
    updateProjectPanel();
    reloadPresentationAssets();
    selectEntityRow(m_entities.rowCount() > 0 ? 0 : -1);
    renderCurrentScene();
    updateProjectTrustUi();
    if (!currentProjectTrusted()) {
        appendConsoleMessage(
            tr("UNTRUSTED PROJECT: build, Play mode, PC player, script, and package execution "
               "remain disabled until this exact project path is explicitly trusted."));
    }
    updateWorkflowActions();
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
    if (m_projectTree == nullptr || m_projectModel == nullptr || m_assetTree == nullptr ||
        m_assetBrowserController == nullptr) {
        return;
    }
    const bool hasProject = !m_projectFilePath.isEmpty();
    m_projectTree->setEnabled(hasProject);
    if (!hasProject) {
        m_projectTree->setRootIndex({});
        m_assetBrowserController->clearProject();
        m_assetTree->setRootIndex({});
        m_assetTree->setEnabled(false);
        if (m_memoryAnalyzer != nullptr) {
            m_memoryAnalyzer->setProjectRoot({});
            m_memoryAnalyzer->setPerformanceBudgets(m_projectData.performance);
            m_memoryAnalyzer->setEsp32StorageEstimates(0U, 0U, 0U, 0U);
            m_memoryAnalyzer->setMeasuredPcResidentBytes(std::nullopt);
        }
        if (m_codeEditor != nullptr) {
            m_codeEditor->setProjectRoot({});
        }
        if (m_packageManager != nullptr) {
            m_packageManager->setProjectManifestPath({});
            m_packageManager->setProjectTrusted(false);
        }
        if (m_prefabEditor != nullptr) {
            m_prefabEditor->setProjectContext({}, {}, {});
        }
        if (m_inputMapEditor != nullptr) {
            m_inputMapEditor->setProjectContext({}, {});
        }
        updateProjectTrustUi();
        updateProjectTargetProfileUi();
        updateWindowTitle();
        return;
    }
    const auto root = projectRoot();
    if (m_memoryAnalyzer != nullptr) {
        m_memoryAnalyzer->setPerformanceBudgets(m_projectData.performance);
    }
    m_projectTree->setRootIndex(m_projectModel->setRootPath(root));
    m_projectTree->setToolTip(QDir::toNativeSeparators(root));
    QVector<AssetBrowserProjectEntry> browserEntries;
    browserEntries.reserve(m_projectData.assets.size());
    for (const auto& asset : m_projectData.assets) {
        auto guid = fabgl::AssetGuid::parse(asset.guid.toStdString());
        if (!guid) {
            appendConsoleMessage(
                tr("Asset Browser skipped manifest entry with invalid GUID %1.").arg(asset.guid));
            continue;
        }
        AssetBrowserProjectEntry browserEntry;
        browserEntry.guid = guid.value();
        browserEntry.relativePath = asset.path;
        browserEntry.type = asset.type;
        browserEntry.normalizedSettings = asset.importSettings;
        browserEntry.esp32Target =
            asset.esp32Target == QStringLiteral("psram") ? fabgl::assets::AssetTarget::Esp32Psram
            : asset.esp32Target == QStringLiteral("sd")  ? fabgl::assets::AssetTarget::Esp32Sd
                                                         : fabgl::assets::AssetTarget::Esp32Flash;
        for (const auto& dependencyText : asset.dependencies) {
            auto dependency = fabgl::AssetGuid::parse(dependencyText.toStdString());
            if (dependency)
                browserEntry.dependencies.push_back(dependency.value());
        }
        browserEntry.hasExplicitImportMetadata = asset.hasImportMetadata;
        browserEntries.push_back(std::move(browserEntry));
    }
    const auto pcBudget = fabgl::project::selectedPerformanceBudget(
        m_projectData.performance, fabgl::project::PerformanceTarget::Pc);
    const auto esp32Budget = fabgl::project::selectedPerformanceBudget(
        m_projectData.performance, fabgl::project::PerformanceTarget::Esp32);
    if (m_profilerTimeline != nullptr) {
        QString budgetError;
        if (!m_profilerTimeline->setBudget(QStringLiteral("pc.frame"),
                                           pcBudget.frameTotalMilliseconds,
                                           fabgl::ProfilerUnit::Milliseconds, budgetError) ||
            !m_profilerTimeline->setBudget(QStringLiteral("pc.ai"), pcBudget.aiMilliseconds,
                                           fabgl::ProfilerUnit::Milliseconds, budgetError) ||
            !m_profilerTimeline->setBudget(QStringLiteral("esp32.frame"),
                                           esp32Budget.frameTotalMilliseconds,
                                           fabgl::ProfilerUnit::Milliseconds, budgetError)) {
            appendConsoleMessage(tr("Performance budget setup failed: %1").arg(budgetError));
        }
    }
    AssetBrowserLimits browserLimits;
    browserLimits.maximumAssets = std::clamp<std::size_t>(esp32Budget.components, 64U, 4096U);
    browserLimits.maximumSourceBytes = std::clamp<std::uint64_t>(
        esp32Budget.assetResidentBytes, 1024U * 1024U, 128U * 1024U * 1024U);
    browserLimits.maximumAggregateSourceBytes = std::max<std::uint64_t>(
        browserLimits.maximumSourceBytes,
        std::min<std::uint64_t>(esp32Budget.flashBytes + esp32Budget.sdBytes,
                                512U * 1024U * 1024U));
    browserLimits.flashBudgetBytes = esp32Budget.flashBytes;
    browserLimits.internalRamBudgetBytes = esp32Budget.internalRamBytes;
    browserLimits.psramBudgetBytes = esp32Budget.psramBytes;
    browserLimits.sdBudgetBytes = esp32Budget.sdBytes;
    const auto configured =
        m_assetBrowserController->setProject(root, std::move(browserEntries), browserLimits);
    if (!configured) {
        const QString message = engineError(configured.error());
        m_assetTree->setEnabled(false);
        m_assetTree->setToolTip(message);
        appendConsoleMessage(tr("Asset Browser refused the project asset tree: %1").arg(message));
    } else {
        m_assetTree->setEnabled(true);
        m_assetTree->setRootIndex({});
        m_assetTree->setToolTip(
            tr("%1 project assets. Hover rows for import diagnostics and PC/ESP32 costs.")
                .arg(m_assetBrowserController->model()->rowCount()));
        bool mappingChanged = false;
        QSet<QString> manifestAssetGuids;
        for (auto& asset : m_projectData.assets) {
            const auto guid = fabgl::AssetGuid::parse(asset.guid.toStdString());
            if (!guid)
                continue;
            manifestAssetGuids.insert(
                QString::fromStdString(guid.value().toString()).toCaseFolded());
            const auto* indexed = m_assetBrowserController->model()->entry(guid.value());
            if (indexed != nullptr &&
                indexed->relativePath.compare(asset.path, Qt::CaseInsensitive) != 0) {
                asset.path = indexed->relativePath;
                mappingChanged = true;
            }
            if (indexed != nullptr) {
                QStringList dependencies;
                for (const auto& dependency : indexed->dependencies)
                    dependencies.push_back(QString::fromStdString(dependency.toString()));
                std::sort(dependencies.begin(), dependencies.end());
                const auto target = indexed->esp32Target == fabgl::assets::AssetTarget::Esp32Psram
                                        ? QStringLiteral("psram")
                                    : indexed->esp32Target == fabgl::assets::AssetTarget::Esp32Sd
                                        ? QStringLiteral("sd")
                                        : QStringLiteral("flash");
                const bool meaningfulMetadata =
                    asset.hasImportMetadata ||
                    indexed->normalizedSettings != QStringLiteral("{}") ||
                    target != QStringLiteral("flash") || !dependencies.isEmpty();
                if (meaningfulMetadata &&
                    (!asset.hasImportMetadata ||
                     asset.importSettings != indexed->normalizedSettings ||
                     asset.esp32Target != target || asset.dependencies != dependencies)) {
                    asset.importSettings = indexed->normalizedSettings;
                    asset.esp32Target = target;
                    asset.dependencies = dependencies;
                    asset.hasImportMetadata = true;
                    mappingChanged = true;
                }
            }
        }
        for (const auto& indexed : m_assetBrowserController->model()->entries()) {
            const auto guidText = QString::fromStdString(indexed.guid.toString());
            if (manifestAssetGuids.contains(guidText.toCaseFolded()))
                continue;
            ProjectAssetEntry discovered;
            discovered.guid = guidText;
            discovered.path = indexed.relativePath;
            discovered.type = indexed.type;
            discovered.importSettings = indexed.normalizedSettings;
            discovered.esp32Target = indexed.esp32Target == fabgl::assets::AssetTarget::Esp32Psram
                                         ? QStringLiteral("psram")
                                     : indexed.esp32Target == fabgl::assets::AssetTarget::Esp32Sd
                                         ? QStringLiteral("sd")
                                         : QStringLiteral("flash");
            for (const auto& dependency : indexed.dependencies)
                discovered.dependencies.push_back(QString::fromStdString(dependency.toString()));
            std::sort(discovered.dependencies.begin(), discovered.dependencies.end());
            discovered.hasImportMetadata = true;
            m_projectData.assets.push_back(std::move(discovered));
            manifestAssetGuids.insert(guidText.toCaseFolded());
            mappingChanged = true;
        }
        if (mappingChanged) {
            setDocumentModified(true);
        }
        if (m_memoryAnalyzer != nullptr) {
            std::uint64_t flash = 0U;
            std::uint64_t internalRam = 0U;
            std::uint64_t psram = 0U;
            std::uint64_t sd = 0U;
            for (const auto& entry : m_assetBrowserController->model()->entries()) {
                flash += entry.esp32Cost.flashBytes;
                internalRam += entry.esp32Cost.internalRamBytes;
                psram += entry.esp32Cost.psramBytes;
                sd += entry.esp32Cost.sdBytes;
            }
            m_memoryAnalyzer->setEsp32StorageEstimates(flash, internalRam, psram, sd);
        }
    }
    if (m_memoryAnalyzer != nullptr) {
        m_memoryAnalyzer->setProjectRoot(root);
    }
    if (m_codeEditor != nullptr) {
        m_codeEditor->setProjectRoot(root);
    }
    if (m_packageManager != nullptr) {
        auto projectCli =
            findBuiltTool(QStringLiteral("fabgl_project_cli"), QStringLiteral("tools/project_cli"));
        if (projectCli.isEmpty()) {
            projectCli = QStringLiteral("fabgl_project_cli");
        }
        m_packageManager->setProjectCliPath(projectCli);
        m_packageManager->setProjectManifestPath(m_projectFilePath);
        m_packageManager->setProjectTrusted(currentProjectTrusted());
    }
    if (m_prefabEditor != nullptr) {
        m_prefabEditor->setProjectContext(root, m_projectData.projectGuid, m_projectData.assets);
        m_prefabEditor->setSelectedEntity(selectedEntityId());
    }
    if (m_inputMapEditor != nullptr) {
        m_inputMapEditor->setProjectContext(m_projectFilePath, m_projectData);
    }
    updateProjectTrustUi();
    updateProjectTargetProfileUi();
    updateWindowTitle();
}

void MainWindow::reloadPresentationAssets() {
    m_projectAssetLibrary.reset();
    if (m_gameView != nullptr) {
        m_gameView->setPresentationResources({});
    }
    if (m_projectFilePath.isEmpty()) {
        return;
    }

    QFile projectFile(m_projectFilePath);
    constexpr qint64 MaximumManifestBytes = 1024 * 1024;
    if (!projectFile.open(QIODevice::ReadOnly) || projectFile.size() <= 0 ||
        projectFile.size() > MaximumManifestBytes) {
        appendConsoleMessage(
            tr("Presentation assets could not be loaded; bounded placeholders remain active: "
               "the project manifest is unavailable or exceeds 1 MiB."));
        return;
    }
    const QByteArray source = projectFile.readAll();
    if (source.isEmpty() || source.size() > MaximumManifestBytes) {
        appendConsoleMessage(
            tr("Presentation assets could not be loaded; bounded placeholders remain active: "
               "the project manifest could not be read safely."));
        return;
    }

    const auto manifest = fabgl::project::parseManifest(
        std::string_view(source.constData(), static_cast<std::size_t>(source.size())));
    if (!manifest) {
        appendConsoleMessage(
            tr("Presentation assets could not be loaded; bounded placeholders remain active: %1")
                .arg(engineError(manifest.error())));
        return;
    }

    const std::string rootUtf8 = projectRoot().toUtf8().toStdString();
    auto library = fabgl::project::ProjectAssetLibrary::load(rootUtf8, manifest.value());
    if (!library) {
        appendConsoleMessage(
            tr("Presentation assets could not be loaded; bounded placeholders remain active: %1")
                .arg(engineError(library.error())));
        return;
    }

    m_projectAssetLibrary =
        std::make_unique<fabgl::project::ProjectAssetLibrary>(std::move(library.value()));
    if (m_gameView != nullptr) {
        m_gameView->setPresentationResources(m_projectAssetLibrary->resources());
    }
    const auto& stats = m_projectAssetLibrary->stats();
    appendConsoleMessage(
        tr("Loaded %1 presentation asset(s) (%2 source bytes); skipped %3 non-visual asset(s).")
            .arg(stats.loadedAssets)
            .arg(static_cast<qulonglong>(stats.sourceBytes))
            .arg(stats.skippedNonVisualAssets));
}

void MainWindow::updateProjectTargetProfileUi() {
    if (m_projectTargetProfileStatus == nullptr || m_targetCombo == nullptr) {
        return;
    }
    if (m_projectFilePath.isEmpty()) {
        m_projectTargetProfileStatus->setText(tr("No project"));
        m_projectTargetProfileStatus->setStyleSheet({});
        return;
    }
    const auto target = static_cast<BuildTarget>(m_targetCombo->currentData().toInt());
    const QString profile = target == BuildTarget::Pc ? m_projectData.targetProfiles.pc
                                                      : m_projectData.targetProfiles.esp32;
    if (currentTargetProfileSupported()) {
        m_projectTargetProfileStatus->setText(tr("%1 — supported").arg(profile));
        m_projectTargetProfileStatus->setStyleSheet(QStringLiteral("color: #65d46e;"));
    } else {
        m_projectTargetProfileStatus->setText(
            tr("%1 — unsupported by this Studio build").arg(profile));
        m_projectTargetProfileStatus->setStyleSheet(
            QStringLiteral("color: #ff6b6b; font-weight: bold;"));
    }
}

bool MainWindow::currentTargetProfileSupported() const {
    if (m_projectFilePath.isEmpty() || m_targetCombo == nullptr) {
        return false;
    }
    const auto target = static_cast<BuildTarget>(m_targetCombo->currentData().toInt());
    return target == BuildTarget::Pc
               ? m_projectData.targetProfiles.pc == QStringLiteral("pc.default")
               : m_projectData.targetProfiles.esp32 ==
                     QString::fromLatin1(WorkflowCommands::BoardProfile);
}

bool MainWindow::pcTargetProfileSupported() const {
    return !m_projectFilePath.isEmpty() &&
           m_projectData.targetProfiles.pc == QStringLiteral("pc.default");
}

void MainWindow::updateProjectTrustUi() {
    if (m_trustProjectAction == nullptr || m_projectTrustStatus == nullptr) {
        return;
    }
    const bool hasProject = !m_projectFilePath.isEmpty();
    const bool trusted = hasProject && m_projectTrustStore.isTrusted(m_projectFilePath);
    m_trustProjectAction->setEnabled(hasProject);
    m_trustProjectAction->setChecked(trusted);
    m_trustProjectAction->setText(trusted ? tr("Revoke Project &Trust")
                                          : tr("Trust Project for Code &Execution"));
    if (!hasProject) {
        m_projectTrustStatus->setText(tr("No project — local unsaved scene only"));
        m_projectTrustStatus->setStyleSheet({});
    } else if (trusted) {
        m_projectTrustStatus->setText(tr("Trusted — code execution enabled for this exact path"));
        m_projectTrustStatus->setStyleSheet(QStringLiteral("color: #65d46e;"));
    } else {
        m_projectTrustStatus->setText(
            tr("UNTRUSTED — build, Play, scripts, and package hooks blocked"));
        m_projectTrustStatus->setStyleSheet(QStringLiteral("color: #ff6b6b; font-weight: bold;"));
    }
    if (m_packageManager != nullptr) {
        m_packageManager->setProjectManifestPath(m_projectFilePath);
        m_packageManager->setProjectTrusted(trusted);
    }
    refreshExtensionServices();
    updateRunActions();
    updateWorkflowActions();
}

QString MainWindow::projectRoot() const {
    return m_projectFilePath.isEmpty() ? QDir::currentPath()
                                       : ProjectDocument::absoluteProjectRoot(
                                             m_projectFilePath, m_projectData.relativeRoot);
}

bool MainWindow::isNativeGameplaySource(const QString& filePath) const {
    if (m_projectFilePath.isEmpty() || filePath.trimmed().isEmpty()) {
        return false;
    }
    static const QStringList NativeSuffixes{
        QStringLiteral("c"),   QStringLiteral("cc"), QStringLiteral("cpp"), QStringLiteral("cxx"),
        QStringLiteral("h"),   QStringLiteral("hh"), QStringLiteral("hpp"), QStringLiteral("hxx"),
        QStringLiteral("ipp"), QStringLiteral("inl")};
    const QFileInfo sourceInfo(filePath);
    if (!sourceInfo.isFile() || !NativeSuffixes.contains(sourceInfo.suffix().toLower())) {
        return false;
    }
    const QFileInfo scriptsInfo(QDir(projectRoot()).absoluteFilePath(QStringLiteral("Scripts")));
    if (!scriptsInfo.isDir()) {
        return false;
    }

    const QString scriptsRoot = scriptsInfo.absoluteFilePath();
    const QString sourcePath = sourceInfo.absoluteFilePath();
    if (!pathInsideRoot(scriptsRoot, sourcePath) || pathCrossesLink(scriptsRoot, sourcePath)) {
        return false;
    }

    const QString canonicalScriptsRoot = scriptsInfo.canonicalFilePath();
    const QString canonicalSourcePath = sourceInfo.canonicalFilePath();
    return !canonicalScriptsRoot.isEmpty() && !canonicalSourcePath.isEmpty() &&
           pathInsideRoot(canonicalScriptsRoot, canonicalSourcePath);
}

QString MainWindow::selectedAssetPath() const {
    if (m_assetTree == nullptr || m_assetBrowserController == nullptr) {
        return {};
    }
    const auto index = m_assetTree->currentIndex();
    if (!index.isValid()) {
        return {};
    }
    const QString relativePath = index.data(AssetBrowserModel::RelativePathRole).toString();
    if (relativePath.isEmpty()) {
        return {};
    }
    const QString path = QDir(projectRoot()).filePath(relativePath);
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || info.isSymLink() ||
        !pathInsideRoot(projectRoot(), path)) {
        return {};
    }
    return info.absoluteFilePath();
}

void MainWindow::editSelectedAssetImportSettings() {
    if (m_assetTree == nullptr || m_assetBrowserController == nullptr) {
        return;
    }
    const auto index = m_assetTree->currentIndex();
    const auto* entry =
        index.isValid() ? m_assetBrowserController->model()->entryAt(index.row()) : nullptr;
    if (entry == nullptr || selectedAssetPath().isEmpty()) {
        statusBar()->showMessage(tr("Select a regular project asset file first."), 4000);
        return;
    }
    const auto guid = entry->guid;
    const auto relativePath = entry->relativePath;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Import Settings — %1").arg(relativePath));
    dialog.setMinimumWidth(560);
    auto* layout = new QVBoxLayout(&dialog);
    auto* summary =
        new QLabel(tr("Source: %1 bytes, modified %2\nCurrent importer: %3\n"
                      "PC payload: %4 bytes; ESP32 flash/RAM/PSRAM/SD: %5/%6/%7/%8 bytes\n"
                      "Estimated ESP32 decode: %9 µs; render: %10 pixels/frame")
                       .arg(entry->source.bytes)
                       .arg(entry->source.modifiedUtc.isValid()
                                ? entry->source.modifiedUtc.toLocalTime().toString(Qt::ISODate)
                                : tr("unknown"))
                       .arg(entry->importer)
                       .arg(entry->pcCost.payloadBytes)
                       .arg(entry->esp32Cost.flashBytes)
                       .arg(entry->esp32Cost.internalRamBytes)
                       .arg(entry->esp32Cost.psramBytes)
                       .arg(entry->esp32Cost.sdBytes)
                       .arg(entry->esp32Cost.estimatedDecodeMicros)
                       .arg(entry->esp32Cost.estimatedRenderPixelsPerFrame),
                   &dialog);
    summary->setWordWrap(true);
    auto* previewRow = new QWidget(&dialog);
    auto* previewLayout = new QHBoxLayout(previewRow);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    auto* preview = new QLabel(previewRow);
    preview->setObjectName(QStringLiteral("assetImportPreview"));
    const auto icon = index.data(Qt::DecorationRole).value<QIcon>();
    preview->setPixmap(icon.pixmap(96, 96));
    preview->setFixedSize(104, 104);
    preview->setAlignment(Qt::AlignCenter);
    previewLayout->addWidget(preview);
    previewLayout->addWidget(summary, 1);
    layout->addWidget(previewRow);

    const auto suffix = QFileInfo(relativePath).suffix().toLower();
    const bool imageSource = QStringList{QStringLiteral("png"), QStringLiteral("jpg"),
                                         QStringLiteral("jpeg"), QStringLiteral("bmp")}
                                 .contains(suffix);
    ImageImportSettingsWidget* imageSettings = nullptr;
    QPlainTextEdit* settingsEdit = nullptr;
    if (imageSource) {
        imageSettings = new ImageImportSettingsWidget(entry->normalizedSettings, &dialog);
        imageSettings->setObjectName(QStringLiteral("typedImageImportSettings"));
        auto* scroll = new QScrollArea(&dialog);
        scroll->setObjectName(QStringLiteral("typedImageImportSettingsScroll"));
        scroll->setWidgetResizable(true);
        scroll->setMinimumHeight(430);
        scroll->setWidget(imageSettings);
        layout->addWidget(scroll, 1);
        auto* note = new QLabel(
            tr("Preview and costs show the current imported result. Saving performs a validated "
               "reimport and refreshes Flash, RAM, PSRAM, SD, decode and render estimates."),
            &dialog);
        note->setWordWrap(true);
        layout->addWidget(note);
    } else {
        layout->addWidget(new QLabel(tr("Normalized importer settings (JSON object)"), &dialog));
        settingsEdit = new QPlainTextEdit(entry->normalizedSettings, &dialog);
        settingsEdit->setObjectName(QStringLiteral("assetImportSettingsJson"));
        settingsEdit->setMinimumHeight(150);
        layout->addWidget(settingsEdit);
    }
    auto* targetRow = new QWidget(&dialog);
    auto* targetLayout = new QHBoxLayout(targetRow);
    targetLayout->setContentsMargins(0, 0, 0, 0);
    targetLayout->addWidget(new QLabel(tr("ESP32 storage"), targetRow));
    auto* target = new QComboBox(targetRow);
    target->setObjectName(QStringLiteral("assetImportStorageTarget"));
    target->addItem(tr("Flash"), static_cast<int>(fabgl::assets::AssetTarget::Esp32Flash));
    target->addItem(tr("PSRAM"), static_cast<int>(fabgl::assets::AssetTarget::Esp32Psram));
    target->addItem(tr("SD card"), static_cast<int>(fabgl::assets::AssetTarget::Esp32Sd));
    target->setCurrentIndex(target->findData(static_cast<int>(entry->esp32Target)));
    targetLayout->addWidget(target, 1);
    layout->addWidget(targetRow);
    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const auto storage = static_cast<fabgl::assets::AssetTarget>(target->currentData().toInt());
    QString normalizedSettings;
    if (imageSettings != nullptr) {
        auto encoded = imageSettings->settingsJson();
        if (!encoded) {
            const auto message = engineError(encoded.error());
            QMessageBox::warning(this, tr("Import Settings Rejected"), message);
            return;
        }
        normalizedSettings = std::move(encoded.value());
    } else {
        normalizedSettings = settingsEdit->toPlainText();
    }
    auto configured =
        m_assetBrowserController->setImportSettings(guid, std::move(normalizedSettings), storage);
    if (configured) {
        configured = m_assetBrowserController->refreshNow();
    }
    if (!configured) {
        const auto message = engineError(configured.error());
        QMessageBox::warning(this, tr("Import Settings Rejected"), message);
        appendConsoleMessage(
            tr("Asset import settings for %1 were rejected: %2").arg(relativePath, message));
        return;
    }
    const auto* refreshedEntry = m_assetBrowserController->model()->entry(guid);
    if (refreshedEntry != nullptr) {
        for (auto& asset : m_projectData.assets) {
            if (asset.guid.compare(QString::fromStdString(guid.toString()), Qt::CaseInsensitive) !=
                0) {
                continue;
            }
            asset.importSettings = refreshedEntry->normalizedSettings;
            asset.type = refreshedEntry->type;
            asset.esp32Target =
                storage == fabgl::assets::AssetTarget::Esp32Psram ? QStringLiteral("psram")
                : storage == fabgl::assets::AssetTarget::Esp32Sd  ? QStringLiteral("sd")
                                                                  : QStringLiteral("flash");
            asset.dependencies.clear();
            for (const auto& dependency : refreshedEntry->dependencies)
                asset.dependencies.push_back(QString::fromStdString(dependency.toString()));
            std::sort(asset.dependencies.begin(), asset.dependencies.end());
            asset.hasImportMetadata = true;
            setDocumentModified(true);
            break;
        }
    }
    statusBar()->showMessage(tr("Reimported %1 with updated settings.").arg(relativePath), 4000);
}

void MainWindow::renameSelectedAsset() {
    const QString sourcePath = selectedAssetPath();
    if (sourcePath.isEmpty()) {
        statusBar()->showMessage(tr("Select a regular project asset file first."), 4000);
        return;
    }
    bool accepted = false;
    const QFileInfo sourceInfo(sourcePath);
    QString name = QInputDialog::getText(this, tr("Rename Asset"), tr("File name"),
                                         QLineEdit::Normal, sourceInfo.fileName(), &accepted)
                       .trimmed();
    if (!accepted || name.isEmpty()) {
        return;
    }
    if (name == QStringLiteral(".") || name == QStringLiteral("..") ||
        name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) {
        QMessageBox::warning(this, tr("Invalid Asset Name"),
                             tr("Use one file name without path separators."));
        return;
    }
    const QString destinationPath = QDir(sourceInfo.absolutePath()).filePath(name);
    if (QDir::cleanPath(destinationPath) == QDir::cleanPath(sourcePath)) {
        return;
    }
    m_undoStack.push(new MoveAssetCommand(
        sourcePath, destinationPath,
        [this](const QString& source, const QString& destination, QString& errorMessage) {
            return relocateProjectAsset(source, destination, errorMessage);
        },
        [this](const QString& errorMessage) {
            appendConsoleMessage(tr("Asset rename failed: %1").arg(errorMessage));
            QMessageBox::warning(this, tr("Asset Rename Failed"), errorMessage);
        }));
}

void MainWindow::moveSelectedAsset() {
    const QString sourcePath = selectedAssetPath();
    if (sourcePath.isEmpty()) {
        statusBar()->showMessage(tr("Select a regular project asset file first."), 4000);
        return;
    }
    const QString directory = QFileDialog::getExistingDirectory(
        this, tr("Move Asset Into Project Directory"), QFileInfo(sourcePath).absolutePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (directory.isEmpty()) {
        return;
    }
    if (!pathInsideRoot(projectRoot(), directory) || pathCrossesLink(projectRoot(), directory)) {
        QMessageBox::warning(
            this, tr("Unsafe Asset Destination"),
            tr("FabGL Studio only moves assets into regular directories inside the project."));
        return;
    }
    const QString destinationPath = QDir(directory).filePath(QFileInfo(sourcePath).fileName());
    if (QDir::cleanPath(destinationPath) == QDir::cleanPath(sourcePath)) {
        return;
    }
    m_undoStack.push(new MoveAssetCommand(
        sourcePath, destinationPath,
        [this](const QString& source, const QString& destination, QString& errorMessage) {
            return relocateProjectAsset(source, destination, errorMessage);
        },
        [this](const QString& errorMessage) {
            appendConsoleMessage(tr("Asset move failed: %1").arg(errorMessage));
            QMessageBox::warning(this, tr("Asset Move Failed"), errorMessage);
        }));
}

bool MainWindow::relocateProjectAsset(const QString& sourcePath, const QString& destinationPath,
                                      QString& errorMessage) {
    const QString root = projectRoot();
    const QFileInfo sourceInfo(sourcePath);
    const QFileInfo destinationInfo(destinationPath);
    const QFileInfo destinationDirectory(destinationInfo.absolutePath());
    if (m_projectFilePath.isEmpty() || !sourceInfo.exists() || !sourceInfo.isFile() ||
        sourceInfo.isSymLink() || !destinationDirectory.exists() || !destinationDirectory.isDir() ||
        destinationDirectory.isSymLink() || !pathInsideRoot(root, sourceInfo.absoluteFilePath()) ||
        !pathInsideRoot(root, destinationInfo.absoluteFilePath()) ||
        pathCrossesLink(root, sourceInfo.absoluteFilePath()) ||
        pathCrossesLink(root, destinationDirectory.absoluteFilePath())) {
        errorMessage = tr("Source and destination must be regular files/directories inside the "
                          "active project, without symbolic-link or junction traversal.");
        return false;
    }
    if (destinationInfo.exists()) {
        errorMessage = tr("The destination already exists: %1")
                           .arg(QDir::toNativeSeparators(destinationInfo.absoluteFilePath()));
        return false;
    }

    const QString sourceRelative =
        QDir::fromNativeSeparators(QDir(root).relativeFilePath(sourceInfo.absoluteFilePath()));
    const QString destinationRelative =
        QDir::fromNativeSeparators(QDir(root).relativeFilePath(destinationInfo.absoluteFilePath()));
    ProjectData updatedProject = m_projectData;
    std::optional<fabgl::AssetGuid> mappedGuid;
    for (auto& asset : updatedProject.assets) {
        if (asset.path.compare(sourceRelative, Qt::CaseInsensitive) == 0) {
            asset.path = destinationRelative;
            auto parsed = fabgl::AssetGuid::parse(asset.guid.toStdString());
            if (parsed) {
                mappedGuid = parsed.value();
            }
        }
    }
    if (!mappedGuid.has_value() && m_assetBrowserController != nullptr) {
        for (const auto& entry : m_assetBrowserController->model()->entries()) {
            if (entry.relativePath.compare(sourceRelative, Qt::CaseInsensitive) == 0) {
                mappedGuid = entry.guid;
                break;
            }
        }
    }
    if (!mappedGuid.has_value() || m_assetBrowserController == nullptr) {
        errorMessage = tr("The selected file has no Asset Browser GUID mapping.");
        return false;
    }
    QString validationError;
    if (ProjectDocument::serialized(updatedProject, validationError).isEmpty()) {
        errorMessage =
            tr("The relocated project asset mapping is invalid: %1").arg(validationError);
        return false;
    }
    const auto relocated =
        m_assetBrowserController->relocateAsset(mappedGuid.value(), destinationRelative);
    if (!relocated) {
        errorMessage = tr("Asset Browser refused to move %1 to %2: %3")
                           .arg(QDir::toNativeSeparators(sourceInfo.absoluteFilePath()),
                                QDir::toNativeSeparators(destinationInfo.absoluteFilePath()),
                                engineError(relocated.error()));
        return false;
    }

    m_projectData = std::move(updatedProject);
    updateProjectPanel();
    reloadPresentationAssets();
    renderCurrentScene();
    appendConsoleMessage(tr("Moved project asset %1 to %2; GUID references were preserved.")
                             .arg(sourceRelative, destinationRelative));
    errorMessage.clear();
    return true;
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

void MainWindow::addEntityFromAsset(const QString& filePath, const float x, const float y,
                                    const float z) {
    if (m_runState != RunState::Editing) {
        return;
    }
    EntitySnapshot entity;
    entity.id = fabgl::EntityGuid::generate();
    entity.name = uniqueEntityName(QFileInfo(filePath).completeBaseName());
    entity.position = {x, y, z};
    const auto id = entity.id;
    m_undoStack.push(new AddEntityCommand(&m_sceneDocument, entity));
    selectEntityRow(m_entities.rowForId(id));
    appendConsoleMessage(tr("Created %1 from dropped asset %2.")
                             .arg(entity.name, QDir::toNativeSeparators(filePath)));
}

void MainWindow::duplicateSelectedEntities() {
    if (m_runState != RunState::Editing) {
        return;
    }
    auto ids = selectedEntityIds();
    if (ids.empty()) {
        return;
    }
    std::vector<EntitySnapshot> snapshots;
    snapshots.reserve(ids.size());
    for (const auto id : ids) {
        if (auto snapshot = m_sceneDocument.snapshot(id)) {
            snapshots.push_back(std::move(*snapshot));
        }
    }
    const auto depth = [this](const EntitySnapshot& snapshot) {
        int value = 0;
        auto parent = snapshot.parent;
        while (parent && value < 128) {
            const auto ancestor = m_sceneDocument.snapshot(*parent);
            parent = ancestor ? ancestor->parent : std::nullopt;
            ++value;
        }
        return value;
    };
    std::stable_sort(
        snapshots.begin(), snapshots.end(),
        [&depth](const auto& left, const auto& right) { return depth(left) < depth(right); });

    std::vector<std::pair<fabgl::EntityGuid, fabgl::EntityGuid>> replacements;
    replacements.reserve(snapshots.size());
    for (const auto& snapshot : snapshots) {
        replacements.emplace_back(snapshot.id, fabgl::EntityGuid::generate());
    }
    const auto replacementFor =
        [&replacements](const fabgl::EntityGuid id) -> std::optional<fabgl::EntityGuid> {
        const auto found = std::find_if(replacements.cbegin(), replacements.cend(),
                                        [id](const auto& item) { return item.first == id; });
        return found == replacements.cend() ? std::nullopt
                                            : std::optional<fabgl::EntityGuid>(found->second);
    };

    auto* transaction = new QUndoCommand(snapshots.size() == 1U
                                             ? tr("Duplicate entity")
                                             : tr("Duplicate %1 entities").arg(snapshots.size()));
    QStringList duplicatedGuids;
    for (auto snapshot : snapshots) {
        snapshot.id = *replacementFor(snapshot.id);
        snapshot.name = uniqueEntityName(snapshot.name + tr(" Copy"));
        snapshot.position.x += 0.5F;
        snapshot.position.y += 0.5F;
        snapshot.children.clear();
        if (snapshot.parent) {
            if (const auto replacement = replacementFor(*snapshot.parent)) {
                snapshot.parent = *replacement;
            }
        }
        duplicatedGuids.push_back(SceneDocument::guidString(snapshot.id));
        (void)new AddEntityCommand(&m_sceneDocument, std::move(snapshot), transaction);
    }
    m_undoStack.push(transaction);
    selectEntityGuids(duplicatedGuids);
    appendConsoleMessage(tr("Duplicated %1 selected entity(s) as one undo transaction.")
                             .arg(duplicatedGuids.size()));
}

void MainWindow::deleteSelectedEntity() {
    if (m_runState != RunState::Editing) {
        return;
    }
    auto ids = selectedEntityIds();
    if (ids.empty()) {
        return;
    }
    const auto depth = [this](const fabgl::EntityGuid id) {
        int value = 0;
        auto snapshot = m_sceneDocument.snapshot(id);
        auto parent = snapshot ? snapshot->parent : std::nullopt;
        while (parent && value < 128) {
            snapshot = m_sceneDocument.snapshot(*parent);
            parent = snapshot ? snapshot->parent : std::nullopt;
            ++value;
        }
        return value;
    };
    std::stable_sort(ids.begin(), ids.end(), [&depth](const auto left, const auto right) {
        return depth(left) > depth(right);
    });
    if (ids.size() == 1U) {
        const auto entity = m_sceneDocument.snapshot(ids.front());
        m_undoStack.push(new DeleteEntityCommand(&m_sceneDocument, ids.front()));
        appendConsoleMessage(tr("Deleted %1; Undo restores its GUID and transform.")
                                 .arg(entity ? entity->name : tr("entity")));
        return;
    }
    auto* transaction =
        new QUndoCommand(tr("Delete %1 entities").arg(static_cast<qulonglong>(ids.size())));
    for (const auto id : ids) {
        (void)new DeleteEntityCommand(&m_sceneDocument, id, transaction);
    }
    m_undoStack.push(transaction);
    appendConsoleMessage(tr("Deleted %1 entities as one undo transaction.")
                             .arg(static_cast<qulonglong>(ids.size())));
}

void MainWindow::setSelectedEntitiesParent() {
    const auto selected = selectedEntityIds();
    if (m_runState != RunState::Editing || selected.empty()) {
        return;
    }
    QStringList labels;
    std::vector<fabgl::EntityGuid> candidates;
    for (const auto* entity : m_sceneDocument.scene().entities()) {
        if (std::find(selected.cbegin(), selected.cend(), entity->id()) != selected.cend()) {
            continue;
        }
        bool wouldCycle = false;
        auto parent = entity->transform().parent();
        auto cursor = entity->id();
        while (!wouldCycle) {
            wouldCycle = std::find(selected.cbegin(), selected.cend(), cursor) != selected.cend();
            if (wouldCycle || !parent) {
                break;
            }
            cursor = *parent;
            const auto* ancestor = m_sceneDocument.scene().findEntity(cursor);
            parent = ancestor != nullptr ? ancestor->transform().parent() : std::nullopt;
        }
        if (!wouldCycle) {
            candidates.push_back(entity->id());
            labels.push_back(tr("%1 — %2").arg(QString::fromStdString(entity->name()),
                                               SceneDocument::guidString(entity->id())));
        }
    }
    if (candidates.empty()) {
        statusBar()->showMessage(tr("No cycle-safe parent candidate is available."), 4000);
        return;
    }
    bool accepted = false;
    const auto label = QInputDialog::getItem(
        this, tr("Set Parent"), tr("Parent for %1 selected entity(s):").arg(selected.size()),
        labels, 0, false, &accepted);
    const qsizetype index = labels.indexOf(label);
    if (!accepted || index < 0) {
        return;
    }
    const auto target = candidates.at(static_cast<std::size_t>(index));
    auto* transaction =
        new QUndoCommand(tr("Reparent %1 entities").arg(static_cast<qulonglong>(selected.size())));
    std::size_t changes = 0U;
    for (const auto id : selected) {
        const auto before = m_sceneDocument.snapshot(id);
        if (!before || before->parent == target) {
            continue;
        }
        auto after = *before;
        after.parent = target;
        (void)new EditEntityCommand(&m_sceneDocument, *before, std::move(after),
                                    tr("Set entity parent"), transaction);
        ++changes;
    }
    if (changes == 0U) {
        delete transaction;
        return;
    }
    m_undoStack.push(transaction);
}

void MainWindow::clearSelectedEntitiesParent() {
    const auto selected = selectedEntityIds();
    if (m_runState != RunState::Editing || selected.empty()) {
        return;
    }
    auto* transaction = new QUndoCommand(
        tr("Move %1 entities to scene root").arg(static_cast<qulonglong>(selected.size())));
    std::size_t changes = 0U;
    for (const auto id : selected) {
        const auto before = m_sceneDocument.snapshot(id);
        if (!before || !before->parent) {
            continue;
        }
        auto after = *before;
        after.parent.reset();
        (void)new EditEntityCommand(&m_sceneDocument, *before, std::move(after),
                                    tr("Clear entity parent"), transaction);
        ++changes;
    }
    if (changes == 0U) {
        delete transaction;
        return;
    }
    m_undoStack.push(transaction);
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

int MainWindow::selectedEntityRow() const {
    return m_hierarchyView != nullptr && m_hierarchyView->currentIndex().isValid()
               ? m_hierarchyView->currentIndex().row()
               : -1;
}

std::optional<fabgl::EntityGuid> MainWindow::selectedEntityId() const {
    return m_entities.entityIdAt(selectedEntityRow());
}

std::vector<fabgl::EntityGuid> MainWindow::selectedEntityIds() const {
    std::vector<fabgl::EntityGuid> result;
    if (m_hierarchyView == nullptr || m_hierarchyView->selectionModel() == nullptr) {
        return result;
    }
    const auto rows = m_hierarchyView->selectionModel()->selectedRows();
    result.reserve(static_cast<std::size_t>(rows.size()));
    for (const auto& row : rows) {
        if (const auto id = m_entities.entityIdAt(row.row())) {
            result.push_back(*id);
        }
    }
    return result;
}

void MainWindow::selectEntityRow(const int row) {
    const auto id =
        row >= 0 && row < m_entities.rowCount() ? m_entities.entityIdAt(row) : std::nullopt;
    selectEntityGuids(id ? QStringList{SceneDocument::guidString(*id)} : QStringList{});
}

void MainWindow::selectEntityGuid(const QString& guid) {
    selectEntityGuids(guid.isEmpty() ? QStringList{} : QStringList{guid});
}

void MainWindow::selectEntityGuids(const QStringList& guids) {
    if (m_hierarchyView == nullptr || m_hierarchyView->selectionModel() == nullptr) {
        return;
    }
    QStringList normalized;
    QModelIndex primary;
    {
        const QSignalBlocker blocker(m_hierarchyView->selectionModel());
        m_hierarchyView->selectionModel()->clearSelection();
        for (const auto& guid : guids) {
            const auto id = SceneDocument::parseEntityGuid(guid);
            const int row = id ? m_entities.rowForId(*id) : -1;
            if (row < 0) {
                continue;
            }
            const auto index = m_entities.index(row, 0);
            m_hierarchyView->selectionModel()->select(index, QItemSelectionModel::Select |
                                                                 QItemSelectionModel::Rows);
            primary = index;
            if (!normalized.contains(guid)) {
                normalized.push_back(guid);
            }
        }
        m_hierarchyView->selectionModel()->setCurrentIndex(primary, QItemSelectionModel::NoUpdate);
    }
    m_sceneView->setSelectedEntities(normalized);
    updateInspector(primary);
    updateRunActions();
}

void MainWindow::updateInspector(const QModelIndex& index) {
    m_updatingInspector = true;
    const auto id = index.isValid() ? m_entities.entityIdAt(index.row()) : std::nullopt;
    const auto entity = id ? m_sceneDocument.snapshot(*id) : std::nullopt;
    auto selected = selectedEntityIds();
    if (selected.empty() && id)
        selected.push_back(*id);
    const bool componentEditingEnabled = !selected.empty() && m_runState == RunState::Editing;
    const bool singleEntityEditingEnabled =
        selected.size() == 1U && entity.has_value() && m_runState == RunState::Editing;
    m_entityNameEdit->setEnabled(singleEntityEditingEnabled);
    m_entityActiveCheck->setEnabled(singleEntityEditingEnabled);
    m_deleteEntityAction->setEnabled(componentEditingEnabled);
    if (!entity || selected.empty()) {
        m_entityNameEdit->clear();
        m_entityActiveCheck->setChecked(false);
        m_entityIdLabel->setText(tr("No selection"));
        m_componentInspector->setEntities({}, false);
        m_updatingInspector = false;
        return;
    }
    if (selected.size() == 1U) {
        m_entityNameEdit->setPlaceholderText({});
        m_entityNameEdit->setText(entity->name);
        m_entityActiveCheck->setChecked(entity->active);
        m_entityIdLabel->setText(SceneDocument::guidString(entity->id));
    } else {
        m_entityNameEdit->clear();
        m_entityNameEdit->setPlaceholderText(tr("Multiple values"));
        m_entityActiveCheck->setChecked(false);
        m_entityIdLabel->setText(
            tr("%1 entities selected").arg(static_cast<qulonglong>(selected.size())));
    }
    m_componentInspector->setEntities(std::move(selected), componentEditingEnabled);
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
    if (m_pcRunner.isRunning()) {
        statusBar()->showMessage(tr("Stop the external PC player before starting Studio Play."),
                                 5000);
        return;
    }
    if (!currentProjectTrusted()) {
        QMessageBox::warning(
            this, tr("Play Blocked for Untrusted Project"),
            tr("Trust this project path explicitly before running scripts or entering Play mode."));
        return;
    }
    if (!m_projectFilePath.isEmpty() && m_launchOptions.pluginsEnabled &&
        !m_launchOptions.safeMode && m_projectExtensions == nullptr) {
        QMessageBox::warning(
            this, tr("Play Blocked by Extension Failure"),
            tr("This project's extension host did not initialize successfully. Reopen the "
               "project or disable plugins explicitly before entering Play mode."));
        return;
    }
    if (m_runState == RunState::Paused) {
        m_gameView->setRuntimeInputEnabled(true);
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
    auto playScene = m_sceneDocument.cloneScene(errorMessage);
    if (playScene == nullptr) {
        QMessageBox::critical(this, tr("Play Failed"), errorMessage);
        return;
    }

    const auto serializedManifest = ProjectDocument::serialized(m_projectData, errorMessage);
    if (serializedManifest.isEmpty()) {
        QMessageBox::critical(this, tr("Play Failed"), errorMessage);
        return;
    }
    auto manifest = fabgl::project::parseManifest(std::string_view(
        serializedManifest.constData(), static_cast<std::size_t>(serializedManifest.size())));
    if (!manifest) {
        QMessageBox::critical(this, tr("Play Failed"), engineError(manifest.error()));
        return;
    }

    StudioPlaySessionConfig config;
    const QByteArray encodedRoot = projectRoot().toUtf8();
    config.projectRoot.assign(encodedRoot.constData(),
                              static_cast<std::size_t>(encodedRoot.size()));
    config.manifest = std::move(manifest.value());
    if (!m_lastPcScriptModule.isEmpty()) {
        const QByteArray encodedModule = m_lastPcScriptModule.toUtf8();
        config.nativeScriptModules.emplace_back(encodedModule.constData(),
                                                static_cast<std::size_t>(encodedModule.size()));
    }
#if defined(_WIN32)
    auto audioOutput = std::make_unique<fabgl::player::Win32AudioOutput>();
    std::string audioError;
    if (audioOutput->open(48'000U, audioError)) {
        config.audioOutput = audioOutput.get();
    } else {
        appendConsoleMessage(
            tr("Studio Play audio device unavailable; bounded mixer remains active: %1")
                .arg(QString::fromStdString(audioError)));
        audioOutput.reset();
    }
#endif
    auto created = StudioPlaySession::create(std::move(playScene), std::move(config));
    if (!created) {
        QMessageBox::critical(this, tr("Play Failed"), engineError(created.error()));
        return;
    }
    m_playSession = std::move(created.value());
#if defined(_WIN32)
    m_playAudioOutput = std::move(audioOutput);
#endif
    auto initialized = m_playSession->initialize();
    if (!initialized) {
        QMessageBox::critical(this, tr("Play Failed"), engineError(initialized.error()));
        m_playSession.reset();
#if defined(_WIN32)
        m_playAudioOutput.reset();
#endif
        return;
    }
    if (m_projectExtensions != nullptr) {
        auto context = extensionHostContext(m_extensionProjectManifestPath, m_extensionProjectRoot,
                                            m_playSession->scene(), &m_playSession->runtime());
        auto started = m_projectExtensions->invokeAll(
            fabgl::project::RuntimeStartExtensionCapability, {"start", "studio", &context});
        if (!started) {
            static_cast<void>(
                m_projectExtensions->invokeAll(fabgl::project::RuntimeStopExtensionCapability,
                                               {"stop", "runtime-start-failed", &context}));
            m_playSession->shutdown();
            m_playSession.reset();
#if defined(_WIN32)
            m_playAudioOutput.reset();
#endif
            QString shutdownError;
            static_cast<void>(deactivateProjectExtensions(shutdownError));
            QMessageBox::critical(
                this, tr("Play Failed"),
                tr("A project runtime extension failed to start and all project extensions were "
                   "disabled: %1")
                    .arg(engineError(started.error())));
            return;
        }
    }
    if (m_projectExtensionServices != nullptr) {
        auto context = extensionHostContext(m_extensionProjectManifestPath, m_extensionProjectRoot,
                                            m_playSession->scene(), &m_playSession->runtime());
        const auto report = m_projectExtensionServices->runtimeStart(context, "studio");
        static_cast<void>(reportExtensionServiceFailures(tr("runtime startup"), report, false));
    }
    const auto runtimeStats = m_playSession->stats();
    m_gameView->setPresentationResources(m_playSession->presentationResources());
    m_gameView->setRuntimeInputEnabled(true);
    m_lastRuntimeMetrics = {};
    m_simulationElapsed = 0.0;
    m_frameClock.start();
    setRunState(RunState::Playing);
    m_playTimer->start();
    renderCurrentScene();
    appendConsoleMessage(tr("Studio Play started with project runtime parity; edit scene isolated. "
                            "animators=%1 visual_scripts=%2 gameplay_entities=%3 audio_voices=%4 "
                            "native_scripts=%5")
                             .arg(runtimeStats.animators)
                             .arg(runtimeStats.visualScripts)
                             .arg(runtimeStats.controlledGameplayEntities)
                             .arg(runtimeStats.activeAudioVoices)
                             .arg(runtimeStats.nativeScriptComponents));
}

void MainWindow::pause() {
    if (m_runState != RunState::Playing) {
        return;
    }
    m_gameView->setRuntimeInputEnabled(false);
    m_playTimer->stop();
    setRunState(RunState::Paused);
    appendConsoleMessage(tr("Runtime scene paused."));
}

void MainWindow::step() {
    if (m_runState != RunState::Paused || m_playSession == nullptr) {
        return;
    }
    const int targetFps = m_gameView != nullptr ? m_gameView->targetFps() : 60;
    const double speed = m_gameView != nullptr ? m_gameView->simulationSpeed() : 1.0;
    advancePlayScene(static_cast<float>(speed / static_cast<double>(targetFps)));
    renderCurrentScene();
    appendConsoleMessage(tr("Runtime scene advanced by one 1/60 s step."));
}

void MainWindow::stop() {
    if (m_runState == RunState::Editing) {
        return;
    }
    m_gameView->setRuntimeInputEnabled(false);
    m_playTimer->stop();
    QString runtimeExtensionError;
    if (m_playSession != nullptr && m_projectExtensionServices != nullptr) {
        auto context = extensionHostContext(m_extensionProjectManifestPath, m_extensionProjectRoot,
                                            m_playSession->scene(), &m_playSession->runtime());
        const auto report = m_projectExtensionServices->runtimeStop(context, "studio");
        static_cast<void>(reportExtensionServiceFailures(tr("runtime shutdown"), report, false));
    }
    if (m_playSession != nullptr && m_projectExtensions != nullptr) {
        auto context = extensionHostContext(m_extensionProjectManifestPath, m_extensionProjectRoot,
                                            m_playSession->scene(), &m_playSession->runtime());
        auto stopped = m_projectExtensions->invokeAll(
            fabgl::project::RuntimeStopExtensionCapability, {"stop", "studio", &context});
        if (!stopped) {
            runtimeExtensionError = engineError(stopped.error());
        }
    }
    if (m_playSession != nullptr) {
        m_playSession->shutdown();
    }
    m_playSession.reset();
    if (!runtimeExtensionError.isEmpty()) {
        QString shutdownError;
        static_cast<void>(deactivateProjectExtensions(shutdownError));
        appendConsoleMessage(
            tr("Runtime extension shutdown failed; all project extensions were disabled: %1")
                .arg(runtimeExtensionError));
    }
#if defined(_WIN32)
    m_playAudioOutput.reset();
#endif
    m_lastRuntimeMetrics = {};
    m_gameView->setPresentationResources(m_projectAssetLibrary != nullptr
                                             ? m_projectAssetLibrary->resources()
                                             : fabgl::rendering::ScenePresentationResources{});
    setRunState(RunState::Editing);
    renderCurrentScene();
    appendConsoleMessage(tr("Play stopped; the authoring scene remained unchanged."));
}

void MainWindow::tickPlayMode() {
    if (m_runState != RunState::Playing || m_playSession == nullptr) {
        return;
    }
    const qint64 elapsedMs = m_frameClock.restart();
    const double speed = m_gameView != nullptr ? m_gameView->simulationSpeed() : 1.0;
    const float deltaTime =
        std::clamp(static_cast<float>(static_cast<double>(elapsedMs) / 1000.0 * speed), 0.0F, 0.1F);
    advancePlayScene(deltaTime);
    renderCurrentScene();
}

void MainWindow::advancePlayScene(const float deltaTime) {
    if (m_playSession == nullptr) {
        return;
    }
    auto advanced = m_playSession->tick(static_cast<double>(deltaTime));
    m_playSession->clearTransientControls();
    if (!advanced) {
        const auto& error = advanced.error();
        appendConsoleMessage(tr("Runtime update error: %1").arg(engineError(error)));
        pause();
        return;
    }
    m_lastRuntimeMetrics = advanced.value();
    m_simulationElapsed += m_lastRuntimeMetrics.simulatedDeltaSeconds;
    if (m_projectExtensionServices != nullptr) {
        auto context = extensionHostContext(m_extensionProjectManifestPath, m_extensionProjectRoot,
                                            m_playSession->scene(), &m_playSession->runtime());
        const auto report = m_projectExtensionServices->runtimeUpdate(context, deltaTime);
        static_cast<void>(reportExtensionServiceFailures(tr("runtime update"), report, false));
    }

    const auto recordPhase = [this](const char* metric, const double seconds) {
        (void)m_engineProfiler.recordMeasured(metric, seconds * 1000.0,
                                              fabgl::ProfilerUnit::Milliseconds,
                                              fabgl::ProfilerSampleSource::MeasuredPc);
    };
    recordPhase("pc.runtime", m_lastRuntimeMetrics.measuredCpuSeconds);
    recordPhase("pc.fixed_update", m_lastRuntimeMetrics.fixedUpdateCpuSeconds);
    recordPhase("pc.physics", m_lastRuntimeMetrics.physicsCpuSeconds);
    recordPhase("pc.update", m_lastRuntimeMetrics.updateCpuSeconds);
    recordPhase("pc.ai", m_lastRuntimeMetrics.aiCpuSeconds);
    recordPhase("pc.animation", m_lastRuntimeMetrics.animationCpuSeconds);
    recordPhase("pc.audio", m_lastRuntimeMetrics.audioCpuSeconds);
    recordPhase("pc.asset_streaming", m_lastRuntimeMetrics.assetStreamingCpuSeconds);

    constexpr std::uint64_t RuntimeTimelineSampleInterval = 30U;
    if (m_profilerTimeline != nullptr &&
        (m_lastRuntimeMetrics.frameIndex == 1U ||
         m_lastRuntimeMetrics.frameIndex % RuntimeTimelineSampleInterval == 0U)) {
        QString errorMessage;
        (void)m_profilerTimeline->recordMeasuredPc(QStringLiteral("pc.runtime"),
                                                   m_lastRuntimeMetrics.measuredCpuSeconds * 1000.0,
                                                   fabgl::ProfilerUnit::Milliseconds, errorMessage);
        (void)m_profilerTimeline->recordMeasuredPc(QStringLiteral("pc.ai"),
                                                   m_lastRuntimeMetrics.aiCpuSeconds * 1000.0,
                                                   fabgl::ProfilerUnit::Milliseconds, errorMessage);
    }
}

void MainWindow::renderCurrentScene() {
    if (m_gameView == nullptr) {
        return;
    }
    const auto& scene = m_playSession != nullptr ? m_playSession->scene() : m_sceneDocument.scene();
    const auto* runtime = m_playSession != nullptr ? &m_playSession->runtime() : nullptr;
    m_lastFrameStats = m_gameView->renderScene(scene, m_simulationElapsed, runtime);
    if (m_playSession != nullptr) {
        m_lastFrameStats.pcFrameMilliseconds += m_lastRuntimeMetrics.measuredCpuSeconds * 1000.0;
    }
    (void)m_engineProfiler.recordMeasured("pc.frame", m_lastFrameStats.pcFrameMilliseconds,
                                          fabgl::ProfilerUnit::Milliseconds,
                                          fabgl::ProfilerSampleSource::MeasuredPc);
    (void)m_engineProfiler.recordMeasured(
        "pc.draw_calls", static_cast<double>(m_lastFrameStats.drawCalls),
        fabgl::ProfilerUnit::Count, fabgl::ProfilerSampleSource::MeasuredPc);
    (void)m_engineProfiler.recordMeasured(
        "pc.triangles", static_cast<double>(m_lastFrameStats.triangles), fabgl::ProfilerUnit::Count,
        fabgl::ProfilerSampleSource::MeasuredPc);
    (void)m_engineProfiler.recordMeasured("pc.rays", static_cast<double>(m_lastFrameStats.rays),
                                          fabgl::ProfilerUnit::Count,
                                          fabgl::ProfilerSampleSource::MeasuredPc);
    (void)m_engineProfiler.recordMeasured(
        "pc.particles", static_cast<double>(m_lastFrameStats.particles), fabgl::ProfilerUnit::Count,
        fabgl::ProfilerSampleSource::MeasuredPc);
    const double estimatedEsp32Frame = m_lastFrameStats.pcFrameMilliseconds * 4.0 +
                                       static_cast<double>(m_lastFrameStats.drawCalls) * 0.05;
    (void)m_engineProfiler.recordEstimated("esp32.frame.estimated", estimatedEsp32Frame,
                                           fabgl::ProfilerUnit::Milliseconds);
    (void)m_engineProfiler.recordEstimated("esp32.draw_calls",
                                           static_cast<double>(m_lastFrameStats.drawCalls),
                                           fabgl::ProfilerUnit::Count);
    if (m_profilerTimeline != nullptr) {
        QString errorMessage;
        (void)m_profilerTimeline->recordMeasuredPc(QStringLiteral("pc.frame"),
                                                   m_lastFrameStats.pcFrameMilliseconds,
                                                   fabgl::ProfilerUnit::Milliseconds, errorMessage);
        (void)m_profilerTimeline->recordEstimatedEsp32(
            QStringLiteral("esp32.frame"), estimatedEsp32Frame, fabgl::ProfilerUnit::Milliseconds,
            errorMessage);
    }
    updateProfiler();
}

void MainWindow::recordSerialProfilerMetrics(const QString& text) {
    if (m_profilerTimeline == nullptr || text.isEmpty()) {
        return;
    }
    constexpr qsizetype MaximumPendingMetricText = 64 * 1024;
    m_serialMetricBuffer += text;
    if (m_serialMetricBuffer.size() > MaximumPendingMetricText) {
        m_serialMetricBuffer = m_serialMetricBuffer.right(MaximumPendingMetricText);
    }
    for (;;) {
        const auto newline = m_serialMetricBuffer.indexOf(QLatin1Char('\n'));
        if (newline < 0) {
            break;
        }
        const QString line = m_serialMetricBuffer.left(newline).trimmed();
        m_serialMetricBuffer.remove(0, newline + 1);
        if (!line.startsWith(QStringLiteral("FABGLSTUDIO|1|METRIC|runtime|"))) {
            continue;
        }
        const auto valueFor = [&line](const QString& key) -> std::optional<double> {
            const QRegularExpression expression(
                QStringLiteral("(?:^|[|;])%1=([-+]?[0-9]+(?:\\.[0-9]+)?)")
                    .arg(QRegularExpression::escape(key)));
            const auto match = expression.match(line);
            if (!match.hasMatch()) {
                return std::nullopt;
            }
            bool ok = false;
            const double value = match.captured(1).toDouble(&ok);
            return ok ? std::optional<double>{value} : std::nullopt;
        };
        QString errorMessage;
        if (const auto fps = valueFor(QStringLiteral("fps")); fps && *fps > 0.0) {
            m_lastMeasuredEsp32FrameMilliseconds = 1000.0 / *fps;
            (void)m_profilerTimeline->recordMeasuredEsp32(
                QStringLiteral("esp32.frame"), *m_lastMeasuredEsp32FrameMilliseconds,
                fabgl::ProfilerUnit::Milliseconds, errorMessage);
        }
        const auto heap = valueFor(QStringLiteral("heapFree"));
        if (heap && std::isfinite(*heap) && *heap >= 0.0 &&
            *heap <= static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
            m_lastMeasuredEsp32HeapFreeBytes = static_cast<std::uint64_t>(*heap);
            (void)m_profilerTimeline->recordMeasuredEsp32(QStringLiteral("esp32.heap_free"), *heap,
                                                          fabgl::ProfilerUnit::Bytes, errorMessage);
        }
        const auto largestBlock = valueFor(QStringLiteral("largestBlock"));
        if (heap && largestBlock && std::isfinite(*heap) && std::isfinite(*largestBlock) &&
            *heap > 0.0 && *largestBlock >= 0.0 && *largestBlock <= *heap) {
            m_lastMeasuredEsp32HeapFragmentationPercent =
                std::clamp((1.0 - *largestBlock / *heap) * 100.0, 0.0, 100.0);
            (void)m_profilerTimeline->recordMeasuredEsp32(
                QStringLiteral("esp32.heap_fragmentation"),
                *m_lastMeasuredEsp32HeapFragmentationPercent, fabgl::ProfilerUnit::Percent,
                errorMessage);
        }
        if (const auto sdRead = valueFor(QStringLiteral("sdReadBytes"));
            sdRead && std::isfinite(*sdRead) && *sdRead >= 0.0 &&
            *sdRead <= static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
            m_lastMeasuredEsp32SdReadBytes = static_cast<std::uint64_t>(*sdRead);
            (void)m_profilerTimeline->recordMeasuredEsp32(QStringLiteral("esp32.sd_read"), *sdRead,
                                                          fabgl::ProfilerUnit::Bytes, errorMessage);
        }
    }
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
    const bool executionAllowed = currentProjectTrusted();
    m_playAction->setEnabled((editing || paused) && executionAllowed && !m_pcRunner.isRunning());
    m_playAction->setText(paused ? tr("&Resume") : tr("&Play"));
    m_pauseAction->setEnabled(playing);
    m_stepAction->setEnabled(paused);
    m_stopAction->setEnabled(!editing);
    m_addEntityAction->setEnabled(editing);
    const auto selection = selectedEntityIds();
    const bool hasSelection = !selection.empty();
    m_duplicateEntityAction->setEnabled(editing && hasSelection);
    m_deleteEntityAction->setEnabled(editing && hasSelection);
    m_setParentAction->setEnabled(editing && hasSelection);
    const bool hasParent = std::any_of(selection.cbegin(), selection.cend(), [this](const auto id) {
        const auto snapshot = m_sceneDocument.snapshot(id);
        return snapshot && snapshot->parent.has_value();
    });
    m_clearParentAction->setEnabled(editing && hasParent);
    const bool hasSelectedAsset = editing && !selectedAssetPath().isEmpty();
    m_assetImportSettingsAction->setEnabled(hasSelectedAsset);
    m_renameAssetAction->setEnabled(hasSelectedAsset);
    m_moveAssetAction->setEnabled(hasSelectedAsset);
    m_renameEntityAction->setEnabled(editing && selectedEntityId().has_value());
    m_focusInspectorAction->setEnabled(editing && selectedEntityId().has_value());
    m_snapAction->setEnabled(editing);
    m_selectToolAction->setEnabled(true);
    m_moveToolAction->setEnabled(editing);
    m_rotateToolAction->setEnabled(editing);
    m_scaleToolAction->setEnabled(editing);
    m_frameSelectedAction->setEnabled(selectedEntityId().has_value());
    m_zoomInAction->setEnabled(true);
    m_zoomOutAction->setEnabled(true);
    m_undoAction->setEnabled(editing && m_undoStack.canUndo());
    m_redoAction->setEnabled(editing && m_undoStack.canRedo());
}

void MainWindow::updateProfiler() {
    if (m_profiler == nullptr) {
        return;
    }
    const auto pcBudget = fabgl::project::selectedPerformanceBudget(
        m_projectData.performance, fabgl::project::PerformanceTarget::Pc);
    const auto esp32Budget = fabgl::project::selectedPerformanceBudget(
        m_projectData.performance, fabgl::project::PerformanceTarget::Esp32);
    const double estimatedEsp32Frame = m_lastFrameStats.pcFrameMilliseconds * 4.0 +
                                       static_cast<double>(m_lastFrameStats.drawCalls) * 0.05;
    const QString state = m_runState == RunState::Editing
                              ? tr("Editing")
                              : (m_runState == RunState::Playing ? tr("Playing") : tr("Paused"));
    const auto millisecondsFromSeconds = [](const double seconds) {
        return QStringLiteral("%1 ms").arg(seconds * 1000.0, 0, 'f', 3);
    };
    const auto milliseconds = [](const double value) {
        return QStringLiteral("%1 ms").arg(value, 0, 'f', 3);
    };
    const auto bytes = [](const std::uint64_t value) {
        constexpr double KiB = 1024.0;
        constexpr double MiB = KiB * 1024.0;
        if (value >= static_cast<std::uint64_t>(MiB))
            return QStringLiteral("%1 MiB").arg(static_cast<double>(value) / MiB, 0, 'f', 2);
        if (value >= static_cast<std::uint64_t>(KiB))
            return QStringLiteral("%1 KiB").arg(static_cast<double>(value) / KiB, 0, 'f', 2);
        return QStringLiteral("%1 B").arg(value);
    };
    struct ProfilerRow final {
        QString metric;
        QString source;
        QString value;
        QString budget;
        bool warning = false;
        bool error = false;
        QString recommendation;
    };
    QVector<ProfilerRow> rows;
    const auto addNumeric = [&rows](QString metric, QString source, QString value, QString budget,
                                    const double observation, const double limit,
                                    const fabgl::project::PerformanceMetric kind) {
        const bool warning = limit > 0.0 && observation > limit;
        rows.push_back({std::move(metric), std::move(source), std::move(value), std::move(budget),
                        warning, warning && observation > limit * 1.25,
                        warning ? QString::fromStdString(
                                      fabgl::project::performanceOptimizationRecommendation(kind))
                                : QString{}});
    };
    const auto addUnavailable = [&rows](QString metric, QString budget) {
        rows.push_back({std::move(metric), QObject::tr("Unavailable"), QObject::tr("—"),
                        std::move(budget), false, false,
                        QObject::tr("No runtime counter is exposed; no value is inferred.")});
    };
    addNumeric(
        tr("Total frame"), tr("Measured PC"), milliseconds(m_lastFrameStats.pcFrameMilliseconds),
        milliseconds(pcBudget.frameTotalMilliseconds), m_lastFrameStats.pcFrameMilliseconds,
        pcBudget.frameTotalMilliseconds, fabgl::project::PerformanceMetric::FrameTotalMilliseconds);
    addNumeric(tr("Runtime total"), tr("Measured PC"),
               millisecondsFromSeconds(m_lastRuntimeMetrics.measuredCpuSeconds),
               milliseconds(pcBudget.frameTotalMilliseconds),
               m_lastRuntimeMetrics.measuredCpuSeconds * 1000.0, pcBudget.frameTotalMilliseconds,
               fabgl::project::PerformanceMetric::FrameTotalMilliseconds);
    const auto addPcPhase = [&](const QString& metric, const double seconds, const double limit,
                                const fabgl::project::PerformanceMetric kind) {
        addNumeric(metric, tr("Measured PC"), millisecondsFromSeconds(seconds), milliseconds(limit),
                   seconds * 1000.0, limit, kind);
    };
    addPcPhase(tr("Fixed update"), m_lastRuntimeMetrics.fixedUpdateCpuSeconds,
               pcBudget.fixedUpdateMilliseconds,
               fabgl::project::PerformanceMetric::FixedUpdateMilliseconds);
    addPcPhase(tr("Variable update"), m_lastRuntimeMetrics.updateCpuSeconds,
               pcBudget.updateMilliseconds, fabgl::project::PerformanceMetric::UpdateMilliseconds);
    addPcPhase(tr("Physics"), m_lastRuntimeMetrics.physicsCpuSeconds, pcBudget.physicsMilliseconds,
               fabgl::project::PerformanceMetric::PhysicsMilliseconds);
    addPcPhase(tr("Animation"), m_lastRuntimeMetrics.animationCpuSeconds,
               pcBudget.animationMilliseconds,
               fabgl::project::PerformanceMetric::AnimationMilliseconds);
    addPcPhase(tr("Gameplay / AI"), m_lastRuntimeMetrics.aiCpuSeconds, pcBudget.aiMilliseconds,
               fabgl::project::PerformanceMetric::AiMilliseconds);
    const double renderSeconds = m_lastRuntimeMetrics.renderSubmissionCpuSeconds +
                                 m_lastRuntimeMetrics.renderingCpuSeconds +
                                 m_lastRuntimeMetrics.presentCpuSeconds;
    addPcPhase(tr("Render + present"), renderSeconds, pcBudget.renderMilliseconds,
               fabgl::project::PerformanceMetric::RenderMilliseconds);
    addPcPhase(tr("Audio"), m_lastRuntimeMetrics.audioCpuSeconds, pcBudget.audioMilliseconds,
               fabgl::project::PerformanceMetric::AudioMilliseconds);
    addPcPhase(tr("Asset streaming"), m_lastRuntimeMetrics.assetStreamingCpuSeconds,
               pcBudget.assetStreamingMilliseconds,
               fabgl::project::PerformanceMetric::AssetStreamingMilliseconds);

    const auto& scene = m_playSession != nullptr ? m_playSession->scene() : m_sceneDocument.scene();
    std::size_t componentCount = 0U;
    for (const auto* entity : scene.entities()) {
        if (entity != nullptr)
            componentCount += entity->components().size();
    }
    addNumeric(tr("Entities"), tr("Runtime scene (exact)"),
               QString::number(static_cast<qulonglong>(scene.entityCount())),
               QString::number(pcBudget.entities), static_cast<double>(scene.entityCount()),
               static_cast<double>(pcBudget.entities), fabgl::project::PerformanceMetric::Entities);
    addNumeric(tr("Components"), tr("Runtime scene (exact)"),
               QString::number(static_cast<qulonglong>(componentCount)),
               QString::number(pcBudget.components), static_cast<double>(componentCount),
               static_cast<double>(pcBudget.components),
               fabgl::project::PerformanceMetric::Components);
    addNumeric(tr("Draw calls"), tr("Measured PC"), QString::number(m_lastFrameStats.drawCalls),
               QString::number(pcBudget.drawCalls), m_lastFrameStats.drawCalls, pcBudget.drawCalls,
               fabgl::project::PerformanceMetric::DrawCalls);
    addNumeric(tr("Sprites"), tr("Measured PC"), QString::number(m_lastFrameStats.spritesSubmitted),
               QString::number(pcBudget.sprites), m_lastFrameStats.spritesSubmitted,
               pcBudget.sprites, fabgl::project::PerformanceMetric::Sprites);
    addNumeric(tr("Triangles"), tr("Measured PC presenter"),
               QString::number(m_lastFrameStats.triangles), QString::number(pcBudget.triangles),
               m_lastFrameStats.triangles, pcBudget.triangles,
               fabgl::project::PerformanceMetric::Triangles);
    addNumeric(tr("Rays"), tr("Measured PC presenter"), QString::number(m_lastFrameStats.rays),
               QString::number(pcBudget.rays), m_lastFrameStats.rays, pcBudget.rays,
               fabgl::project::PerformanceMetric::Rays);
    addNumeric(tr("Particles"), tr("Measured PC runtime"),
               QString::number(m_lastFrameStats.particles), QString::number(pcBudget.particles),
               m_lastFrameStats.particles, pcBudget.particles,
               fabgl::project::PerformanceMetric::Particles);
    if (m_playSession != nullptr) {
        const auto voices = m_playSession->stats().activeAudioVoices;
        addNumeric(tr("Audio voices"), tr("Measured PC mixer"),
                   QString::number(static_cast<qulonglong>(voices)),
                   QString::number(pcBudget.audioVoices), static_cast<double>(voices),
                   pcBudget.audioVoices, fabgl::project::PerformanceMetric::AudioVoices);
    } else {
        addUnavailable(tr("Audio voices"), QString::number(pcBudget.audioVoices));
    }
    addUnavailable(tr("Script update"), milliseconds(pcBudget.updateMilliseconds));

    if (m_playSession != nullptr) {
        const auto resident = m_playSession->stats().residentAssetBytes;
        addNumeric(tr("Resident assets"), tr("Measured PC asset manager"),
                   bytes(static_cast<std::uint64_t>(resident)), bytes(pcBudget.assetResidentBytes),
                   static_cast<double>(resident), static_cast<double>(pcBudget.assetResidentBytes),
                   fabgl::project::PerformanceMetric::AssetResidentBytes);
        if (m_memoryAnalyzer != nullptr)
            m_memoryAnalyzer->setMeasuredPcResidentBytes(static_cast<std::uint64_t>(resident));
    } else {
        addUnavailable(tr("Resident assets"), bytes(pcBudget.assetResidentBytes));
        if (m_memoryAnalyzer != nullptr)
            m_memoryAnalyzer->setMeasuredPcResidentBytes(std::nullopt);
    }

    std::uint64_t flashEstimate = 0U;
    std::uint64_t internalRamEstimate = 0U;
    std::uint64_t psramEstimate = 0U;
    std::uint64_t sdEstimate = 0U;
    const AssetBrowserEntry* largestAsset = nullptr;
    if (m_assetBrowserController != nullptr) {
        for (const auto& asset : m_assetBrowserController->model()->entries()) {
            flashEstimate += asset.esp32Cost.flashBytes;
            internalRamEstimate += asset.esp32Cost.internalRamBytes;
            psramEstimate += asset.esp32Cost.psramBytes;
            sdEstimate += asset.esp32Cost.sdBytes;
            if (largestAsset == nullptr ||
                asset.esp32Cost.payloadBytes > largestAsset->esp32Cost.payloadBytes) {
                largestAsset = &asset;
            }
        }
    }
    const auto addEsp32Storage = [&](const QString& metric, const std::uint64_t value,
                                     const std::uint64_t limit,
                                     const fabgl::project::PerformanceMetric kind) {
        if (limit == 0U) {
            rows.push_back(
                {metric, tr("Estimated ESP32 importer"), value == 0U ? tr("0 B") : bytes(value),
                 tr("Unavailable"), value > 0U, value > 0U,
                 value > 0U ? QString::fromStdString(
                                  fabgl::project::performanceOptimizationRecommendation(kind))
                            : QString{}});
            return;
        }
        addNumeric(metric, tr("Estimated ESP32 importer"), bytes(value), bytes(limit),
                   static_cast<double>(value), static_cast<double>(limit), kind);
    };
    addEsp32Storage(tr("Internal RAM"), internalRamEstimate, esp32Budget.internalRamBytes,
                    fabgl::project::PerformanceMetric::InternalRamBytes);
    addEsp32Storage(tr("PSRAM"), psramEstimate, esp32Budget.psramBytes,
                    fabgl::project::PerformanceMetric::PsramBytes);
    addEsp32Storage(tr("Flash"), flashEstimate, esp32Budget.flashBytes,
                    fabgl::project::PerformanceMetric::FlashBytes);
    addEsp32Storage(tr("SD storage"), sdEstimate, esp32Budget.sdBytes,
                    fabgl::project::PerformanceMetric::SdBytes);
    if (m_lastMeasuredEsp32HeapFragmentationPercent) {
        const double fragmentation = *m_lastMeasuredEsp32HeapFragmentationPercent;
        rows.push_back({tr("Heap fragmentation indicator"), tr("Measured ESP32 serial telemetry"),
                        tr("%1%").arg(fragmentation, 0, 'f', 2), tr("Informational"),
                        fragmentation > 50.0, fragmentation > 75.0,
                        fragmentation > 50.0
                            ? tr("Reduce allocation churn or reserve fixed-capacity pools.")
                            : QString{}});
    } else {
        addUnavailable(tr("Heap fragmentation indicator"),
                       tr("Requires heapFree + largestBlock telemetry"));
    }
    if (m_lastMeasuredEsp32SdReadBytes) {
        rows.push_back({tr("SD bytes read"),
                        tr("Measured ESP32 serial telemetry"),
                        bytes(*m_lastMeasuredEsp32SdReadBytes),
                        tr("Informational"),
                        false,
                        false,
                        {}});
    } else {
        addUnavailable(tr("SD bytes read"), tr("Requires sdReadBytes telemetry"));
    }
    if (largestAsset != nullptr) {
        rows.push_back({tr("Largest asset"),
                        tr("Estimated ESP32 importer"),
                        tr("%1 — %2").arg(largestAsset->relativePath,
                                          bytes(largestAsset->esp32Cost.payloadBytes)),
                        tr("Informational"),
                        false,
                        false,
                        {}});
    } else {
        addUnavailable(tr("Largest asset"), tr("Informational"));
    }
    const std::array systemCosts = {
        std::pair{tr("Fixed update"), m_lastRuntimeMetrics.fixedUpdateCpuSeconds},
        std::pair{tr("Update"), m_lastRuntimeMetrics.updateCpuSeconds},
        std::pair{tr("Physics"), m_lastRuntimeMetrics.physicsCpuSeconds},
        std::pair{tr("Animation"), m_lastRuntimeMetrics.animationCpuSeconds},
        std::pair{tr("AI"), m_lastRuntimeMetrics.aiCpuSeconds},
        std::pair{tr("Render"), renderSeconds},
        std::pair{tr("Audio"), m_lastRuntimeMetrics.audioCpuSeconds},
        std::pair{tr("Asset streaming"), m_lastRuntimeMetrics.assetStreamingCpuSeconds}};
    const auto mostExpensive = std::max_element(
        systemCosts.cbegin(), systemCosts.cend(),
        [](const auto& left, const auto& right) { return left.second < right.second; });
    rows.push_back(
        {tr("Most expensive system"),
         tr("Measured PC phases"),
         tr("%1 — %2").arg(mostExpensive->first, millisecondsFromSeconds(mostExpensive->second)),
         tr("Informational"),
         false,
         false,
         {}});

    addNumeric(tr("Total frame"), tr("Estimated ESP32 (PC ×4 + draw model)"),
               milliseconds(estimatedEsp32Frame), milliseconds(esp32Budget.frameTotalMilliseconds),
               estimatedEsp32Frame, esp32Budget.frameTotalMilliseconds,
               fabgl::project::PerformanceMetric::FrameTotalMilliseconds);
    if (m_lastMeasuredEsp32FrameMilliseconds) {
        addNumeric(tr("Total frame"), tr("Measured ESP32 serial telemetry"),
                   milliseconds(*m_lastMeasuredEsp32FrameMilliseconds),
                   milliseconds(esp32Budget.frameTotalMilliseconds),
                   *m_lastMeasuredEsp32FrameMilliseconds, esp32Budget.frameTotalMilliseconds,
                   fabgl::project::PerformanceMetric::FrameTotalMilliseconds);
    } else {
        addUnavailable(tr("ESP32 measured frame"),
                       milliseconds(esp32Budget.frameTotalMilliseconds));
    }
    if (m_lastMeasuredEsp32HeapFreeBytes) {
        rows.push_back({tr("ESP32 heap free"),
                        tr("Measured ESP32 serial telemetry"),
                        bytes(*m_lastMeasuredEsp32HeapFreeBytes),
                        tr("Informational"),
                        false,
                        false,
                        {}});
    } else {
        addUnavailable(tr("ESP32 heap free"), tr("Informational"));
    }
    addNumeric(tr("Draw calls"), tr("Projected ESP32 workload"),
               QString::number(m_lastFrameStats.drawCalls), QString::number(esp32Budget.drawCalls),
               m_lastFrameStats.drawCalls, esp32Budget.drawCalls,
               fabgl::project::PerformanceMetric::DrawCalls);
    addNumeric(tr("Sprites"), tr("Projected ESP32 workload"),
               QString::number(m_lastFrameStats.spritesSubmitted),
               QString::number(esp32Budget.sprites), m_lastFrameStats.spritesSubmitted,
               esp32Budget.sprites, fabgl::project::PerformanceMetric::Sprites);
    rows.push_back({tr("Runtime state"), tr("Editor"), state, tr("—"), false, false, {}});

    m_profiler->setRowCount(static_cast<int>(rows.size()));
    for (int row = 0; row < rows.size(); ++row) {
        for (int column = 0; column < 4; ++column) {
            const auto& rowData = rows.at(row);
            const std::array values{rowData.metric, rowData.source, rowData.value, rowData.budget};
            QString display = values.at(static_cast<std::size_t>(column));
            if (column == 3 && rowData.error)
                display = tr("ERROR ") + display;
            else if (column == 3 && rowData.warning)
                display = tr("WARNING ") + display;
            auto* item = new QTableWidgetItem(display);
            if (rowData.error)
                item->setForeground(QColor(QStringLiteral("#ff4d4d")));
            else if (rowData.warning)
                item->setForeground(QColor(QStringLiteral("#ffb347")));
            if (!rowData.recommendation.isEmpty())
                item->setToolTip(rowData.recommendation);
            m_profiler->setItem(row, column, item);
        }
    }
}

void MainWindow::configureBuildCommand() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Project Settings"));
    dialog.resize(780, 720);
    auto* layout = new QFormLayout(&dialog);
    auto* programEdit = new QLineEdit(m_projectData.buildProgram, &dialog);
    auto* argumentsEdit = new QLineEdit(argumentsForDisplay(m_projectData.buildArguments), &dialog);
    argumentsEdit->setPlaceholderText(tr("--build out/build/dev"));
    layout->addRow(tr("Program"), programEdit);
    layout->addRow(tr("Arguments"), argumentsEdit);
    auto* pcProfile = new QComboBox(&dialog);
    pcProfile->setObjectName(QStringLiteral("projectPcPerformanceProfileCombo"));
    auto* esp32Profile = new QComboBox(&dialog);
    esp32Profile->setObjectName(QStringLiteral("projectEsp32PerformanceProfileCombo"));
    const auto populateProfile = [](QComboBox* combo,
                                    const fabgl::project::PerformanceBudgetProfile current) {
        combo->addItem(QObject::tr("Safe"),
                       static_cast<int>(fabgl::project::PerformanceBudgetProfile::Safe));
        combo->addItem(QObject::tr("Balanced"),
                       static_cast<int>(fabgl::project::PerformanceBudgetProfile::Balanced));
        combo->addItem(QObject::tr("Maximum"),
                       static_cast<int>(fabgl::project::PerformanceBudgetProfile::Maximum));
        combo->addItem(QObject::tr("Custom"),
                       static_cast<int>(fabgl::project::PerformanceBudgetProfile::Custom));
        combo->setCurrentIndex(combo->findData(static_cast<int>(current)));
    };
    populateProfile(pcProfile, m_projectData.performance.pcProfile);
    populateProfile(esp32Profile, m_projectData.performance.esp32Profile);
    layout->addRow(tr("PC performance budget"), pcProfile);
    layout->addRow(tr("ESP32 performance budget"), esp32Profile);

    auto* budgetTable = new QTableWidget(22, 4, &dialog);
    budgetTable->setObjectName(QStringLiteral("projectPerformanceBudgetTable"));
    budgetTable->setHorizontalHeaderLabels(
        {tr("Custom metric"), tr("Unit"), tr("PC"), tr("ESP32")});
    budgetTable->verticalHeader()->setVisible(false);
    budgetTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    budgetTable->setMinimumHeight(360);
    struct TimeBudgetField final {
        const char* label;
        double fabgl::project::PerformanceBudgetValues::*member;
    };
    const std::array timeFields = {
        TimeBudgetField{"Total frame",
                        &fabgl::project::PerformanceBudgetValues::frameTotalMilliseconds},
        TimeBudgetField{"Fixed update",
                        &fabgl::project::PerformanceBudgetValues::fixedUpdateMilliseconds},
        TimeBudgetField{"Update", &fabgl::project::PerformanceBudgetValues::updateMilliseconds},
        TimeBudgetField{"Physics", &fabgl::project::PerformanceBudgetValues::physicsMilliseconds},
        TimeBudgetField{"Animation",
                        &fabgl::project::PerformanceBudgetValues::animationMilliseconds},
        TimeBudgetField{"AI", &fabgl::project::PerformanceBudgetValues::aiMilliseconds},
        TimeBudgetField{"Render", &fabgl::project::PerformanceBudgetValues::renderMilliseconds},
        TimeBudgetField{"Audio", &fabgl::project::PerformanceBudgetValues::audioMilliseconds},
        TimeBudgetField{"Asset streaming",
                        &fabgl::project::PerformanceBudgetValues::assetStreamingMilliseconds}};
    struct CountBudgetField final {
        const char* label;
        std::uint32_t fabgl::project::PerformanceBudgetValues::*member;
    };
    const std::array countFields = {
        CountBudgetField{"Entities", &fabgl::project::PerformanceBudgetValues::entities},
        CountBudgetField{"Components", &fabgl::project::PerformanceBudgetValues::components},
        CountBudgetField{"Draw calls", &fabgl::project::PerformanceBudgetValues::drawCalls},
        CountBudgetField{"Sprites", &fabgl::project::PerformanceBudgetValues::sprites},
        CountBudgetField{"Triangles", &fabgl::project::PerformanceBudgetValues::triangles},
        CountBudgetField{"Rays", &fabgl::project::PerformanceBudgetValues::rays},
        CountBudgetField{"Particles", &fabgl::project::PerformanceBudgetValues::particles},
        CountBudgetField{"Audio voices", &fabgl::project::PerformanceBudgetValues::audioVoices}};
    struct ByteBudgetField final {
        const char* label;
        std::uint64_t fabgl::project::PerformanceBudgetValues::*member;
    };
    const std::array byteFields = {
        ByteBudgetField{"Resident assets",
                        &fabgl::project::PerformanceBudgetValues::assetResidentBytes},
        ByteBudgetField{"Internal RAM", &fabgl::project::PerformanceBudgetValues::internalRamBytes},
        ByteBudgetField{"PSRAM", &fabgl::project::PerformanceBudgetValues::psramBytes},
        ByteBudgetField{"Flash", &fabgl::project::PerformanceBudgetValues::flashBytes},
        ByteBudgetField{"SD", &fabgl::project::PerformanceBudgetValues::sdBytes}};
    int budgetRow = 0;
    const auto addBudgetRow = [&](const QString& label, const QString& unit, const QString& pcValue,
                                  const QString& esp32Value) {
        auto* labelItem = new QTableWidgetItem(label);
        labelItem->setFlags(labelItem->flags() & ~Qt::ItemIsEditable);
        auto* unitItem = new QTableWidgetItem(unit);
        unitItem->setFlags(unitItem->flags() & ~Qt::ItemIsEditable);
        budgetTable->setItem(budgetRow, 0, labelItem);
        budgetTable->setItem(budgetRow, 1, unitItem);
        budgetTable->setItem(budgetRow, 2, new QTableWidgetItem(pcValue));
        budgetTable->setItem(budgetRow, 3, new QTableWidgetItem(esp32Value));
        ++budgetRow;
    };
    for (const auto& field : timeFields) {
        addBudgetRow(tr(field.label), tr("ms"),
                     QString::number(m_projectData.performance.pcCustom.*field.member, 'g', 12),
                     QString::number(m_projectData.performance.esp32Custom.*field.member, 'g', 12));
    }
    for (const auto& field : countFields) {
        addBudgetRow(tr(field.label), tr("count"),
                     QString::number(m_projectData.performance.pcCustom.*field.member),
                     QString::number(m_projectData.performance.esp32Custom.*field.member));
    }
    for (const auto& field : byteFields) {
        addBudgetRow(tr(field.label), tr("bytes"),
                     QString::number(m_projectData.performance.pcCustom.*field.member),
                     QString::number(m_projectData.performance.esp32Custom.*field.member));
    }
    layout->addRow(budgetTable);
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
    auto nextPerformance = m_projectData.performance;
    nextPerformance.pcProfile =
        static_cast<fabgl::project::PerformanceBudgetProfile>(pcProfile->currentData().toInt());
    nextPerformance.esp32Profile =
        static_cast<fabgl::project::PerformanceBudgetProfile>(esp32Profile->currentData().toInt());
    const auto readTimeFields = [&](fabgl::project::PerformanceBudgetValues& values,
                                    const int column) {
        int row = 0;
        for (const auto& field : timeFields) {
            bool ok = false;
            const double value = budgetTable->item(row++, column)->text().toDouble(&ok);
            if (!ok || !std::isfinite(value) || value <= 0.0 || value > 10000.0)
                return false;
            values.*field.member = value;
        }
        return true;
    };
    const auto readCountFields = [&](fabgl::project::PerformanceBudgetValues& values,
                                     const int column) {
        int row = static_cast<int>(timeFields.size());
        for (const auto& field : countFields) {
            bool ok = false;
            const qulonglong value = budgetTable->item(row++, column)->text().toULongLong(&ok);
            if (!ok || value == 0U || value > std::numeric_limits<std::uint32_t>::max())
                return false;
            values.*field.member = static_cast<std::uint32_t>(value);
        }
        return true;
    };
    const auto readByteFields = [&](fabgl::project::PerformanceBudgetValues& values,
                                    const int column) {
        int row = static_cast<int>(timeFields.size() + countFields.size());
        for (std::size_t index = 0U; index < byteFields.size(); ++index) {
            bool ok = false;
            const qulonglong value = budgetTable->item(row++, column)->text().toULongLong(&ok);
            const bool allowZero = index == 2U || index == 4U;
            if (!ok || (!allowZero && value == 0U) || value > (1ULL << 40U))
                return false;
            values.*byteFields[index].member = static_cast<std::uint64_t>(value);
        }
        return true;
    };
    if (!readTimeFields(nextPerformance.pcCustom, 2) ||
        !readTimeFields(nextPerformance.esp32Custom, 3) ||
        !readCountFields(nextPerformance.pcCustom, 2) ||
        !readCountFields(nextPerformance.esp32Custom, 3) ||
        !readByteFields(nextPerformance.pcCustom, 2) ||
        !readByteFields(nextPerformance.esp32Custom, 3) ||
        !fabgl::project::validPerformanceBudget(nextPerformance.pcCustom) ||
        !fabgl::project::validPerformanceBudget(nextPerformance.esp32Custom)) {
        QMessageBox::warning(this, tr("Invalid Performance Budget"),
                             tr("Custom times/counts must be positive and byte budgets must be "
                                "within 1 TiB. PSRAM and SD may be zero when unavailable."));
        return;
    }
    if (program == m_projectData.buildProgram && arguments == m_projectData.buildArguments &&
        nextPerformance == m_projectData.performance) {
        return;
    }
    m_projectData.buildProgram = program;
    m_projectData.buildArguments = arguments;
    m_projectData.performance = nextPerformance;
    setDocumentModified(true);
    if (m_memoryAnalyzer != nullptr)
        m_memoryAnalyzer->setPerformanceBudgets(m_projectData.performance);
    updateProjectPanel();
    updateProfiler();
    appendConsoleMessage(
        tr("Build command changed to %1.").arg(commandForDisplay(program, arguments)));
}

void MainWindow::runBuild() {
    if (m_buildRunner.isRunning()) {
        return;
    }
    if (!prepareProjectForExternalWorkflow()) {
        return;
    }

    const auto repositoryRoot = studioRepositoryRoot();
    if (repositoryRoot.isEmpty()) {
        QMessageBox::critical(this, tr("Build Unavailable"),
                              tr("FabGL Studio's scripts directory could not be located."));
        return;
    }
    const auto target = static_cast<BuildTarget>(m_targetCombo->currentData().toInt());
    if (target == BuildTarget::Pc) {
        m_lastPcScriptModule.clear();
        if (m_configurationCombo->currentData().toString() == QStringLiteral("custom")) {
            startWorkflow({tr("Custom project build"), m_projectData.buildProgram,
                           m_projectData.buildArguments, projectRoot()},
                          WorkflowState::CustomBuild);
            return;
        }
        const QString outputRoot = QDir(repositoryRoot)
                                       .filePath(QStringLiteral("out/project-builds/%1/pc")
                                                     .arg(m_projectData.projectGuid.toLower()));
        const auto command = WorkflowCommands::projectBuild(
            repositoryRoot, m_projectFilePath, QStringLiteral("Pc"),
            m_configurationCombo->currentData().toString(), outputRoot);
        if (!command.isValid()) {
            QMessageBox::critical(this, tr("PC Build Unavailable"),
                                  tr("The selected PC build profile is invalid."));
            return;
        }
        startWorkflow(command, WorkflowState::PcBuild);
        return;
    }

    const QString unifiedOutputRoot = QDir(repositoryRoot)
                                          .filePath(QStringLiteral("out/project-builds/%1/esp32")
                                                        .arg(m_projectData.projectGuid.toLower()));
    const auto command = WorkflowCommands::projectBuild(
        repositoryRoot, m_projectFilePath, QStringLiteral("Esp32"),
        m_configurationCombo->currentData().toString(), unifiedOutputRoot);
    if (!command.isValid()) {
        QMessageBox::critical(this, tr("ESP32 Build Unavailable"),
                              tr("The selected ESP32 build profile is invalid."));
        return;
    }
    startWorkflow(command, WorkflowState::Esp32Build);
}

void MainWindow::exportEsp32() {
    if (m_buildRunner.isRunning() || !prepareProjectForExternalWorkflow()) {
        return;
    }
    const auto repositoryRoot = studioRepositoryRoot();
    const auto projectCli =
        findBuiltTool(QStringLiteral("fabgl_project_cli"), QStringLiteral("tools/project_cli"));
    if (repositoryRoot.isEmpty() || projectCli.isEmpty()) {
        QMessageBox::critical(
            this, tr("ESP32 Export Unavailable"),
            tr("The repository scripts or fabgl_project_cli executable could not be located."));
        return;
    }
    m_pendingEsp32Sketch = createEsp32ExportPath();
    if (m_pendingEsp32Sketch.isEmpty()) {
        return;
    }
    startWorkflow(WorkflowCommands::esp32Export(
                      projectCli, m_projectFilePath,
                      QDir(repositoryRoot).filePath(QStringLiteral("platforms/fabgl/firmware")),
                      m_pendingEsp32Sketch, repositoryRoot),
                  WorkflowState::Esp32ExportOnly);
}

void MainWindow::playPc() {
    if (m_runState != RunState::Editing) {
        statusBar()->showMessage(tr("Stop Studio Play before starting the external PC player."),
                                 5000);
        return;
    }
    if (m_pcRunner.isRunning() || !prepareProjectForExternalWorkflow()) {
        return;
    }
    const auto player =
        findBuiltTool(QStringLiteral("fabgl_player_pc"), QStringLiteral("apps/player_pc"));
    if (player.isEmpty()) {
        QMessageBox::critical(this, tr("PC Player Unavailable"),
                              tr("fabgl_player_pc was not found. Select PC and build it first."));
        return;
    }
    if (auto* dock = findChild<QDockWidget*>(QStringLiteral("buildOutputDock"))) {
        dock->show();
        dock->raise();
    }
    const auto command =
        WorkflowCommands::pcPlay(player, m_projectFilePath, projectRoot(), m_lastPcScriptModule);
    m_pcRunner.startBuild(command.program, command.arguments, command.workingDirectory);
}

void MainWindow::stopPc() {
    m_pcRunner.stopBuild();
}

void MainWindow::nativeGameplaySourceChanged(const QString& filePath, const bool localChangesKept) {
    if (m_closing || localChangesKept || !isNativeGameplaySource(filePath)) {
        return;
    }

    if (m_previewRestartController.pending()) {
        const bool alreadyDeferred = m_previewRestartController.rebuildDeferred();
        static_cast<void>(m_previewRestartController.request(PreviewKind::None));
        if (m_previewRestartController.phase() == PreviewRestartController::Phase::Building &&
            !alreadyDeferred) {
            appendBuildOutput(
                tr("Native source changed during the rebuild; one additional PC build was "
                   "coalesced.\n"),
                false);
            appendConsoleMessage(
                tr("A newer native source save was coalesced into one additional rebuild."));
        }
        return;
    }

    const auto preview = activePreviewKind();
    if (preview == PreviewKind::None) {
        return;
    }
    if (!currentProjectTrusted()) {
        appendBuildOutput(tr("Native preview restart blocked: this project path is not trusted.\n"),
                          true);
        return;
    }
    if (!pcTargetProfileSupported()) {
        appendBuildOutput(
            tr("Native preview restart blocked: project PC profile '%1' is unsupported.\n")
                .arg(m_projectData.targetProfiles.pc),
            true);
        return;
    }
    if (m_buildRunner.isRunning() || m_workflowState != WorkflowState::Idle) {
        const auto message =
            tr("Native preview restart was not scheduled because another workflow is active; "
               "save the source again after it finishes.");
        appendBuildOutput(message + QLatin1Char('\n'), true);
        appendConsoleMessage(message);
        return;
    }

    m_previewRestartConfiguration = nativePreviewBuildConfiguration();
    m_previewRestartSource = QFileInfo(filePath).absoluteFilePath();
    appendBuildOutput(
        tr("\n===== Native gameplay restart fallback =====\n"
           "Source: %1\nConfiguration: %2\n")
            .arg(QDir::toNativeSeparators(m_previewRestartSource), m_previewRestartConfiguration),
        false);
    appendConsoleMessage(
        tr("Native gameplay source changed; stopping the active preview for a verified %1 "
           "rebuild.")
            .arg(m_previewRestartConfiguration));
    performPreviewRestartAction(m_previewRestartController.request(preview));
}

PreviewKind MainWindow::activePreviewKind() const noexcept {
    if (m_pcRunner.isRunning()) {
        return PreviewKind::ExternalPlayer;
    }
    if (m_runState == RunState::Playing) {
        return PreviewKind::StudioPlaying;
    }
    if (m_runState == RunState::Paused) {
        return PreviewKind::StudioPaused;
    }
    return PreviewKind::None;
}

void MainWindow::performPreviewRestartAction(const PreviewRestartAction action) {
    switch (action) {
    case PreviewRestartAction::None:
        return;
    case PreviewRestartAction::StopStudio:
        stop();
        performPreviewRestartAction(m_previewRestartController.previewStopped());
        return;
    case PreviewRestartAction::StopExternalPlayer:
        stopPc();
        if (!m_pcRunner.isRunning() && m_previewRestartController.phase() ==
                                           PreviewRestartController::Phase::StoppingPreview) {
            performPreviewRestartAction(m_previewRestartController.previewStopped());
        }
        return;
    case PreviewRestartAction::BuildPc:
        if (!startNativePreviewBuild()) {
            finishNativePreviewBuild(false, tr("the PC rebuild could not be started"));
        }
        return;
    case PreviewRestartAction::StartStudioPlaying:
        play();
        if (m_runState == RunState::Playing) {
            const auto message = tr("Studio Play fully restarted with the verified native module; "
                                    "the authoring scene was preserved.");
            appendBuildOutput(message + QLatin1Char('\n'), false);
            appendConsoleMessage(message);
        } else {
            appendBuildOutput(tr("Native module built, but Studio Play could not be restarted.\n"),
                              true);
        }
        return;
    case PreviewRestartAction::StartStudioPaused:
        play();
        if (m_runState == RunState::Playing) {
            pause();
        }
        if (m_runState == RunState::Paused) {
            const auto message = tr("Studio Play fully restarted and restored to Paused with the "
                                    "verified native module; the authoring scene was preserved.");
            appendBuildOutput(message + QLatin1Char('\n'), false);
            appendConsoleMessage(message);
        } else {
            appendBuildOutput(
                tr("Native module built, but paused Studio Play could not be restored.\n"), true);
        }
        return;
    case PreviewRestartAction::StartExternalPlayer:
        playPc();
        if (m_pcRunner.isRunning()) {
            const auto message =
                tr("External PC player fully restarted with the verified native module.");
            appendBuildOutput(message + QLatin1Char('\n'), false);
            appendConsoleMessage(message);
        } else {
            appendBuildOutput(
                tr("Native module built, but the external PC player could not be restarted.\n"),
                true);
        }
        return;
    case PreviewRestartAction::ReportFailure:
        appendBuildOutput(
            tr("Native preview remains stopped because the replacement module was not verified.\n"),
            true);
        return;
    }
}

bool MainWindow::startNativePreviewBuild() {
    if (!currentProjectTrusted()) {
        appendBuildOutput(tr("Native preview rebuild blocked: project trust was revoked.\n"), true);
        return false;
    }
    if (!pcTargetProfileSupported()) {
        appendBuildOutput(
            tr("Native preview rebuild blocked: the project's PC target profile is unsupported.\n"),
            true);
        return false;
    }
    if (m_buildRunner.isRunning() || m_workflowState != WorkflowState::Idle) {
        appendBuildOutput(
            tr("Native preview rebuild blocked: another workflow is already active.\n"), true);
        return false;
    }
    if (m_projectFilePath.isEmpty() || (isWindowModified() && !saveProject())) {
        appendBuildOutput(
            tr("Native preview rebuild cancelled because the authoring project could not be "
               "saved.\n"),
            true);
        return false;
    }
    const auto repositoryRoot = studioRepositoryRoot();
    if (repositoryRoot.isEmpty()) {
        appendBuildOutput(
            tr("Native preview rebuild unavailable: scripts/build_project.ps1 was not found.\n"),
            true);
        return false;
    }
    const QString outputRoot = QDir(repositoryRoot)
                                   .filePath(QStringLiteral("out/project-builds/%1/pc")
                                                 .arg(m_projectData.projectGuid.toLower()));
    const auto command =
        WorkflowCommands::projectBuild(repositoryRoot, m_projectFilePath, QStringLiteral("Pc"),
                                       m_previewRestartConfiguration, outputRoot);
    if (!command.isValid()) {
        appendBuildOutput(
            tr("Native preview rebuild unavailable: only unified PC Debug/Release profiles are "
               "supported.\n"),
            true);
        return false;
    }
    startWorkflow(command, WorkflowState::PcBuild);
    if (!m_buildRunner.isRunning()) {
        return false;
    }
    m_workflowStatus->setText(tr("Native preview rebuild (%1)").arg(m_previewRestartConfiguration));
    statusBar()->showMessage(tr("Rebuilding native gameplay for preview restart..."));
    return true;
}

void MainWindow::finishNativePreviewBuild(const bool succeeded, const QString& detail) {
    if (m_previewRestartController.phase() != PreviewRestartController::Phase::Building) {
        return;
    }
    const auto action = m_previewRestartController.buildFinished(succeeded);
    if (!succeeded) {
        const auto message =
            tr("Native preview restart failed: %1. The authoring scene is unchanged.").arg(detail);
        appendBuildOutput(message + QLatin1Char('\n'), true);
        appendConsoleMessage(message);
    } else if (action == PreviewRestartAction::BuildPc) {
        appendBuildOutput(
            tr("A newer native source save is pending; starting the single coalesced rebuild.\n"),
            false);
    }
    performPreviewRestartAction(action);
}

QString MainWindow::nativePreviewBuildConfiguration() const {
    if (m_configurationCombo != nullptr) {
        const auto selected = m_configurationCombo->currentData().toString();
        if (selected == QStringLiteral("Debug") || selected == QStringLiteral("Release")) {
            return selected;
        }
    }
    return QStringLiteral("Debug");
}

void MainWindow::refreshSerialPorts() {
    if (m_portDetector.isRunning()) {
        return;
    }
    const auto repositoryRoot = studioRepositoryRoot();
    if (repositoryRoot.isEmpty()) {
        QMessageBox::critical(this, tr("Port Detection Unavailable"),
                              tr("scripts/detect_serial_ports.ps1 could not be located."));
        return;
    }
    m_serialPortCombo->clear();
    m_serialPortCombo->addItem(tr("Detecting ports (read-only)..."));
    m_uploadConfirmation->setChecked(false);
    m_workflowStatus->setText(tr("Detecting serial ports without opening them..."));
    const auto command = WorkflowCommands::detectSerialPorts(repositoryRoot);
    appendBuildOutput(tr("\n===== Read-only serial-port detection =====\n> %1\n")
                          .arg(commandForDisplay(command.program, command.arguments)),
                      false);
    m_portDetector.startBuild(command.program, command.arguments, command.workingDirectory);
    updateWorkflowActions();
}

void MainWindow::uploadEsp32() {
    if (!currentProjectTrusted()) {
        QMessageBox::warning(
            this, tr("Upload Blocked for Untrusted Project"),
            tr("Trust this project path explicitly before building or uploading its code."));
        return;
    }
    const auto port = selectedSerialPort();
    if (!selectedPortIsCandidate() || !m_uploadConfirmation->isChecked() ||
        m_lastEsp32BuildResult.isEmpty() || !QFileInfo(m_lastEsp32BuildResult).isFile()) {
        QMessageBox::warning(
            this, tr("Upload Blocked"),
            tr("Choose a detected board candidate, confirm the exact board profile, and "
               "complete an ESP32 build before uploading."));
        return;
    }
    const auto answer = QMessageBox::warning(
        this, tr("Confirm Firmware Upload"),
        tr("Upload the verified firmware to %1?\n\nBoard profile: %2\nBuild result: %3")
            .arg(port, QString::fromLatin1(WorkflowCommands::BoardProfile),
                 QDir::toNativeSeparators(m_lastEsp32BuildResult)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        appendConsoleMessage(tr("ESP32 upload cancelled before the port was opened."));
        return;
    }
    const auto repositoryRoot = studioRepositoryRoot();
    const auto command =
        WorkflowCommands::uploadEsp32(repositoryRoot, port, m_lastEsp32BuildResult);
    if (!command.isValid()) {
        QMessageBox::critical(this, tr("Upload Blocked"),
                              tr("The selected serial port is invalid."));
        return;
    }
    startWorkflow(command, WorkflowState::Esp32Upload);
}

void MainWindow::deployEsp32Diagnostics() {
    runHardwareDiagnostic(QStringLiteral("all"));
}

void MainWindow::runHardwareDiagnostic(const QString& diagnosticCheck) {
    if (m_buildRunner.isRunning() || !prepareProjectForExternalWorkflow()) {
        return;
    }
    const auto target = static_cast<BuildTarget>(m_targetCombo->currentData().toInt());
    const auto port = selectedSerialPort();
    if (target != BuildTarget::Esp32 || !selectedPortIsCandidate() ||
        !m_uploadConfirmation->isChecked()) {
        QMessageBox::warning(
            this, tr("Hardware Diagnostics Blocked"),
            tr("Select the ESP32 target, enter or choose an explicit safe serial port, and "
               "confirm that it is an Olimex ESP32-SBC-FabGL Rev.B. No upload was attempted."));
        return;
    }
    bool baudValid = false;
    const int baud = m_baudCombo->currentText().toInt(&baudValid);
    if (!baudValid) {
        QMessageBox::warning(this, tr("Hardware Diagnostics Blocked"),
                             tr("Select a valid serial-monitor baud rate."));
        return;
    }
    const auto answer = QMessageBox::warning(
        this, tr("Confirm Build, Upload, and Hardware Diagnostics"),
        tr("Run the complete ESP32 project pipeline, upload its verified firmware to %1, then "
           "open a bounded serial capture for the '%2' structured diagnostic?\n\n"
           "Board profile: %3\n"
           "VGA appearance, audible sound, and physical input remain manual where reported.")
            .arg(port, diagnosticCheck, QString::fromLatin1(WorkflowCommands::BoardProfile)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        appendConsoleMessage(
            tr("ESP32 hardware diagnostic cancelled before build or port access."));
        return;
    }

    const auto repositoryRoot = studioRepositoryRoot();
    const QString outputRoot = QDir(repositoryRoot)
                                   .filePath(QStringLiteral("out/project-builds/%1/esp32")
                                                 .arg(m_projectData.projectGuid.toLower()));
    const auto command = WorkflowCommands::esp32BuildUploadDiagnostics(
        repositoryRoot, m_projectFilePath, m_configurationCombo->currentData().toString(),
        outputRoot, port, baud, diagnosticCheck);
    if (!command.isValid()) {
        QMessageBox::critical(
            this, tr("Hardware Diagnostics Unavailable"),
            tr("The selected build profile, port, baud, or diagnostic check is invalid."));
        return;
    }
    if (auto* dock = findChild<QDockWidget*>(QStringLiteral("serialMonitorDock"))) {
        dock->show();
    }
    m_lastEsp32BuildResult.clear();
    m_activeDiagnosticCheck = diagnosticCheck;
    startWorkflow(command, WorkflowState::Esp32DeployDiagnostics);
}

void MainWindow::startSerialMonitor() {
    if (m_serialRunner.isRunning() || !selectedPortIsCandidate()) {
        return;
    }
    bool baudValid = false;
    const int baud = m_baudCombo->currentText().toInt(&baudValid);
    const auto command =
        WorkflowCommands::serialMonitor(studioRepositoryRoot(), selectedSerialPort(), baud);
    if (!baudValid || !command.isValid()) {
        QMessageBox::warning(this, tr("Serial Monitor Blocked"),
                             tr("Select a detected board candidate and a valid baud rate."));
        return;
    }
    if (auto* dock = findChild<QDockWidget*>(QStringLiteral("serialMonitorDock"))) {
        dock->show();
        dock->raise();
    }
    m_serialRunner.startBuild(command.program, command.arguments, command.workingDirectory);
}

void MainWindow::stopSerialMonitor() {
    m_serialRunner.stopBuild();
}

void MainWindow::cancelWorkflow() {
    if (!m_buildRunner.isRunning()) {
        return;
    }
    m_workflowCancelled = true;
    m_workflowStatus->setText(tr("Cancelling..."));
    m_buildRunner.stopBuild();
}

void MainWindow::updateTargetConfiguration() {
    const auto target = static_cast<BuildTarget>(m_targetCombo->currentData().toInt());
    const QSignalBlocker blocker(m_configurationCombo);
    const auto previous = m_configurationCombo->currentData().toString();
    m_configurationCombo->clear();
    if (target == BuildTarget::Pc) {
        m_configurationCombo->addItem(tr("Debug"), QStringLiteral("Debug"));
        m_configurationCombo->addItem(tr("Release"), QStringLiteral("Release"));
        m_configurationCombo->addItem(tr("Project command"), QStringLiteral("custom"));
    } else {
        m_configurationCombo->addItem(tr("Debug"), QStringLiteral("Debug"));
        m_configurationCombo->addItem(tr("Release"), QStringLiteral("Release"));
        m_configurationCombo->addItem(tr("Size Optimized"), QStringLiteral("SizeOptimized"));
        m_configurationCombo->addItem(tr("Performance Optimized"),
                                      QStringLiteral("PerformanceOptimized"));
        m_configurationCombo->addItem(tr("Release + Experimental PSRAM"),
                                      QStringLiteral("ReleasePsram"));
    }
    const int previousIndex = m_configurationCombo->findData(previous);
    m_configurationCombo->setCurrentIndex(previousIndex >= 0 ? previousIndex : 0);
    if (m_toolbarTargetCombo != nullptr && m_toolbarConfigurationCombo != nullptr) {
        const QSignalBlocker targetBlocker(m_toolbarTargetCombo);
        const QSignalBlocker configurationBlocker(m_toolbarConfigurationCombo);
        m_toolbarTargetCombo->setCurrentIndex(m_targetCombo->currentIndex());
        m_toolbarConfigurationCombo->clear();
        for (int index = 0; index < m_configurationCombo->count(); ++index) {
            m_toolbarConfigurationCombo->addItem(m_configurationCombo->itemText(index),
                                                 m_configurationCombo->itemData(index));
        }
        m_toolbarConfigurationCombo->setCurrentIndex(m_configurationCombo->currentIndex());
    }
    updateProjectTargetProfileUi();
    updateWorkflowActions();
}

void MainWindow::updateWorkflowActions() {
    if (m_targetCombo == nullptr) {
        return;
    }
    const bool workflowRunning =
        m_buildRunner.isRunning() || m_workflowState != WorkflowState::Idle;
    const bool pcRunning = m_pcRunner.isRunning();
    const bool serialRunning = m_serialRunner.isRunning();
    const bool detecting = m_portDetector.isRunning();
    const bool hasProject = !m_projectFilePath.isEmpty();
    const bool esp32 =
        static_cast<BuildTarget>(m_targetCombo->currentData().toInt()) == BuildTarget::Esp32;
    const bool candidateSelected = selectedPortIsCandidate();
    const bool executionAllowed = currentProjectTrusted();
    const bool profileSupported = currentTargetProfileSupported();

    m_targetCombo->setEnabled(!workflowRunning);
    m_configurationCombo->setEnabled(!workflowRunning);
    m_toolbarTargetCombo->setEnabled(!workflowRunning);
    m_toolbarConfigurationCombo->setEnabled(!workflowRunning);
    m_buildAction->setEnabled(hasProject && executionAllowed && profileSupported &&
                              !workflowRunning && (esp32 || !pcRunning));
    m_buildSettingsAction->setEnabled(!workflowRunning);
    m_cancelBuildAction->setEnabled(m_buildRunner.isRunning());
    m_pcPlayAction->setEnabled(hasProject && executionAllowed && profileSupported && !esp32 &&
                               m_runState == RunState::Editing && !pcRunning && !workflowRunning);
    m_pcStopAction->setEnabled(pcRunning);
    m_exportEsp32Action->setEnabled(hasProject && executionAllowed && profileSupported && esp32 &&
                                    !workflowRunning);
    m_refreshPortsAction->setEnabled(!detecting && !workflowRunning && !serialRunning);
    m_serialPortCombo->setEnabled(!detecting && !workflowRunning && !serialRunning);
    m_baudCombo->setEnabled(!workflowRunning && !serialRunning);
    m_uploadConfirmation->setEnabled(profileSupported && esp32 && candidateSelected &&
                                     !workflowRunning && !serialRunning);
    m_uploadEsp32Action->setEnabled(executionAllowed && profileSupported && esp32 &&
                                    candidateSelected && m_uploadConfirmation->isChecked() &&
                                    !workflowRunning && !serialRunning &&
                                    QFileInfo(m_lastEsp32BuildResult).isFile());
    const bool diagnosticReady = executionAllowed && profileSupported && esp32 && hasProject &&
                                 candidateSelected && m_uploadConfirmation->isChecked() &&
                                 !workflowRunning && !serialRunning;
    m_deployEsp32Action->setEnabled(diagnosticReady);
    for (auto* action : std::as_const(m_hardwareDiagnosticActions)) {
        action->setEnabled(diagnosticReady);
    }
    m_serialMonitorAction->setEnabled(candidateSelected && !workflowRunning && !serialRunning);
    m_stopSerialMonitorAction->setEnabled(serialRunning);

    if (workflowRunning || detecting) {
        m_workflowProgress->setRange(0, 0);
    } else {
        m_workflowProgress->setRange(0, 1);
        m_workflowProgress->setValue(0);
    }
}

void MainWindow::startWorkflow(const ProcessCommand& command, const WorkflowState state) {
    if (!currentProjectTrusted()) {
        appendBuildOutput(tr("Workflow blocked: the project path is not trusted for execution.\n"),
                          true);
        return;
    }
    if (!command.isValid() || m_buildRunner.isRunning()) {
        appendBuildOutput(tr("Workflow command is invalid or another workflow is active.\n"), true);
        return;
    }
    if (auto* dock = findChild<QDockWidget*>(QStringLiteral("buildOutputDock"))) {
        dock->show();
        dock->raise();
    }
    m_workflowCancelled = false;
    m_workflowState = state;
    switch (state) {
    case WorkflowState::PcBuild:
        m_activeWorkflowTarget = QStringLiteral("Pc");
        m_activeWorkflowConfiguration =
            m_previewRestartController.phase() == PreviewRestartController::Phase::Building
                ? m_previewRestartConfiguration
                : m_configurationCombo->currentData().toString();
        break;
    case WorkflowState::Esp32ExportOnly:
    case WorkflowState::Esp32Build:
    case WorkflowState::Esp32Upload:
    case WorkflowState::Esp32DeployDiagnostics:
        m_activeWorkflowTarget = QStringLiteral("Esp32");
        m_activeWorkflowConfiguration = m_configurationCombo->currentData().toString();
        break;
    case WorkflowState::CustomBuild:
        m_activeWorkflowTarget = QStringLiteral("Custom");
        m_activeWorkflowConfiguration = QStringLiteral("custom");
        break;
    case WorkflowState::Idle:
        m_activeWorkflowTarget.clear();
        m_activeWorkflowConfiguration.clear();
        break;
    }
    m_workflowStatus->setText(command.operation);
    appendBuildOutput(tr("\n===== %1 =====\n").arg(command.operation), false);
    if (!dispatchBuildExtensionServices(state, QStringLiteral("pre-build"), false, -1)) {
        const auto message =
            tr("Workflow blocked because a pre-build extension service failed. Other registered "
               "services were still dispatched; the failing service is disabled for this "
               "project session.");
        appendBuildOutput(message + QLatin1Char('\n'), true);
        appendConsoleMessage(message);
        m_workflowState = WorkflowState::Idle;
        m_activeWorkflowTarget.clear();
        m_activeWorkflowConfiguration.clear();
        m_activeDiagnosticCheck.clear();
        m_workflowStatus->setText(tr("Blocked by pre-build extension"));
        updateWorkflowActions();
        return;
    }
    m_buildRunner.startBuild(command.program, command.arguments, command.workingDirectory);
    updateWorkflowActions();
}

void MainWindow::workflowFinished(const int exitCode, const QProcess::ExitStatus exitStatus) {
    const auto completedState = m_workflowState;
    const bool failed = exitStatus == QProcess::CrashExit || exitCode != 0;
    static_cast<void>(dispatchBuildExtensionServices(completedState, QStringLiteral("post-build"),
                                                     !failed && !m_workflowCancelled, exitCode));
    if (m_workflowCancelled) {
        appendBuildOutput(tr("\nWorkflow cancelled; no success is recorded.\n"), true);
        appendConsoleMessage(tr("Workflow cancelled."));
        m_workflowState = WorkflowState::Idle;
        m_activeWorkflowTarget.clear();
        m_activeWorkflowConfiguration.clear();
        m_activeDiagnosticCheck.clear();
        m_workflowStatus->setText(tr("Cancelled"));
        updateWorkflowActions();
        if (completedState == WorkflowState::PcBuild) {
            finishNativePreviewBuild(false, tr("the build was cancelled"));
        }
        return;
    }
    if (failed) {
        appendBuildOutput(
            tr("\nWorkflow failed (exit code %1); no success is recorded.\n").arg(exitCode), true);
        appendConsoleMessage(tr("Workflow failed with exit code %1.").arg(exitCode));
        m_workflowState = WorkflowState::Idle;
        m_activeWorkflowTarget.clear();
        m_activeWorkflowConfiguration.clear();
        m_activeDiagnosticCheck.clear();
        m_workflowStatus->setText(tr("Failed (exit code %1)").arg(exitCode));
        statusBar()->showMessage(tr("Workflow failed"), 5000);
        updateWorkflowActions();
        if (completedState == WorkflowState::PcBuild) {
            finishNativePreviewBuild(false,
                                     tr("the compiler process exited with code %1").arg(exitCode));
        }
        return;
    }

    QString successMessage;
    switch (completedState) {
    case WorkflowState::CustomBuild:
        successMessage = tr("Custom project build completed successfully.");
        break;
    case WorkflowState::PcBuild: {
        const QString projectOutputRoot =
            QDir(studioRepositoryRoot())
                .filePath(QStringLiteral("out/project-builds/%1/pc")
                              .arg(m_projectData.projectGuid.toLower()));
        const QString scriptOutputRoot =
            QDir(studioRepositoryRoot())
                .filePath(QStringLiteral("out/project-scripts/%1")
                              .arg(m_projectData.projectGuid.toLower()));
        const QString resultPath =
            QDir(projectOutputRoot).filePath(QStringLiteral("project-build-result.json"));
        QFile resultFile(resultPath);
        constexpr qint64 MaximumBuildResultBytes = 1024 * 1024;
        QByteArray resultBytes;
        if (resultFile.open(QIODevice::ReadOnly) && resultFile.size() > 0 &&
            resultFile.size() <= MaximumBuildResultBytes) {
            resultBytes = resultFile.readAll();
        }
        QString resultError;
        QString unusedEsp32Result;
        if (!WorkflowCommands::parseProjectBuildResult(
                resultBytes, m_projectData.projectGuid, m_projectFilePath, QStringLiteral("Pc"),
                projectOutputRoot, scriptOutputRoot, m_lastPcScriptModule, unusedEsp32Result,
                resultError)) {
            appendBuildOutput(
                tr("PC compiler returned success but its project build result is invalid: %1\n"
                   "Expected: %2\n")
                    .arg(resultError, QDir::toNativeSeparators(resultPath)),
                true);
            m_workflowState = WorkflowState::Idle;
            m_activeWorkflowTarget.clear();
            m_activeWorkflowConfiguration.clear();
            m_workflowStatus->setText(tr("Failed: gameplay build result invalid"));
            statusBar()->showMessage(tr("PC build result validation failed"), 5000);
            updateWorkflowActions();
            finishNativePreviewBuild(
                false, tr("the PC build result failed verification: %1").arg(resultError));
            return;
        }
        successMessage = m_lastPcScriptModule.isEmpty()
                             ? tr("PC project validation, asset load, and runtime smoke completed.")
                             : tr("PC project build and runtime smoke completed with verified "
                                  "gameplay module %1.")
                                   .arg(QDir::toNativeSeparators(m_lastPcScriptModule));
    } break;
    case WorkflowState::Esp32ExportOnly:
        successMessage =
            tr("ESP32 sketch exported to %1.").arg(QDir::toNativeSeparators(m_pendingEsp32Sketch));
        break;
    case WorkflowState::Esp32Build: {
        const QString projectOutputRoot =
            QDir(studioRepositoryRoot())
                .filePath(QStringLiteral("out/project-builds/%1/esp32")
                              .arg(m_projectData.projectGuid.toLower()));
        const QString resultPath =
            QDir(projectOutputRoot).filePath(QStringLiteral("project-build-result.json"));
        QFile resultFile(resultPath);
        constexpr qint64 MaximumBuildResultBytes = 1024 * 1024;
        QByteArray resultBytes;
        if (resultFile.open(QIODevice::ReadOnly) && resultFile.size() > 0 &&
            resultFile.size() <= MaximumBuildResultBytes) {
            resultBytes = resultFile.readAll();
        }
        QString resultError;
        QString unusedScriptModule;
        if (!WorkflowCommands::parseProjectBuildResult(
                resultBytes, m_projectData.projectGuid, m_projectFilePath, QStringLiteral("Esp32"),
                projectOutputRoot, QString{}, unusedScriptModule, m_lastEsp32BuildResult,
                resultError)) {
            appendBuildOutput(
                tr("ESP32 compiler returned success but its project build result is invalid: %1\n"
                   "Expected: %2\n")
                    .arg(resultError, QDir::toNativeSeparators(resultPath)),
                true);
            m_workflowState = WorkflowState::Idle;
            m_activeWorkflowTarget.clear();
            m_activeWorkflowConfiguration.clear();
            m_workflowStatus->setText(tr("Failed: ESP32 build result invalid"));
            updateWorkflowActions();
            return;
        }
        successMessage = tr("ESP32 build completed and produced a verified build result.");
    } break;
    case WorkflowState::Esp32Upload:
        successMessage = tr("ESP32 upload completed on %1.").arg(selectedSerialPort());
        m_uploadConfirmation->setChecked(false);
        break;
    case WorkflowState::Esp32DeployDiagnostics: {
        const QString projectOutputRoot =
            QDir(studioRepositoryRoot())
                .filePath(QStringLiteral("out/project-builds/%1/esp32")
                              .arg(m_projectData.projectGuid.toLower()));
        const QString resultPath =
            QDir(projectOutputRoot).filePath(QStringLiteral("project-build-result.json"));
        QFile resultFile(resultPath);
        constexpr qint64 MaximumBuildResultBytes = 1024 * 1024;
        QByteArray resultBytes;
        if (resultFile.open(QIODevice::ReadOnly) && resultFile.size() > 0 &&
            resultFile.size() <= MaximumBuildResultBytes) {
            resultBytes = resultFile.readAll();
        }
        Esp32DeploymentSummary summary;
        QString resultError;
        if (!WorkflowCommands::parseEsp32DeploymentResult(
                resultBytes, m_projectData.projectGuid, m_projectFilePath, projectOutputRoot,
                selectedSerialPort(), m_activeDiagnosticCheck, m_lastEsp32BuildResult, summary,
                resultError)) {
            appendBuildOutput(
                tr("ESP32 pipeline exited successfully but its deployment/diagnostic evidence "
                   "is invalid: %1\nExpected: %2\n")
                    .arg(resultError, QDir::toNativeSeparators(resultPath)),
                true);
            m_workflowState = WorkflowState::Idle;
            m_activeWorkflowTarget.clear();
            m_activeWorkflowConfiguration.clear();
            m_activeDiagnosticCheck.clear();
            m_workflowStatus->setText(tr("Failed: deployment evidence invalid"));
            updateWorkflowActions();
            return;
        }
        const auto detectionLabel =
            summary.selectedPortDetected ? tr("detected") : tr("manual explicit selection");
        const auto manualLabel = summary.manualVerificationPending
                                     ? tr("manual physical verification remains pending")
                                     : tr("no check-specific manual step was emitted");
        successMessage =
            tr("ESP32 pipeline completed on %1 (%2): binary %3 bytes, program %4 bytes, "
               "static RAM %5 bytes; '%6' automated diagnostics PASS, %7. Memory map: %8")
                .arg(summary.port, detectionLabel)
                .arg(summary.binaryBytes)
                .arg(summary.programStorageBytes)
                .arg(summary.globalStaticRamBytes)
                .arg(summary.diagnosticCheck, manualLabel,
                     QDir::toNativeSeparators(summary.memoryMap));
        m_uploadConfirmation->setChecked(false);
    } break;
    case WorkflowState::Idle:
        successMessage = tr("Workflow completed successfully.");
        break;
    }
    appendBuildOutput(QStringLiteral("\n") + successMessage + QLatin1Char('\n'), false);
    appendConsoleMessage(successMessage);
    m_workflowState = WorkflowState::Idle;
    m_activeWorkflowTarget.clear();
    m_activeWorkflowConfiguration.clear();
    m_activeDiagnosticCheck.clear();
    m_workflowStatus->setText(successMessage);
    statusBar()->showMessage(successMessage, 5000);
    updateWorkflowActions();
    if (completedState == WorkflowState::PcBuild) {
        finishNativePreviewBuild(true, successMessage);
    }
}

void MainWindow::portDetectionFinished(const int exitCode, const QProcess::ExitStatus exitStatus) {
    m_serialPortCombo->clear();
    m_serialPortCombo->addItem(tr("Select a port explicitly"));
    m_uploadConfirmation->setChecked(false);
    if (exitStatus == QProcess::CrashExit || exitCode != 0) {
        appendBuildOutput(tr("Port detection failed (exit code %1).\n").arg(exitCode), true);
        m_workflowStatus->setText(tr("Port detection failed"));
        updateWorkflowActions();
        return;
    }
    QVector<SerialPortCandidate> candidates;
    QString errorMessage;
    if (!WorkflowCommands::parseSerialPortReport(m_portDetector.standardOutput(), candidates,
                                                 errorMessage)) {
        appendBuildOutput(errorMessage + QLatin1Char('\n'), true);
        m_workflowStatus->setText(tr("Unsafe/invalid port report rejected"));
        updateWorkflowActions();
        return;
    }
    for (const auto& candidate : candidates) {
        const auto label =
            tr("%1 — %2 [%3]")
                .arg(candidate.port,
                     candidate.displayName.isEmpty() ? candidate.reason : candidate.displayName,
                     candidate.confidence);
        m_serialPortCombo->addItem(label, candidate.port);
        const int index = m_serialPortCombo->count() - 1;
        m_serialPortCombo->setItemData(index, candidate.boardCandidate, Qt::UserRole + 1);
        m_serialPortCombo->setItemData(index, candidate.reason, Qt::ToolTipRole);
    }
    const auto boardCandidateCount = std::count_if(
        candidates.cbegin(), candidates.cend(),
        [](const SerialPortCandidate& candidate) { return candidate.boardCandidate; });
    const auto message = tr("Detected %1 serial port(s); %2 are board candidates. Nothing was "
                            "opened or uploaded.")
                             .arg(candidates.size())
                             .arg(static_cast<qlonglong>(boardCandidateCount));
    appendBuildOutput(message + QLatin1Char('\n'), false);
    m_workflowStatus->setText(message);
    updateWorkflowActions();
}

bool MainWindow::prepareProjectForExternalWorkflow() {
    if (!currentProjectTrusted()) {
        QMessageBox::warning(
            this, tr("Execution Blocked for Untrusted Project"),
            tr("Trust this exact project path before compiling or executing project code."));
        return false;
    }
    if (!currentTargetProfileSupported()) {
        QMessageBox::warning(
            this, tr("Unsupported Target Profile"),
            tr("This Studio build does not support the target profile selected by the project. "
               "Choose a supported project profile before compiling or executing it."));
        return false;
    }
    if (m_projectFilePath.isEmpty() || isWindowModified()) {
        return saveProject();
    }
    return true;
}

QString MainWindow::studioRepositoryRoot() const {
    const auto isRepositoryRoot = [](const QString& path) {
        const QDir root(path);
        return QFileInfo(root.filePath(QStringLiteral("scripts/detect_serial_ports.ps1")))
                   .isFile() &&
               QFileInfo(root.filePath(QStringLiteral("scripts/build_esp32.ps1"))).isFile() &&
               QFileInfo(root.filePath(QStringLiteral("scripts/build_project.ps1"))).isFile();
    };
    QStringList seeds;
    seeds << projectRoot() << QCoreApplication::applicationDirPath()
          << QDir(QCoreApplication::applicationDirPath())
                 .absoluteFilePath(QStringLiteral("../share/fabgl-studio"));
    for (const auto& seed : std::as_const(seeds)) {
        QDir directory(QDir::cleanPath(seed));
        for (;;) {
            if (isRepositoryRoot(directory.absolutePath())) {
                return directory.absolutePath();
            }
            if (!directory.cdUp()) {
                break;
            }
        }
    }
    return {};
}

QString MainWindow::findBuiltTool(const QString& executableName,
                                  const QString& buildSubdirectory) const {
    QString fileName = executableName;
#ifdef Q_OS_WIN
    if (!fileName.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
        fileName += QStringLiteral(".exe");
    }
#endif
    QStringList candidates;
    const QDir applicationDirectory(QCoreApplication::applicationDirPath());
    candidates << applicationDirectory.filePath(fileName);
    QDir currentBuild(applicationDirectory);
    if (currentBuild.cdUp() && currentBuild.cdUp()) {
        candidates << currentBuild.filePath(buildSubdirectory + QLatin1Char('/') + fileName);
    }
    const auto repositoryRoot = studioRepositoryRoot();
    const QDir builds(QDir(repositoryRoot).filePath(QStringLiteral("out/build")));
    const auto buildDirectories =
        builds.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    for (const auto& directory : buildDirectories) {
        candidates << QDir(directory.absoluteFilePath())
                          .filePath(buildSubdirectory + QLatin1Char('/') + fileName);
    }
    for (const auto& candidate : std::as_const(candidates)) {
        const QFileInfo info(candidate);
        if (info.isFile() && info.isExecutable()) {
            return info.absoluteFilePath();
        }
    }
    return {};
}

QString MainWindow::selectedSerialPort() const {
    if (m_serialPortCombo == nullptr) {
        return {};
    }
    QString port = m_serialPortCombo->currentData().toString().trimmed();
    if (port.isEmpty() || m_serialPortCombo->currentIndex() < 0) {
        port = m_serialPortCombo->currentText().trimmed();
    }
    if (port.startsWith(QStringLiteral("COM"), Qt::CaseInsensitive)) {
        port = port.toUpper();
    }
    return port;
}

bool MainWindow::selectedPortIsCandidate() const {
    if (m_serialPortCombo == nullptr || !WorkflowCommands::isSafeSerialPort(selectedSerialPort())) {
        return false;
    }
    return m_serialPortCombo->currentIndex() < 0 ||
           m_serialPortCombo->currentData(Qt::UserRole + 1).toBool();
}

QString MainWindow::createEsp32ExportPath() {
    const auto repositoryRoot = studioRepositoryRoot();
    if (repositoryRoot.isEmpty()) {
        QMessageBox::critical(this, tr("ESP32 Export Unavailable"),
                              tr("The FabGL Studio repository root could not be located."));
        return {};
    }
    QString stem = m_projectName.toLower();
    stem.replace(QRegularExpression(QStringLiteral("[^a-z0-9_-]+")), QStringLiteral("_"));
    stem = stem.left(30);
    if (stem.isEmpty()) {
        stem = QStringLiteral("project");
    }
    const auto suffix =
        QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmsszzz"));
    return QDir(repositoryRoot)
        .filePath(QStringLiteral("out/esp32/studio-exports/%1-%2").arg(stem, suffix));
}

void MainWindow::appendBuildOutput(const QString& text, const bool standardError) {
    m_buildOutput->appendOutput(text, standardError ? BuildOutputSeverity::Error
                                                    : BuildOutputSeverity::Info);
}

void MainWindow::updateBuildActions(const bool running) {
    Q_UNUSED(running)
    updateWorkflowActions();
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
    const auto savedTarget =
        settings.value(QStringLiteral("workflow/target"), static_cast<int>(BuildTarget::Pc))
            .toInt();
    const int targetIndex = m_targetCombo->findData(savedTarget);
    m_targetCombo->setCurrentIndex(targetIndex >= 0 ? targetIndex : 0);
    const auto savedConfiguration =
        settings.value(QStringLiteral("workflow/configuration")).toString();
    const int configurationIndex = m_configurationCombo->findData(savedConfiguration);
    if (configurationIndex >= 0) {
        m_configurationCombo->setCurrentIndex(configurationIndex);
    }
    const auto baud = settings.value(QStringLiteral("workflow/serialBaud"), 115200).toInt();
    const int baudIndex = m_baudCombo->findData(baud);
    if (baudIndex >= 0) {
        m_baudCombo->setCurrentIndex(baudIndex);
    } else {
        m_baudCombo->setCurrentText(QString::number(baud));
    }
    const QSize previewResolution =
        settings.value(QStringLiteral("preview/resolution"), QSize(320, 180)).toSize();
    const int resolutionIndex = m_gameResolutionCombo->findData(previewResolution);
    if (resolutionIndex >= 0) {
        m_gameResolutionCombo->setCurrentIndex(resolutionIndex);
    }
    const int aspectIndex =
        m_gameAspectCombo->findData(settings
                                        .value(QStringLiteral("preview/aspect"),
                                               static_cast<int>(GameView::AspectMode::Preserve))
                                        .toInt());
    if (aspectIndex >= 0) {
        m_gameAspectCombo->setCurrentIndex(aspectIndex);
    }
    const int paletteIndex =
        m_gamePaletteCombo->findData(settings
                                         .value(QStringLiteral("preview/palette"),
                                                static_cast<int>(GameView::PaletteMode::TrueColor))
                                         .toInt());
    if (paletteIndex >= 0) {
        m_gamePaletteCombo->setCurrentIndex(paletteIndex);
    }
    const int fpsIndex =
        m_gameFpsCombo->findData(settings.value(QStringLiteral("preview/targetFps"), 60).toInt());
    if (fpsIndex >= 0) {
        m_gameFpsCombo->setCurrentIndex(fpsIndex);
    }
    const int speedIndex = m_gameSimulationSpeedCombo->findData(
        settings.value(QStringLiteral("preview/simulationSpeed"), 1.0).toDouble());
    if (speedIndex >= 0) {
        m_gameSimulationSpeedCombo->setCurrentIndex(speedIndex);
    }
    m_gameIntegerScaling->setChecked(
        settings.value(QStringLiteral("preview/integerScaling"), true).toBool());
    m_gamePixelPerfect->setChecked(
        settings.value(QStringLiteral("preview/pixelPerfect"), true).toBool());
    m_gameShowFps->setChecked(settings.value(QStringLiteral("preview/showFps"), true).toBool());
    m_gameEsp32Simulation->setChecked(
        settings.value(QStringLiteral("preview/esp32Simulation"), false).toBool());
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
    m_activeLayoutName =
        settings.value(QStringLiteral("ui/activeLayout"), QStringLiteral("Default")).toString();
    if (m_layoutPresetCombo != nullptr) {
        const QSignalBlocker blocker(m_layoutPresetCombo);
        const int presetIndex = m_layoutPresetCombo->findText(m_activeLayoutName);
        m_layoutPresetCombo->setCurrentIndex(presetIndex);
        m_layoutPresetCombo->setToolTip(presetIndex >= 0
                                            ? tr("Built-in layout: %1").arg(m_activeLayoutName)
                                            : tr("Named layout: %1").arg(m_activeLayoutName));
    }
    rebuildRecentProjectsMenu();
    rebuildCustomLayoutsMenu();
}

void MainWindow::saveSettings() {
    QSettings settings;
    settings.setValue(QStringLiteral("ui/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("ui/windowState"), saveState(LayoutVersion));
    settings.setValue(QStringLiteral("ui/theme"),
                      m_theme == Theme::Dark ? QStringLiteral("dark") : QStringLiteral("light"));
    settings.setValue(QStringLiteral("project/recentFiles"), m_recentProjects);
    settings.setValue(QStringLiteral("workflow/target"), m_targetCombo->currentData());
    settings.setValue(QStringLiteral("workflow/configuration"),
                      m_configurationCombo->currentData());
    settings.setValue(QStringLiteral("workflow/serialBaud"), m_baudCombo->currentText().toInt());
    settings.setValue(QStringLiteral("ui/activeLayout"), m_activeLayoutName);
    settings.setValue(QStringLiteral("preview/resolution"), m_gameView->targetResolution());
    settings.setValue(QStringLiteral("preview/aspect"), static_cast<int>(m_gameView->aspectMode()));
    settings.setValue(QStringLiteral("preview/palette"),
                      static_cast<int>(m_gameView->paletteMode()));
    settings.setValue(QStringLiteral("preview/targetFps"), m_gameView->targetFps());
    settings.setValue(QStringLiteral("preview/simulationSpeed"), m_gameView->simulationSpeed());
    settings.setValue(QStringLiteral("preview/integerScaling"), m_gameView->integerScaling());
    settings.setValue(QStringLiteral("preview/pixelPerfect"), m_gameView->pixelPerfect());
    settings.setValue(QStringLiteral("preview/showFps"), m_gameView->fpsOverlayVisible());
    settings.setValue(QStringLiteral("preview/esp32Simulation"), m_gameView->esp32SimulationMode());
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
    applyLayoutPreset(QStringLiteral("Default"));
}

QStringList MainWindow::namedLayouts() const {
    QSettings settings;
    settings.beginGroup(QStringLiteral("layouts/named"));
    QStringList names = settings.childKeys();
    settings.endGroup();
    names.sort(Qt::CaseInsensitive);
    return names;
}

bool MainWindow::saveNamedLayout(const QString& name, QString& errorMessage) {
    const QString cleanName = name.trimmed();
    if (cleanName.isEmpty() || cleanName.size() > 64 || cleanName.contains(QLatin1Char('/')) ||
        cleanName.contains(QLatin1Char('\\'))) {
        errorMessage =
            tr("Layout names must contain 1–64 characters and cannot contain '/' or '\\'.");
        return false;
    }
    QSettings settings;
    settings.beginGroup(QStringLiteral("layouts/named"));
    settings.setValue(cleanName, saveState(LayoutVersion));
    settings.endGroup();
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        errorMessage = tr("The named layout could not be written to settings.");
        return false;
    }
    m_activeLayoutName = cleanName;
    if (m_layoutPresetCombo != nullptr) {
        const QSignalBlocker blocker(m_layoutPresetCombo);
        m_layoutPresetCombo->setCurrentIndex(-1);
        m_layoutPresetCombo->setToolTip(tr("Named layout: %1").arg(cleanName));
    }
    rebuildCustomLayoutsMenu();
    errorMessage.clear();
    return true;
}

bool MainWindow::loadNamedLayout(const QString& name, QString& errorMessage) {
    const QString cleanName = name.trimmed();
    QSettings settings;
    settings.beginGroup(QStringLiteral("layouts/named"));
    const QByteArray state = settings.value(cleanName).toByteArray();
    settings.endGroup();
    if (state.isEmpty()) {
        errorMessage = tr("Named layout '%1' does not exist.").arg(cleanName);
        return false;
    }
    if (!restoreState(state, LayoutVersion)) {
        errorMessage =
            tr("Named layout '%1' is incompatible with this editor version.").arg(cleanName);
        return false;
    }
    m_activeLayoutName = cleanName;
    if (m_layoutPresetCombo != nullptr) {
        const QSignalBlocker blocker(m_layoutPresetCombo);
        m_layoutPresetCombo->setCurrentIndex(-1);
        m_layoutPresetCombo->setToolTip(tr("Named layout: %1").arg(cleanName));
    }
    statusBar()->showMessage(tr("Loaded layout '%1'").arg(cleanName), 3000);
    errorMessage.clear();
    return true;
}

bool MainWindow::deleteNamedLayout(const QString& name, QString& errorMessage) {
    const QString cleanName = name.trimmed();
    QSettings settings;
    settings.beginGroup(QStringLiteral("layouts/named"));
    if (!settings.contains(cleanName)) {
        settings.endGroup();
        errorMessage = tr("Named layout '%1' does not exist.").arg(cleanName);
        return false;
    }
    settings.remove(cleanName);
    settings.endGroup();
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        errorMessage = tr("The named layout could not be removed from settings.");
        return false;
    }
    if (m_activeLayoutName == cleanName) {
        m_activeLayoutName = QStringLiteral("Default");
    }
    rebuildCustomLayoutsMenu();
    errorMessage.clear();
    return true;
}

void MainWindow::applyLayoutPreset(const QString& name) {
    static const QStringList Presets = {QStringLiteral("Default"),   QStringLiteral("2D"),
                                        QStringLiteral("3D"),        QStringLiteral("Scripting"),
                                        QStringLiteral("Animation"), QStringLiteral("Profiling"),
                                        QStringLiteral("Debug")};
    if (!Presets.contains(name)) {
        return;
    }
    (void)restoreState(m_defaultLayout, LayoutVersion);
    if (name != QStringLiteral("Default")) {
        QStringList visibleDocks;
        QString dockToRaise;
        if (name == QStringLiteral("2D")) {
            visibleDocks = {QStringLiteral("hierarchyDock"),    QStringLiteral("projectDock"),
                            QStringLiteral("assetBrowserDock"), QStringLiteral("sceneDock"),
                            QStringLiteral("gameDock"),         QStringLiteral("inspectorDock")};
            dockToRaise = QStringLiteral("sceneDock");
        } else if (name == QStringLiteral("3D")) {
            visibleDocks = {QStringLiteral("hierarchyDock"), QStringLiteral("sceneDock"),
                            QStringLiteral("gameDock"), QStringLiteral("inspectorDock"),
                            QStringLiteral("profilerDock")};
            dockToRaise = QStringLiteral("sceneDock");
        } else if (name == QStringLiteral("Scripting")) {
            visibleDocks = {QStringLiteral("hierarchyDock"),    QStringLiteral("projectDock"),
                            QStringLiteral("assetBrowserDock"), QStringLiteral("inspectorDock"),
                            QStringLiteral("codeEditorDock"),   QStringLiteral("visualScriptDock"),
                            QStringLiteral("consoleDock"),      QStringLiteral("buildOutputDock")};
            dockToRaise = QStringLiteral("visualScriptDock");
        } else if (name == QStringLiteral("Animation")) {
            visibleDocks = {QStringLiteral("hierarchyDock"),    QStringLiteral("projectDock"),
                            QStringLiteral("assetBrowserDock"), QStringLiteral("sceneDock"),
                            QStringLiteral("gameDock"),         QStringLiteral("inspectorDock"),
                            QStringLiteral("animatorDock")};
            dockToRaise = QStringLiteral("animatorDock");
        } else if (name == QStringLiteral("Profiling")) {
            visibleDocks = {QStringLiteral("gameDock"), QStringLiteral("profilerDock"),
                            QStringLiteral("memoryAnalyzerDock"), QStringLiteral("consoleDock"),
                            QStringLiteral("specialistEditorsDock")};
            dockToRaise = QStringLiteral("specialistEditorsDock");
        } else if (name == QStringLiteral("Debug")) {
            visibleDocks = {
                QStringLiteral("hierarchyDock"),      QStringLiteral("gameDock"),
                QStringLiteral("inspectorDock"),      QStringLiteral("profilerDock"),
                QStringLiteral("memoryAnalyzerDock"), QStringLiteral("consoleDock"),
                QStringLiteral("buildOutputDock"),    QStringLiteral("serialMonitorDock"),
                QStringLiteral("targetDeviceDock")};
            dockToRaise = QStringLiteral("profilerDock");
        }
        for (auto* dock : std::as_const(m_docks)) {
            dock->setVisible(visibleDocks.contains(dock->objectName()));
        }
        if (auto* raised = findChild<QDockWidget*>(dockToRaise)) {
            raised->show();
            raised->raise();
        }
    }
    if (m_sceneViewModeCombo != nullptr) {
        if (name == QStringLiteral("3D")) {
            m_sceneViewModeCombo->setCurrentIndex(m_sceneViewModeCombo->findData(
                static_cast<int>(SceneView::ViewMode::ThreeDimensional)));
        } else if (name == QStringLiteral("2D") || name == QStringLiteral("Default")) {
            m_sceneViewModeCombo->setCurrentIndex(m_sceneViewModeCombo->findData(
                static_cast<int>(SceneView::ViewMode::TwoDimensional)));
        }
    }
    if (name == QStringLiteral("Profiling")) {
        if (auto* tabs = findChild<QTabWidget*>(QStringLiteral("specialistEditorTabs"))) {
            tabs->setCurrentWidget(m_profilerTimeline);
        }
    }
    m_activeLayoutName = name;
    if (m_layoutPresetCombo != nullptr) {
        const QSignalBlocker blocker(m_layoutPresetCombo);
        m_layoutPresetCombo->setCurrentText(name);
        m_layoutPresetCombo->setToolTip(tr("Built-in layout: %1").arg(name));
    }
    statusBar()->showMessage(tr("%1 layout applied").arg(name), 3000);
}

void MainWindow::rebuildCustomLayoutsMenu() {
    if (m_customLayoutsMenu == nullptr) {
        return;
    }
    m_customLayoutsMenu->clear();
    const auto names = namedLayouts();
    if (names.isEmpty()) {
        auto* empty = m_customLayoutsMenu->addAction(tr("No Named Layouts"));
        empty->setEnabled(false);
        return;
    }
    for (const auto& name : names) {
        auto* action = m_customLayoutsMenu->addAction(name);
        action->setData(name);
        connect(action, &QAction::triggered, this, [this, name]() {
            QString errorMessage;
            if (!loadNamedLayout(name, errorMessage)) {
                QMessageBox::warning(this, tr("Layout Load Failed"), errorMessage);
            }
        });
    }
}

void MainWindow::saveNamedLayoutDialog() {
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("Save Named Layout"), tr("Layout name"),
                                               QLineEdit::Normal, {}, &accepted)
                             .trimmed();
    if (!accepted) {
        return;
    }
    if (namedLayouts().contains(name) &&
        QMessageBox::question(this, tr("Replace Layout"),
                              tr("A layout named '%1' already exists. Replace it?").arg(name),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    QString errorMessage;
    if (!saveNamedLayout(name, errorMessage)) {
        QMessageBox::warning(this, tr("Layout Save Failed"), errorMessage);
    } else {
        statusBar()->showMessage(tr("Saved layout '%1'").arg(name), 3000);
    }
}

void MainWindow::loadNamedLayoutDialog() {
    const auto names = namedLayouts();
    if (names.isEmpty()) {
        QMessageBox::information(this, tr("Named Layouts"),
                                 tr("No named layouts have been saved."));
        return;
    }
    bool accepted = false;
    const QString name = QInputDialog::getItem(this, tr("Load Named Layout"), tr("Layout"), names,
                                               0, false, &accepted);
    if (!accepted) {
        return;
    }
    QString errorMessage;
    if (!loadNamedLayout(name, errorMessage)) {
        QMessageBox::warning(this, tr("Layout Load Failed"), errorMessage);
    }
}

void MainWindow::deleteNamedLayoutDialog() {
    const auto names = namedLayouts();
    if (names.isEmpty()) {
        QMessageBox::information(this, tr("Named Layouts"),
                                 tr("No named layouts have been saved."));
        return;
    }
    bool accepted = false;
    const QString name = QInputDialog::getItem(this, tr("Delete Named Layout"), tr("Layout"), names,
                                               0, false, &accepted);
    if (!accepted || QMessageBox::question(
                         this, tr("Delete Layout"), tr("Delete the named layout '%1'?").arg(name),
                         QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    QString errorMessage;
    if (!deleteNamedLayout(name, errorMessage)) {
        QMessageBox::warning(this, tr("Layout Delete Failed"), errorMessage);
    } else {
        statusBar()->showMessage(tr("Deleted layout '%1'").arg(name), 3000);
    }
}

void MainWindow::showRecoveryDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Recovery Sessions"));
    dialog.resize(820, 420);
    auto* layout = new QVBoxLayout(&dialog);
    auto* explanation = new QLabel(
        tr("Recovery snapshots are written atomically and kept separately from project trust. "
           "Corrupt snapshots are listed but cannot be restored."),
        &dialog);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    auto* table = new QTableWidget(&dialog);
    table->setObjectName(QStringLiteral("recoverySessionsTable"));
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({tr("Time (UTC)"), tr("Project"), tr("Status"), tr("File")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    layout->addWidget(table, 1);
    auto* buttonRow = new QHBoxLayout();
    auto* restoreButton = new QPushButton(tr("Restore"), &dialog);
    restoreButton->setObjectName(QStringLiteral("restoreRecoveryButton"));
    auto* discardButton = new QPushButton(tr("Discard"), &dialog);
    discardButton->setObjectName(QStringLiteral("discardRecoveryButton"));
    auto* closeButton = new QPushButton(tr("Close"), &dialog);
    buttonRow->addStretch();
    buttonRow->addWidget(restoreButton);
    buttonRow->addWidget(discardButton);
    buttonRow->addWidget(closeButton);
    layout->addLayout(buttonRow);

    const auto reload = [this, table]() {
        const auto recoveries = recoveryEntries();
        table->setRowCount(static_cast<int>(recoveries.size()));
        for (qsizetype rowIndex = 0; rowIndex < recoveries.size(); ++rowIndex) {
            const int row = static_cast<int>(rowIndex);
            const auto& entry = recoveries.at(row);
            auto* time = new QTableWidgetItem(
                entry.timestamp.isValid() ? entry.timestamp.toUTC().toString(Qt::ISODateWithMs)
                                          : tr("Unknown"));
            time->setData(Qt::UserRole, entry.id);
            table->setItem(row, 0, time);
            table->setItem(row, 1,
                           new QTableWidgetItem(entry.projectPath.isEmpty()
                                                    ? tr("Untitled project")
                                                    : QDir::toNativeSeparators(entry.projectPath)));
            auto* status = new QTableWidgetItem(
                entry.corrupt ? tr("Corrupt — %1").arg(entry.errorMessage) : tr("Ready"));
            if (entry.corrupt) {
                status->setForeground(QColor(QStringLiteral("#ff6b6b")));
            }
            table->setItem(row, 2, status);
            table->setItem(row, 3, new QTableWidgetItem(QDir::toNativeSeparators(entry.filePath)));
        }
        if (table->rowCount() > 0) {
            table->selectRow(0);
        }
    };
    reload();
    const auto selectedRecovery = [this, table]() -> std::optional<RecoveryEntry> {
        if (table->currentRow() < 0 || table->item(table->currentRow(), 0) == nullptr) {
            return std::nullopt;
        }
        const QString id = table->item(table->currentRow(), 0)->data(Qt::UserRole).toString();
        const auto recoveries = recoveryEntries();
        const auto iterator =
            std::find_if(recoveries.cbegin(), recoveries.cend(),
                         [&id](const RecoveryEntry& entry) { return entry.id == id; });
        return iterator == recoveries.cend() ? std::nullopt
                                             : std::optional<RecoveryEntry>(*iterator);
    };
    connect(restoreButton, &QPushButton::clicked, &dialog, [this, &dialog, selectedRecovery]() {
        const auto entry = selectedRecovery();
        if (!entry || entry->corrupt) {
            QMessageBox::warning(this, tr("Recovery Unavailable"),
                                 entry ? entry->errorMessage : tr("Select a recovery session."));
            return;
        }
        QString destination = entry->projectPath;
        if (destination.isEmpty()) {
            destination = QFileDialog::getSaveFileName(
                this, tr("Restore Untitled Project"),
                QDir::home().filePath(QStringLiteral("Recovered.fglproject")),
                projectDialogFilter());
            if (destination.isEmpty()) {
                return;
            }
            destination = ensureProjectExtension(destination);
        }
        QString errorMessage;
        if (!restoreRecovery(entry->id, destination, errorMessage)) {
            QMessageBox::critical(this, tr("Recovery Failed"), errorMessage);
            return;
        }
        if (!openProjectPath(destination)) {
            return;
        }
        QString discardError;
        if (!discardRecovery(entry->id, discardError)) {
            appendConsoleMessage(
                tr("Recovery restored, but snapshot cleanup failed: %1").arg(discardError));
        }
        dialog.accept();
    });
    connect(discardButton, &QPushButton::clicked, &dialog, [this, reload, selectedRecovery]() {
        const auto entry = selectedRecovery();
        if (!entry) {
            return;
        }
        QString errorMessage;
        if (!discardRecovery(entry->id, errorMessage)) {
            QMessageBox::warning(this, tr("Discard Failed"), errorMessage);
            return;
        }
        reload();
    });
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    (void)dialog.exec();
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
    if (m_buildRunner.isRunning() || m_pcRunner.isRunning() || m_serialRunner.isRunning() ||
        m_portDetector.isRunning()) {
        const auto answer = QMessageBox::question(
            this, tr("External Process Is Running"),
            tr("A build, player, upload, monitor, or detector process is still running. Stop it "
               "and exit?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        m_closing = true;
        m_previewRestartController.clear();
        m_workflowCancelled = true;
        m_buildRunner.stopBuild();
        m_pcRunner.stopBuild();
        m_serialRunner.stopBuild();
        m_portDetector.stopBuild();
    }
    m_closing = true;
    m_previewRestartController.clear();
    if (m_runState != RunState::Editing) {
        stop();
    }
    QString extensionError;
    if (!deactivateProjectExtensions(extensionError) && !extensionError.isEmpty()) {
        appendConsoleMessage(
            tr("Project extension shutdown reported an error during exit: %1").arg(extensionError));
    }
    if (m_autosaveTimer != nullptr) {
        m_autosaveTimer->stop();
    }
    saveSettings();
    if (m_sessionStarted) {
        QString sessionError;
        if (!m_recoveryManager.endSession(sessionError)) {
            appendConsoleMessage(tr("Could not clear session marker: %1").arg(sessionError));
        } else {
            m_sessionStarted = false;
        }
    }
    event->accept();
}

} // namespace fgl::studio
