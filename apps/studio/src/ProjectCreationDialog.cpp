#include "ProjectCreationDialog.h"

#include "ProjectDocument.h"
#include "SceneDocument.h"
#include "script_generator.h"

#include <fabgl/core/result.h>
#include <fabgl/scene/entity.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <optional>
#include <string>

namespace fgl::studio {
namespace {

QString errorText(const fabgl::Error& error) {
    QString text = QString::fromStdString(error.message());
    for (const auto& context : error.context()) {
        text +=
            QStringLiteral(" [%1=%2]")
                .arg(QString::fromStdString(context.key), QString::fromStdString(context.value));
    }
    return text;
}

std::string utf8Path(const QString& path) {
    const auto bytes = QDir::fromNativeSeparators(path).toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

bool validStableProfileId(const QString& value) {
    const auto bytes = value.toLatin1();
    if (bytes.isEmpty() || bytes.size() > 80) {
        return false;
    }
    const auto first = static_cast<unsigned char>(bytes.front());
    if (!((first >= 'a' && first <= 'z') || (first >= '0' && first <= '9'))) {
        return false;
    }
    return std::all_of(bytes.cbegin(), bytes.cend(), [](const char character) {
        return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
               character == '.' || character == '_' || character == '-';
    });
}

bool validProjectName(const QString& name, QString& errorMessage) {
    const auto trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed.toUtf8().size() > 160 || trimmed == QStringLiteral(".") ||
        trimmed == QStringLiteral("..") || trimmed != name ||
        trimmed.contains(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*\x00-\x1f])"))) ||
        trimmed.endsWith(QLatin1Char('.')) || trimmed.endsWith(QLatin1Char(' '))) {
        errorMessage = QObject::tr(
            "Project name is empty, too long, or contains unsafe file-name characters.");
        return false;
    }
    const auto base = trimmed.section(QLatin1Char('.'), 0, 0).toUpper();
    const bool numberedDevice =
        base.size() == 4 &&
        (base.startsWith(QStringLiteral("COM")) || base.startsWith(QStringLiteral("LPT"))) &&
        base.at(3) >= QLatin1Char('1') && base.at(3) <= QLatin1Char('9');
    if (QStringList{QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"),
                    QStringLiteral("NUL"), QStringLiteral("CLOCK$")}
            .contains(base) ||
        numberedDevice) {
        errorMessage = QObject::tr("Project name is reserved by the operating system.");
        return false;
    }
    return true;
}

QString gameplayClassName(const ProjectTemplateKind kind) {
    switch (kind) {
    case ProjectTemplateKind::Empty:
        return QStringLiteral("GameController");
    case ProjectTemplateKind::Platformer2D:
        return QStringLiteral("PlatformerGame");
    case ProjectTemplateKind::TopDown:
        return QStringLiteral("TopDownGame");
    case ProjectTemplateKind::RaycastFps:
        return QStringLiteral("RaycastGame");
    case ProjectTemplateKind::Pseudo3DRacer:
        return QStringLiteral("RacerGame");
    case ProjectTemplateKind::ThirdPerson:
        return QStringLiteral("ThirdPersonGame");
    case ProjectTemplateKind::UserInterface:
        return QStringLiteral("UiGame");
    }
    return QStringLiteral("GameController");
}

QString previewDemo(const ProjectTemplateKind kind) {
    switch (kind) {
    case ProjectTemplateKind::Empty:
        return QStringLiteral("empty");
    case ProjectTemplateKind::Platformer2D:
        return QStringLiteral("platformer");
    case ProjectTemplateKind::TopDown:
        return QStringLiteral("topdown");
    case ProjectTemplateKind::RaycastFps:
        return QStringLiteral("raycast");
    case ProjectTemplateKind::Pseudo3DRacer:
        return QStringLiteral("racer");
    case ProjectTemplateKind::ThirdPerson:
        return QStringLiteral("lowpoly");
    case ProjectTemplateKind::UserInterface:
        return QStringLiteral("ui");
    }
    return QStringLiteral("empty");
}

ProjectInputBinding binding(const QString& control, const double scale = 1.0,
                            const double threshold = 0.5) {
    return {control, scale, threshold};
}

ProjectInputValue inputValue(const QString& name,
                             std::initializer_list<ProjectInputBinding> bindings) {
    return {name, QVector<ProjectInputBinding>(bindings)};
}

QVector<ProjectInputContext> templateInput(const ProjectTemplateKind kind) {
    ProjectInputContext context;
    context.name = kind == ProjectTemplateKind::Pseudo3DRacer   ? QStringLiteral("driving")
                   : kind == ProjectTemplateKind::UserInterface ? QStringLiteral("ui")
                                                                : QStringLiteral("gameplay");
    context.priority = kind == ProjectTemplateKind::UserInterface ? 100 : 0;
    if (kind == ProjectTemplateKind::Empty) {
        return {};
    }
    if (kind == ProjectTemplateKind::Platformer2D) {
        context.actions = {
            inputValue(QStringLiteral("Jump"), {binding(QStringLiteral("Key.Space")),
                                                binding(QStringLiteral("Gamepad.A"))}),
            inputValue(QStringLiteral("Pause"), {binding(QStringLiteral("Key.Escape"))})};
        context.axes = {
            inputValue(QStringLiteral("MoveX"),
                       {binding(QStringLiteral("Key.A"), -1.0), binding(QStringLiteral("Key.D")),
                        binding(QStringLiteral("Gamepad.LeftX"), 1.0, 0.15)})};
    } else if (kind == ProjectTemplateKind::TopDown || kind == ProjectTemplateKind::ThirdPerson) {
        context.actions = {
            inputValue(QStringLiteral("Fire"),
                       {binding(QStringLiteral("Mouse.Left")),
                        binding(QStringLiteral("Gamepad.RightTrigger"), 1.0, 0.15)}),
            inputValue(QStringLiteral("Dash"), {binding(QStringLiteral("Key.Shift"))})};
        context.axes = {
            inputValue(QStringLiteral("MoveX"),
                       {binding(QStringLiteral("Key.A"), -1.0), binding(QStringLiteral("Key.D")),
                        binding(QStringLiteral("Gamepad.LeftX"), 1.0, 0.15)}),
            inputValue(QStringLiteral("MoveY"),
                       {binding(QStringLiteral("Key.S"), -1.0), binding(QStringLiteral("Key.W")),
                        binding(QStringLiteral("Gamepad.LeftY"), 1.0, 0.15)})};
    } else if (kind == ProjectTemplateKind::RaycastFps) {
        context.actions = {
            inputValue(QStringLiteral("Fire"), {binding(QStringLiteral("Mouse.Left"))}),
            inputValue(QStringLiteral("Use"), {binding(QStringLiteral("Key.E"))})};
        context.axes = {
            inputValue(QStringLiteral("LookX"),
                       {binding(QStringLiteral("Mouse.X"), 0.75, 0.0),
                        binding(QStringLiteral("Gamepad.RightX"), 1.0, 0.15)}),
            inputValue(QStringLiteral("MoveY"),
                       {binding(QStringLiteral("Key.S"), -1.0), binding(QStringLiteral("Key.W")),
                        binding(QStringLiteral("Gamepad.LeftY"), 1.0, 0.15)})};
    } else if (kind == ProjectTemplateKind::Pseudo3DRacer) {
        context.actions = {
            inputValue(QStringLiteral("Brake"), {binding(QStringLiteral("Key.Space"))}),
            inputValue(QStringLiteral("ResetVehicle"), {binding(QStringLiteral("Key.R"))})};
        context.axes = {
            inputValue(QStringLiteral("Steer"),
                       {binding(QStringLiteral("Key.A"), -1.0), binding(QStringLiteral("Key.D")),
                        binding(QStringLiteral("Gamepad.LeftX"), 1.0, 0.15)}),
            inputValue(QStringLiteral("Throttle"),
                       {binding(QStringLiteral("Key.S"), -1.0), binding(QStringLiteral("Key.W")),
                        binding(QStringLiteral("Gamepad.RightTrigger"), 1.0, 0.15)})};
    } else if (kind == ProjectTemplateKind::UserInterface) {
        context.actions = {
            inputValue(QStringLiteral("Accept"), {binding(QStringLiteral("Key.Enter")),
                                                  binding(QStringLiteral("Gamepad.A"))}),
            inputValue(QStringLiteral("Cancel"), {binding(QStringLiteral("Key.Escape")),
                                                  binding(QStringLiteral("Gamepad.B"))})};
        context.axes = {inputValue(QStringLiteral("NavigateX"),
                                   {binding(QStringLiteral("Key.Left"), -1.0),
                                    binding(QStringLiteral("Key.Right")),
                                    binding(QStringLiteral("Gamepad.LeftX"), 1.0, 0.2)}),
                        inputValue(QStringLiteral("NavigateY"),
                                   {binding(QStringLiteral("Key.Down"), -1.0),
                                    binding(QStringLiteral("Key.Up")),
                                    binding(QStringLiteral("Gamepad.LeftY"), 1.0, 0.2)})};
    }
    return {context};
}

bool addComponent(SceneDocument& document, fabgl::Entity& entity, const QString& type,
                  QString& errorMessage) {
    return document.addBuiltinComponent(entity.id(), type, errorMessage);
}

fabgl::Entity* createEntity(SceneDocument& document, const QString& name, QString& errorMessage) {
    auto created = document.scene().createEntity(name.toStdString());
    if (!created) {
        errorMessage = errorText(created.error());
        return nullptr;
    }
    return created.value();
}

bool configureTemplateScene(SceneDocument& document, const ProjectTemplateKind kind,
                            QString& errorMessage) {
    document.createDefault(ProjectTemplateCreator::displayName(kind));
    const auto entities = document.scene().entities();
    if (entities.size() < 2U) {
        errorMessage = QObject::tr("Could not initialize the template scene.");
        return false;
    }
    auto* camera = entities[0];
    auto* player = entities[1];
    camera->setName("Main Camera");
    player->setName(kind == ProjectTemplateKind::UserInterface ? "UI Root" : "Player");
    if (!addComponent(document, *camera, QStringLiteral("Camera"), errorMessage)) {
        return false;
    }
    switch (kind) {
    case ProjectTemplateKind::Empty:
        player->setName("Root");
        return true;
    case ProjectTemplateKind::Platformer2D: {
        if (!addComponent(document, *player, QStringLiteral("CharacterBody2D"), errorMessage) ||
            !addComponent(document, *player, QStringLiteral("Collider2D"), errorMessage) ||
            !addComponent(document, *player, QStringLiteral("Health"), errorMessage)) {
            return false;
        }
        auto* ground = createEntity(document, QStringLiteral("Ground"), errorMessage);
        return ground != nullptr &&
               addComponent(document, *ground, QStringLiteral("Collider2D"), errorMessage);
    }
    case ProjectTemplateKind::TopDown: {
        if (!addComponent(document, *player, QStringLiteral("CharacterBody2D"), errorMessage) ||
            !addComponent(document, *player, QStringLiteral("Health"), errorMessage)) {
            return false;
        }
        auto* enemy = createEntity(document, QStringLiteral("Enemy"), errorMessage);
        return enemy != nullptr &&
               addComponent(document, *enemy, QStringLiteral("DamageReceiver"), errorMessage);
    }
    case ProjectTemplateKind::RaycastFps: {
        if (!addComponent(document, *player, QStringLiteral("FirstPersonController"),
                          errorMessage)) {
            return false;
        }
        auto* map = createEntity(document, QStringLiteral("Raycast World"), errorMessage);
        return map != nullptr &&
               addComponent(document, *map, QStringLiteral("RaycastMap"), errorMessage);
    }
    case ProjectTemplateKind::Pseudo3DRacer:
        return addComponent(document, *player, QStringLiteral("VehicleController"), errorMessage);
    case ProjectTemplateKind::ThirdPerson: {
        if (!addComponent(document, *player, QStringLiteral("ThirdPersonController"),
                          errorMessage)) {
            return false;
        }
        auto* light = createEntity(document, QStringLiteral("Key Light"), errorMessage);
        return light != nullptr &&
               addComponent(document, *light, QStringLiteral("Light"), errorMessage);
    }
    case ProjectTemplateKind::UserInterface: {
        if (!addComponent(document, *player, QStringLiteral("UITransform"), errorMessage)) {
            return false;
        }
        auto* button = createEntity(document, QStringLiteral("Start Button"), errorMessage);
        if (button == nullptr ||
            !addComponent(document, *button, QStringLiteral("UITransform"), errorMessage) ||
            !addComponent(document, *button, QStringLiteral("UIButton"), errorMessage) ||
            !addComponent(document, *button, QStringLiteral("UIText"), errorMessage)) {
            return false;
        }
        const auto parented = document.scene().setParent(button->id(), player->id());
        if (!parented) {
            errorMessage = errorText(parented.error());
            return false;
        }
        return true;
    }
    }
    errorMessage = QObject::tr("Unknown project template.");
    return false;
}

bool writeText(const QString& path, const QByteArray& bytes, QString& errorMessage) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        errorMessage = QObject::tr("Cannot atomically write %1: %2")
                           .arg(QDir::toNativeSeparators(path), file.errorString());
        return false;
    }
    return true;
}

void removeStagingDirectory(const QString& stagingPath, const QString& parentPath) {
    const QFileInfo staging(stagingPath);
    if (staging.absolutePath() != QFileInfo(parentPath).absoluteFilePath() ||
        !staging.fileName().startsWith(QStringLiteral(".fabglstudio-create-"))) {
        return;
    }
    QDir directory(staging.absoluteFilePath());
    if (directory.exists()) {
        (void)directory.removeRecursively();
    }
}

} // namespace

QString ProjectTemplateCreator::displayName(const ProjectTemplateKind kind) {
    switch (kind) {
    case ProjectTemplateKind::Empty:
        return QObject::tr("Empty");
    case ProjectTemplateKind::Platformer2D:
        return QObject::tr("2D Platformer");
    case ProjectTemplateKind::TopDown:
        return QObject::tr("Top-Down");
    case ProjectTemplateKind::RaycastFps:
        return QObject::tr("Raycast FPS");
    case ProjectTemplateKind::Pseudo3DRacer:
        return QObject::tr("Pseudo-3D Racer");
    case ProjectTemplateKind::ThirdPerson:
        return QObject::tr("TPS (Experimental)");
    case ProjectTemplateKind::UserInterface:
        return QObject::tr("UI");
    }
    return QObject::tr("Unknown");
}

bool ProjectTemplateCreator::create(const ProjectCreationRequest& request, QString& projectFilePath,
                                    QString& errorMessage) {
    projectFilePath.clear();
    errorMessage.clear();
    if (!validProjectName(request.name, errorMessage) || !validStableProfileId(request.pcProfile) ||
        !validStableProfileId(request.esp32Profile)) {
        if (errorMessage.isEmpty()) {
            errorMessage = QObject::tr("Target profile IDs must be stable lowercase IDs.");
        }
        return false;
    }
    const QFileInfo parentInfo(request.parentDirectory);
    if (!parentInfo.exists() || !parentInfo.isDir() || parentInfo.isSymLink()) {
        errorMessage = QObject::tr("Project parent directory does not exist or is a link.");
        return false;
    }
    const auto parentPath = parentInfo.absoluteFilePath();
    const auto targetPath = QFileInfo(QDir(parentPath).filePath(request.name)).absoluteFilePath();
    const auto relativeTarget = QDir(parentPath).relativeFilePath(targetPath);
    if (relativeTarget == QStringLiteral("..") ||
        relativeTarget.startsWith(QStringLiteral("../")) || QFileInfo(targetPath).exists()) {
        errorMessage = QFileInfo(targetPath).exists()
                           ? QObject::tr("Project destination already exists: %1")
                                 .arg(QDir::toNativeSeparators(targetPath))
                           : QObject::tr("Project destination escapes the selected root.");
        return false;
    }
    const auto stagingName =
        QStringLiteral(".fabglstudio-create-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    const auto stagingPath = QDir(parentPath).filePath(stagingName);
    if (!QDir(parentPath).mkdir(stagingName)) {
        errorMessage = QObject::tr("Cannot create project staging directory.");
        return false;
    }
    const auto rollback = [&]() { removeStagingDirectory(stagingPath, parentPath); };
    const QStringList folders = {QStringLiteral("Assets"), QStringLiteral("Scenes"),
                                 QStringLiteral("Scripts"), QStringLiteral("Packages")};
    for (const auto& folder : folders) {
        if (!QDir(stagingPath).mkpath(folder)) {
            errorMessage = QObject::tr("Cannot create template folder %1.").arg(folder);
            rollback();
            return false;
        }
    }
    if (request.projectTemplate == ProjectTemplateKind::RaycastFps &&
        !QDir(stagingPath).mkpath(QStringLiteral("Maps"))) {
        errorMessage = QObject::tr("Cannot create the raycast map folder.");
        rollback();
        return false;
    }
    if (request.projectTemplate == ProjectTemplateKind::Pseudo3DRacer &&
        !QDir(stagingPath).mkpath(QStringLiteral("Tracks"))) {
        errorMessage = QObject::tr("Cannot create the racer track folder.");
        rollback();
        return false;
    }
    const auto gameplayFiles = fabgl::project::writeGameplayScript(
        utf8Path(stagingPath), gameplayClassName(request.projectTemplate).toStdString());
    if (!gameplayFiles) {
        errorMessage = errorText(gameplayFiles.error());
        rollback();
        return false;
    }
    SceneDocument scene;
    if (!configureTemplateScene(scene, request.projectTemplate, errorMessage) ||
        !scene.saveAs(QDir(stagingPath).filePath(QStringLiteral("Scenes/Main.fglscene")),
                      errorMessage)) {
        rollback();
        return false;
    }
    ProjectData data;
    data.projectGuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    data.name = request.name;
    data.previewDemo = previewDemo(request.projectTemplate);
    data.inputContexts = templateInput(request.projectTemplate);
    data.targetProfiles.pc = request.pcProfile;
    data.targetProfiles.esp32 = request.esp32Profile;
    const auto manifestName = request.name + QStringLiteral(".fglproject");
    const auto stagingManifest = QDir(stagingPath).filePath(manifestName);
    if (!ProjectDocument::save(stagingManifest, data, errorMessage)) {
        rollback();
        return false;
    }
    const auto readme = QObject::tr("# %1\n\nCreated from the FabGL Studio %2 template.\n\n"
                                    "PC profile: `%3`  \nESP32 profile: `%4`\n")
                            .arg(request.name, displayName(request.projectTemplate),
                                 request.pcProfile, request.esp32Profile)
                            .toUtf8();
    if (!writeText(QDir(stagingPath).filePath(QStringLiteral("README.md")), readme, errorMessage)) {
        rollback();
        return false;
    }
    ProjectData validatedProject;
    SceneDocument validatedScene;
    if (!ProjectDocument::load(stagingManifest, validatedProject, errorMessage) ||
        validatedProject.sourceFormatVersion != ProjectDocument::FormatVersion ||
        !validatedScene.load(ProjectDocument::absoluteScenePath(stagingManifest, validatedProject),
                             errorMessage)) {
        if (errorMessage.isEmpty()) {
            errorMessage = QObject::tr("Generated project did not validate as format v2.");
        }
        rollback();
        return false;
    }
    if (!QDir(parentPath).rename(stagingName, request.name)) {
        errorMessage = QObject::tr("Cannot atomically publish the staged project directory.");
        rollback();
        return false;
    }
    projectFilePath = QDir(targetPath).filePath(manifestName);
    return true;
}

ProjectCreationDialog::ProjectCreationDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("projectCreationDialog"));
    setWindowTitle(tr("Create FabGL Studio Project"));
    resize(560, 360);
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    m_nameEdit = new QLineEdit(QStringLiteral("NewGame"), this);
    m_nameEdit->setObjectName(QStringLiteral("projectCreationNameEdit"));
    m_parentEdit = new QLineEdit(QDir::homePath(), this);
    m_parentEdit->setObjectName(QStringLiteral("projectCreationRootEdit"));
    auto* rootRow = new QWidget(this);
    auto* rootLayout = new QHBoxLayout(rootRow);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->addWidget(m_parentEdit, 1);
    auto* browseButton = new QPushButton(tr("Browse..."), rootRow);
    browseButton->setObjectName(QStringLiteral("projectCreationBrowseButton"));
    rootLayout->addWidget(browseButton);
    m_templateCombo = new QComboBox(this);
    m_templateCombo->setObjectName(QStringLiteral("projectCreationTemplateCombo"));
    for (const auto kind : {ProjectTemplateKind::Empty, ProjectTemplateKind::Platformer2D,
                            ProjectTemplateKind::TopDown, ProjectTemplateKind::RaycastFps,
                            ProjectTemplateKind::Pseudo3DRacer, ProjectTemplateKind::ThirdPerson,
                            ProjectTemplateKind::UserInterface}) {
        m_templateCombo->addItem(ProjectTemplateCreator::displayName(kind), static_cast<int>(kind));
    }
    m_pcProfileCombo = new QComboBox(this);
    m_pcProfileCombo->setObjectName(QStringLiteral("projectCreationPcProfileCombo"));
    m_pcProfileCombo->setEditable(true);
    m_pcProfileCombo->addItem(QStringLiteral("pc.default"));
    m_esp32ProfileCombo = new QComboBox(this);
    m_esp32ProfileCombo->setObjectName(QStringLiteral("projectCreationEsp32ProfileCombo"));
    m_esp32ProfileCombo->setEditable(true);
    m_esp32ProfileCombo->addItem(QStringLiteral("olimex-esp32-sbc-fabgl-revb"));
    form->addRow(tr("Project name"), m_nameEdit);
    form->addRow(tr("Parent directory"), rootRow);
    form->addRow(tr("Template"), m_templateCombo);
    form->addRow(tr("PC profile"), m_pcProfileCombo);
    form->addRow(tr("ESP32 profile"), m_esp32ProfileCombo);
    layout->addLayout(form);
    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setObjectName(QStringLiteral("projectCreationSummaryLabel"));
    m_summaryLabel->setWordWrap(true);
    layout->addWidget(m_summaryLabel);
    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(QStringLiteral("projectCreationErrorLabel"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet(QStringLiteral("color: #c43b3b;"));
    layout->addWidget(m_errorLabel);
    layout->addStretch(1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_createButton = buttons->addButton(tr("Create Project"), QDialogButtonBox::AcceptRole);
    m_createButton->setObjectName(QStringLiteral("projectCreationCreateButton"));
    layout->addWidget(buttons);
    connect(browseButton, &QPushButton::clicked, this,
            &ProjectCreationDialog::browseForParentDirectory);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_createButton, &QPushButton::clicked, this, &ProjectCreationDialog::createProject);
    connect(m_nameEdit, &QLineEdit::textChanged, this, &ProjectCreationDialog::refreshSummary);
    connect(m_parentEdit, &QLineEdit::textChanged, this, &ProjectCreationDialog::refreshSummary);
    connect(m_templateCombo, &QComboBox::currentIndexChanged, this,
            &ProjectCreationDialog::refreshSummary);
    refreshSummary();
}

void ProjectCreationDialog::setInitialParentDirectory(const QString& directory) {
    m_parentEdit->setText(QFileInfo(directory).absoluteFilePath());
}

ProjectCreationRequest ProjectCreationDialog::request() const {
    return {m_nameEdit->text().trimmed(), m_parentEdit->text().trimmed(),
            static_cast<ProjectTemplateKind>(m_templateCombo->currentData().toInt()),
            m_pcProfileCombo->currentText().trimmed(),
            m_esp32ProfileCombo->currentText().trimmed()};
}

QString ProjectCreationDialog::createdProjectPath() const {
    return m_createdProjectPath;
}

void ProjectCreationDialog::browseForParentDirectory() {
    const auto directory = QFileDialog::getExistingDirectory(
        this, tr("Choose Project Parent Directory"), m_parentEdit->text());
    if (!directory.isEmpty()) {
        m_parentEdit->setText(directory);
    }
}

void ProjectCreationDialog::createProject() {
    QString errorMessage;
    if (!ProjectTemplateCreator::create(request(), m_createdProjectPath, errorMessage)) {
        m_errorLabel->setText(errorMessage);
        return;
    }
    m_errorLabel->clear();
    emit projectCreated(m_createdProjectPath);
    accept();
}

void ProjectCreationDialog::refreshSummary() {
    const auto selected = request();
    m_summaryLabel->setText(
        tr("Creates %1 with a canonical v2 manifest, scene, input defaults and C++ build glue at "
           "%2.")
            .arg(ProjectTemplateCreator::displayName(selected.projectTemplate),
                 QDir::toNativeSeparators(QDir(selected.parentDirectory).filePath(selected.name))));
}

} // namespace fgl::studio
