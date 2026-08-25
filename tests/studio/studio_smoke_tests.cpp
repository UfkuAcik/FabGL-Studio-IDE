#include "AdvancedEditorPanels.h"
#include "AssetBrowserController.h"
#include "CodeEditor.h"
#include "EditorViews.h"
#include "EntityCommands.h"
#include "ImageImportSettingsWidget.h"
#include "MainWindow.h"
#include "ProjectDocument.h"
#include "ProjectTrustStore.h"
#include "RecoveryManager.h"
#include "SceneDocument.h"
#include "SerialConsoleWidget.h"
#include "WorkflowCommands.h"

#include <fabgl/scene/entity.h>

#include <project_format.h>

#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QUndoStack>
#include <QtTest/QTest>

#include <array>

namespace {

bool createProjectFixture(const QString& projectPath, const QString& name, QString& errorMessage) {
    fgl::studio::ProjectData project;
    project.projectGuid = QStringLiteral("40000000-0000-4000-8000-000000000001");
    project.name = name;
    project.sceneFile = QStringLiteral("Scenes/Main.fglscene");
    fgl::studio::SceneDocument scene;
    scene.createDefault(QStringLiteral("Recovery Test Scene"));
    return scene.saveAs(fgl::studio::ProjectDocument::absoluteScenePath(projectPath, project),
                        errorMessage) &&
           fgl::studio::ProjectDocument::save(projectPath, project, errorMessage);
}

bool writeTestFile(const QString& filePath, const QByteArray& bytes) {
    if (!QDir().mkpath(QFileInfo(filePath).absolutePath())) {
        return false;
    }
    QFile file(filePath);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.write(bytes) == static_cast<qint64>(bytes.size());
}

template <typename Predicate>
bool waitForCondition(Predicate predicate, const int timeoutMilliseconds) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMilliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(10);
    }
    return predicate();
}

class StudioSmokeTests final : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase();
    void createsMainWindowWithCoreUi();
    void opensBundledExampleProject();
    void projectDocumentMigratesV1AndPreservesV2Fields();
    void editsReflectedComponentAndRoundTripsSceneV2();
    void modelsExternalCommandsAsProgramAndArgumentVectors();
    void keepsUploadDisabledWithoutExplicitSafeSelectionAndBuild();
    void moveAssetCommandMovesMappingAndRollsBackFailedRedo();
    void sceneToolsSynchronizeSelectionAndSupportCameraAndUndo();
    void hierarchyMultiSelectionGroupsDuplicateDeleteReparentAndBoxSelection();
    void serialConsoleFiltersClearsAndEmitsExactLineEndingBytes();
    void advancedPanelsUseEngineModelsAndLiveSceneMetrics();
    void typedImageImportSettingsEmitValidatedCanonicalSchema();
    void menusLayoutsThemesAndRunControlsAreFunctional();
    void studioPlayUsesProjectRuntimeForAnimatorAndInput();
    void nativeSourceSaveRestartsPreviewAndCoalescesBuilds();
    void gameViewControlsChangeLivePreviewSettings();
    void codeTextEditMatchesBracketsAndAutoIndents();
    void codeEditorIndexesProjectAndDetectsExternalChanges();
    void visualScriptGraphFilesRoundTripThroughUiModel();
    void visualScriptCanvasSearchHistoryAndLayoutPersist();
    void visualScriptTypedHostNodesAuthorReferencesAndValidate();
    void animatorAssetsRoundTripWithStableReferencesAndPreview();
    void recoveryManagerRotatesDetectsCorruptionAndRestores();
    void projectTrustAndSafeModeGateExecution();
    void mainWindowAutosaveProvidesDialoglessRecovery();

  private:
    QTemporaryDir m_settingsDirectory;
};

void StudioSmokeTests::initTestCase() {
    QVERIFY2(m_settingsDirectory.isValid(), "Could not create isolated settings directory");
    QCoreApplication::setOrganizationName(QStringLiteral("FabGLStudioTests"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("tests.fabgl-studio.org"));
    QCoreApplication::setApplicationName(QStringLiteral("FabGLStudioSmokeTests"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDirectory.path());
}

void StudioSmokeTests::nativeSourceSaveRestartsPreviewAndCoalescesBuilds() {
#ifndef FGL_TEST_NATIVE_SCRIPT_MODULE
    QSKIP("The native script test module is required for preview restart integration coverage.");
#else
#ifdef Q_OS_WIN
    if (QStandardPaths::findExecutable(QStringLiteral("powershell.exe")).isEmpty() &&
        QStandardPaths::findExecutable(QStringLiteral("powershell")).isEmpty()) {
        QSKIP("Windows PowerShell is required for the native preview restart integration test.");
    }
#else
    if (QStandardPaths::findExecutable(QStringLiteral("pwsh")).isEmpty()) {
        QSKIP("PowerShell Core is required for the native preview restart integration test.");
    }
#endif

    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString projectPath =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("Restart.fglproject"));
    QString errorMessage;
    QVERIFY2(createProjectFixture(projectPath, QStringLiteral("Restart Preview"), errorMessage),
             qPrintable(errorMessage));

    const QString scriptsDirectory =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("scripts"));
    const QString nativeModulePath = QStringLiteral(FGL_TEST_NATIVE_SCRIPT_MODULE);
    QVERIFY2(QFileInfo(nativeModulePath).isFile(), qPrintable(nativeModulePath));
    QString powerShellModulePath = QDir::toNativeSeparators(nativeModulePath);
    powerShellModulePath.replace(QLatin1Char('\''), QStringLiteral("''"));
    const QByteArray fakeBuildScript =
        QByteArrayLiteral(
            "param([string]$ProjectPath,[string]$Target,[string]$OutputRoot,"
            "[string]$Configuration)\n"
            "Start-Sleep -Milliseconds 300\n"
            "New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null\n"
            "$manifest = Get-Content -LiteralPath $ProjectPath -Raw | ConvertFrom-Json\n"
            "$moduleSource = '") +
        powerShellModulePath.toUtf8() +
        QByteArrayLiteral(
            "'\n"
            "$moduleRoot = Join-Path (Get-Location) "
            "('out/project-scripts/' + ([string]$manifest.projectGuid).ToLowerInvariant())\n"
            "New-Item -ItemType Directory -Force -Path $moduleRoot | Out-Null\n"
            "$modulePath = Join-Path $moduleRoot ([System.IO.Path]::GetFileName($moduleSource))\n"
            "Copy-Item -LiteralPath $moduleSource -Destination $modulePath -Force\n"
            "$sha256 = [System.Security.Cryptography.SHA256]::Create()\n"
            "$moduleStream = [System.IO.File]::OpenRead($modulePath)\n"
            "try { $moduleHashBytes = $sha256.ComputeHash($moduleStream) } "
            "finally { $moduleStream.Dispose(); $sha256.Dispose() }\n"
            "$moduleHash = ([System.BitConverter]::ToString($moduleHashBytes)).Replace('-', '')."
            "ToLowerInvariant()\n"
            "$result = [ordered]@{ schemaVersion = 1; "
            "kind = 'FabGLStudioProjectBuildResult'; success = $true; dryRun = $false; "
            "project = (Get-Item -LiteralPath $ProjectPath).FullName; "
            "projectGuid = [string]$manifest.projectGuid; target = $Target; "
            "pc = [ordered]@{ success = $true }; "
            "nativeScripts = [ordered]@{ built = $true; module = $modulePath; "
            "moduleSha256 = $moduleHash } }\n"
            "$json = $result | ConvertTo-Json -Depth 5\n"
            "$utf8 = New-Object System.Text.UTF8Encoding($false)\n"
            "[System.IO.File]::WriteAllText((Join-Path $OutputRoot "
            "'project-build-result.json'), $json, $utf8)\n");
    QVERIFY(writeTestFile(QDir(scriptsDirectory).filePath(QStringLiteral("build_project.ps1")),
                          fakeBuildScript));
    QVERIFY(writeTestFile(QDir(scriptsDirectory).filePath(QStringLiteral("build_esp32.ps1")),
                          QByteArrayLiteral("exit 0\n")));
    QVERIFY(
        writeTestFile(QDir(scriptsDirectory).filePath(QStringLiteral("detect_serial_ports.ps1")),
                      QByteArrayLiteral("exit 0\n")));
    const QString sourcePath =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("Scripts/Game.cpp"));
    QVERIFY(writeTestFile(sourcePath, QByteArrayLiteral("int revision = 0;\n")));

    fgl::studio::MainWindow window;
    QString unexpectedDialog;
    QTimer dialogGuard;
    dialogGuard.setInterval(25);
    connect(&dialogGuard, &QTimer::timeout, &window, [&unexpectedDialog]() {
        for (auto* widget : QApplication::topLevelWidgets()) {
            auto* messageBox = qobject_cast<QMessageBox*>(widget);
            if (messageBox == nullptr || !messageBox->isVisible()) {
                continue;
            }
            unexpectedDialog =
                messageBox->windowTitle() + QStringLiteral(": ") + messageBox->text();
            messageBox->done(QMessageBox::Cancel);
        }
    });
    dialogGuard.start();
    QVERIFY2(window.openProjectPath(projectPath, errorMessage), qPrintable(errorMessage));
    QVERIFY2(window.setCurrentProjectTrusted(true, errorMessage), qPrintable(errorMessage));
    const auto editSceneId = window.sceneDocument().scene().id();
    const auto editSceneName = window.sceneDocument().scene().name();
    const auto editEntityCount = window.sceneDocument().scene().entityCount();

    auto* playAction = window.findChild<QAction*>(QStringLiteral("playAction"));
    auto* pauseAction = window.findChild<QAction*>(QStringLiteral("pauseAction"));
    auto* stepAction = window.findChild<QAction*>(QStringLiteral("stepAction"));
    auto* runner =
        window.findChild<fgl::studio::BuildRunner*>(QStringLiteral("workflowBuildRunner"));
    auto* codeEditor = window.findChild<fgl::studio::CodeEditorWidget*>();
    auto* console = window.findChild<QPlainTextEdit*>(QStringLiteral("consoleOutput"));
    auto* buildOutput = window.findChild<fgl::studio::DiagnosticOutputEdit*>();
    QVERIFY(playAction != nullptr);
    QVERIFY(pauseAction != nullptr);
    QVERIFY(stepAction != nullptr);
    QVERIFY(runner != nullptr);
    QVERIFY(codeEditor != nullptr);
    QVERIFY(console != nullptr);
    QVERIFY(buildOutput != nullptr);
    QSignalSpy buildStarted(runner, &fgl::studio::BuildRunner::buildStarted);
    QSignalSpy fileSaved(codeEditor, &fgl::studio::CodeEditorWidget::fileSaved);

    playAction->trigger();
    QVERIFY2(waitForCondition([pauseAction]() { return pauseAction->isEnabled(); }, 3000),
             "Studio Play did not start");
    QVERIFY2(unexpectedDialog.isEmpty(), qPrintable(unexpectedDialog));
    codeEditor->openFile(sourcePath);
    auto* tabs = codeEditor->findChild<QTabWidget*>(QStringLiteral("codeEditorTabs"));
    QVERIFY(tabs != nullptr);
    auto* editor = qobject_cast<fgl::studio::CodeTextEdit*>(tabs->currentWidget());
    QVERIFY(editor != nullptr);

    editor->setPlainText(QStringLiteral("int revision = 1;\n"));
    editor->document()->setModified(true);
    QVERIFY2(codeEditor->saveCurrentFileAs(sourcePath, errorMessage), qPrintable(errorMessage));
    QCOMPARE(fileSaved.count(), 1);
    const QString firstBuildDiagnostic = unexpectedDialog + QLatin1Char('\n') +
                                         buildOutput->toPlainText() + QLatin1Char('\n') +
                                         console->toPlainText();
    QVERIFY2(buildStarted.count() == 1, qPrintable(firstBuildDiagnostic));
    editor->setPlainText(QStringLiteral("int revision = 2;\n"));
    editor->document()->setModified(true);
    QVERIFY2(codeEditor->saveCurrentFileAs(sourcePath, errorMessage), qPrintable(errorMessage));
    editor->setPlainText(QStringLiteral("int revision = 3;\n"));
    editor->document()->setModified(true);
    QVERIFY2(codeEditor->saveCurrentFileAs(sourcePath, errorMessage), qPrintable(errorMessage));

    const bool coalescedBuildStarted =
        waitForCondition([&buildStarted]() { return buildStarted.count() == 2; }, 10000);
    const QString coalescedBuildDiagnostic = unexpectedDialog + QLatin1Char('\n') +
                                             buildOutput->toPlainText() + QLatin1Char('\n') +
                                             console->toPlainText();
    QVERIFY2(coalescedBuildStarted, qPrintable(coalescedBuildDiagnostic));
    QVERIFY2(waitForCondition([runner]() { return !runner->isRunning(); }, 10000),
             "The coalesced native rebuild did not finish");
    QVERIFY2(waitForCondition([pauseAction]() { return pauseAction->isEnabled(); }, 3000),
             "Studio Play did not restart");
    QVERIFY2(unexpectedDialog.isEmpty(), qPrintable(unexpectedDialog));
    QCOMPARE(buildStarted.count(), 2);
    QVERIFY(console->toPlainText().contains(QStringLiteral("coalesced"), Qt::CaseInsensitive));
    QVERIFY(
        console->toPlainText().contains(QStringLiteral("fully restarted"), Qt::CaseInsensitive));

    pauseAction->trigger();
    QVERIFY2(waitForCondition([stepAction]() { return stepAction->isEnabled(); }, 3000),
             "Studio Play did not enter Paused before the external reload");
    QVERIFY(writeTestFile(sourcePath, QByteArrayLiteral("int revision = 4;\n")));
    codeEditor->scanForExternalChanges();
    QVERIFY2(waitForCondition([&buildStarted]() { return buildStarted.count() == 3; }, 10000),
             "The clean external reload did not start a native rebuild");
    QVERIFY2(waitForCondition([runner]() { return !runner->isRunning(); }, 10000),
             "The external-reload native rebuild did not finish");
    QVERIFY2(waitForCondition([stepAction]() { return stepAction->isEnabled(); }, 3000),
             "Studio Play did not restart in its previous Paused state after the external reload");
    QVERIFY2(unexpectedDialog.isEmpty(), qPrintable(unexpectedDialog));

    QCOMPARE(window.sceneDocument().scene().id(), editSceneId);
    QCOMPARE(window.sceneDocument().scene().name(), editSceneName);
    QCOMPARE(window.sceneDocument().scene().entityCount(), editEntityCount);
#endif
}

void StudioSmokeTests::typedImageImportSettingsEmitValidatedCanonicalSchema() {
    fgl::studio::ImageImportSettingsWidget widget(QStringLiteral(
        R"json({"paletteSize":8,"crop":{"x":1,"y":2,"width":16,"height":8},"pivot":{"x":0.25,"y":0.75},"pixelsPerUnit":32,"compression":"rle","residency":"stream"})json"));
    QVERIFY(widget.loadError().isEmpty());
    QCOMPARE(widget.findChild<QSpinBox*>(QStringLiteral("imagePaletteSize"))->value(), 8);
    QVERIFY(widget.findChild<QCheckBox*>(QStringLiteral("imageCropEnabled"))->isChecked());
    QCOMPARE(widget.findChild<QSpinBox*>(QStringLiteral("imageCropWidth"))->value(), 16);
    QCOMPARE(
        widget.findChild<QComboBox*>(QStringLiteral("imageResidency"))->currentData().toString(),
        QStringLiteral("stream"));

    auto* slice = widget.findChild<QCheckBox*>(QStringLiteral("imageSliceEnabled"));
    auto* atlas = widget.findChild<QCheckBox*>(QStringLiteral("imageAtlasEnabled"));
    slice->setChecked(true);
    QVERIFY(atlas->isChecked());
    widget.findChild<QSpinBox*>(QStringLiteral("imageFrameWidth"))->setValue(4);
    widget.findChild<QSpinBox*>(QStringLiteral("imageFrameHeight"))->setValue(4);
    widget.findChild<QSpinBox*>(QStringLiteral("imageAtlasMaximumWidth"))->setValue(128);
    QVERIFY(!widget.findChild<QSpinBox*>(QStringLiteral("imageTargetWidth"))->isEnabled());
    auto encoded = widget.settingsJson();
    QVERIFY2(encoded, encoded ? "" : encoded.error().message().c_str());
    auto decoded =
        fabgl::project::decodeProjectImageImportSettings(encoded.value().toUtf8().toStdString());
    QVERIFY(decoded);
    QCOMPARE(decoded.value().frameWidth, 4);
    QCOMPARE(decoded.value().frameHeight, 4);
    QCOMPARE(decoded.value().atlasMaximumWidth, 128);
    QVERIFY(decoded.value().outputKind == fabgl::assets::ImageOutputKind::SpriteAtlas);
    QVERIFY(decoded.value().residency == fabgl::assets::ImageResidency::Stream);

    fgl::studio::ImageImportSettingsWidget rejected(
        QStringLiteral(R"json({"unknownField":true})json"));
    QVERIFY(!rejected.loadError().isEmpty());
}

void StudioSmokeTests::createsMainWindowWithCoreUi() {
    fgl::studio::MainWindow window;
    window.show();
    QCoreApplication::processEvents();

    QVERIFY(window.isVisible());
    QCOMPARE(window.objectName(), QStringLiteral("FabGLStudioMainWindow"));

    constexpr std::array<const char*, 16> DockNames = {
        "hierarchyDock",   "projectDock",        "sceneDock",         "gameDock",
        "inspectorDock",   "profilerDock",       "codeEditorDock",    "consoleDock",
        "buildOutputDock", "targetDeviceDock",   "serialMonitorDock", "visualScriptDock",
        "animatorDock",    "memoryAnalyzerDock", "assetBrowserDock",  "specialistEditorsDock"};
    for (const auto* dockName : DockNames) {
        const auto* dock = window.findChild<QDockWidget*>(QString::fromLatin1(dockName));
        QVERIFY2(dock != nullptr, dockName);
        QVERIFY2(dock->widget() != nullptr, dockName);
    }
    const auto* specialistTabs =
        window.findChild<QTabWidget*>(QStringLiteral("specialistEditorTabs"));
    QVERIFY(specialistTabs != nullptr);
    QCOMPARE(specialistTabs->count(), 9);
    constexpr std::array<const char*, 9> SpecialistTabLabels = {
        "Material",  "Particles", "Tilemap",     "Raycast Map",      "Racer Track",
        "UI Editor", "Packages",  "Audio Mixer", "Profiler Timeline"};
    for (int index = 0; index < specialistTabs->count(); ++index)
        QCOMPARE(specialistTabs->tabText(index),
                 QString::fromLatin1(SpecialistTabLabels.at(static_cast<std::size_t>(index))));
    constexpr std::array<const char*, 9> SpecialistPanelNames = {
        "materialEditor",      "particleEditor",   "tilemapEditor",
        "raycastMapEditor",    "trackEditor",      "uiEditorPanel",
        "packageManagerPanel", "audioMixerEditor", "profilerTimeline"};
    for (const auto* panelName : SpecialistPanelNames)
        QVERIFY2(window.findChild<QWidget*>(QString::fromLatin1(panelName)) != nullptr, panelName);
    QVERIFY(window.findChild<QAction*>(QStringLiteral("showUiEditorAction")) != nullptr);
    QVERIFY(window.findChild<QAction*>(QStringLiteral("renameAssetAction")) != nullptr);
    QVERIFY(window.findChild<QAction*>(QStringLiteral("moveAssetAction")) != nullptr);
    QVERIFY(window.findChild<QAction*>(QStringLiteral("assetImportSettingsAction")) != nullptr);
    auto* assetBrowser = window.findChild<fgl::studio::AssetBrowserController*>(
        QStringLiteral("assetBrowserController"));
    auto* assetTree = window.findChild<QTreeView*>(QStringLiteral("assetBrowserTree"));
    QVERIFY(assetBrowser != nullptr);
    QVERIFY(assetTree != nullptr);
    QCOMPARE(assetTree->model(), assetBrowser->model());
    QCOMPARE(assetTree->model()->columnCount(),
             static_cast<int>(fgl::studio::AssetBrowserModel::ColumnCount));

    constexpr std::array<const char*, 15> ActionNames = {
        "newProjectAction", "openProjectAction",  "saveProjectAction", "saveProjectAsAction",
        "undoAction",       "redoAction",         "addEntityAction",   "deleteEntityAction",
        "snapToGridAction", "playAction",         "pauseAction",       "stepAction",
        "stopAction",       "buildProjectAction", "cancelBuildAction"};
    for (const auto* actionName : ActionNames) {
        QVERIFY2(window.findChild<QAction*>(QString::fromLatin1(actionName)) != nullptr,
                 actionName);
    }

    QVERIFY(window.findChild<QAction*>(QStringLiteral("playAction"))->isEnabled());
    QVERIFY(!window.findChild<QAction*>(QStringLiteral("pauseAction"))->isEnabled());
    QVERIFY(!window.findChild<QAction*>(QStringLiteral("stopAction"))->isEnabled());
    QVERIFY(!window.findChild<QAction*>(QStringLiteral("cancelBuildAction"))->isEnabled());
    constexpr std::array<const char*, 14> WorkflowActionNames = {
        "pcPlayAction",        "pcStopAction",
        "exportEsp32Action",   "refreshSerialPortsAction",
        "uploadEsp32Action",   "deployEsp32DiagnosticsAction",
        "serialMonitorAction", "stopSerialMonitorAction",
        "selectToolAction",    "moveToolAction",
        "rotateToolAction",    "scaleToolAction",
        "frameSelectedAction", "sceneZoomInAction"};
    for (const auto* actionName : WorkflowActionNames) {
        QVERIFY2(window.findChild<QAction*>(QString::fromLatin1(actionName)) != nullptr,
                 actionName);
    }
    QVERIFY(!window.findChild<QAction*>(QStringLiteral("uploadEsp32Action"))->isEnabled());
    QVERIFY(!window.findChild<QAction*>(QStringLiteral("pcStopAction"))->isEnabled());
    QVERIFY(!window.findChild<QAction*>(QStringLiteral("stopSerialMonitorAction"))->isEnabled());
    constexpr std::array<const char*, 7> HardwareActionNames = {
        "vgaHardwareDiagnosticAction",      "keyboardHardwareDiagnosticAction",
        "mouseHardwareDiagnosticAction",    "audioHardwareDiagnosticAction",
        "sdHardwareDiagnosticAction",       "psramHardwareDiagnosticAction",
        "frameRateHardwareDiagnosticAction"};
    for (const auto* actionName : HardwareActionNames) {
        const auto* action = window.findChild<QAction*>(QString::fromLatin1(actionName));
        QVERIFY2(action != nullptr, actionName);
        QVERIFY2(!action->isEnabled(), actionName);
    }
    constexpr std::array<const char*, 7> MenuNames = {"assetsMenu",    "entityMenu",
                                                      "componentMenu", "debugMenu",
                                                      "toolsMenu",     "hardwareDiagnosticsMenu",
                                                      "windowMenu"};
    for (const auto* menuName : MenuNames) {
        const auto* menu = window.findChild<QMenu*>(QString::fromLatin1(menuName));
        QVERIFY2(menu != nullptr, menuName);
        QVERIFY2(!menu->actions().isEmpty(), menuName);
    }
    QVERIFY(window.close());
}

void StudioSmokeTests::opensBundledExampleProject() {
    const QDir repositoryRoot(QString::fromUtf8(FGL_TEST_REPOSITORY_ROOT));
    const QDir examplesRoot(repositoryRoot.filePath(QStringLiteral("examples")));
    qsizetype manifestCount = 0;
    const auto exampleDirectories =
        examplesRoot.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto& exampleDirectory : exampleDirectories) {
        const QDir directory(exampleDirectory.absoluteFilePath());
        const auto manifests =
            directory.entryInfoList({QStringLiteral("*.fglproject")}, QDir::Files, QDir::Name);
        for (const auto& manifest : manifests) {
            fgl::studio::ProjectData project;
            QString loadError;
            QVERIFY2(
                fgl::studio::ProjectDocument::load(manifest.absoluteFilePath(), project, loadError),
                qPrintable(QStringLiteral("%1: %2").arg(manifest.absoluteFilePath(), loadError)));
            QCOMPARE(project.sourceFormatVersion, fgl::studio::ProjectDocument::FormatVersion);
            fgl::studio::SceneDocument scene;
            QString sceneError;
            const QString scenePath = fgl::studio::ProjectDocument::absoluteScenePath(
                manifest.absoluteFilePath(), project);
            QVERIFY2(scene.load(scenePath, sceneError),
                     qPrintable(QStringLiteral("%1: %2").arg(scenePath, sceneError)));
            ++manifestCount;
        }
    }
    QVERIFY2(manifestCount >= 10, "Expected every bundled example manifest to be discovered");

    const QString projectPath =
        repositoryRoot.filePath(QStringLiteral("examples/empty/Empty.fglproject"));
    QVERIFY2(QFileInfo::exists(projectPath), qPrintable(projectPath));

    QTemporaryDir assetProjects;
    QVERIFY(assetProjects.isValid());
    QString errorMessage;
    fgl::studio::MainWindow window;
    QVERIFY2(window.openProjectPath(projectPath, errorMessage), qPrintable(errorMessage));
    QVERIFY(window.windowTitle().contains(QStringLiteral("Empty Project")));

    const auto* hierarchyDock = window.findChild<QDockWidget*>(QStringLiteral("hierarchyDock"));
    QVERIFY(hierarchyDock != nullptr);
    const auto* hierarchyView = hierarchyDock->findChild<QListView*>();
    QVERIFY(hierarchyView != nullptr);
    QVERIFY(hierarchyView->model() != nullptr);
    QCOMPARE(hierarchyView->model()->rowCount(), 0);

    const auto* projectDock = window.findChild<QDockWidget*>(QStringLiteral("projectDock"));
    QVERIFY(projectDock != nullptr);
    QVERIFY(projectDock->widget() != nullptr);
    QVERIFY(projectDock->widget()->isEnabled());
    const auto* targetProfile =
        window.findChild<QLabel*>(QStringLiteral("projectTargetProfileStatus"));
    QVERIFY(targetProfile != nullptr);
    QVERIFY(targetProfile->text().contains(QStringLiteral("pc.default")));
    QVERIFY(targetProfile->text().contains(QStringLiteral("supported"), Qt::CaseInsensitive));

    const auto* consoleDock = window.findChild<QDockWidget*>(QStringLiteral("consoleDock"));
    QVERIFY(consoleDock != nullptr);
    auto* console = consoleDock->findChild<QPlainTextEdit*>();
    QVERIFY(console != nullptr);
    const QString racerPath =
        repositoryRoot.filePath(QStringLiteral("examples/pseudo3d_racer/Racer.fglproject"));
    QVERIFY2(window.openProjectPath(racerPath, errorMessage), qPrintable(errorMessage));
    const QString racerConsole = console->toPlainText();
    const bool generatedAssetsAvailable =
        racerConsole.contains(QStringLiteral("Loaded 2 presentation asset(s)"));
    const bool sourceOnlyCheckout =
        racerConsole.contains(QStringLiteral("placeholders remain active")) &&
        racerConsole.contains(QStringLiteral("Assets/RacerSprite.fgli"));
    QVERIFY2(generatedAssetsAvailable || sourceOnlyCheckout, qPrintable(racerConsole));

    const QString reloadPath =
        QDir(assetProjects.path()).filePath(QStringLiteral("Reload/RacerAsset.fglproject"));
    fgl::studio::ProjectData reloadData;
    reloadData.projectGuid = QStringLiteral("40000000-0000-4000-8000-000000000006");
    reloadData.name = QStringLiteral("Reload Asset Project");
    reloadData.assets.push_back({QStringLiteral("50000000-0000-4000-8000-000000000005"),
                                 QStringLiteral("Tracks/Main.fgltrack"),
                                 QStringLiteral("racer.track"),
                                 QStringLiteral("{}"),
                                 QStringLiteral("flash"),
                                 {},
                                 false});
    const QString reloadTrack =
        QDir(QFileInfo(reloadPath).absolutePath()).filePath(QStringLiteral("Tracks/Main.fgltrack"));
    QVERIFY(QDir().mkpath(QFileInfo(reloadTrack).absolutePath()));
    QVERIFY(QFile::copy(
        repositoryRoot.filePath(QStringLiteral("examples/pseudo3d_racer/Tracks/Main.fgltrack")),
        reloadTrack));
    fgl::studio::SceneDocument reloadScene;
    reloadScene.createDefault(QStringLiteral("Reload Asset Scene"));
    QVERIFY2(
        reloadScene.saveAs(fgl::studio::ProjectDocument::absoluteScenePath(reloadPath, reloadData),
                           errorMessage),
        qPrintable(errorMessage));
    QVERIFY2(fgl::studio::ProjectDocument::save(reloadPath, reloadData, errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(window.openProjectPath(reloadPath, errorMessage), qPrintable(errorMessage));
    auto* assetBrowser = window.findChild<fgl::studio::AssetBrowserController*>(
        QStringLiteral("assetBrowserController"));
    QVERIFY(assetBrowser != nullptr);
    QCOMPARE(assetBrowser->model()->rowCount(), 1);
    QCOMPARE(assetBrowser->model()
                 ->index(0, fgl::studio::AssetBrowserModel::TypeColumn)
                 .data(fgl::studio::AssetBrowserModel::TypeRole)
                 .toString(),
             QStringLiteral("racer.track"));

    const QString loadedMessage = QStringLiteral("Loaded 1 presentation asset(s)");
    auto* saveAction = window.findChild<QAction*>(QStringLiteral("saveProjectAction"));
    auto* refreshAction = window.findChild<QAction*>(QStringLiteral("refreshAssetsAction"));
    QVERIFY(saveAction != nullptr);
    QVERIFY(refreshAction != nullptr);
    qsizetype loadCount = console->toPlainText().count(loadedMessage);
    saveAction->trigger();
    QCoreApplication::processEvents();
    QCOMPARE(console->toPlainText().count(loadedMessage), loadCount + 1);
    ++loadCount;
    refreshAction->trigger();
    QCoreApplication::processEvents();
    QCOMPARE(console->toPlainText().count(loadedMessage), loadCount + 1);

    const QString brokenPath =
        QDir(assetProjects.path()).filePath(QStringLiteral("Broken/BrokenAsset.fglproject"));
    fgl::studio::ProjectData brokenData;
    brokenData.projectGuid = QStringLiteral("40000000-0000-4000-8000-000000000005");
    brokenData.name = QStringLiteral("Broken Asset Project");
    brokenData.assets.push_back({QStringLiteral("50000000-0000-4000-8000-000000000010"),
                                 QStringLiteral("Assets/Missing.fgli"),
                                 QStringLiteral("image"),
                                 QStringLiteral("{}"),
                                 QStringLiteral("flash"),
                                 {},
                                 false});
    fgl::studio::SceneDocument brokenScene;
    brokenScene.createDefault(QStringLiteral("Broken Asset Scene"));
    QVERIFY2(
        brokenScene.saveAs(fgl::studio::ProjectDocument::absoluteScenePath(brokenPath, brokenData),
                           errorMessage),
        qPrintable(errorMessage));
    QVERIFY2(fgl::studio::ProjectDocument::save(brokenPath, brokenData, errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(window.openProjectPath(brokenPath, errorMessage), qPrintable(errorMessage));
    QVERIFY(window.windowTitle().contains(QStringLiteral("Broken Asset Project")));
    QVERIFY(console->toPlainText().contains(QStringLiteral("placeholders remain active")));
    QVERIFY(console->toPlainText().contains(QStringLiteral("Assets/Missing.fgli")));
}

void StudioSmokeTests::projectDocumentMigratesV1AndPreservesV2Fields() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString legacyPath =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("Legacy.fglproject"));
    const QByteArray legacy = QByteArrayLiteral(
        R"json({"kind":"FabGLStudioProject","formatVersion":1,"name":"Legacy Project","projectRoot":".","startupScene":"Scenes/Main.fglscene","build":{"program":"cmake","arguments":["--build","out/build/dev"]}})json");
    QVERIFY(writeTestFile(legacyPath, legacy));

    QString errorMessage;
    fgl::studio::ProjectData project;
    QVERIFY2(fgl::studio::ProjectDocument::load(legacyPath, project, errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(project.sourceFormatVersion, 1);
    QVERIFY(project.projectGuid.size() == 36);
    QVERIFY(project.inputContexts.isEmpty());
    QVERIFY(project.packageDependencies.isEmpty());
    QCOMPARE(project.targetProfiles.pc, QStringLiteral("pc.default"));
    QCOMPARE(project.targetProfiles.esp32, QStringLiteral("olimex-esp32-sbc-fabgl-revb"));
    QCOMPARE(project.performance.pcProfile, fabgl::project::PerformanceBudgetProfile::Balanced);
    QCOMPARE(project.performance.esp32Profile, fabgl::project::PerformanceBudgetProfile::Safe);

    fgl::studio::ProjectInputContext gameplay;
    gameplay.name = QStringLiteral("gameplay");
    gameplay.priority = 5;
    gameplay.actions.push_back({QStringLiteral("Jump"), {{QStringLiteral("Key.Space"), 1.0, 0.5}}});
    gameplay.axes.push_back(
        {QStringLiteral("MoveX"),
         {{QStringLiteral("Key.A"), -1.0, 0.5}, {QStringLiteral("Key.D"), 1.0, 0.5}}});
    project.inputContexts.push_back(gameplay);
    project.packageDependencies.push_back(
        {QStringLiteral("sample.runtime"), QStringLiteral("^1.2.0")});
    project.assets.push_back({QStringLiteral("50000000-0000-4000-8000-000000000001"),
                              QStringLiteral("Tracks\\Main.fgltrack"),
                              QStringLiteral("racer.track"),
                              QStringLiteral("{}"),
                              QStringLiteral("flash"),
                              {},
                              false});
    project.performance.esp32Profile = fabgl::project::PerformanceBudgetProfile::Custom;
    project.performance.esp32Custom.particles = 321U;
    project.performance.esp32Custom.sdBytes = 0U;

    const QString migratedPath =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("Migrated.fglproject"));
    QVERIFY2(fgl::studio::ProjectDocument::save(migratedPath, project, errorMessage),
             qPrintable(errorMessage));
    QFile migratedFile(migratedPath);
    QVERIFY(migratedFile.open(QIODevice::ReadOnly));
    const auto migratedJson = QJsonDocument::fromJson(migratedFile.readAll()).object();
    QCOMPARE(migratedJson.value(QStringLiteral("formatVersion")).toInt(),
             fgl::studio::ProjectDocument::FormatVersion);
    QCOMPARE(migratedJson.value(QStringLiteral("performance"))
                 .toObject()
                 .value(QStringLiteral("esp32Profile"))
                 .toString(),
             QStringLiteral("custom"));

    fgl::studio::ProjectData roundTrip;
    QVERIFY2(fgl::studio::ProjectDocument::load(migratedPath, roundTrip, errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(roundTrip.sourceFormatVersion, fgl::studio::ProjectDocument::FormatVersion);
    QCOMPARE(roundTrip.projectGuid, project.projectGuid);
    QVERIFY(roundTrip.inputContexts == project.inputContexts);
    QVERIFY(roundTrip.packageDependencies == project.packageDependencies);
    QVERIFY(roundTrip.targetProfiles == project.targetProfiles);
    QVERIFY(roundTrip.performance == project.performance);
    QCOMPARE(roundTrip.assets.size(), qsizetype{1});
    QCOMPARE(roundTrip.assets.constFirst().guid,
             QStringLiteral("50000000-0000-4000-8000-000000000001"));
    QCOMPARE(roundTrip.assets.constFirst().path, QStringLiteral("Tracks/Main.fgltrack"));
    QCOMPARE(roundTrip.assets.constFirst().type, QStringLiteral("racer.track"));

    const QString unsafePath =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("Unsafe.fglproject"));
    QByteArray unsafe = fgl::studio::ProjectDocument::serialized(project, errorMessage);
    QVERIFY(!unsafe.isEmpty());
    unsafe.replace("Scenes/Main.fglscene", "../Outside.fglscene");
    QVERIFY(writeTestFile(unsafePath, unsafe));
    QVERIFY(!fgl::studio::ProjectDocument::load(unsafePath, roundTrip, errorMessage));
    QVERIFY(errorMessage.contains(QStringLiteral("path"), Qt::CaseInsensitive));

    auto duplicateAssetProject = project;
    duplicateAssetProject.assets.push_back({QStringLiteral("50000000-0000-4000-8000-000000000002"),
                                            QStringLiteral("tracks/main.fgltrack"),
                                            QStringLiteral("racer.track"),
                                            QStringLiteral("{}"),
                                            QStringLiteral("flash"),
                                            {},
                                            false});
    QVERIFY(
        fgl::studio::ProjectDocument::serialized(duplicateAssetProject, errorMessage).isEmpty());
    QVERIFY(errorMessage.contains(QStringLiteral("asset"), Qt::CaseInsensitive));
}

void StudioSmokeTests::editsReflectedComponentAndRoundTripsSceneV2() {
    const auto flushDeferredUiWork = []() {
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();
    };

    fgl::studio::MainWindow window;
    window.show();
    QCoreApplication::processEvents();

    auto* addCombo = window.findChild<QComboBox*>(QStringLiteral("addComponentCombo"));
    auto* addButton = window.findChild<QPushButton*>(QStringLiteral("addComponentButton"));
    QVERIFY(addCombo != nullptr);
    QVERIFY(addButton != nullptr);
    const int healthIndex = addCombo->findText(QStringLiteral("Health"));
    QVERIFY(healthIndex >= 0);
    addCombo->setCurrentIndex(healthIndex);
    QTest::mouseClick(addButton, Qt::LeftButton);
    flushDeferredUiWork();

    QVERIFY(window.findChild<QWidget*>(QStringLiteral("component.Health")) != nullptr);
    auto* currentEditor =
        window.findChild<QDoubleSpinBox*>(QStringLiteral("property.Health.current"));
    QVERIFY(currentEditor != nullptr);
    currentEditor->setValue(73.0);
    QVERIFY(QMetaObject::invokeMethod(currentEditor, "editingFinished", Qt::DirectConnection));
    flushDeferredUiWork();

    auto& document = window.sceneDocument();
    const auto entities = document.scene().entities();
    QVERIFY(!entities.empty());
    const auto entityId = entities.front()->id();
    const auto* healthMetadata = document.reflectionRegistry().find("fabgl.Health");
    QVERIFY(healthMetadata != nullptr);

    const auto readHealth = [&document, entityId, healthMetadata]() {
        QString errorMessage;
        return document.componentProperty(entityId, healthMetadata->typeId, "current",
                                          errorMessage);
    };
    QVERIFY(readHealth().has_value());
    QCOMPARE(std::get<std::int64_t>(*readHealth()), std::int64_t{73});

    auto* undoAction = window.findChild<QAction*>(QStringLiteral("undoAction"));
    auto* redoAction = window.findChild<QAction*>(QStringLiteral("redoAction"));
    QVERIFY(undoAction != nullptr);
    QVERIFY(redoAction != nullptr);
    undoAction->trigger();
    QCOMPARE(std::get<std::int64_t>(*readHealth()), std::int64_t{100});
    redoAction->trigger();
    QCOMPARE(std::get<std::int64_t>(*readHealth()), std::int64_t{73});

    auto* deleteAction = window.findChild<QAction*>(QStringLiteral("deleteEntityAction"));
    QVERIFY(deleteAction != nullptr);
    deleteAction->trigger();
    QVERIFY(document.scene().findEntity(entityId) == nullptr);
    undoAction->trigger();
    QVERIFY(document.scene().findEntity(entityId) != nullptr);
    QCOMPARE(std::get<std::int64_t>(*readHealth()), std::int64_t{73});

    auto* removeHealth = window.findChild<QToolButton*>(QStringLiteral("removeComponent.Health"));
    QVERIFY(removeHealth != nullptr);
    QTest::mouseClick(removeHealth, Qt::LeftButton);
    flushDeferredUiWork();
    QString removedError;
    QVERIFY(!document.componentProperty(entityId, healthMetadata->typeId, "current", removedError)
                 .has_value());
    undoAction->trigger();
    QCOMPARE(std::get<std::int64_t>(*readHealth()), std::int64_t{73});

    auto* rotationEditor =
        window.findChild<QWidget*>(QStringLiteral("property.Transform.localRotation"));
    QVERIFY(rotationEditor != nullptr);
    QCOMPARE(rotationEditor->findChildren<QDoubleSpinBox*>().size(), qsizetype{3});

    const auto addComponent = [&window, &flushDeferredUiWork](const QString& name) {
        auto* componentCombo = window.findChild<QComboBox*>(QStringLiteral("addComponentCombo"));
        auto* componentButton =
            window.findChild<QPushButton*>(QStringLiteral("addComponentButton"));
        if (componentCombo == nullptr || componentButton == nullptr)
            return false;
        const int index = componentCombo->findText(name);
        if (index < 0)
            return false;
        componentCombo->setCurrentIndex(index);
        QTest::mouseClick(componentButton, Qt::LeftButton);
        flushDeferredUiWork();
        return window.findChild<QWidget*>(QStringLiteral("component.%1").arg(name)) != nullptr;
    };
    QVERIFY(addComponent(QStringLiteral("Light")));
    QVERIFY(addComponent(QStringLiteral("ScriptComponent")));
    QVERIFY(addComponent(QStringLiteral("UIImage")));
    QVERIFY(addComponent(QStringLiteral("Collider3D")));
    QVERIFY(addComponent(QStringLiteral("VisualScriptComponent")));
    QVERIFY(addComponent(QStringLiteral("NavigationAgent")));
    QVERIFY(addComponent(QStringLiteral("DamageReceiver")));

    auto* intensitySlider =
        window.findChild<QSlider*>(QStringLiteral("property.Light.intensity.slider"));
    auto* intensitySpin =
        window.findChild<QDoubleSpinBox*>(QStringLiteral("property.Light.intensity.spin"));
    QVERIFY(intensitySlider != nullptr);
    QVERIFY(intensitySpin != nullptr);
    intensitySpin->setValue(2.5);
    QVERIFY(QMetaObject::invokeMethod(intensitySpin, "editingFinished", Qt::DirectConnection));
    flushDeferredUiWork();

    auto* notesEditor =
        window.findChild<QPlainTextEdit*>(QStringLiteral("property.ScriptComponent.notes.text"));
    auto* notesApply =
        window.findChild<QPushButton*>(QStringLiteral("property.ScriptComponent.notes.apply"));
    QVERIFY(notesEditor != nullptr);
    QVERIFY(notesApply != nullptr);
    notesEditor->setPlainText(QStringLiteral("first line\nsecond line"));
    QTest::mouseClick(notesApply, Qt::LeftButton);
    flushDeferredUiWork();

    const auto frameAsset = fabgl::AssetGuid::fromStableName("tests.studio.inspector.frame");
    auto* framesTable =
        window.findChild<QTableWidget*>(QStringLiteral("property.UIImage.frames.table"));
    auto* framesAdd = window.findChild<QPushButton*>(QStringLiteral("property.UIImage.frames.add"));
    auto* framesApply =
        window.findChild<QPushButton*>(QStringLiteral("property.UIImage.frames.apply"));
    QVERIFY(framesTable != nullptr);
    QVERIFY(framesAdd != nullptr);
    QVERIFY(framesApply != nullptr);
    QTest::mouseClick(framesAdd, Qt::LeftButton);
    QCOMPARE(framesTable->rowCount(), 1);
    framesTable->item(0, 0)->setText(QString::fromStdString(frameAsset.toString()));
    QTest::mouseClick(framesApply, Qt::LeftButton);
    flushDeferredUiWork();

    auto* falloffTable =
        window.findChild<QTableWidget*>(QStringLiteral("property.Light.falloff.table"));
    auto* falloffApply =
        window.findChild<QPushButton*>(QStringLiteral("property.Light.falloff.apply"));
    QVERIFY(falloffTable != nullptr);
    QVERIFY(falloffApply != nullptr);
    QCOMPARE(falloffTable->rowCount(), 2);
    falloffTable->item(0, 1)->setText(QStringLiteral("0.75"));
    QTest::mouseClick(falloffApply, Qt::LeftButton);
    flushDeferredUiWork();

    auto* animationTable = window.findChild<QTableWidget*>(
        QStringLiteral("property.DamageReceiver.responseCurve.table"));
    QVERIFY(animationTable != nullptr);
    QCOMPARE(animationTable->columnCount(), 4);

    auto* orientationEditor =
        window.findChild<QWidget*>(QStringLiteral("property.Collider3D.orientation"));
    QVERIFY(orientationEditor != nullptr);
    const auto orientationSpins = orientationEditor->findChildren<QDoubleSpinBox*>();
    QCOMPARE(orientationSpins.size(), qsizetype{4});
    orientationSpins.constLast()->setValue(0.5);
    QVERIFY(QMetaObject::invokeMethod(orientationSpins.constLast(), "editingFinished",
                                      Qt::DirectConnection));
    flushDeferredUiWork();

    auto* actionEditor = window.findChild<QLineEdit*>(
        QStringLiteral("property.VisualScriptComponent.triggerAction"));
    auto* eventEditor = window.findChild<QLineEdit*>(
        QStringLiteral("property.VisualScriptComponent.completionEvent"));
    QVERIFY(actionEditor != nullptr);
    QVERIFY(eventEditor != nullptr);
    actionEditor->setText(QStringLiteral("Jump"));
    eventEditor->setText(QStringLiteral("Completed"));
    QVERIFY(QMetaObject::invokeMethod(actionEditor, "editingFinished", Qt::DirectConnection));
    QVERIFY(QMetaObject::invokeMethod(eventEditor, "editingFinished", Qt::DirectConnection));
    flushDeferredUiWork();

    auto* referenceEntity = window.findChild<QComboBox*>(
        QStringLiteral("property.NavigationAgent.targetComponent.entity"));
    auto* referenceComponent = window.findChild<QComboBox*>(
        QStringLiteral("property.NavigationAgent.targetComponent.component"));
    QVERIFY(referenceEntity != nullptr);
    QVERIFY(referenceComponent != nullptr);
    referenceEntity->setCurrentIndex(1);
    QVERIFY(QMetaObject::invokeMethod(referenceEntity, "activated", Qt::DirectConnection,
                                      Q_ARG(int, 1)));
    const auto* transformMetadata = document.reflectionRegistry().find("fabgl.Transform");
    QVERIFY(transformMetadata != nullptr);
    const int transformIndex =
        referenceComponent->findData(QString::fromStdString(transformMetadata->typeId.toString()));
    QVERIFY(transformIndex > 0);
    referenceComponent->setCurrentIndex(transformIndex);
    QVERIFY(QMetaObject::invokeMethod(referenceComponent, "activated", Qt::DirectConnection,
                                      Q_ARG(int, transformIndex)));
    flushDeferredUiWork();

    const auto* lightMetadata = document.reflectionRegistry().find("fabgl.Light");
    const auto* scriptMetadata = document.reflectionRegistry().find("fabgl.ScriptComponent");
    const auto* imageMetadata = document.reflectionRegistry().find("fabgl.UIImage");
    QVERIFY(lightMetadata != nullptr);
    QVERIFY(scriptMetadata != nullptr);
    QVERIFY(imageMetadata != nullptr);
    QString structuredError;
    QCOMPARE(std::get<double>(*document.componentProperty(entityId, lightMetadata->typeId,
                                                          "intensity", structuredError)),
             2.5);
    QCOMPARE(std::get<std::string>(*document.componentProperty(entityId, scriptMetadata->typeId,
                                                               "notes", structuredError)),
             std::string("first line\nsecond line"));
    const auto frames = std::get<fabgl::PropertyList>(
        *document.componentProperty(entityId, imageMetadata->typeId, "frames", structuredError));
    QCOMPARE(frames.values.size(), std::size_t{1});
    QCOMPARE(std::get<fabgl::AssetGuid>(frames.values.front()), frameAsset);

    const QString scenePath = m_settingsDirectory.filePath(QStringLiteral("component-v2.fglscene"));
    QString errorMessage;
    QVERIFY2(document.saveAs(scenePath, errorMessage), qPrintable(errorMessage));
    QFile sceneFile(scenePath);
    QVERIFY(sceneFile.open(QIODevice::ReadOnly));
    QVERIFY(sceneFile.readLine().trimmed() == QByteArrayLiteral("fglscene 2"));

    fgl::studio::SceneDocument reopened;
    QVERIFY2(reopened.load(scenePath, errorMessage), qPrintable(errorMessage));
    const auto restored =
        reopened.componentProperty(entityId, healthMetadata->typeId, "current", errorMessage);
    QVERIFY2(restored.has_value(), qPrintable(errorMessage));
    QCOMPARE(std::get<std::int64_t>(*restored), std::int64_t{73});
    const auto restoredFrames =
        reopened.componentProperty(entityId, imageMetadata->typeId, "frames", errorMessage);
    QVERIFY2(restoredFrames.has_value(), qPrintable(errorMessage));
    QCOMPARE(std::get<fabgl::PropertyList>(*restoredFrames).values.size(), std::size_t{1});
}

void StudioSmokeTests::modelsExternalCommandsAsProgramAndArgumentVectors() {
    using fgl::studio::WorkflowCommands;
    const QDir repositoryRoot(QString::fromUtf8(FGL_TEST_REPOSITORY_ROOT));
    const auto project = repositoryRoot.filePath(QStringLiteral("examples/empty/Empty.fglproject"));
    const auto player = repositoryRoot.filePath(
        QStringLiteral("out/build/example path/apps/player_pc/fabgl_player_pc.exe"));

    const auto pcBuild =
        WorkflowCommands::pcBuild(repositoryRoot.absolutePath(), project, QStringLiteral("Debug"));
    QVERIFY(pcBuild.isValid());
    QVERIFY(pcBuild.program.endsWith(QStringLiteral("powershell.exe"), Qt::CaseInsensitive));
    QCOMPARE(pcBuild.arguments.at(pcBuild.arguments.indexOf(QStringLiteral("-Configuration")) + 1),
             QStringLiteral("Debug"));
    QVERIFY(pcBuild.arguments.join(QLatin1Char('\n'))
                .contains(QStringLiteral("scripts/build_project_scripts.ps1")));
    QCOMPARE(pcBuild.arguments.at(pcBuild.arguments.indexOf(QStringLiteral("-ProjectPath")) + 1),
             QDir::cleanPath(project));

    const QString unifiedPcOutput =
        repositoryRoot.filePath(QStringLiteral("out/project-builds/test-guid/pc"));
    const auto unifiedPc =
        WorkflowCommands::projectBuild(repositoryRoot.absolutePath(), project, QStringLiteral("Pc"),
                                       QStringLiteral("Release"), unifiedPcOutput);
    QVERIFY(unifiedPc.isValid());
    QVERIFY(unifiedPc.arguments.join(QLatin1Char('\n'))
                .contains(QStringLiteral("scripts/build_project.ps1")));
    QCOMPARE(unifiedPc.arguments.at(unifiedPc.arguments.indexOf(QStringLiteral("-Target")) + 1),
             QStringLiteral("Pc"));
    QCOMPARE(
        unifiedPc.arguments.at(unifiedPc.arguments.indexOf(QStringLiteral("-Configuration")) + 1),
        QStringLiteral("Release"));
    QVERIFY(!unifiedPc.arguments.contains(QStringLiteral("-Upload")));

    const auto unifiedEsp32 = WorkflowCommands::projectBuild(
        repositoryRoot.absolutePath(), project, QStringLiteral("Esp32"),
        QStringLiteral("PerformanceOptimized"),
        repositoryRoot.filePath(QStringLiteral("out/project-builds/test-guid/esp32")));
    QVERIFY(unifiedEsp32.isValid());
    QCOMPARE(unifiedEsp32.arguments.at(
                 unifiedEsp32.arguments.indexOf(QStringLiteral("-Esp32BuildProfile")) + 1),
             QStringLiteral("PerformanceOptimized"));
    QVERIFY(unifiedEsp32.arguments.contains(QStringLiteral("-Clean")));
    QVERIFY(!unifiedEsp32.arguments.contains(QStringLiteral("-Upload")));
    const auto unifiedEsp32Psram = WorkflowCommands::projectBuild(
        repositoryRoot.absolutePath(), project, QStringLiteral("Esp32"),
        QStringLiteral("ReleasePsram"),
        repositoryRoot.filePath(QStringLiteral("out/project-builds/test-guid/esp32-psram")));
    QVERIFY(unifiedEsp32Psram.isValid());
    QVERIFY(unifiedEsp32Psram.arguments.contains(QStringLiteral("-EnablePsram")));
    const auto deployment = WorkflowCommands::esp32BuildUploadDiagnostics(
        repositoryRoot.absolutePath(), project, QStringLiteral("PerformanceOptimized"),
        repositoryRoot.filePath(QStringLiteral("out/project-builds/test-guid/esp32-deploy")),
        QStringLiteral("COM5"), 115200, QStringLiteral("all"), 15);
    QVERIFY(deployment.isValid());
    QVERIFY(deployment.arguments.contains(QStringLiteral("-Upload")));
    QVERIFY(deployment.arguments.contains(QStringLiteral("-RuntimeDiagnostics")));
    QVERIFY(deployment.arguments.contains(QStringLiteral("-EnablePsram")));
    QCOMPARE(deployment.arguments.at(deployment.arguments.indexOf(QStringLiteral("-Port")) + 1),
             QStringLiteral("COM5"));
    QCOMPARE(deployment.arguments.at(
                 deployment.arguments.indexOf(QStringLiteral("-ConfirmBoardProfile")) + 1),
             QString::fromLatin1(WorkflowCommands::BoardProfile));
    QCOMPARE(deployment.arguments.at(
                 deployment.arguments.indexOf(QStringLiteral("-DiagnosticCheck")) + 1),
             QStringLiteral("all"));
    QVERIFY(!WorkflowCommands::esp32BuildUploadDiagnostics(
                 repositoryRoot.absolutePath(), project, QStringLiteral("Release"), unifiedPcOutput,
                 QStringLiteral("unsafe;port"), 115200, QStringLiteral("vga"))
                 .isValid());
    QVERIFY(!WorkflowCommands::projectBuild(repositoryRoot.absolutePath(), project,
                                            QStringLiteral("Unknown"), QStringLiteral("Release"),
                                            unifiedPcOutput)
                 .isValid());

    const auto play = WorkflowCommands::pcPlay(player, project, repositoryRoot.absolutePath());
    QCOMPARE(play.program, QDir::cleanPath(player));
    QCOMPARE(play.arguments, QStringList({QStringLiteral("--project"), QDir::cleanPath(project)}));

    QTemporaryDir gameplayBuild;
    QVERIFY(gameplayBuild.isValid());
    const QString gameplayOutput =
        QDir(gameplayBuild.path()).filePath(QStringLiteral("project-guid"));
    QVERIFY(QDir().mkpath(QDir(gameplayOutput).filePath(QStringLiteral("build/module"))));
#ifdef Q_OS_WIN
    const QString moduleName = QStringLiteral("fabgl_gameplay_scripts.dll");
#elif defined(Q_OS_MACOS)
    const QString moduleName = QStringLiteral("libfabgl_gameplay_scripts.dylib");
#else
    const QString moduleName = QStringLiteral("libfabgl_gameplay_scripts.so");
#endif
    const QString modulePath =
        QDir(gameplayOutput).filePath(QStringLiteral("build/module/") + moduleName);
    QVERIFY(writeTestFile(modulePath, QByteArrayLiteral("native-module-fixture")));
    constexpr auto ProjectGuid = "40000000-0000-4000-8000-000000000020";
    const QByteArray buildResult =
        QJsonDocument(
            QJsonObject{{QStringLiteral("schemaVersion"), 2},
                        {QStringLiteral("kind"), QStringLiteral("FabGLStudioGameplayBuildResult")},
                        {QStringLiteral("success"), true},
                        {QStringLiteral("projectGuid"), QString::fromLatin1(ProjectGuid)},
                        {QStringLiteral("module"), QFileInfo(modulePath).absoluteFilePath()}})
            .toJson(QJsonDocument::Compact);
    QString parsedModule;
    QString resultError;
    QVERIFY2(WorkflowCommands::parsePcBuildResult(buildResult, QString::fromLatin1(ProjectGuid),
                                                  gameplayOutput, parsedModule, resultError),
             qPrintable(resultError));
    QCOMPARE(parsedModule, QFileInfo(modulePath).canonicalFilePath());

    const auto playWithModule =
        WorkflowCommands::pcPlay(player, project, repositoryRoot.absolutePath(), parsedModule);
    QCOMPARE(playWithModule.arguments,
             QStringList({QStringLiteral("--project"), QDir::cleanPath(project),
                          QStringLiteral("--script-module"), QDir::cleanPath(parsedModule)}));

    const QString projectBuildOutput =
        QDir(gameplayBuild.path()).filePath(QStringLiteral("unified-output"));
    QVERIFY(QDir().mkpath(projectBuildOutput));
    QFile moduleFixture(modulePath);
    QVERIFY(moduleFixture.open(QIODevice::ReadOnly));
    const QString moduleSha256 = QString::fromLatin1(
        QCryptographicHash::hash(moduleFixture.readAll(), QCryptographicHash::Sha256).toHex());
    const QByteArray unifiedBuildResult =
        QJsonDocument(
            QJsonObject{
                {QStringLiteral("schemaVersion"), 1},
                {QStringLiteral("kind"), QStringLiteral("FabGLStudioProjectBuildResult")},
                {QStringLiteral("success"), true},
                {QStringLiteral("dryRun"), false},
                {QStringLiteral("project"), QFileInfo(project).absoluteFilePath()},
                {QStringLiteral("projectGuid"), QString::fromLatin1(ProjectGuid)},
                {QStringLiteral("target"), QStringLiteral("Pc")},
                {QStringLiteral("nativeScripts"),
                 QJsonObject{{QStringLiteral("built"), true},
                             {QStringLiteral("module"), QFileInfo(modulePath).absoluteFilePath()},
                             {QStringLiteral("moduleSha256"), moduleSha256}}},
                {QStringLiteral("pc"), QJsonObject{{QStringLiteral("success"), true}}}})
            .toJson(QJsonDocument::Compact);
    QString parsedEsp32Result;
    parsedModule.clear();
    QVERIFY2(WorkflowCommands::parseProjectBuildResult(
                 unifiedBuildResult, QString::fromLatin1(ProjectGuid), project,
                 QStringLiteral("Pc"), projectBuildOutput, gameplayOutput, parsedModule,
                 parsedEsp32Result, resultError),
             qPrintable(resultError));
    QCOMPARE(parsedModule, QFileInfo(modulePath).canonicalFilePath());
    QVERIFY(parsedEsp32Result.isEmpty());

    auto tamperedUnified = QJsonDocument::fromJson(unifiedBuildResult).object();
    auto tamperedScripts = tamperedUnified.value(QStringLiteral("nativeScripts")).toObject();
    tamperedScripts.insert(QStringLiteral("moduleSha256"), QString(64, QLatin1Char('0')));
    tamperedUnified.insert(QStringLiteral("nativeScripts"), tamperedScripts);
    QVERIFY(!WorkflowCommands::parseProjectBuildResult(
        QJsonDocument(tamperedUnified).toJson(QJsonDocument::Compact),
        QString::fromLatin1(ProjectGuid), project, QStringLiteral("Pc"), projectBuildOutput,
        gameplayOutput, parsedModule, parsedEsp32Result, resultError));

    auto invalidResult = QJsonDocument::fromJson(buildResult).object();
    invalidResult.insert(QStringLiteral("kind"), QStringLiteral("UnexpectedBuildResult"));
    QVERIFY(!WorkflowCommands::parsePcBuildResult(
        QJsonDocument(invalidResult).toJson(QJsonDocument::Compact),
        QString::fromLatin1(ProjectGuid), gameplayOutput, parsedModule, resultError));
    QVERIFY(parsedModule.isEmpty());

    const QString outsideModule =
        QDir(gameplayBuild.path()).filePath(QStringLiteral("outside-module.dll"));
    QVERIFY(writeTestFile(outsideModule, QByteArrayLiteral("outside-module-fixture")));
    invalidResult = QJsonDocument::fromJson(buildResult).object();
    invalidResult.insert(QStringLiteral("module"), QFileInfo(outsideModule).absoluteFilePath());
    QVERIFY(!WorkflowCommands::parsePcBuildResult(
        QJsonDocument(invalidResult).toJson(QJsonDocument::Compact),
        QString::fromLatin1(ProjectGuid), gameplayOutput, parsedModule, resultError));
    QVERIFY(resultError.contains(QStringLiteral("escapes"), Qt::CaseInsensitive));

    const auto detection = WorkflowCommands::detectSerialPorts(repositoryRoot.absolutePath());
    QVERIFY(detection.isValid());
    QVERIFY(!detection.arguments.contains(QStringLiteral("upload"), Qt::CaseInsensitive));
    QVERIFY(detection.arguments.join(QLatin1Char('\n'))
                .contains(QStringLiteral("scripts/detect_serial_ports.ps1")));

    const auto sketch = repositoryRoot.filePath(QStringLiteral("out/esp32/studio-exports/test"));
    const auto exported = WorkflowCommands::esp32Export(
        repositoryRoot.filePath(
            QStringLiteral("out/build/dev/tools/project_cli/fabgl_project_cli.exe")),
        project, repositoryRoot.filePath(QStringLiteral("platforms/fabgl/firmware")), sketch,
        repositoryRoot.absolutePath());
    QVERIFY(exported.isValid());
    QCOMPARE(exported.arguments.constFirst(), QStringLiteral("export-esp32"));
    QCOMPARE(exported.arguments.constLast(), QDir::cleanPath(sketch));
    const auto esp32Build = WorkflowCommands::esp32Build(
        repositoryRoot.absolutePath(), sketch,
        repositoryRoot.filePath(QStringLiteral("out/esp32/studio-builds/test")), true);
    QVERIFY(esp32Build.isValid());
    QVERIFY(esp32Build.arguments.contains(QStringLiteral("-EnablePsram")));
    QVERIFY(!esp32Build.arguments.contains(QStringLiteral("upload"), Qt::CaseInsensitive));

    QVERIFY(!WorkflowCommands::uploadEsp32(repositoryRoot.absolutePath(),
                                           QStringLiteral("not-a-port"),
                                           QStringLiteral("result.json"))
                 .isValid());
    const auto upload = WorkflowCommands::uploadEsp32(
        repositoryRoot.absolutePath(), QStringLiteral("COM12"),
        repositoryRoot.filePath(QStringLiteral("out/esp32/build-result.json")));
    QVERIFY(upload.isValid());
    QCOMPARE(upload.arguments.at(upload.arguments.indexOf(QStringLiteral("-Port")) + 1),
             QStringLiteral("COM12"));
    QCOMPARE(
        upload.arguments.at(upload.arguments.indexOf(QStringLiteral("-ConfirmBoardProfile")) + 1),
        QString::fromLatin1(WorkflowCommands::BoardProfile));

    const auto monitor = WorkflowCommands::serialMonitor(repositoryRoot.absolutePath(),
                                                         QStringLiteral("COM12"), 115200);
    QVERIFY(monitor.isValid());
    QVERIFY(!monitor.arguments.contains(QStringLiteral("upload"), Qt::CaseInsensitive));
    QCOMPARE(monitor.arguments.at(monitor.arguments.indexOf(QStringLiteral("-Baud")) + 1),
             QStringLiteral("115200"));

    QFile fixture(
        repositoryRoot.filePath(QStringLiteral("tests/hardware/fixtures/serial-ports.json")));
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    const auto fixtureCommand =
        WorkflowCommands::detectSerialPorts(repositoryRoot.absolutePath(), fixture.fileName());
    QVERIFY(fixtureCommand.arguments.contains(QStringLiteral("-FixturePath")));

    const QByteArray report = QByteArrayLiteral(
        R"json({"schemaVersion":1,"operation":"read-only-port-detection","uploadPerformed":false,"portOpened":false,"ports":[{"port":"COM12","displayName":"CH340 (COM12)","confidence":"high","reason":"known bridge","boardCandidate":true,"requiresUserConfirmation":true}]})json");
    QVector<fgl::studio::SerialPortCandidate> ports;
    QString parseError;
    QVERIFY2(WorkflowCommands::parseSerialPortReport(report, ports, parseError),
             qPrintable(parseError));
    QCOMPARE(ports.size(), 1);
    QCOMPARE(ports.constFirst().port, QStringLiteral("COM12"));
    QVERIFY(ports.constFirst().boardCandidate);

    const QByteArray unsafeReport = QByteArrayLiteral(
        R"json({"schemaVersion":1,"operation":"read-only-port-detection","uploadPerformed":true,"portOpened":false,"ports":[]})json");
    QVERIFY(!WorkflowCommands::parseSerialPortReport(unsafeReport, ports, parseError));

    const QString deploymentRoot =
        QDir(gameplayBuild.path()).filePath(QStringLiteral("deployment-output"));
    QVERIFY(QDir().mkpath(
        QDir(deploymentRoot).filePath(QStringLiteral("esp32/hardware-diagnostics/vga"))));
    const QString nestedEsp32Result =
        QDir(deploymentRoot).filePath(QStringLiteral("esp32/build-result.json"));
    const QString uploadResult =
        QDir(deploymentRoot).filePath(QStringLiteral("esp32/upload-result.json"));
    const QString memoryMap =
        QDir(deploymentRoot).filePath(QStringLiteral("esp32/firmware.ino.map"));
    const QString diagnosticResult =
        QDir(deploymentRoot)
            .filePath(
                QStringLiteral("esp32/hardware-diagnostics/vga/hardware-diagnostic-result.json"));
    QVERIFY(writeTestFile(nestedEsp32Result, QByteArrayLiteral("{}")));
    QVERIFY(writeTestFile(uploadResult, QByteArrayLiteral("{}")));
    QVERIFY(writeTestFile(memoryMap, QByteArrayLiteral("memory map fixture")));
    QJsonObject structuredDiagnostic{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("operation"), QStringLiteral("bounded-hardware-diagnostic-capture")},
        {QStringLiteral("dryRun"), false},
        {QStringLiteral("fixtureMode"), false},
        {QStringLiteral("portOpened"), true},
        {QStringLiteral("uploadPerformed"), false},
        {QStringLiteral("profile"), QString::fromLatin1(WorkflowCommands::BoardProfile)},
        {QStringLiteral("port"), QStringLiteral("COM5")},
        {QStringLiteral("diagnosticCheck"), QStringLiteral("vga")},
        {QStringLiteral("automatedResult"), QStringLiteral("PASS")},
        {QStringLiteral("manualVerificationPending"), true},
        {QStringLiteral("hardwareVerified"), false}};
    QVERIFY(writeTestFile(diagnosticResult,
                          QJsonDocument(structuredDiagnostic).toJson(QJsonDocument::Compact)));
    const QJsonObject deploymentResultObject{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("kind"), QStringLiteral("FabGLStudioProjectBuildResult")},
        {QStringLiteral("success"), true},
        {QStringLiteral("dryRun"), false},
        {QStringLiteral("project"), QFileInfo(project).absoluteFilePath()},
        {QStringLiteral("projectGuid"), QString::fromLatin1(ProjectGuid)},
        {QStringLiteral("target"), QStringLiteral("Esp32")},
        {QStringLiteral("esp32"),
         QJsonObject{{QStringLiteral("success"), true},
                     {QStringLiteral("result"), QFileInfo(nestedEsp32Result).absoluteFilePath()},
                     {QStringLiteral("binaryBytes"), 601234.0},
                     {QStringLiteral("programStorageBytes"), 600000.0},
                     {QStringLiteral("globalStaticRamBytes"), 120000.0},
                     {QStringLiteral("map"), QFileInfo(memoryMap).absoluteFilePath()}}},
        {QStringLiteral("portDetection"),
         QJsonObject{{QStringLiteral("performed"), true},
                     {QStringLiteral("readOnly"), true},
                     {QStringLiteral("selectedPort"), QStringLiteral("COM5")},
                     {QStringLiteral("selectedPortDetected"), true}}},
        {QStringLiteral("upload"),
         QJsonObject{
             {QStringLiteral("requested"), true},
             {QStringLiteral("performed"), true},
             {QStringLiteral("port"), QStringLiteral("COM5")},
             {QStringLiteral("boardProfile"), QString::fromLatin1(WorkflowCommands::BoardProfile)},
             {QStringLiteral("result"), QFileInfo(uploadResult).absoluteFilePath()}}},
        {QStringLiteral("monitor"), QJsonObject{{QStringLiteral("requested"), true},
                                                {QStringLiteral("performed"), true},
                                                {QStringLiteral("bounded"), true}}},
        {QStringLiteral("runtimeDiagnostics"),
         QJsonObject{{QStringLiteral("requested"), true},
                     {QStringLiteral("performed"), true},
                     {QStringLiteral("diagnosticCheck"), QStringLiteral("vga")},
                     {QStringLiteral("automatedResult"), QStringLiteral("PASS")},
                     {QStringLiteral("hardwareVerified"), false},
                     {QStringLiteral("result"), QFileInfo(diagnosticResult).absoluteFilePath()}}}};
    const auto deploymentResult =
        QJsonDocument(deploymentResultObject).toJson(QJsonDocument::Compact);
    fgl::studio::Esp32DeploymentSummary deploymentSummary;
    parsedEsp32Result.clear();
    QVERIFY2(WorkflowCommands::parseEsp32DeploymentResult(
                 deploymentResult, QString::fromLatin1(ProjectGuid), project, deploymentRoot,
                 QStringLiteral("COM5"), QStringLiteral("vga"), parsedEsp32Result,
                 deploymentSummary, resultError),
             qPrintable(resultError));
    QVERIFY(deploymentSummary.manualVerificationPending);
    QVERIFY(deploymentSummary.selectedPortDetected);
    QCOMPARE(deploymentSummary.binaryBytes, quint64{601234});
    QCOMPARE(deploymentSummary.memoryMap, QFileInfo(memoryMap).canonicalFilePath());
    structuredDiagnostic.insert(QStringLiteral("fixtureMode"), true);
    QVERIFY(writeTestFile(diagnosticResult,
                          QJsonDocument(structuredDiagnostic).toJson(QJsonDocument::Compact)));
    QVERIFY(!WorkflowCommands::parseEsp32DeploymentResult(
        deploymentResult, QString::fromLatin1(ProjectGuid), project, deploymentRoot,
        QStringLiteral("COM5"), QStringLiteral("vga"), parsedEsp32Result, deploymentSummary,
        resultError));

    QCOMPARE(fgl::studio::BuildRunner::classifyDiagnosticLine(
                 QStringLiteral("Game.cpp:17:9: warning: unused variable [-Wunused-variable]")),
             fgl::studio::BuildOutputSeverity::Warning);
    QCOMPARE(fgl::studio::BuildRunner::classifyDiagnosticLine(
                 QStringLiteral("CMake Error at CMakeLists.txt:24 (message): broken")),
             fgl::studio::BuildOutputSeverity::Error);
    QCOMPARE(fgl::studio::BuildRunner::classifyDiagnosticLine(
                 QStringLiteral("Downloading platform index...")),
             fgl::studio::BuildOutputSeverity::Info);
}

void StudioSmokeTests::keepsUploadDisabledWithoutExplicitSafeSelectionAndBuild() {
    const QDir repositoryRoot(QString::fromUtf8(FGL_TEST_REPOSITORY_ROOT));
    fgl::studio::MainWindow window;
    QString openError;
    QVERIFY2(
        window.openProjectPath(
            repositoryRoot.filePath(QStringLiteral("examples/empty/Empty.fglproject")), openError),
        qPrintable(openError));

    auto* target = window.findChild<QComboBox*>(QStringLiteral("buildTargetCombo"));
    auto* ports = window.findChild<QComboBox*>(QStringLiteral("serialPortCombo"));
    auto* confirmation = window.findChild<QCheckBox*>(QStringLiteral("uploadBoardConfirmation"));
    auto* upload = window.findChild<QAction*>(QStringLiteral("uploadEsp32Action"));
    auto* monitor = window.findChild<QAction*>(QStringLiteral("serialMonitorAction"));
    QVERIFY(target != nullptr);
    QVERIFY(ports != nullptr);
    QVERIFY(confirmation != nullptr);
    QVERIFY(upload != nullptr);
    QVERIFY(monitor != nullptr);

    target->setCurrentIndex(target->findText(QStringLiteral("ESP32")));
    QVERIFY(!upload->isEnabled());
    QVERIFY(!monitor->isEnabled());
    ports->addItem(QStringLiteral("COM12 — test candidate"), QStringLiteral("COM12"));
    ports->setItemData(1, true, Qt::UserRole + 1);
    ports->setCurrentIndex(1);
    QVERIFY(monitor->isEnabled());
    QVERIFY(!upload->isEnabled());
    confirmation->setChecked(true);
    QVERIFY(!upload->isEnabled());
    ports->setCurrentIndex(0);
    QVERIFY(!confirmation->isChecked());
    QVERIFY(!upload->isEnabled());
}

void StudioSmokeTests::moveAssetCommandMovesMappingAndRollsBackFailedRedo() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QDir root(temporaryDirectory.path());
    QVERIFY(root.mkpath(QStringLiteral("Assets")));
    QVERIFY(root.mkpath(QStringLiteral("Moved")));
    const QString sourcePath = root.filePath(QStringLiteral("Assets/Player.fgli"));
    const QString destinationPath = root.filePath(QStringLiteral("Moved/Player.fgli"));
    QVERIFY(writeTestFile(sourcePath, QByteArrayLiteral("indexed-image-payload")));

    struct AssetMapping final {
        QString guid;
        QString path;
    } mapping{QStringLiteral("50000000-0000-4000-8000-000000000001"),
              QStringLiteral("Assets/Player.fgli")};
    const QString stableGuid = mapping.guid;
    QStringList relocations;
    QString reportedError;
    const auto relocate = [&root, &mapping, &relocations](const QString& source,
                                                          const QString& destination,
                                                          QString& errorMessage) {
        if (!QFileInfo(source).isFile()) {
            errorMessage = QStringLiteral("source is unavailable");
            return false;
        }
        if (QFileInfo::exists(destination)) {
            errorMessage = QStringLiteral("destination exists");
            return false;
        }
        if (!QFile::rename(source, destination)) {
            errorMessage = QStringLiteral("filesystem move failed");
            return false;
        }
        mapping.path = QDir::fromNativeSeparators(
            root.relativeFilePath(QFileInfo(destination).absoluteFilePath()));
        relocations.push_back(QStringLiteral("%1 -> %2").arg(source, destination));
        errorMessage.clear();
        return true;
    };
    const auto report = [&reportedError](const QString& errorMessage) {
        reportedError = errorMessage;
    };

    QUndoStack history;
    history.push(new fgl::studio::MoveAssetCommand(sourcePath, destinationPath, relocate, report));
    QCOMPARE(history.count(), 1);
    QVERIFY(!QFileInfo::exists(sourcePath));
    QVERIFY(QFileInfo::exists(destinationPath));
    QCOMPARE(mapping.guid, stableGuid);
    QCOMPARE(mapping.path, QStringLiteral("Moved/Player.fgli"));

    history.undo();
    QVERIFY(QFileInfo::exists(sourcePath));
    QVERIFY(!QFileInfo::exists(destinationPath));
    QCOMPARE(mapping.guid, stableGuid);
    QCOMPARE(mapping.path, QStringLiteral("Assets/Player.fgli"));
    history.redo();
    QVERIFY(QFileInfo::exists(destinationPath));
    QCOMPARE(mapping.path, QStringLiteral("Moved/Player.fgli"));
    history.undo();
    QCOMPARE(relocations.size(), qsizetype{4});

    const QString collisionPath = root.filePath(QStringLiteral("Moved/Collision.fgli"));
    QVERIFY(writeTestFile(collisionPath, QByteArrayLiteral("existing-destination")));
    reportedError.clear();
    QUndoStack failedHistory;
    failedHistory.push(
        new fgl::studio::MoveAssetCommand(sourcePath, collisionPath, relocate, report));
    QCOMPARE(failedHistory.count(), 0);
    QVERIFY(!failedHistory.canUndo());
    QVERIFY(QFileInfo::exists(sourcePath));
    QCOMPARE(mapping.guid, stableGuid);
    QCOMPARE(mapping.path, QStringLiteral("Assets/Player.fgli"));
    QVERIFY(reportedError.contains(QStringLiteral("destination exists")));
}

void StudioSmokeTests::sceneToolsSynchronizeSelectionAndSupportCameraAndUndo() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    QString openError;
    const QString projectPath =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("SceneTools.fglproject"));
    QVERIFY2(createProjectFixture(projectPath, QStringLiteral("Scene Tools"), openError),
             qPrintable(openError));
    fgl::studio::MainWindow window;
    QVERIFY2(window.openProjectPath(projectPath, openError), qPrintable(openError));
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();

    auto* sceneView = window.findChild<fgl::studio::SceneView*>(QStringLiteral("sceneView"));
    auto* hierarchy = window.findChild<QListView*>();
    QVERIFY(sceneView != nullptr);
    QVERIFY(hierarchy != nullptr);
    QVERIFY(hierarchy->currentIndex().isValid());
    const auto entities = window.sceneDocument().scene().entities();
    QVERIFY(!entities.empty());
    const auto id = entities.front()->id();
    QCOMPARE(sceneView->selectedEntityGuid(), fgl::studio::SceneDocument::guidString(id));

    auto snapshot = window.sceneDocument().snapshot(id);
    QVERIFY(snapshot.has_value());
    snapshot->position = {4.0F, 2.0F, 0.0F};
    QString errorMessage;
    QVERIFY(window.sceneDocument().applySnapshot(*snapshot, errorMessage));
    auto* frame = window.findChild<QAction*>(QStringLiteral("frameSelectedAction"));
    auto* zoom = window.findChild<QAction*>(QStringLiteral("sceneZoomInAction"));
    QVERIFY(frame != nullptr);
    QVERIFY(zoom != nullptr);
    frame->trigger();
    QVERIFY(sceneView->cameraOffset() != QPointF());
    const float oldZoom = sceneView->zoomFactor();
    zoom->trigger();
    QVERIFY(sceneView->zoomFactor() > oldZoom);

    const QPoint center = sceneView->rect().center();
    const QPointF beforePan = sceneView->cameraOffset();
    QTest::mousePress(sceneView, Qt::MiddleButton, Qt::NoModifier, center);
    QTest::mouseMove(sceneView, center + QPoint(25, 15));
    QTest::mouseRelease(sceneView, Qt::MiddleButton, Qt::NoModifier, center + QPoint(25, 15));
    QVERIFY(sceneView->cameraOffset() != beforePan);

    frame->trigger();
    auto* rotate = window.findChild<QAction*>(QStringLiteral("rotateToolAction"));
    auto* scale = window.findChild<QAction*>(QStringLiteral("scaleToolAction"));
    auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
    QVERIFY(rotate != nullptr);
    QVERIFY(scale != nullptr);
    QVERIFY(undo != nullptr);
    rotate->trigger();
    QCOMPARE(sceneView->tool(), fgl::studio::SceneView::Tool::Rotate);
    const auto rotationBefore = window.sceneDocument().snapshot(id)->rotation;
    QTest::mousePress(sceneView, Qt::LeftButton, Qt::NoModifier, center);
    QTest::mouseMove(sceneView, center + QPoint(40, 0));
    QTest::mouseRelease(sceneView, Qt::LeftButton, Qt::NoModifier, center + QPoint(40, 0));
    const auto rotationAfter = window.sceneDocument().snapshot(id)->rotation;
    QVERIFY(rotationAfter != rotationBefore);
    undo->trigger();
    QVERIFY(window.sceneDocument().snapshot(id)->rotation == rotationBefore);

    scale->trigger();
    QCOMPARE(sceneView->tool(), fgl::studio::SceneView::Tool::Scale);
    const auto scaleBefore = window.sceneDocument().snapshot(id)->scale;
    QTest::mousePress(sceneView, Qt::LeftButton, Qt::NoModifier, center);
    QTest::mouseMove(sceneView, center + QPoint(30, 0));
    QTest::mouseRelease(sceneView, Qt::LeftButton, Qt::NoModifier, center + QPoint(30, 0));
    QVERIFY(window.sceneDocument().snapshot(id)->scale != scaleBefore);
    undo->trigger();
    QVERIFY(window.sceneDocument().snapshot(id)->scale == scaleBefore);
}

void StudioSmokeTests::hierarchyMultiSelectionGroupsDuplicateDeleteReparentAndBoxSelection() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    QString errorMessage;
    const QString projectPath =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("MultiSelect.fglproject"));
    QVERIFY2(createProjectFixture(projectPath, QStringLiteral("Multi Select"), errorMessage),
             qPrintable(errorMessage));

    fgl::studio::MainWindow window;
    QVERIFY2(window.openProjectPath(projectPath, errorMessage), qPrintable(errorMessage));
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();

    auto* hierarchy = window.findChild<QListView*>();
    auto* sceneView = window.findChild<fgl::studio::SceneView*>(QStringLiteral("sceneView"));
    auto* add = window.findChild<QAction*>(QStringLiteral("addEntityAction"));
    auto* duplicate = window.findChild<QAction*>(QStringLiteral("duplicateEntityAction"));
    auto* remove = window.findChild<QAction*>(QStringLiteral("deleteEntityAction"));
    auto* clearParent = window.findChild<QAction*>(QStringLiteral("clearEntityParentAction"));
    auto* setParent = window.findChild<QAction*>(QStringLiteral("setEntityParentAction"));
    auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
    auto* selectTool = window.findChild<QAction*>(QStringLiteral("selectToolAction"));
    QVERIFY(hierarchy != nullptr);
    QVERIFY(sceneView != nullptr);
    QVERIFY(add != nullptr);
    QVERIFY(duplicate != nullptr);
    QVERIFY(remove != nullptr);
    QVERIFY(clearParent != nullptr);
    QVERIFY(setParent != nullptr);
    QVERIFY(undo != nullptr);
    QVERIFY(selectTool != nullptr);
    QCOMPARE(hierarchy->selectionMode(), QAbstractItemView::ExtendedSelection);

    const std::size_t initialEntityCount = window.sceneDocument().scene().entities().size();
    add->trigger();
    add->trigger();
    QCOMPARE(window.sceneDocument().scene().entities().size(), initialEntityCount + 2U);
    auto entities = window.sceneDocument().scene().entities();
    const auto parentId = entities.at(0)->id();
    const auto childId = entities.at(1)->id();
    const auto thirdId = entities.at(2)->id();
    const auto rowForId = [hierarchy](const fabgl::EntityGuid id) {
        const QString wanted = QString::fromStdString(id.toString());
        for (int row = 0; row < hierarchy->model()->rowCount(); ++row) {
            const auto index = hierarchy->model()->index(row, 0);
            if (index.data(Qt::UserRole + 1).toString() == wanted)
                return row;
        }
        return -1;
    };
    const int parentRow = rowForId(parentId);
    const int childRow = rowForId(childId);
    QVERIFY(parentRow >= 0);
    QVERIFY(childRow >= 0);
    auto setPosition = [&window, &errorMessage](const fabgl::EntityGuid id,
                                                const fabgl::Vec3 position) {
        auto snapshot = window.sceneDocument().snapshot(id);
        QVERIFY(snapshot.has_value());
        snapshot->position = position;
        QVERIFY2(window.sceneDocument().applySnapshot(*snapshot, errorMessage),
                 qPrintable(errorMessage));
    };
    setPosition(parentId, {-1.0F, 0.0F, 0.0F});
    setPosition(childId, {1.0F, 0.0F, 0.0F});
    setPosition(thirdId, {0.0F, 1.0F, 0.0F});

    const auto selectRows = [hierarchy](const std::initializer_list<int> rows) {
        hierarchy->setCurrentIndex(QModelIndex{});
        hierarchy->selectionModel()->clearSelection();
        hierarchy->selectionModel()->clearCurrentIndex();
        QModelIndex primary;
        for (const int row : rows) {
            const auto index = hierarchy->model()->index(row, 0);
            hierarchy->selectionModel()->select(index, QItemSelectionModel::Select |
                                                           QItemSelectionModel::Rows);
            primary = index;
        }
        hierarchy->selectionModel()->setCurrentIndex(primary, QItemSelectionModel::NoUpdate);
        QCoreApplication::processEvents();
    };

    selectRows({parentRow, childRow});
    QCOMPARE(sceneView->selectedEntityGuids().size(), qsizetype{2});
    QVERIFY(duplicate->isEnabled());
    duplicate->trigger();
    QCOMPARE(window.sceneDocument().scene().entities().size(), initialEntityCount + 4U);
    undo->trigger();
    QCOMPARE(window.sceneDocument().scene().entities().size(), initialEntityCount + 2U);

    selectRows({parentRow, childRow});
    remove->trigger();
    QCOMPARE(window.sceneDocument().scene().entities().size(), initialEntityCount);
    undo->trigger();
    QCOMPARE(window.sceneDocument().scene().entities().size(), initialEntityCount + 2U);

    entities = window.sceneDocument().scene().entities();
    auto child = window.sceneDocument().snapshot(childId);
    QVERIFY(child.has_value());
    child->parent = parentId;
    QVERIFY2(window.sceneDocument().applySnapshot(*child, errorMessage), qPrintable(errorMessage));
    const int currentChildRow = rowForId(childId);
    QVERIFY(currentChildRow >= 0);
    selectRows({currentChildRow});
    QVERIFY(clearParent->isEnabled());
    clearParent->trigger();
    QVERIFY(!window.sceneDocument().snapshot(childId)->parent.has_value());
    undo->trigger();
    QCOMPARE(window.sceneDocument().snapshot(childId)->parent, std::optional(parentId));
    clearParent->trigger();
    QVERIFY(!window.sceneDocument().snapshot(childId)->parent.has_value());

    QVERIFY(setParent->isEnabled());

    auto rootChild = window.sceneDocument().snapshot(childId);
    QVERIFY(rootChild.has_value());
    rootChild->parent.reset();
    QVERIFY2(window.sceneDocument().applySnapshot(*rootChild, errorMessage),
             qPrintable(errorMessage));
    selectTool->trigger();
    const QPoint center = sceneView->rect().center();
    QSignalSpy boxSelection(sceneView, &fgl::studio::SceneView::entitiesSelected);
    QTest::mousePress(sceneView, Qt::LeftButton, Qt::NoModifier, center + QPoint(-80, -80));
    QTest::mouseMove(sceneView, center + QPoint(80, 80));
    QTest::mouseRelease(sceneView, Qt::LeftButton, Qt::NoModifier, center + QPoint(80, 80));
    QVERIFY(!boxSelection.isEmpty());
    QVERIFY(sceneView->selectedEntityGuids().size() >= qsizetype{3});
}

void StudioSmokeTests::serialConsoleFiltersClearsAndEmitsExactLineEndingBytes() {
    fgl::studio::SerialConsoleWidget console;
    console.show();
    auto* input = console.findChild<QLineEdit*>(QStringLiteral("serialInputEdit"));
    auto* endings = console.findChild<QComboBox*>(QStringLiteral("serialLineEndingCombo"));
    auto* send = console.findChild<QPushButton*>(QStringLiteral("serialSendButton"));
    auto* filter = console.findChild<QLineEdit*>(QStringLiteral("serialFilterEdit"));
    auto* clear = console.findChild<QPushButton*>(QStringLiteral("serialClearButton"));
    QVERIFY(input != nullptr);
    QVERIFY(endings != nullptr);
    QVERIFY(send != nullptr);
    QVERIFY(filter != nullptr);
    QVERIFY(clear != nullptr);
    QVERIFY(!send->isEnabled());

    QSignalSpy sent(&console, &fgl::studio::SerialConsoleWidget::sendRequested);
    console.setConnected(true);
    const int crlf = endings->findText(QStringLiteral("CRLF"));
    QVERIFY(crlf >= 0);
    endings->setCurrentIndex(crlf);
    input->setText(QStringLiteral("status"));
    QTest::mouseClick(send, Qt::LeftButton);
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.constFirst().constFirst().toByteArray(), QByteArrayLiteral("status\r\n"));

    console.appendChunk(QStringLiteral("[INFO] engine ready\n[ERROR] device failed\n"));
    QVERIFY(console.visibleText().contains(QStringLiteral("engine ready")));
    QVERIFY(console.visibleText().contains(QStringLiteral("device failed")));
    filter->setText(QStringLiteral("ERROR"));
    QVERIFY(!console.visibleText().contains(QStringLiteral("engine ready")));
    QVERIFY(console.visibleText().contains(QStringLiteral("device failed")));
    QTest::mouseClick(clear, Qt::LeftButton);
    QVERIFY(console.visibleText().isEmpty());
}

void StudioSmokeTests::advancedPanelsUseEngineModelsAndLiveSceneMetrics() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    QString openError;
    const QString projectPath =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("AdvancedPanels.fglproject"));
    QVERIFY2(createProjectFixture(projectPath, QStringLiteral("Advanced Panels"), openError),
             qPrintable(openError));
    fgl::studio::MainWindow window;
    QVERIFY2(window.openProjectPath(projectPath, openError), qPrintable(openError));
    window.show();
    QCoreApplication::processEvents();

    auto* packageTrust = window.findChild<QLabel*>(QStringLiteral("packageTrustStatus"));
    QVERIFY(packageTrust != nullptr);
    QVERIFY(packageTrust->text().contains(QStringLiteral("untrusted"), Qt::CaseInsensitive));
    QString trustError;
    QVERIFY2(window.setCurrentProjectTrusted(true, trustError), qPrintable(trustError));
    QVERIFY(packageTrust->text().contains(QStringLiteral("Trusted"), Qt::CaseSensitive));

    auto* visual = window.findChild<fgl::studio::VisualScriptEditorWidget*>(
        QStringLiteral("visualScriptEditor"));
    QVERIFY(visual != nullptr);
    QCOMPARE(visual->nodeCount(), qsizetype{3});
    QCOMPARE(visual->edgeCount(), qsizetype{2});
    QCOMPARE(visual->validationIssueCount(), qsizetype{0});
    QVERIFY(!visual->hasValidationErrors());
    const auto* compileStatus = visual->findChild<QLabel*>(QStringLiteral("visualCompileStatus"));
    QVERIFY(compileStatus != nullptr);
    QVERIFY(compileStatus->text().contains(QStringLiteral("Compiled")));

    auto* sourceNode = visual->findChild<QComboBox*>(QStringLiteral("visualSourceNodeCombo"));
    auto* targetNode = visual->findChild<QComboBox*>(QStringLiteral("visualTargetNodeCombo"));
    auto* targetPin = visual->findChild<QComboBox*>(QStringLiteral("visualTargetPinCombo"));
    auto* addConnection =
        visual->findChild<QPushButton*>(QStringLiteral("visualAddConnectionButton"));
    auto* removeConnection =
        visual->findChild<QPushButton*>(QStringLiteral("visualRemoveConnectionButton"));
    auto* connections = visual->findChild<QTableWidget*>(QStringLiteral("visualConnectionTable"));
    QVERIFY(sourceNode != nullptr);
    QVERIFY(targetNode != nullptr);
    QVERIFY(targetPin != nullptr);
    QVERIFY(addConnection != nullptr);
    QVERIFY(removeConnection != nullptr);
    QVERIFY(connections != nullptr);
    sourceNode->setCurrentIndex(1); // Number output.
    targetNode->setCurrentIndex(2); // Return node; flow is its first input.
    targetPin->setCurrentIndex(0);
    QTest::mouseClick(addConnection, Qt::LeftButton);
    QCOMPARE(visual->edgeCount(), qsizetype{3});
    QVERIFY(visual->hasValidationErrors());
    connections->selectRow(connections->rowCount() - 1);
    QTest::mouseClick(removeConnection, Qt::LeftButton);
    QCOMPARE(visual->edgeCount(), qsizetype{2});
    QVERIFY(!visual->hasValidationErrors());
    auto* addNode = visual->findChild<QPushButton*>(QStringLiteral("visualAddNodeButton"));
    QVERIFY(addNode != nullptr);
    QTest::mouseClick(addNode, Qt::LeftButton);
    QCOMPARE(visual->nodeCount(), qsizetype{4});
    QVERIFY(visual->validationIssueCount() >= 1); // New disconnected node is a real warning.

    auto* animator =
        window.findChild<fgl::studio::AnimatorEditorWidget*>(QStringLiteral("animatorEditor"));
    QVERIFY(animator != nullptr);
    QCOMPARE(animator->stateCount(), 2);
    QCOMPARE(animator->parameterCount(), 1);
    QCOMPARE(animator->transitionCount(), 1);
    QVERIFY(animator->validationText().startsWith(QStringLiteral("Valid controller")));
    auto* states = animator->findChild<QTableWidget*>(QStringLiteral("animatorStatesTable"));
    QVERIFY(states != nullptr);
    states->item(0, 1)->setText(QStringLiteral("0"));
    QVERIFY(animator->validationText().startsWith(QStringLiteral("Invalid controller")));
    states->item(0, 1)->setText(QStringLiteral("1"));
    QVERIFY(animator->validationText().startsWith(QStringLiteral("Valid controller")));
    auto* addState = animator->findChild<QPushButton*>(QStringLiteral("animatorAddStateButton"));
    QVERIFY(addState != nullptr);
    QTest::mouseClick(addState, Qt::LeftButton);
    QCOMPARE(animator->stateCount(), 3);

    auto& document = window.sceneDocument();
    const auto entities = document.scene().entities();
    QVERIFY(!entities.empty());
    QString errorMessage;
    QVERIFY2(document.addBuiltinComponent(entities.front()->id(), QStringLiteral("Health"),
                                          errorMessage),
             qPrintable(errorMessage));
    auto* memory =
        window.findChild<fgl::studio::MemoryAnalyzerWidget*>(QStringLiteral("memoryAnalyzer"));
    QVERIFY(memory != nullptr);
    memory->refresh();
    QCOMPARE(memory->entityCount(), entities.size());
    QVERIFY(memory->componentCount() >= std::size_t{1});
    QVERIFY(memory->estimatedRuntimeBytes() > 0);
    auto* summary = memory->findChild<QTableWidget*>(QStringLiteral("memorySummaryTable"));
    QVERIFY(summary != nullptr);
    QCOMPARE(summary->rowCount(), 10);
    QVERIFY(memory->findChild<QComboBox*>(QStringLiteral("pcPerformanceProfileCombo")) != nullptr);
    QVERIFY(memory->findChild<QComboBox*>(QStringLiteral("esp32PerformanceProfileCombo")) !=
            nullptr);
}

void StudioSmokeTests::menusLayoutsThemesAndRunControlsAreFunctional() {
    const QDir repositoryRoot(QString::fromUtf8(FGL_TEST_REPOSITORY_ROOT));
    fgl::studio::MainWindow window;
    QString openError;
    QVERIFY2(
        window.openProjectPath(
            repositoryRoot.filePath(QStringLiteral("examples/empty/Empty.fglproject")), openError),
        qPrintable(openError));
    window.show();
    QCoreApplication::processEvents();

    constexpr std::array<const char*, 7> LayoutActions = {
        "defaultLayoutAction",   "layout2DAction",        "layout3DAction",
        "scriptingLayoutAction", "animationLayoutAction", "profilingLayoutAction",
        "debugLayoutAction"};
    for (const auto* actionName : LayoutActions) {
        QVERIFY2(window.findChild<QAction*>(QString::fromLatin1(actionName)) != nullptr,
                 actionName);
    }
    auto* visualDock = window.findChild<QDockWidget*>(QStringLiteral("visualScriptDock"));
    auto* gameDock = window.findChild<QDockWidget*>(QStringLiteral("gameDock"));
    QVERIFY(visualDock != nullptr);
    QVERIFY(gameDock != nullptr);
    window.findChild<QAction*>(QStringLiteral("scriptingLayoutAction"))->trigger();
    QCoreApplication::processEvents();
    QVERIFY(!visualDock->isHidden());
    QVERIFY(gameDock->isHidden());

    QString errorMessage;
    QVERIFY2(window.saveNamedLayout(QStringLiteral("My Scripting Layout"), errorMessage),
             qPrintable(errorMessage));
    QVERIFY(window.namedLayouts().contains(QStringLiteral("My Scripting Layout")));
    window.findChild<QAction*>(QStringLiteral("profilingLayoutAction"))->trigger();
    QVERIFY(visualDock->isHidden());
    QVERIFY2(window.loadNamedLayout(QStringLiteral("My Scripting Layout"), errorMessage),
             qPrintable(errorMessage));
    QVERIFY(!visualDock->isHidden());
    QVERIFY(gameDock->isHidden());
    QVERIFY2(window.deleteNamedLayout(QStringLiteral("My Scripting Layout"), errorMessage),
             qPrintable(errorMessage));
    QVERIFY(!window.namedLayouts().contains(QStringLiteral("My Scripting Layout")));

    auto* dark = window.findChild<QAction*>(QStringLiteral("darkThemeAction"));
    auto* light = window.findChild<QAction*>(QStringLiteral("lightThemeAction"));
    QVERIFY(dark != nullptr);
    QVERIFY(light != nullptr);
    light->trigger();
    QVERIFY(light->isChecked());
    dark->trigger();
    QVERIFY(dark->isChecked());

    auto* play = window.findChild<QAction*>(QStringLiteral("playAction"));
    auto* pause = window.findChild<QAction*>(QStringLiteral("pauseAction"));
    auto* step = window.findChild<QAction*>(QStringLiteral("stepAction"));
    auto* stop = window.findChild<QAction*>(QStringLiteral("stopAction"));
    QVERIFY(play != nullptr);
    QVERIFY(pause != nullptr);
    QVERIFY(step != nullptr);
    QVERIFY(stop != nullptr);
    QVERIFY2(window.setCurrentProjectTrusted(true, errorMessage), qPrintable(errorMessage));
    play->trigger();
    QVERIFY(!play->isEnabled());
    QVERIFY(pause->isEnabled());
    QVERIFY(stop->isEnabled());
    pause->trigger();
    QVERIFY(play->isEnabled());
    QVERIFY(step->isEnabled());
    step->trigger();
    QVERIFY(step->isEnabled());
    stop->trigger();
    QVERIFY(play->isEnabled());
    QVERIFY(!pause->isEnabled());
    QVERIFY(!step->isEnabled());
    QVERIFY(!stop->isEnabled());
}

void StudioSmokeTests::gameViewControlsChangeLivePreviewSettings() {
    fgl::studio::MainWindow window;
    window.show();
    QCoreApplication::processEvents();
    auto* gameView = window.findChild<fgl::studio::GameView*>(QStringLiteral("gameView"));
    auto* resolution = window.findChild<QComboBox*>(QStringLiteral("gameResolutionCombo"));
    auto* aspect = window.findChild<QComboBox*>(QStringLiteral("gameAspectCombo"));
    auto* palette = window.findChild<QComboBox*>(QStringLiteral("gamePaletteCombo"));
    auto* fps = window.findChild<QComboBox*>(QStringLiteral("gameFpsCombo"));
    auto* speed = window.findChild<QComboBox*>(QStringLiteral("gameSimulationSpeedCombo"));
    auto* integerScaling = window.findChild<QCheckBox*>(QStringLiteral("gameIntegerScalingCheck"));
    auto* pixelPerfect = window.findChild<QCheckBox*>(QStringLiteral("gamePixelPerfectCheck"));
    auto* showFps = window.findChild<QCheckBox*>(QStringLiteral("gameShowFpsCheck"));
    auto* esp32Simulation =
        window.findChild<QCheckBox*>(QStringLiteral("gameEsp32SimulationCheck"));
    auto* fullscreen = window.findChild<QPushButton*>(QStringLiteral("gameFullscreenButton"));
    QVERIFY(gameView != nullptr);
    QVERIFY(resolution != nullptr);
    QVERIFY(aspect != nullptr);
    QVERIFY(palette != nullptr);
    QVERIFY(fps != nullptr);
    QVERIFY(speed != nullptr);
    QVERIFY(integerScaling != nullptr);
    QVERIFY(pixelPerfect != nullptr);
    QVERIFY(showFps != nullptr);
    QVERIFY(esp32Simulation != nullptr);
    QVERIFY(fullscreen != nullptr);

    resolution->setCurrentIndex(resolution->findData(QSize(320, 200)));
    QCOMPARE(gameView->targetResolution(), QSize(320, 200));
    aspect->setCurrentIndex(
        aspect->findData(static_cast<int>(fgl::studio::GameView::AspectMode::FourThree)));
    QCOMPARE(gameView->aspectMode(), fgl::studio::GameView::AspectMode::FourThree);
    palette->setCurrentIndex(
        palette->findData(static_cast<int>(fgl::studio::GameView::PaletteMode::Esp32Rgb222)));
    QCOMPARE(gameView->paletteMode(), fgl::studio::GameView::PaletteMode::Esp32Rgb222);
    fps->setCurrentIndex(fps->findData(30));
    QCOMPARE(gameView->targetFps(), 30);
    speed->setCurrentIndex(speed->findData(2.0));
    QCOMPARE(gameView->simulationSpeed(), 2.0);
    integerScaling->setChecked(false);
    pixelPerfect->setChecked(false);
    showFps->setChecked(false);
    esp32Simulation->setChecked(true);
    QVERIFY(!gameView->integerScaling());
    QVERIFY(!gameView->pixelPerfect());
    QVERIFY(!gameView->fpsOverlayVisible());
    QVERIFY(gameView->esp32SimulationMode());

    QSignalSpy inputSpy(gameView, &fgl::studio::GameView::runtimeControlChanged);
    gameView->setRuntimeInputEnabled(true);
    QTest::keyPress(gameView, Qt::Key_D);
    QTest::keyRelease(gameView, Qt::Key_D);
    QCOMPARE(inputSpy.size(), 2);
    QCOMPARE(inputSpy.at(0).at(0).toString(), QStringLiteral("Key.D"));
    QCOMPARE(inputSpy.at(0).at(1).toFloat(), 1.0F);
    QCOMPARE(inputSpy.at(1).at(0).toString(), QStringLiteral("Key.D"));
    QCOMPARE(inputSpy.at(1).at(1).toFloat(), 0.0F);
    gameView->setRuntimeInputEnabled(false);

    auto& document = window.sceneDocument();
    const auto entities = document.scene().entities();
    QVERIFY(!entities.empty());
    QString errorMessage;
    QVERIFY2(document.addBuiltinComponent(entities.front()->id(), QStringLiteral("SpriteRenderer"),
                                          errorMessage),
             qPrintable(errorMessage));
    const auto presentation = gameView->renderScene(document.scene(), 0.25);
    QCOMPARE(presentation.drawCalls, std::uint32_t{1});
    QCOMPARE(presentation.spritesSubmitted, std::uint32_t{1});
    QCOMPARE(presentation.triangles, std::uint32_t{0});
    QCOMPARE(presentation.rays, std::uint32_t{0});
    QCOMPARE(presentation.particles, std::uint32_t{0});
}

void StudioSmokeTests::studioPlayUsesProjectRuntimeForAnimatorAndInput() {
    const QDir repositoryRoot(QString::fromUtf8(FGL_TEST_REPOSITORY_ROOT));
    fgl::studio::MainWindow window;
    QString errorMessage;
    QVERIFY2(
        window.openProjectPath(repositoryRoot.filePath(QStringLiteral(
                                   "examples/animation_showcase/AnimationShowcase.fglproject")),
                               errorMessage),
        qPrintable(errorMessage));
    QVERIFY2(window.setCurrentProjectTrusted(true, errorMessage), qPrintable(errorMessage));
    window.show();
    QCoreApplication::processEvents();

    auto* play = window.findChild<QAction*>(QStringLiteral("playAction"));
    auto* stop = window.findChild<QAction*>(QStringLiteral("stopAction"));
    auto* gameView = window.findChild<fgl::studio::GameView*>(QStringLiteral("gameView"));
    auto* console = window.findChild<QPlainTextEdit*>(QStringLiteral("consoleOutput"));
    auto* profiler = window.findChild<QTableWidget*>(QStringLiteral("profilerTable"));
    auto* timeline = window.findChild<QTableWidget*>(QStringLiteral("profilerTimelineTable"));
    QVERIFY(play != nullptr);
    QVERIFY(stop != nullptr);
    QVERIFY(gameView != nullptr);
    QVERIFY(console != nullptr);
    QVERIFY(profiler != nullptr);
    QVERIFY(timeline != nullptr);

    play->trigger();
    QCoreApplication::processEvents();
    QVERIFY(stop->isEnabled());
    QVERIFY(gameView->runtimeInputEnabled());
    QVERIFY2(console->toPlainText().contains(QStringLiteral("animators=1")),
             qPrintable(console->toPlainText()));

    QTest::keyPress(gameView, Qt::Key_Space);
    QTest::keyRelease(gameView, Qt::Key_Space);
    QTest::qWait(25);
    QVERIFY(stop->isEnabled());
    QVERIFY(profiler->rowCount() >= 24);
    bool profilerHasAi = false;
    for (int row = 0; row < profiler->rowCount(); ++row) {
        profilerHasAi =
            profilerHasAi || (profiler->item(row, 0) != nullptr &&
                              profiler->item(row, 0)->text().contains(QStringLiteral("AI")));
    }
    QVERIFY(profilerHasAi);
    bool foundAiMetric = false;
    for (int row = 0; row < timeline->rowCount(); ++row) {
        foundAiMetric =
            foundAiMetric || (timeline->item(row, 1) != nullptr &&
                              timeline->item(row, 1)->text() == QStringLiteral("pc.ai"));
    }
    QVERIFY(foundAiMetric);
    stop->trigger();
    QVERIFY(!gameView->runtimeInputEnabled());
}

void StudioSmokeTests::codeTextEditMatchesBracketsAndAutoIndents() {
    fgl::studio::CodeTextEdit editor;
    editor.show();
    editor.setPlainText(QStringLiteral("int main() {}"));
    QTextCursor cursor = editor.textCursor();
    const int opening = static_cast<int>(editor.toPlainText().indexOf(QLatin1Char('{')));
    cursor.setPosition(opening + 1);
    editor.setTextCursor(cursor);
    const auto match = editor.matchingBracketPositions();
    QVERIFY(match.has_value());
    QCOMPARE(match->first, opening);
    QCOMPARE(match->second, static_cast<int>(editor.toPlainText().indexOf(QLatin1Char('}'))));

    editor.setPlainText(QStringLiteral("if (ready) {}"));
    cursor = editor.textCursor();
    cursor.setPosition(static_cast<int>(editor.toPlainText().indexOf(QLatin1Char('{'))) + 1);
    editor.setTextCursor(cursor);
    editor.setFocus();
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(editor.toPlainText(), QStringLiteral("if (ready) {\n    \n}"));

    editor.setPlainText(QStringLiteral("if (ready) {\n    "));
    cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::End);
    editor.setTextCursor(cursor);
    QTest::keyClicks(&editor, QStringLiteral("}"));
    QCOMPARE(editor.toPlainText(), QStringLiteral("if (ready) {\n}"));
}

void StudioSmokeTests::codeEditorIndexesProjectAndDetectsExternalChanges() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString sourcePath =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("src/player.cpp"));
    const QString headerPath =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("include/player.h"));
    QVERIFY(writeTestFile(sourcePath, QByteArrayLiteral("#include \"player.h\"\n"
                                                        "int jump(int amount) {\n"
                                                        "    return amount + magic_token;\n"
                                                        "}\n")));
    QVERIFY(writeTestFile(headerPath,
                          QByteArrayLiteral("class Player {\npublic:\n    void update();\n};\n")));
    fgl::studio::CodeEditorWidget editor;
    editor.setProjectRoot(temporaryDirectory.path());
    QCOMPARE(editor.projectFileCount(), qsizetype{2});
    QVERIFY(editor.findChild<QTreeWidget*>(QStringLiteral("codeProjectFileTree")) != nullptr);
    QVERIFY(editor.symbolNames().contains(QStringLiteral("Player")));
    QVERIFY(editor.symbolNames().contains(QStringLiteral("jump")));
    QCOMPARE(editor.findInFiles(QStringLiteral("magic_token")), 1);
    auto* results = editor.findChild<QTableWidget*>(QStringLiteral("findInFilesResults"));
    QVERIFY(results != nullptr);
    QCOMPARE(results->rowCount(), 1);

    editor.openFile(sourcePath, 2);
    fgl::studio::CodeTextEdit* sourceEditor = nullptr;
    const auto openEditors = editor.findChildren<fgl::studio::CodeTextEdit*>();
    for (auto* candidate : openEditors) {
        if (QFileInfo(candidate->property("filePath").toString()).absoluteFilePath() ==
            QFileInfo(sourcePath).absoluteFilePath()) {
            sourceEditor = candidate;
            break;
        }
    }
    QVERIFY(sourceEditor != nullptr);
    sourceEditor->appendPlainText(QStringLiteral("// local edit"));
    QVERIFY(sourceEditor->document()->isModified());
    QSignalSpy externalChangeSpy(&editor, &fgl::studio::CodeEditorWidget::externalFileChanged);
    QVERIFY(
        writeTestFile(sourcePath, QByteArrayLiteral("int externally_changed() { return 7; }\n")));
    editor.scanForExternalChanges();
    QVERIFY(editor.hasExternalChange(sourcePath));
    QCOMPARE(externalChangeSpy.size(), 1);
    QCOMPARE(externalChangeSpy.constFirst().at(1).toBool(), true);
    QString errorMessage;
    QVERIFY2(editor.reloadFileFromDisk(sourcePath, errorMessage), qPrintable(errorMessage));
    QVERIFY(!editor.hasExternalChange(sourcePath));
    QVERIFY(sourceEditor->toPlainText().contains(QStringLiteral("externally_changed")));

    QVERIFY(writeTestFile(sourcePath,
                          QByteArrayLiteral("int automatically_reloaded() { return 9; }\n")));
    editor.scanForExternalChanges();
    QVERIFY(sourceEditor->toPlainText().contains(QStringLiteral("automatically_reloaded")));
    QVERIFY(!editor.hasExternalChange(sourcePath));
    QCOMPARE(externalChangeSpy.size(), 2);
    QCOMPARE(externalChangeSpy.constLast().at(1).toBool(), false);
}

void StudioSmokeTests::visualScriptGraphFilesRoundTripThroughUiModel() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    fgl::studio::VisualScriptEditorWidget editor;
    QCOMPARE(editor.nodeCount(), qsizetype{3});
    QCOMPARE(editor.edgeCount(), qsizetype{2});
    QVERIFY(editor.findChild<QPushButton*>(QStringLiteral("visualNewGraphButton")) != nullptr);
    QVERIFY(editor.findChild<QPushButton*>(QStringLiteral("visualOpenGraphButton")) != nullptr);
    QVERIFY(editor.findChild<QPushButton*>(QStringLiteral("visualSaveGraphButton")) != nullptr);
    const QString graphPath =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("logic/Player.fglvisual"));
    QString errorMessage;
    QVERIFY2(editor.saveGraphFile(graphPath, errorMessage), qPrintable(errorMessage));
    QVERIFY(!editor.graphModified());
    QCOMPARE(editor.graphFilePath(), QFileInfo(graphPath).absoluteFilePath());
    QFile graphFile(graphPath);
    QVERIFY(graphFile.open(QIODevice::ReadOnly));
    QVERIFY(graphFile.readLine().trimmed() == QByteArrayLiteral("fglvisual 1"));

    auto* addNode = editor.findChild<QPushButton*>(QStringLiteral("visualAddNodeButton"));
    QVERIFY(addNode != nullptr);
    QTest::mouseClick(addNode, Qt::LeftButton);
    QCOMPARE(editor.nodeCount(), qsizetype{4});
    QVERIFY(editor.graphModified());
    QVERIFY2(editor.openGraphFile(graphPath, errorMessage), qPrintable(errorMessage));
    QCOMPARE(editor.nodeCount(), qsizetype{3});
    QCOMPARE(editor.edgeCount(), qsizetype{2});
    QVERIFY(!editor.graphModified());

    const QString corruptPath =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("logic/Corrupt.fglvisual"));
    QVERIFY(writeTestFile(corruptPath, QByteArrayLiteral("fglvisual 999\n")));
    QVERIFY(!editor.openGraphFile(corruptPath, errorMessage));
    QCOMPARE(editor.nodeCount(), qsizetype{3});
}

void StudioSmokeTests::visualScriptCanvasSearchHistoryAndLayoutPersist() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    fgl::studio::VisualScriptEditorWidget editor;
    editor.resize(1200, 800);
    editor.show();
    QCoreApplication::processEvents();

    auto* search = editor.findChild<QLineEdit*>(QStringLiteral("visualNodeSearchEdit"));
    auto* tree = editor.findChild<QTreeWidget*>(QStringLiteral("visualNodeTree"));
    auto* canvas = editor.findChild<QGraphicsView*>(QStringLiteral("visualGraphCanvas"));
    auto* undo = editor.findChild<QPushButton*>(QStringLiteral("visualUndoButton"));
    auto* redo = editor.findChild<QPushButton*>(QStringLiteral("visualRedoButton"));
    auto* comment = editor.findChild<QPushButton*>(QStringLiteral("visualAddCommentButton"));
    auto* copy = editor.findChild<QPushButton*>(QStringLiteral("visualCanvasCopyButton"));
    auto* paste = editor.findChild<QPushButton*>(QStringLiteral("visualCanvasPasteButton"));
    QVERIFY(search != nullptr);
    QVERIFY(tree != nullptr);
    QVERIFY(canvas != nullptr);
    QVERIFY(canvas->scene() != nullptr);
    QVERIFY(undo != nullptr);
    QVERIFY(redo != nullptr);
    QVERIFY(comment != nullptr);
    QVERIFY(copy != nullptr);
    QVERIFY(paste != nullptr);
    QVERIFY(!editor.canUndoGraphEdit());
    QString debugError;
    QVERIFY2(editor.executeDebugPreview(debugError), qPrintable(debugError));
    QCOMPARE(editor.debugTraceNodeCount(), qsizetype{3});
    QVERIFY(editor.activeDebugNodeId() != fabgl::VisualNodeId{0});
    bool activeHighlight = false;
    for (const auto* item : canvas->scene()->items())
        activeHighlight = activeHighlight || item->data(4).toBool();
    QVERIFY(activeHighlight);
    const auto* debugStatus = editor.findChild<QLabel*>(QStringLiteral("visualDebugStatus"));
    QVERIFY(debugStatus != nullptr);
    QVERIFY(debugStatus->text().startsWith(QStringLiteral("Local VM")));

    search->setText(QStringLiteral("Number"));
    QCOMPARE(tree->topLevelItemCount(), 1);
    QTest::keyClick(search, Qt::Key_Return);
    QCOMPARE(editor.nodeCount(), qsizetype{4});
    QVERIFY(editor.canUndoGraphEdit());
    QTest::mouseClick(undo, Qt::LeftButton);
    QCOMPARE(editor.nodeCount(), qsizetype{3});
    QVERIFY(editor.canRedoGraphEdit());
    QTest::mouseClick(redo, Qt::LeftButton);
    QCOMPARE(editor.nodeCount(), qsizetype{4});

    search->clear();
    QCOMPARE(tree->topLevelItemCount(), 4);
    QList<QGraphicsItem*> canvasNodes;
    for (auto* item : canvas->scene()->items()) {
        if (item->data(0).toInt() == 1)
            canvasNodes.push_back(item);
    }
    QVERIFY(canvasNodes.size() >= 2);
    canvas->scene()->clearSelection();
    canvasNodes.at(0)->setSelected(true);
    canvasNodes.at(1)->setSelected(true);
    QCoreApplication::processEvents();
    QCOMPARE(editor.selectedCanvasNodeCount(), qsizetype{2});
    QCOMPARE(tree->selectedItems().size(), qsizetype{2});

    tree->clearSelection();
    tree->topLevelItem(0)->setSelected(true);
    tree->topLevelItem(1)->setSelected(true);
    QCoreApplication::processEvents();
    QCOMPARE(editor.selectedCanvasNodeCount(), qsizetype{2});
    QTest::mouseClick(comment, Qt::LeftButton);
    QCOMPARE(editor.commentCount(), qsizetype{1});
    QTest::mouseClick(copy, Qt::LeftButton);
    QTest::mouseClick(paste, Qt::LeftButton);
    QCOMPARE(editor.nodeCount(), qsizetype{6});
    QTest::mouseClick(undo, Qt::LeftButton);
    QCOMPARE(editor.nodeCount(), qsizetype{4});

    QGraphicsItem* movedItem = nullptr;
    for (auto* item : canvas->scene()->items()) {
        if (item->flags().testFlag(QGraphicsItem::ItemIsMovable) && item->zValue() > 0.0) {
            movedItem = item;
            break;
        }
    }
    QVERIFY(movedItem != nullptr);
    const auto movedNodeId = static_cast<fabgl::VisualNodeId>(movedItem->data(1).toUInt());
    const QPointF movedPosition = movedItem->pos() + QPointF(73.0, 41.0);
    movedItem->setPos(movedPosition);
    QVERIFY(editor.graphModified());

    const QString graphPath =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("Graph/Canvas.fglvisual"));
    QString errorMessage;
    QVERIFY2(editor.saveGraphFile(graphPath, errorMessage), qPrintable(errorMessage));
    QFile graphFile(graphPath);
    QVERIFY(graphFile.open(QIODevice::ReadOnly));
    const QByteArray bytes = graphFile.readAll();
    auto decoded = fabgl::deserializeVisualGraph(
        std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    if (!decoded)
        QFAIL(qPrintable(QString::fromStdString(decoded.error().message())));
    QCOMPARE(decoded.value().comments().size(), std::size_t{1});
    const auto* movedNode = decoded.value().findNode(movedNodeId);
    QVERIFY(movedNode != nullptr);
    QCOMPARE(movedNode->layout.x, static_cast<float>(movedPosition.x()));
    QCOMPARE(movedNode->layout.y, static_cast<float>(movedPosition.y()));
}

void StudioSmokeTests::visualScriptTypedHostNodesAuthorReferencesAndValidate() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    fgl::studio::VisualScriptEditorWidget editor;
    editor.show();
    QCoreApplication::processEvents();

    auto* kinds = editor.findChild<QComboBox*>(QStringLiteral("visualNodeKindCombo"));
    auto* add = editor.findChild<QPushButton*>(QStringLiteral("visualAddNodeButton"));
    auto* payload = editor.findChild<QLineEdit*>(QStringLiteral("visualNodePayloadEdit"));
    auto* asset = editor.findChild<QLineEdit*>(QStringLiteral("visualNodeAssetReferenceEdit"));
    auto* entity = editor.findChild<QLineEdit*>(QStringLiteral("visualNodeEntityReferenceEdit"));
    auto* component =
        editor.findChild<QLineEdit*>(QStringLiteral("visualNodeComponentReferenceEdit"));
    auto* callback = editor.findChild<QComboBox*>(QStringLiteral("visualNodeCallbackEdit"));
    auto* validation = editor.findChild<QTableWidget*>(QStringLiteral("visualValidationTable"));
    auto* nodeTree = editor.findChild<QTreeWidget*>(QStringLiteral("visualNodeTree"));
    auto* sourceNode = editor.findChild<QComboBox*>(QStringLiteral("visualSourceNodeCombo"));
    auto* sourcePin = editor.findChild<QComboBox*>(QStringLiteral("visualSourcePinCombo"));
    auto* targetNode = editor.findChild<QComboBox*>(QStringLiteral("visualTargetNodeCombo"));
    auto* targetPin = editor.findChild<QComboBox*>(QStringLiteral("visualTargetPinCombo"));
    auto* addConnection =
        editor.findChild<QPushButton*>(QStringLiteral("visualAddConnectionButton"));
    auto* removeConnection =
        editor.findChild<QPushButton*>(QStringLiteral("visualRemoveConnectionButton"));
    auto* connections = editor.findChild<QTableWidget*>(QStringLiteral("visualConnectionTable"));
    QVERIFY(kinds != nullptr);
    QVERIFY(add != nullptr);
    QVERIFY(payload != nullptr);
    QVERIFY(asset != nullptr);
    QVERIFY(entity != nullptr);
    QVERIFY(component != nullptr);
    QVERIFY(callback != nullptr);
    QVERIFY(validation != nullptr);
    QVERIFY(nodeTree != nullptr);
    QVERIFY(sourceNode != nullptr);
    QVERIFY(sourcePin != nullptr);
    QVERIFY(targetNode != nullptr);
    QVERIFY(targetPin != nullptr);
    QVERIFY(addConnection != nullptr);
    QVERIFY(removeConnection != nullptr);
    QVERIFY(connections != nullptr);

    const auto selectKind = [kinds](const QString& stableName) {
        for (int index = 0; index < kinds->count(); ++index) {
            if (kinds->itemText(index).contains(QStringLiteral("[%1]").arg(stableName))) {
                kinds->setCurrentIndex(index);
                return true;
            }
        }
        return false;
    };
    QVERIFY(selectKind(QStringLiteral("audio.play")));
    QTest::mouseClick(add, Qt::LeftButton);
    QVERIFY(nodeTree->currentItem() != nullptr);
    const auto audioNodeId = nodeTree->currentItem()->data(0, Qt::UserRole).toUInt();
    QVERIFY(payload->isEnabled());
    QVERIFY(asset->isEnabled());
    asset->setText(QStringLiteral("not-a-guid"));
    QVERIFY(QMetaObject::invokeMethod(asset, "editingFinished", Qt::DirectConnection));
    bool reportedGuidError = false;
    for (int row = 0; row < validation->rowCount(); ++row) {
        const auto* message = validation->item(row, 2);
        reportedGuidError = reportedGuidError ||
                            (message != nullptr &&
                             message->text().contains(QStringLiteral("GUID"), Qt::CaseInsensitive));
    }
    QVERIFY(reportedGuidError);

    const QString audioGuid = QStringLiteral("71000000-0000-4000-8000-000000000001");
    payload->setText(QStringLiteral("sfx.loop"));
    asset->setText(audioGuid);
    QVERIFY(QMetaObject::invokeMethod(asset, "editingFinished", Qt::DirectConnection));

    QVERIFY(selectKind(QStringLiteral("function.call")));
    QTest::mouseClick(add, Qt::LeftButton);
    QVERIFY(nodeTree->currentItem() != nullptr);
    const auto functionNodeId = nodeTree->currentItem()->data(0, Qt::UserRole).toUInt();
    QVERIFY(callback->isEnabled());
    QCOMPARE(callback->currentText(), QStringLiteral("function.identity"));
    callback->setEditText(QStringLiteral("math.abs"));
    QVERIFY(
        QMetaObject::invokeMethod(callback->lineEdit(), "editingFinished", Qt::DirectConnection));

    QVERIFY(selectKind(QStringLiteral("component.action")));
    QTest::mouseClick(add, Qt::LeftButton);
    QVERIFY(nodeTree->currentItem() != nullptr);
    const auto componentNodeId = nodeTree->currentItem()->data(0, Qt::UserRole).toUInt();
    QVERIFY(entity->isEnabled());
    QVERIFY(component->isEnabled());
    const QString entityGuid = QString::fromStdString(
        fabgl::EntityGuid::fromStableName("tests.studio.visual-host.entity").toString());
    const QString componentGuid = QString::fromStdString(
        fabgl::ComponentTypeGuid::fromStableName("fabgl.component.Health.v1").toString());
    payload->setText(QStringLiteral("set:current"));
    entity->setText(entityGuid);
    component->setText(componentGuid);
    QVERIFY(QMetaObject::invokeMethod(component, "editingFinished", Qt::DirectConnection));

    int directReturnEdge = -1;
    for (int row = 0; row < connections->rowCount(); ++row) {
        if (connections->item(row, 0)->text() == QStringLiteral("1") &&
            connections->item(row, 1)->text() == QStringLiteral("1") &&
            connections->item(row, 2)->text() == QStringLiteral("3") &&
            connections->item(row, 3)->text() == QStringLiteral("1")) {
            directReturnEdge = row;
            break;
        }
    }
    QVERIFY(directReturnEdge >= 0);
    connections->selectRow(directReturnEdge);
    QTest::mouseClick(removeConnection, Qt::LeftButton);

    const auto selectValue = [](QComboBox* combo, const quint32 value) {
        const int index = combo->findData(value);
        if (index < 0)
            return false;
        combo->setCurrentIndex(index);
        return true;
    };
    const auto connectPins = [&](const quint32 source, const quint32 output, const quint32 target,
                                 const quint32 input) {
        if (!selectValue(sourceNode, source) || !selectValue(sourcePin, output) ||
            !selectValue(targetNode, target) || !selectValue(targetPin, input)) {
            return false;
        }
        const auto previous = editor.edgeCount();
        QTest::mouseClick(addConnection, Qt::LeftButton);
        return editor.edgeCount() == previous + 1;
    };
    QVERIFY(connectPins(1U, 1U, audioNodeId, 1U));
    QVERIFY(connectPins(audioNodeId, 3U, functionNodeId, 1U));
    QVERIFY(connectPins(functionNodeId, 3U, componentNodeId, 1U));
    QVERIFY(connectPins(componentNodeId, 3U, 3U, 1U));
    QVERIFY(connectPins(2U, 1U, audioNodeId, 2U));
    QVERIFY(connectPins(2U, 1U, functionNodeId, 2U));
    QVERIFY(connectPins(2U, 1U, componentNodeId, 2U));
    QVERIFY(!editor.hasValidationErrors());

    const QString graphPath =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("TypedHost.fglvisual"));
    QString errorMessage;
    QVERIFY2(editor.saveGraphFile(graphPath, errorMessage), qPrintable(errorMessage));
    QFile file(graphPath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto bytes = file.readAll();
    auto graph = fabgl::deserializeVisualGraph(
        std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    QVERIFY(graph);
    bool foundAudio = false;
    bool foundFunction = false;
    bool foundComponent = false;
    for (const auto& [id, node] : graph.value().nodes()) {
        Q_UNUSED(id);
        if (node.builtinType == fabgl::VisualBuiltinNodeType::AudioPlay) {
            foundAudio = true;
            QCOMPARE(QString::fromStdString(node.callbackPayload), QStringLiteral("sfx.loop"));
            QVERIFY(node.assetReference.has_value());
            QCOMPARE(QString::fromStdString(node.assetReference->toString()), audioGuid);
        }
        if (node.builtinType == fabgl::VisualBuiltinNodeType::FunctionCall) {
            foundFunction = true;
            QCOMPARE(QString::fromStdString(node.callbackName), QStringLiteral("math.abs"));
        }
        if (node.builtinType == fabgl::VisualBuiltinNodeType::ComponentAction) {
            foundComponent = true;
            QCOMPARE(QString::fromStdString(node.callbackPayload), QStringLiteral("set:current"));
            QVERIFY(node.entityReference.has_value());
            QVERIFY(node.componentReference.has_value());
            QCOMPARE(QString::fromStdString(node.entityReference->toString()), entityGuid);
            QCOMPARE(QString::fromStdString(node.componentReference->toString()), componentGuid);
        }
    }
    QVERIFY(foundAudio);
    QVERIFY(foundFunction);
    QVERIFY(foundComponent);
}

void StudioSmokeTests::animatorAssetsRoundTripWithStableReferencesAndPreview() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    fgl::studio::AnimatorEditorWidget editor;
    editor.show();
    QCoreApplication::processEvents();
    QVERIFY(editor.validationText().startsWith(QStringLiteral("Valid controller")));
    QVERIFY(!editor.previewText().isEmpty());
    auto* undoAsset = editor.findChild<QPushButton*>(QStringLiteral("animatorUndoButton"));
    auto* redoAsset = editor.findChild<QPushButton*>(QStringLiteral("animatorRedoButton"));
    auto* addState = editor.findChild<QPushButton*>(QStringLiteral("animatorAddStateButton"));
    QVERIFY(undoAsset != nullptr);
    QVERIFY(redoAsset != nullptr);
    QVERIFY(addState != nullptr);
    const int initialControllerStateCount = editor.stateCount();
    QVERIFY(initialControllerStateCount > 0);
    QTest::mouseClick(addState, Qt::LeftButton);
    QCOMPARE(editor.stateCount(), initialControllerStateCount + 1);
    QVERIFY(editor.canUndoAssetEdit());
    QTest::mouseClick(undoAsset, Qt::LeftButton);
    QCOMPARE(editor.stateCount(), initialControllerStateCount);
    QVERIFY(editor.canRedoAssetEdit());
    QTest::mouseClick(redoAsset, Qt::LeftButton);
    QCOMPARE(editor.stateCount(), initialControllerStateCount + 1);

    editor.newAnimationClip();
    auto* clipName = editor.findChild<QLineEdit*>(QStringLiteral("animatorClipNameEdit"));
    auto* clipGuid = editor.findChild<QLineEdit*>(QStringLiteral("animatorClipGuidEdit"));
    auto* timeline = editor.findChild<QSlider*>(QStringLiteral("animatorTimelineSlider"));
    auto* keys = editor.findChild<QTableWidget*>(QStringLiteral("animatorClipKeysTable"));
    QVERIFY(clipName != nullptr);
    QVERIFY(clipGuid != nullptr);
    QVERIFY(timeline != nullptr);
    QVERIFY(keys != nullptr);
    QCOMPARE(keys->rowCount(), 2);
    const QString stableClipGuid = clipGuid->text();
    clipName->setText(QStringLiteral("UI Authored Clip"));
    QVERIFY(editor.canUndoAssetEdit());
    QTest::mouseClick(undoAsset, Qt::LeftButton);
    QCOMPARE(clipName->text(), QStringLiteral("Untitled Animation"));
    QVERIFY(editor.canRedoAssetEdit());
    QTest::mouseClick(redoAsset, Qt::LeftButton);
    QCOMPARE(clipName->text(), QStringLiteral("UI Authored Clip"));
    timeline->setValue(500);
    QVERIFY(editor.previewText().contains(QStringLiteral("0.500")));
    QVERIFY(editor.assetModified());

    const QString clipPath =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("Animation/Authored.fglanim"));
    QString errorMessage;
    QVERIFY2(editor.saveAssetFile(clipPath, errorMessage), qPrintable(errorMessage));
    QVERIFY(!editor.assetModified());
    QFile clipFile(clipPath);
    QVERIFY(clipFile.open(QIODevice::ReadOnly));
    QCOMPARE(clipFile.readLine().trimmed(), QByteArrayLiteral("fglanim 1"));
    QVERIFY2(editor.openAssetFile(clipPath, errorMessage), qPrintable(errorMessage));
    QCOMPARE(clipGuid->text(), stableClipGuid);
    QCOMPARE(clipName->text(), QStringLiteral("UI Authored Clip"));

    const QString corruptPath =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("Animation/Corrupt.fglanim"));
    QVERIFY(writeTestFile(corruptPath, QByteArrayLiteral("fglanim 99\n")));
    QVERIFY(!editor.openAssetFile(corruptPath, errorMessage));
    QCOMPARE(editor.assetFilePath(), QFileInfo(clipPath).absoluteFilePath());
    QCOMPARE(clipGuid->text(), stableClipGuid);

    fabgl::AnimatorControllerAsset controller;
    controller.guid = fabgl::AssetGuid::fromStableName("studio.test.controller");
    controller.name = "Stable Reference Controller";
    controller.initialState = "Idle";
    const auto idle = fabgl::AssetGuid::fromStableName("studio.test.clip.idle");
    const auto run = fabgl::AssetGuid::fromStableName("studio.test.clip.run");
    controller.states.emplace("Idle", fabgl::AnimatorStateDefinition{idle});
    controller.states.emplace("Run", fabgl::AnimatorStateDefinition{run});
    controller.parameters.emplace(
        "moving",
        fabgl::AnimatorParameterDefinition{fabgl::AnimatorParameterType::Boolean, false, 0, 0.0F});
    controller.parameters.emplace(
        "speed",
        fabgl::AnimatorParameterDefinition{fabgl::AnimatorParameterType::Float, false, 0, 0.0F});
    fabgl::AnimatorTransitionDefinition transition;
    transition.fromState = "Idle";
    transition.toState = "Run";
    transition.blendDurationSeconds = 0.1F;
    transition.conditions.push_back(
        {"moving", fabgl::AnimationConditionMode::BooleanEquals, true, 0, 0.0F});
    transition.conditions.push_back(
        {"speed", fabgl::AnimationConditionMode::FloatGreater, false, 0, 0.5F});
    controller.transitions.push_back(std::move(transition));
    auto controllerSource = fabgl::serializeAnimatorControllerAsset(controller);
    QVERIFY(controllerSource);
    const QString controllerPath =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("Animation/Player.fglcontroller"));
    QVERIFY(writeTestFile(controllerPath,
                          QByteArray(controllerSource.value().data(),
                                     static_cast<qsizetype>(controllerSource.value().size()))));
    QVERIFY2(editor.openAssetFile(controllerPath, errorMessage), qPrintable(errorMessage));
    QCOMPARE(editor.stateCount(), 2);
    QCOMPARE(editor.parameterCount(), 2);
    QCOMPARE(editor.transitionCount(), 1);
    QVERIFY(editor.validationText().startsWith(QStringLiteral("Valid controller")));
    const QString secondControllerPath =
        QDir(temporaryDirectory.path())
            .filePath(QStringLiteral("Animation/PlayerCopy.fglcontroller"));
    QVERIFY2(editor.saveAssetFile(secondControllerPath, errorMessage), qPrintable(errorMessage));
    QFile copiedController(secondControllerPath);
    QVERIFY(copiedController.open(QIODevice::ReadOnly));
    QCOMPARE(copiedController.readAll(),
             QByteArray(controllerSource.value().data(),
                        static_cast<qsizetype>(controllerSource.value().size())));
}

void StudioSmokeTests::recoveryManagerRotatesDetectsCorruptionAndRestores() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString recoveryRoot =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("recovery"));
    QString errorMessage;

    fgl::studio::RecoveryManager abandonedSession(recoveryRoot, 3);
    QVERIFY2(abandonedSession.beginSession(errorMessage), qPrintable(errorMessage));
    QVERIFY(!abandonedSession.previousSessionWasUnclean());
    fgl::studio::RecoveryManager restartedSession(recoveryRoot, 3);
    QVERIFY2(restartedSession.beginSession(errorMessage), qPrintable(errorMessage));
    QVERIFY(restartedSession.previousSessionWasUnclean());
    QVERIFY2(restartedSession.endSession(errorMessage), qPrintable(errorMessage));

    fgl::studio::ProjectData project;
    project.projectGuid = QStringLiteral("40000000-0000-4000-8000-000000000002");
    project.name = QStringLiteral("Recover Me");
    project.sceneFile = QStringLiteral("Scenes/Main.fglscene");
    const QByteArray projectBytes = fgl::studio::ProjectDocument::serialized(project, errorMessage);
    QVERIFY2(!projectBytes.isEmpty(), qPrintable(errorMessage));
    fgl::studio::SceneDocument scene;
    scene.createDefault(QStringLiteral("Recovered Scene"));
    const QByteArray sceneBytes = scene.serialized(errorMessage);
    QVERIFY2(!sceneBytes.isEmpty(), qPrintable(errorMessage));

    const QString originalProjectPath =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("original/Original.fglproject"));
    for (int snapshot = 0; snapshot < 4; ++snapshot) {
        QVERIFY2(restartedSession.writeAutosave(originalProjectPath, project.sceneFile,
                                                projectBytes, sceneBytes, errorMessage),
                 qPrintable(errorMessage));
    }
    auto entries = restartedSession.entries();
    for (const auto& entry : entries) {
        QVERIFY2(!entry.corrupt, qPrintable(entry.errorMessage));
    }
    QCOMPARE(entries.size(), qsizetype{3});

    const QString restoredProjectPath =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("restored/Restored.fglproject"));
    QVERIFY(QDir().mkpath(QFileInfo(restoredProjectPath).absolutePath()));
    fgl::studio::ProjectData existingProject = project;
    existingProject.name = QStringLiteral("Before Restore");
    QVERIFY2(fgl::studio::ProjectDocument::save(restoredProjectPath, existingProject, errorMessage),
             qPrintable(errorMessage));
    fgl::studio::SceneDocument existingScene;
    existingScene.createDefault(QStringLiteral("Before Restore"));
    const QString restoredScenePath =
        fgl::studio::ProjectDocument::absoluteScenePath(restoredProjectPath, project);
    QVERIFY2(existingScene.saveAs(restoredScenePath, errorMessage), qPrintable(errorMessage));
    QVERIFY2(restartedSession.restore(entries.constFirst(), restoredProjectPath, errorMessage),
             qPrintable(errorMessage));
    QVERIFY(QFileInfo::exists(restoredProjectPath + QStringLiteral(".bak.1")));
    QVERIFY(QFileInfo::exists(restoredScenePath + QStringLiteral(".bak.1")));
    fgl::studio::ProjectData restoredProject;
    QVERIFY2(fgl::studio::ProjectDocument::load(restoredProjectPath, restoredProject, errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(restoredProject.name, project.name);
    fgl::studio::SceneDocument restoredScene;
    QVERIFY2(restoredScene.load(restoredScenePath, errorMessage), qPrintable(errorMessage));
    QCOMPARE(restoredScene.scene().entities().size(), std::size_t{2});

    QFile corruptFile(entries.constLast().filePath);
    QVERIFY(corruptFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(corruptFile.write(QByteArrayLiteral("{")) > 0);
    corruptFile.close();
    entries = restartedSession.entries();
    const auto corruptEntry =
        std::find_if(entries.cbegin(), entries.cend(),
                     [](const fgl::studio::RecoveryEntry& entry) { return entry.corrupt; });
    QVERIFY(corruptEntry != entries.cend());
    QVERIFY(!restartedSession.restore(*corruptEntry, restoredProjectPath, errorMessage));
    QVERIFY2(restartedSession.discard(*corruptEntry, errorMessage), qPrintable(errorMessage));

    restartedSession.recordLastProject(originalProjectPath);
    QCOMPARE(restartedSession.lastProjectPath(),
             fgl::studio::ProjectTrustStore::normalizedProjectPath(originalProjectPath));
}

void StudioSmokeTests::projectTrustAndSafeModeGateExecution() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    QString errorMessage;
    const QString projectPath =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("External.fglproject"));
    QVERIFY2(createProjectFixture(projectPath, QStringLiteral("External Project"), errorMessage),
             qPrintable(errorMessage));
    QFile projectFile(projectPath);
    QVERIFY(projectFile.open(QIODevice::ReadOnly));
    const QByteArray manifestBeforeTrust = projectFile.readAll();
    projectFile.close();

    fgl::studio::ProjectTrustStore trustStore;
    QVERIFY(!trustStore.isTrusted(projectPath));
    QVERIFY2(trustStore.setTrusted(projectPath, true, errorMessage), qPrintable(errorMessage));
    QVERIFY(fgl::studio::ProjectTrustStore().isTrusted(projectPath));
    QVERIFY(projectFile.open(QIODevice::ReadOnly));
    QCOMPARE(projectFile.readAll(), manifestBeforeTrust);
    projectFile.close();
    const QString copiedProjectPath =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("Copied.fglproject"));
    fgl::studio::ProjectData copiedProject;
    copiedProject.projectGuid = QStringLiteral("40000000-0000-4000-8000-000000000003");
    copiedProject.name = QStringLiteral("Copied Project");
    QVERIFY2(fgl::studio::ProjectDocument::save(copiedProjectPath, copiedProject, errorMessage),
             qPrintable(errorMessage));
    QVERIFY(!trustStore.isTrusted(copiedProjectPath));
    QVERIFY2(trustStore.clearDecision(projectPath, errorMessage), qPrintable(errorMessage));
    QVERIFY(!trustStore.isTrusted(projectPath));

    {
        fgl::studio::StudioLaunchOptions launchOptions;
        launchOptions.safeMode = true;
        launchOptions.pluginsEnabled = true;
        launchOptions.reopenLastProject = true;
        launchOptions.recoveryRoot =
            QDir(temporaryDirectory.path()).filePath(QStringLiteral("safe-recovery"));
        fgl::studio::MainWindow safeWindow(nullptr, launchOptions);
        QVERIFY(safeWindow.safeMode());
        QVERIFY(!safeWindow.pluginsEnabled());
        QVERIFY(!safeWindow.telemetryEnabled());
        const auto* security = safeWindow.findChild<QLabel*>(QStringLiteral("securityModeStatus"));
        QVERIFY(security != nullptr);
        QVERIFY(security->text().contains(QStringLiteral("telemetry off"), Qt::CaseInsensitive));
    }

    fgl::studio::StudioLaunchOptions launchOptions;
    launchOptions.recoveryRoot =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("trust-recovery"));
    fgl::studio::MainWindow window(nullptr, launchOptions);
    QVERIFY2(window.openProjectPath(projectPath, errorMessage), qPrintable(errorMessage));
    QVERIFY(!window.currentProjectTrusted());
    auto* trustAction = window.findChild<QAction*>(QStringLiteral("trustProjectAction"));
    auto* playAction = window.findChild<QAction*>(QStringLiteral("playAction"));
    auto* buildAction = window.findChild<QAction*>(QStringLiteral("buildProjectAction"));
    auto* pcPlayAction = window.findChild<QAction*>(QStringLiteral("pcPlayAction"));
    QVERIFY(trustAction != nullptr);
    QVERIFY(playAction != nullptr);
    QVERIFY(buildAction != nullptr);
    QVERIFY(pcPlayAction != nullptr);
    QVERIFY(!trustAction->isChecked());
    QVERIFY(!playAction->isEnabled());
    QVERIFY(!buildAction->isEnabled());
    QVERIFY(!pcPlayAction->isEnabled());
    QVERIFY2(window.setCurrentProjectTrusted(true, errorMessage), qPrintable(errorMessage));
    QVERIFY(window.currentProjectTrusted());
    QVERIFY(trustAction->isChecked());
    QVERIFY(playAction->isEnabled());
    QVERIFY(buildAction->isEnabled());
    QVERIFY(pcPlayAction->isEnabled());
    QVERIFY2(window.setCurrentProjectTrusted(false, errorMessage), qPrintable(errorMessage));
    QVERIFY(!playAction->isEnabled());
    QVERIFY(!buildAction->isEnabled());
    QVERIFY(!pcPlayAction->isEnabled());

    const QString unsupportedProjectPath =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("Unsupported.fglproject"));
    fgl::studio::ProjectData unsupportedProject;
    unsupportedProject.projectGuid = QStringLiteral("40000000-0000-4000-8000-000000000004");
    unsupportedProject.name = QStringLiteral("Unsupported Target Project");
    unsupportedProject.sceneFile = QStringLiteral("Scenes/Main.fglscene");
    unsupportedProject.targetProfiles.pc = QStringLiteral("pc.experimental");
    fgl::studio::SceneDocument unsupportedScene;
    unsupportedScene.createDefault(QStringLiteral("Unsupported Target Scene"));
    QVERIFY2(unsupportedScene.saveAs(fgl::studio::ProjectDocument::absoluteScenePath(
                                         unsupportedProjectPath, unsupportedProject),
                                     errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(fgl::studio::ProjectDocument::save(unsupportedProjectPath, unsupportedProject,
                                                errorMessage),
             qPrintable(errorMessage));

    fgl::studio::StudioLaunchOptions unsupportedOptions;
    unsupportedOptions.recoveryRoot =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("unsupported-recovery"));
    fgl::studio::MainWindow unsupportedWindow(nullptr, unsupportedOptions);
    QVERIFY2(unsupportedWindow.openProjectPath(unsupportedProjectPath, errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(unsupportedWindow.setCurrentProjectTrusted(true, errorMessage),
             qPrintable(errorMessage));
    const auto* unsupportedProfile =
        unsupportedWindow.findChild<QLabel*>(QStringLiteral("projectTargetProfileStatus"));
    const auto* unsupportedBuild =
        unsupportedWindow.findChild<QAction*>(QStringLiteral("buildProjectAction"));
    const auto* unsupportedPcPlay =
        unsupportedWindow.findChild<QAction*>(QStringLiteral("pcPlayAction"));
    QVERIFY(unsupportedProfile != nullptr);
    QVERIFY(unsupportedProfile->text().contains(QStringLiteral("pc.experimental")));
    QVERIFY(
        unsupportedProfile->text().contains(QStringLiteral("unsupported"), Qt::CaseInsensitive));
    QVERIFY(unsupportedBuild != nullptr);
    QVERIFY(unsupportedPcPlay != nullptr);
    QVERIFY(!unsupportedBuild->isEnabled());
    QVERIFY(!unsupportedPcPlay->isEnabled());
}

void StudioSmokeTests::mainWindowAutosaveProvidesDialoglessRecovery() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    QString errorMessage;
    const QString projectPath =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("Autosave.fglproject"));
    QVERIFY2(createProjectFixture(projectPath, QStringLiteral("Autosave Project"), errorMessage),
             qPrintable(errorMessage));
    fgl::studio::StudioLaunchOptions launchOptions;
    launchOptions.recoveryRoot =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("window-recovery"));
    fgl::studio::MainWindow window(nullptr, launchOptions);
    QVERIFY2(window.openProjectPath(projectPath, errorMessage), qPrintable(errorMessage));
    const auto entities = window.sceneDocument().scene().entities();
    QVERIFY(!entities.empty());
    auto snapshot = window.sceneDocument().snapshot(entities.front()->id());
    QVERIFY(snapshot.has_value());
    snapshot->position.x += 1.0F;
    QVERIFY2(window.sceneDocument().applySnapshot(*snapshot, errorMessage),
             qPrintable(errorMessage));
    QVERIFY(window.isWindowModified());
    QVERIFY2(window.performAutosave(errorMessage), qPrintable(errorMessage));
    const auto recoveries = window.recoveryEntries();
    QCOMPARE(recoveries.size(), qsizetype{1});
    QVERIFY2(!recoveries.constFirst().corrupt, qPrintable(recoveries.constFirst().errorMessage));
    QCOMPARE(window.lastProjectPath(),
             fgl::studio::ProjectTrustStore::normalizedProjectPath(projectPath));

    const QString restoredProjectPath =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("recovered/Recovered.fglproject"));
    QVERIFY2(window.restoreRecovery(recoveries.constFirst().id, restoredProjectPath, errorMessage),
             qPrintable(errorMessage));
    fgl::studio::ProjectData restoredProject;
    QVERIFY2(fgl::studio::ProjectDocument::load(restoredProjectPath, restoredProject, errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(restoredProject.name, QStringLiteral("Autosave Project"));
    QVERIFY2(window.discardRecovery(recoveries.constFirst().id, errorMessage),
             qPrintable(errorMessage));
    QVERIFY(window.recoveryEntries().isEmpty());
}

} // namespace

QTEST_MAIN(StudioSmokeTests)

#include "studio_smoke_tests.moc"
