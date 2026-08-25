#include "InputMapEditorWidget.h"
#include "MainWindow.h"
#include "ProjectCreationDialog.h"
#include "ProjectDocument.h"
#include "SceneDocument.h"
#include "ToolchainSetupWidget.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>

namespace {

using fgl::studio::InputMapEditorWidget;
using fgl::studio::MainWindow;
using fgl::studio::ProjectAssetEntry;
using fgl::studio::ProjectCreationDialog;
using fgl::studio::ProjectCreationRequest;
using fgl::studio::ProjectData;
using fgl::studio::ProjectDocument;
using fgl::studio::ProjectTemplateCreator;
using fgl::studio::ProjectTemplateKind;
using fgl::studio::SceneDocument;
using fgl::studio::StudioLaunchOptions;
using fgl::studio::ToolchainSetupWidget;

bool writeFile(const QString& path, const QByteArray& bytes = QByteArrayLiteral("fixture")) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return false;
    }
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

QTreeWidgetItem* topLevelItem(QTreeWidget* tree, const QString& name) {
    for (int index = 0; index < tree->topLevelItemCount(); ++index) {
        auto* item = tree->topLevelItem(index);
        if (item->text(1) == name) {
            return item;
        }
    }
    return nullptr;
}

class ProjectWizardInputTests final : public QObject {
    Q_OBJECT

  private slots:
    void allTemplatesCreateCanonicalOpenableProjects();
    void dialogCreatesSelectedTemplateByRealClick();
    void inputMapEditsValidateUndoAndRoundTripLosslessly();
    void toolchainStatusPreviewOfflineAndRepairAreGated();
    void mainWindowExposesInputAndToolchainDocks();
};

void ProjectWizardInputTests::allTemplatesCreateCanonicalOpenableProjects() {
    QTemporaryDir parent;
    QVERIFY(parent.isValid());
    const std::array kinds = {
        ProjectTemplateKind::Empty,         ProjectTemplateKind::Platformer2D,
        ProjectTemplateKind::TopDown,       ProjectTemplateKind::RaycastFps,
        ProjectTemplateKind::Pseudo3DRacer, ProjectTemplateKind::ThirdPerson,
        ProjectTemplateKind::UserInterface,
    };
    for (std::size_t index = 0; index < kinds.size(); ++index) {
        const auto name = index + 1U == kinds.size() ? QStringLiteral("Arayüz Şablonu")
                                                     : QStringLiteral("Template%1").arg(index);
        const ProjectCreationRequest request{name, parent.path(), kinds[index],
                                             QStringLiteral("pc.default"),
                                             QStringLiteral("olimex-esp32-sbc-fabgl-revb")};
        QString path;
        QString error;
        QVERIFY2(ProjectTemplateCreator::create(request, path, error), qPrintable(error));
        QVERIFY(QFileInfo(path).isFile());
        const QDir root(QFileInfo(path).absolutePath());
        for (const auto& folder : {QStringLiteral("Assets"), QStringLiteral("Scenes"),
                                   QStringLiteral("Scripts"), QStringLiteral("Packages")}) {
            QVERIFY2(root.exists(folder), qPrintable(folder));
        }
        QVERIFY(root.exists(QStringLiteral("CMakeLists.txt")));
        QVERIFY(root.exists(QStringLiteral("Scripts/FabGLStudioScripts.cmake")));
        QVERIFY(root.entryList({QStringLiteral("*.cpp")}, QDir::Files).isEmpty());
        QVERIFY(!QDir(root.filePath(QStringLiteral("Scripts")))
                     .entryList({QStringLiteral("*.cpp")}, QDir::Files)
                     .isEmpty());
        ProjectData project;
        QVERIFY2(ProjectDocument::load(path, project, error), qPrintable(error));
        QCOMPARE(project.sourceFormatVersion, ProjectDocument::FormatVersion);
        QCOMPARE(project.name, name);
        QCOMPARE(project.relativeRoot, QStringLiteral("."));
        QCOMPARE(project.targetProfiles.pc, QStringLiteral("pc.default"));
        QCOMPARE(project.targetProfiles.esp32, QStringLiteral("olimex-esp32-sbc-fabgl-revb"));
        QVERIFY(!project.projectGuid.isEmpty());
        SceneDocument scene;
        QVERIFY2(scene.load(ProjectDocument::absoluteScenePath(path, project), error),
                 qPrintable(error));
        QVERIFY(scene.scene().entityCount() >= 2U);
        if (kinds[index] == ProjectTemplateKind::RaycastFps) {
            QVERIFY(root.exists(QStringLiteral("Maps")));
        }
        if (kinds[index] == ProjectTemplateKind::Pseudo3DRacer) {
            QVERIFY(root.exists(QStringLiteral("Tracks")));
        }
        const auto originalManifest = [&]() {
            QFile file(path);
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
        }();
        QString duplicatePath;
        QVERIFY(!ProjectTemplateCreator::create(request, duplicatePath, error));
        QFile unchanged(path);
        QVERIFY(unchanged.open(QIODevice::ReadOnly));
        QCOMPARE(unchanged.readAll(), originalManifest);
    }
    QString escapedPath;
    QString error;
    QVERIFY(!ProjectTemplateCreator::create(
        {QStringLiteral("../Escape"), parent.path(), ProjectTemplateKind::Empty,
         QStringLiteral("pc.default"), QStringLiteral("olimex-esp32-sbc-fabgl-revb")},
        escapedPath, error));
    QVERIFY(!QFileInfo(QDir(parent.path()).filePath(QStringLiteral("../Escape"))).exists());
}

void ProjectWizardInputTests::dialogCreatesSelectedTemplateByRealClick() {
    QTemporaryDir parent;
    QVERIFY(parent.isValid());
    ProjectCreationDialog dialog;
    dialog.setInitialParentDirectory(parent.path());
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    auto* name = dialog.findChild<QLineEdit*>(QStringLiteral("projectCreationNameEdit"));
    auto* templates = dialog.findChild<QComboBox*>(QStringLiteral("projectCreationTemplateCombo"));
    auto* create = dialog.findChild<QPushButton*>(QStringLiteral("projectCreationCreateButton"));
    QVERIFY(name != nullptr);
    QVERIFY(templates != nullptr);
    QVERIFY(create != nullptr);
    name->setText(QStringLiteral("ClickedPlatformer"));
    const auto platformer =
        templates->findData(static_cast<int>(ProjectTemplateKind::Platformer2D));
    QVERIFY(platformer >= 0);
    templates->setCurrentIndex(platformer);
    QSignalSpy created(&dialog, &ProjectCreationDialog::projectCreated);
    QTest::mouseClick(create, Qt::LeftButton);
    QCOMPARE(created.count(), 1);
    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
    ProjectData project;
    QString error;
    QVERIFY2(ProjectDocument::load(dialog.createdProjectPath(), project, error), qPrintable(error));
    QCOMPARE(project.previewDemo, QStringLiteral("platformer"));
    QCOMPARE(project.inputContexts.size(), 1);
    QVERIFY(std::any_of(project.inputContexts.front().actions.cbegin(),
                        project.inputContexts.front().actions.cend(), [](const auto& action) {
                            return std::any_of(action.bindings.cbegin(), action.bindings.cend(),
                                               [](const auto& inputBinding) {
                                                   return inputBinding.control.startsWith(
                                                       QStringLiteral("Gamepad."));
                                               });
                        }));
}

void ProjectWizardInputTests::inputMapEditsValidateUndoAndRoundTripLosslessly() {
    QTemporaryDir parent;
    QVERIFY(parent.isValid());
    QString path;
    QString error;
    QVERIFY2(ProjectTemplateCreator::create(
                 {QStringLiteral("InputProject"), parent.path(), ProjectTemplateKind::Empty,
                  QStringLiteral("pc.default"), QStringLiteral("olimex-esp32-sbc-fabgl-revb")},
                 path, error),
             qPrintable(error));
    ProjectData source;
    QVERIFY2(ProjectDocument::load(path, source, error), qPrintable(error));
    source.previewDemo = QStringLiteral("preserve-me");
    source.buildArguments = {QStringLiteral("--build"), QStringLiteral("custom/output")};
    ProjectAssetEntry keepAsset;
    keepAsset.guid = QStringLiteral("50000000-0000-4000-8000-000000000099");
    keepAsset.path = QStringLiteral("Assets/Keep.fgli");
    keepAsset.type = QStringLiteral("image");
    source.assets = {keepAsset};
    QVERIFY2(ProjectDocument::save(path, source, error), qPrintable(error));

    InputMapEditorWidget editor;
    editor.setProjectContext(path, source);
    editor.resize(760, 760);
    editor.show();
    QVERIFY(QTest::qWaitForWindowExposed(&editor));
    auto* contextName = editor.findChild<QLineEdit*>(QStringLiteral("inputContextNameEdit"));
    auto* addContext = editor.findChild<QPushButton*>(QStringLiteral("inputAddContextButton"));
    auto* valueKind = editor.findChild<QComboBox*>(QStringLiteral("inputValueKindCombo"));
    auto* valueName = editor.findChild<QLineEdit*>(QStringLiteral("inputValueNameEdit"));
    auto* addValue = editor.findChild<QPushButton*>(QStringLiteral("inputAddValueButton"));
    auto* tree = editor.findChild<QTreeWidget*>(QStringLiteral("inputMapTree"));
    auto* device = editor.findChild<QComboBox*>(QStringLiteral("inputBindingDeviceCombo"));
    auto* control = editor.findChild<QLineEdit*>(QStringLiteral("inputBindingControlEdit"));
    auto* scale = editor.findChild<QDoubleSpinBox*>(QStringLiteral("inputBindingScaleSpin"));
    auto* deadzone = editor.findChild<QDoubleSpinBox*>(QStringLiteral("inputBindingDeadzoneSpin"));
    auto* addBinding = editor.findChild<QPushButton*>(QStringLiteral("inputAddBindingButton"));
    auto* applyBinding = editor.findChild<QPushButton*>(QStringLiteral("inputApplyBindingButton"));
    auto* undo = editor.findChild<QPushButton*>(QStringLiteral("inputUndoButton"));
    auto* redo = editor.findChild<QPushButton*>(QStringLiteral("inputRedoButton"));
    auto* save = editor.findChild<QPushButton*>(QStringLiteral("inputSaveButton"));
    QVERIFY(contextName && addContext && valueKind && valueName && addValue && tree && device &&
            control && scale && deadzone && addBinding && applyBinding && undo && redo && save);

    contextName->setText(QStringLiteral("gameplay"));
    QTest::mouseClick(addContext, Qt::LeftButton);
    valueName->setText(QStringLiteral("Fire"));
    QTest::mouseClick(addValue, Qt::LeftButton);
    QVERIFY(editor.hasValidationErrors());
    auto* fire = topLevelItem(tree, QStringLiteral("Fire"));
    QVERIFY(fire != nullptr);
    tree->setCurrentItem(fire);
    device->setCurrentIndex(device->findData(QStringLiteral("Mouse")));
    control->setText(QStringLiteral("Left"));
    QTest::mouseClick(addBinding, Qt::LeftButton);
    QVERIFY2(!editor.hasValidationErrors(), qPrintable(editor.validationText()));

    valueName->setText(QStringLiteral("Select"));
    QTest::mouseClick(addValue, Qt::LeftButton);
    auto* select = topLevelItem(tree, QStringLiteral("Select"));
    QVERIFY(select != nullptr);
    tree->setCurrentItem(select);
    device->setCurrentIndex(device->findData(QStringLiteral("Mouse")));
    control->setText(QStringLiteral("Left"));
    QTest::mouseClick(addBinding, Qt::LeftButton);
    QVERIFY(editor.hasValidationErrors());
    QVERIFY(editor.validationText().contains(QStringLiteral("conflict"), Qt::CaseInsensitive));
    QVERIFY(!save->isEnabled());
    QTest::mouseClick(undo, Qt::LeftButton);
    QTest::mouseClick(undo, Qt::LeftButton);
    QVERIFY2(!editor.hasValidationErrors(), qPrintable(editor.validationText()));
    QTest::mouseClick(redo, Qt::LeftButton);
    QTest::mouseClick(redo, Qt::LeftButton);
    QVERIFY(editor.hasValidationErrors());
    QTest::mouseClick(undo, Qt::LeftButton);
    QTest::mouseClick(undo, Qt::LeftButton);

    valueKind->setCurrentIndex(valueKind->findData(1));
    valueName->setText(QStringLiteral("MoveX"));
    QTest::mouseClick(addValue, Qt::LeftButton);
    auto* move = topLevelItem(tree, QStringLiteral("MoveX"));
    QVERIFY(move != nullptr);
    tree->setCurrentItem(move);
    device->setCurrentIndex(device->findData(QStringLiteral("Gamepad")));
    control->setText(QStringLiteral("LeftX"));
    scale->setValue(0.75);
    deadzone->setValue(0.2);
    QTest::mouseClick(addBinding, Qt::LeftButton);
    QVERIFY2(!editor.hasValidationErrors(), qPrintable(editor.validationText()));

    move = topLevelItem(tree, QStringLiteral("MoveX"));
    QVERIFY(move != nullptr);
    tree->setCurrentItem(move);
    QTest::mouseClick(addBinding, Qt::LeftButton);
    QVERIFY(editor.validationText().contains(QStringLiteral("Duplicate binding")));
    QTest::mouseClick(undo, Qt::LeftButton);
    QVERIFY(!editor.hasValidationErrors());
    move = topLevelItem(tree, QStringLiteral("MoveX"));
    QVERIFY(move != nullptr);
    QVERIFY(move->childCount() == 1);
    tree->setCurrentItem(move->child(0));
    control->setText(QStringLiteral("RightX"));
    QTest::mouseClick(applyBinding, Qt::LeftButton);
    QCOMPARE(editor.projectData().inputContexts.front().axes.front().bindings.front().control,
             QStringLiteral("Gamepad.RightX"));
    QTest::mouseClick(undo, Qt::LeftButton);
    QTest::mouseClick(redo, Qt::LeftButton);
    QCOMPARE(editor.projectData().inputContexts.front().axes.front().bindings.front().control,
             QStringLiteral("Gamepad.RightX"));

    QTest::mouseClick(save, Qt::LeftButton);
    ProjectData loaded;
    QVERIFY2(ProjectDocument::load(path, loaded, error), qPrintable(error));
    QCOMPARE(loaded.previewDemo, QStringLiteral("preserve-me"));
    QCOMPARE(loaded.buildArguments, source.buildArguments);
    QCOMPARE(loaded.assets, source.assets);
    QCOMPARE(loaded.inputContexts.size(), 1);
    QCOMPARE(loaded.inputContexts.front().actions.front().name, QStringLiteral("Fire"));
    QCOMPARE(loaded.inputContexts.front().axes.front().bindings.front().control,
             QStringLiteral("Gamepad.RightX"));
    QCOMPARE(loaded.inputContexts.front().axes.front().bindings.front().scale, 0.75);
    QCOMPARE(loaded.inputContexts.front().axes.front().bindings.front().threshold, 0.2);
}

void ProjectWizardInputTests::toolchainStatusPreviewOfflineAndRepairAreGated() {
    QTemporaryDir repository;
    QVERIFY(repository.isValid());
    const QDir root(repository.path());
    QVERIFY(writeFile(root.filePath(QStringLiteral("scripts/bootstrap_desktop.ps1"))));
    QVERIFY(writeFile(root.filePath(QStringLiteral("scripts/bootstrap_toolchain.ps1"))));
    const QByteArray desktopManifest = R"json({
      "qt": {"version":"6.8.3", "installRoot":".toolchains/Qt",
             "sdkDirectory":"6.8.3/mingw_64", "requiredFiles":["bin/qmake.exe"]},
      "compiler": {"version":"13.1.0", "directory":"Tools/mingw1310_64",
                   "cxx":"bin/g++.exe", "make":"bin/mingw32-make.exe"}
    })json";
    const QByteArray esp32Manifest = R"json({
      "profile": {"arduinoCore":{"version":"2.0.11"},
                  "fabgl":{"distributionVersion":"1.0.9+olimex.04f328a"}},
      "storage": {"installDirectory":".toolchains"},
      "artifacts": [{"installDirectory":"arduino-cli/1.5.1"}]
    })json";
    QVERIFY(writeFile(root.filePath(QStringLiteral("toolchains/desktop-manifest.json")),
                      desktopManifest));
    QVERIFY(writeFile(root.filePath(QStringLiteral("toolchains/manifest.json")), esp32Manifest));
    ToolchainSetupWidget widget;
    widget.setCommandExecutionEnabled(false);
    widget.setRepositoryRoot(repository.path());
    widget.resize(760, 560);
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));
    QVERIFY(!widget.selectedProfileInstalled());
    QVERIFY(widget.statusText().contains(QStringLiteral("incomplete")));
    QVERIFY(widget.commandPreview().contains(QStringLiteral("bootstrap_desktop.ps1")));
    auto* install = widget.findChild<QPushButton*>(QStringLiteral("toolchainInstallButton"));
    auto* repair = widget.findChild<QPushButton*>(QStringLiteral("toolchainRepairButton"));
    auto* refresh = widget.findChild<QPushButton*>(QStringLiteral("toolchainRefreshButton"));
    auto* profiles = widget.findChild<QComboBox*>(QStringLiteral("toolchainProfileCombo"));
    auto* offline = widget.findChild<QCheckBox*>(QStringLiteral("toolchainOfflineCheck"));
    auto* offlinePath =
        widget.findChild<QLineEdit*>(QStringLiteral("toolchainOfflineDirectoryEdit"));
    QVERIFY(install && repair && refresh && profiles && offline && offlinePath);
    QSignalSpy commands(&widget, &ToolchainSetupWidget::commandPrepared);
    QTest::mouseClick(install, Qt::LeftButton);
    QCOMPARE(commands.count(), 1);
    auto arguments = commands.takeFirst().at(1).toStringList();
    QVERIFY(arguments.contains(QStringLiteral("-File")));
    QVERIFY(!arguments.contains(QStringLiteral("-Force")));
    QTest::mouseClick(repair, Qt::LeftButton);
    QCOMPARE(commands.count(), 1);
    arguments = commands.takeFirst().at(1).toStringList();
    QVERIFY(arguments.contains(QStringLiteral("-Force")));

    QVERIFY(
        writeFile(root.filePath(QStringLiteral(".toolchains/Qt/6.8.3/mingw_64/bin/qmake.exe"))));
    QVERIFY(
        writeFile(root.filePath(QStringLiteral(".toolchains/Qt/Tools/mingw1310_64/bin/g++.exe"))));
    QVERIFY(writeFile(
        root.filePath(QStringLiteral(".toolchains/Qt/Tools/mingw1310_64/bin/mingw32-make.exe"))));
    QVERIFY(QDir().mkpath(root.filePath(QStringLiteral(".toolchains/python-packages"))));
    QTest::mouseClick(refresh, Qt::LeftButton);
    QVERIFY2(widget.selectedProfileInstalled(), qPrintable(widget.statusText()));

    profiles->setCurrentIndex(1);
    offline->setChecked(true);
    offlinePath->setText(root.filePath(QStringLiteral("missing-offline")));
    QVERIFY(!install->isEnabled());
    auto* fallback = widget.findChild<QLabel*>(QStringLiteral("toolchainFallbackLabel"));
    QVERIFY(fallback != nullptr);
    QVERIFY(fallback->text().contains(QStringLiteral("offline"), Qt::CaseInsensitive));
    const auto approved = root.filePath(QStringLiteral("approved-offline"));
    QVERIFY(QDir().mkpath(approved));
    offlinePath->setText(approved);
    QVERIFY(install->isEnabled());
    QTest::mouseClick(install, Qt::LeftButton);
    QCOMPARE(commands.count(), 1);
    arguments = commands.takeFirst().at(1).toStringList();
    const auto offlineArgument = arguments.indexOf(QStringLiteral("-OfflineSourceDirectory"));
    QVERIFY(offlineArgument >= 0);
    QCOMPARE(arguments.value(offlineArgument + 1), QFileInfo(approved).absoluteFilePath());
}

void ProjectWizardInputTests::mainWindowExposesInputAndToolchainDocks() {
    QTemporaryDir parent;
    QTemporaryDir recovery;
    QVERIFY(parent.isValid());
    QVERIFY(recovery.isValid());
    QString path;
    QString error;
    QVERIFY2(ProjectTemplateCreator::create(
                 {QStringLiteral("IntegratedProject"), parent.path(), ProjectTemplateKind::TopDown,
                  QStringLiteral("pc.default"), QStringLiteral("olimex-esp32-sbc-fabgl-revb")},
                 path, error),
             qPrintable(error));
    StudioLaunchOptions options;
    options.safeMode = true;
    options.pluginsEnabled = false;
    options.recoveryRoot = recovery.path();
    MainWindow window(nullptr, options);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY2(window.openProjectPath(path, error), qPrintable(error));
    auto* inputDock = window.findChild<QDockWidget*>(QStringLiteral("inputMapEditorDock"));
    auto* toolchainDock = window.findChild<QDockWidget*>(QStringLiteral("toolchainSetupDock"));
    auto* inputAction = window.findChild<QAction*>(QStringLiteral("showInputMapEditorAction"));
    auto* toolchainAction = window.findChild<QAction*>(QStringLiteral("showToolchainSetupAction"));
    auto* inputEditor =
        window.findChild<InputMapEditorWidget*>(QStringLiteral("inputMapEditorWidget"));
    QVERIFY(inputDock && toolchainDock && inputAction && toolchainAction && inputEditor);
    QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("inputSaveButton")),
                      Qt::LeftButton);
    inputAction->trigger();
    QCoreApplication::processEvents();
    QVERIFY(inputDock->isVisible());
    toolchainAction->trigger();
    QCoreApplication::processEvents();
    QVERIFY(toolchainDock->isVisible());
    QCOMPARE(inputEditor->projectFilePath(), QFileInfo(path).absoluteFilePath());
    window.close();
}

} // namespace

QTEST_MAIN(ProjectWizardInputTests)
#include "project_wizard_input_tests.moc"
