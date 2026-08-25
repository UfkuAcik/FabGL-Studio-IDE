#include "ToolchainSetupWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QTextCursor>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>

namespace fgl::studio {
namespace {

QString displayArgument(QString value) {
    if (value.isEmpty() || value.contains(QLatin1Char(' ')) || value.contains(QLatin1Char('\t')) ||
        value.contains(QLatin1Char('"'))) {
        value.replace(QLatin1Char('"'), QStringLiteral("\\\""));
        return QLatin1Char('"') + value + QLatin1Char('"');
    }
    return value;
}

std::optional<QJsonObject> readObject(const QString& path, QString& errorMessage) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        errorMessage = QObject::tr("Cannot read %1: %2")
                           .arg(QDir::toNativeSeparators(path), file.errorString());
        return std::nullopt;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        errorMessage = QObject::tr("Invalid JSON in %1: %2")
                           .arg(QDir::toNativeSeparators(path), parseError.errorString());
        return std::nullopt;
    }
    return document.object();
}

void appendMissing(QStringList& missing, const QString& root, const QString& relativePath) {
    const auto path = QDir(root).filePath(QDir::fromNativeSeparators(relativePath));
    if (!QFileInfo::exists(path)) {
        missing.push_back(QDir::toNativeSeparators(path));
    }
}

} // namespace

ToolchainSetupWidget::ToolchainSetupWidget(QWidget* parent)
    : QWidget(parent), m_runner(new BuildRunner(this)) {
    setObjectName(QStringLiteral("toolchainSetupWidget"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    auto* form = new QFormLayout();
    m_profileCombo = new QComboBox(this);
    m_profileCombo->setObjectName(QStringLiteral("toolchainProfileCombo"));
    m_profileCombo->addItem(tr("Managed Desktop — Qt 6.8.3 / MinGW 13.1"),
                            static_cast<int>(Profile::Desktop));
    m_profileCombo->addItem(tr("ESP32 — Olimex ESP32-SBC-FabGL Rev B"),
                            static_cast<int>(Profile::Esp32));
    m_versionLabel = new QLabel(this);
    m_versionLabel->setObjectName(QStringLiteral("toolchainVersionLabel"));
    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("toolchainStatusLabel"));
    m_statusLabel->setWordWrap(true);
    form->addRow(tr("Profile"), m_profileCombo);
    form->addRow(tr("Locked version"), m_versionLabel);
    form->addRow(tr("Status"), m_statusLabel);
    layout->addLayout(form);

    m_fallbackLabel = new QLabel(this);
    m_fallbackLabel->setObjectName(QStringLiteral("toolchainFallbackLabel"));
    m_fallbackLabel->setWordWrap(true);
    layout->addWidget(m_fallbackLabel);
    auto* offlineRow = new QHBoxLayout();
    m_offlineCheck = new QCheckBox(tr("Use approved offline package directory"), this);
    m_offlineCheck->setObjectName(QStringLiteral("toolchainOfflineCheck"));
    m_offlineDirectoryEdit = new QLineEdit(this);
    m_offlineDirectoryEdit->setObjectName(QStringLiteral("toolchainOfflineDirectoryEdit"));
    m_browseOfflineButton = new QPushButton(tr("Browse..."), this);
    m_browseOfflineButton->setObjectName(QStringLiteral("toolchainOfflineBrowseButton"));
    offlineRow->addWidget(m_offlineCheck);
    offlineRow->addWidget(m_offlineDirectoryEdit, 1);
    offlineRow->addWidget(m_browseOfflineButton);
    layout->addLayout(offlineRow);

    m_commandPreviewEdit = new QLineEdit(this);
    m_commandPreviewEdit->setObjectName(QStringLiteral("toolchainCommandPreviewEdit"));
    m_commandPreviewEdit->setReadOnly(true);
    layout->addWidget(new QLabel(tr("Command preview (arguments are passed separately)"), this));
    layout->addWidget(m_commandPreviewEdit);
    auto* buttons = new QHBoxLayout();
    m_refreshButton = new QPushButton(tr("Refresh Status"), this);
    m_refreshButton->setObjectName(QStringLiteral("toolchainRefreshButton"));
    m_installButton = new QPushButton(tr("Install"), this);
    m_installButton->setObjectName(QStringLiteral("toolchainInstallButton"));
    m_repairButton = new QPushButton(tr("Repair"), this);
    m_repairButton->setObjectName(QStringLiteral("toolchainRepairButton"));
    m_cancelButton = new QPushButton(tr("Cancel"), this);
    m_cancelButton->setObjectName(QStringLiteral("toolchainCancelButton"));
    buttons->addWidget(m_refreshButton);
    buttons->addWidget(m_installButton);
    buttons->addWidget(m_repairButton);
    buttons->addWidget(m_cancelButton);
    buttons->addStretch(1);
    layout->addLayout(buttons);
    m_progress = new QProgressBar(this);
    m_progress->setObjectName(QStringLiteral("toolchainProgressBar"));
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    layout->addWidget(m_progress);
    m_log = new QPlainTextEdit(this);
    m_log->setObjectName(QStringLiteral("toolchainLog"));
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(4000);
    layout->addWidget(m_log, 1);

    connect(m_profileCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { refreshStatus(); });
    connect(m_refreshButton, &QPushButton::clicked, this, &ToolchainSetupWidget::refreshStatus);
    connect(m_browseOfflineButton, &QPushButton::clicked, this,
            &ToolchainSetupWidget::browseOfflineDirectory);
    connect(m_offlineCheck, &QCheckBox::toggled, this, [this](bool) { refreshCommandPreview(); });
    connect(m_offlineDirectoryEdit, &QLineEdit::textChanged, this,
            [this](const QString&) { refreshCommandPreview(); });
    connect(m_installButton, &QPushButton::clicked, this, [this]() { start(false); });
    connect(m_repairButton, &QPushButton::clicked, this, [this]() { start(true); });
    connect(m_cancelButton, &QPushButton::clicked, m_runner, &BuildRunner::stopBuild);
    connect(m_runner, &BuildRunner::outputReady, this, [this](const QString& text, bool) {
        m_log->moveCursor(QTextCursor::End);
        m_log->insertPlainText(text);
    });
    connect(m_runner, &BuildRunner::runningChanged, this, &ToolchainSetupWidget::setRunning);
    connect(m_runner, &BuildRunner::buildFinished, this,
            [this](const int exitCode, const QProcess::ExitStatus exitStatus) {
                const bool succeeded = exitStatus == QProcess::NormalExit && exitCode == 0;
                emit statusMessage(succeeded ? tr("Toolchain operation completed successfully.")
                                             : tr("Toolchain operation failed; inspect the log."));
                refreshStatus();
            });
    setRunning(false);
}

void ToolchainSetupWidget::setRepositoryRoot(const QString& repositoryRoot) {
    m_repositoryRoot = repositoryRoot.trimmed().isEmpty()
                           ? QString{}
                           : QFileInfo(repositoryRoot).absoluteFilePath();
    refreshStatus();
}

void ToolchainSetupWidget::setCommandExecutionEnabled(const bool enabled) noexcept {
    m_commandExecutionEnabled = enabled;
}

void ToolchainSetupWidget::refreshStatus() {
    m_missing.clear();
    m_selectedInstalled = false;
    if (m_repositoryRoot.isEmpty() || !QFileInfo(m_repositoryRoot).isDir()) {
        m_statusText = tr("Repository root is unavailable. Install and repair are disabled.");
        m_versionLabel->setText(tr("Unavailable"));
        m_statusLabel->setText(m_statusText);
        m_fallbackLabel->setText(
            tr("Use a complete FabGL Studio installation or portable package."));
        refreshCommandPreview();
        return;
    }
    QString parseError;
    const auto profile = selectedProfile();
    const auto manifest = readObject(manifestPath(profile), parseError);
    if (!manifest) {
        m_statusText = parseError;
        m_versionLabel->setText(tr("Manifest missing or invalid"));
        m_statusLabel->setText(m_statusText);
        m_fallbackLabel->setText(tr("Bootstrap cannot run without its locked manifest."));
        refreshCommandPreview();
        return;
    }
    if (profile == Profile::Desktop) {
        const auto qt = manifest->value(QStringLiteral("qt")).toObject();
        const auto compiler = manifest->value(QStringLiteral("compiler")).toObject();
        m_versionLabel->setText(tr("Qt %1 / MinGW %2")
                                    .arg(qt.value(QStringLiteral("version")).toString(),
                                         compiler.value(QStringLiteral("version")).toString()));
        const auto sdkRoot =
            QDir(m_repositoryRoot).filePath(qt.value(QStringLiteral("installRoot")).toString());
        const auto qtRoot =
            QDir(sdkRoot).filePath(qt.value(QStringLiteral("sdkDirectory")).toString());
        for (const auto& value : qt.value(QStringLiteral("requiredFiles")).toArray()) {
            appendMissing(m_missing, qtRoot, value.toString());
        }
        const auto compilerRoot =
            QDir(sdkRoot).filePath(compiler.value(QStringLiteral("directory")).toString());
        appendMissing(m_missing, compilerRoot, compiler.value(QStringLiteral("cxx")).toString());
        appendMissing(m_missing, compilerRoot, compiler.value(QStringLiteral("make")).toString());
        appendMissing(m_missing, m_repositoryRoot, QStringLiteral(".toolchains/python-packages"));
    } else {
        const auto profileObject = manifest->value(QStringLiteral("profile")).toObject();
        const auto coreVersion = profileObject.value(QStringLiteral("arduinoCore"))
                                     .toObject()
                                     .value(QStringLiteral("version"))
                                     .toString();
        const auto fabglVersion = profileObject.value(QStringLiteral("fabgl"))
                                      .toObject()
                                      .value(QStringLiteral("distributionVersion"))
                                      .toString();
        m_versionLabel->setText(tr("Arduino-ESP32 %1 / FabGL %2").arg(coreVersion, fabglVersion));
        const auto installRoot = QDir(m_repositoryRoot)
                                     .filePath(manifest->value(QStringLiteral("storage"))
                                                   .toObject()
                                                   .value(QStringLiteral("installDirectory"))
                                                   .toString());
        for (const auto& value : manifest->value(QStringLiteral("artifacts")).toArray()) {
            const auto artifact = value.toObject();
            appendMissing(m_missing, installRoot,
                          QDir(artifact.value(QStringLiteral("installDirectory")).toString())
                              .filePath(QStringLiteral(".fabglstudio-install.json")));
        }
        appendMissing(
            m_missing, installRoot,
            QStringLiteral("arduino-data/packages/esp32/hardware/esp32/%1").arg(coreVersion));
        appendMissing(m_missing, installRoot, QStringLiteral("bootstrap-result.json"));
    }
    if (!QFileInfo::exists(scriptPath(profile))) {
        m_missing.push_back(QDir::toNativeSeparators(scriptPath(profile)));
    }
    m_selectedInstalled = m_missing.isEmpty();
    m_statusText =
        m_selectedInstalled
            ? tr("Managed toolchain is installed and required locked files are present.")
            : tr("Managed toolchain is incomplete (%1 missing item(s)).").arg(m_missing.size());
    m_statusLabel->setText(m_statusText);
    if (m_selectedInstalled) {
        m_fallbackLabel->setText(
            tr("Repair revalidates and atomically replaces mismatched managed files."));
    } else {
        const auto visible = m_missing.mid(0, 4).join(QStringLiteral("\n"));
        m_fallbackLabel->setText(
            tr("Missing files:\n%1%2")
                .arg(visible, m_missing.size() > 4 ? tr("\n…and %1 more").arg(m_missing.size() - 4)
                                                   : QString{}));
    }
    refreshCommandPreview();
}

QString ToolchainSetupWidget::repositoryRoot() const {
    return m_repositoryRoot;
}

QString ToolchainSetupWidget::statusText() const {
    return m_statusText;
}

QString ToolchainSetupWidget::commandPreview() const {
    return m_commandPreviewEdit->text();
}

QString ToolchainSetupWidget::commandProgram() const {
    const auto pwsh = QStandardPaths::findExecutable(QStringLiteral("pwsh"));
    if (!pwsh.isEmpty()) {
        return pwsh;
    }
    const auto powershell = QStandardPaths::findExecutable(QStringLiteral("powershell"));
    return powershell.isEmpty() ? QStringLiteral("powershell.exe") : powershell;
}

QStringList ToolchainSetupWidget::commandArguments(const bool repair) const {
    QStringList arguments = {QStringLiteral("-NoProfile"),   QStringLiteral("-ExecutionPolicy"),
                             QStringLiteral("Bypass"),       QStringLiteral("-File"),
                             scriptPath(selectedProfile()),  QStringLiteral("-ManifestPath"),
                             manifestPath(selectedProfile())};
    if (selectedProfile() == Profile::Esp32 && m_offlineCheck->isChecked()) {
        arguments << QStringLiteral("-OfflineSourceDirectory")
                  << QFileInfo(m_offlineDirectoryEdit->text()).absoluteFilePath();
    }
    if (repair) {
        arguments << QStringLiteral("-Force");
    }
    return arguments;
}

bool ToolchainSetupWidget::selectedProfileInstalled() const noexcept {
    return m_selectedInstalled;
}

ToolchainSetupWidget::Profile ToolchainSetupWidget::selectedProfile() const {
    return static_cast<Profile>(m_profileCombo->currentData().toInt());
}

QString ToolchainSetupWidget::manifestPath(const Profile profile) const {
    return QDir(m_repositoryRoot)
        .filePath(profile == Profile::Desktop ? QStringLiteral("toolchains/desktop-manifest.json")
                                              : QStringLiteral("toolchains/manifest.json"));
}

QString ToolchainSetupWidget::scriptPath(const Profile profile) const {
    return QDir(m_repositoryRoot)
        .filePath(profile == Profile::Desktop ? QStringLiteral("scripts/bootstrap_desktop.ps1")
                                              : QStringLiteral("scripts/bootstrap_toolchain.ps1"));
}

bool ToolchainSetupWidget::commandReady(QString& reason) const {
    if (m_runner->isRunning()) {
        reason = tr("A toolchain operation is already running.");
        return false;
    }
    if (!QFileInfo::exists(manifestPath(selectedProfile())) ||
        !QFileInfo::exists(scriptPath(selectedProfile()))) {
        reason = tr("The selected bootstrap script or manifest is missing.");
        return false;
    }
    if (selectedProfile() == Profile::Esp32 && m_offlineCheck->isChecked() &&
        !QFileInfo(m_offlineDirectoryEdit->text()).isDir()) {
        reason = tr("The selected offline package directory is missing.");
        return false;
    }
    if (commandProgram().isEmpty()) {
        reason = tr("PowerShell is unavailable.");
        return false;
    }
    return true;
}

void ToolchainSetupWidget::browseOfflineDirectory() {
    const auto directory = QFileDialog::getExistingDirectory(
        this, tr("Choose Approved Offline Package Directory"), m_offlineDirectoryEdit->text());
    if (!directory.isEmpty()) {
        m_offlineDirectoryEdit->setText(directory);
    }
}

void ToolchainSetupWidget::refreshCommandPreview() {
    const bool esp32 = selectedProfile() == Profile::Esp32;
    m_offlineCheck->setEnabled(esp32 && !m_runner->isRunning());
    m_offlineDirectoryEdit->setEnabled(esp32 && m_offlineCheck->isChecked() &&
                                       !m_runner->isRunning());
    m_browseOfflineButton->setEnabled(esp32 && m_offlineCheck->isChecked() &&
                                      !m_runner->isRunning());
    QStringList display = {displayArgument(commandProgram())};
    for (const auto& argument : commandArguments(false)) {
        display.push_back(displayArgument(argument));
    }
    m_commandPreviewEdit->setText(display.join(QLatin1Char(' ')));
    QString reason;
    const bool ready = commandReady(reason);
    m_installButton->setEnabled(ready);
    m_repairButton->setEnabled(ready);
    if (!ready && !reason.isEmpty() && !m_runner->isRunning()) {
        m_fallbackLabel->setText(reason);
    }
}

void ToolchainSetupWidget::start(const bool repair) {
    QString reason;
    if (!commandReady(reason)) {
        emit statusMessage(reason);
        m_fallbackLabel->setText(reason);
        return;
    }
    const auto program = commandProgram();
    const auto arguments = commandArguments(repair);
    m_log->appendPlainText(
        tr("Prepared %1 operation for %2.")
            .arg(repair ? tr("repair") : tr("install"), m_profileCombo->currentText()));
    emit commandPrepared(program, arguments, m_repositoryRoot);
    if (m_commandExecutionEnabled) {
        m_runner->startBuild(program, arguments, m_repositoryRoot);
    } else {
        emit statusMessage(tr("Toolchain command prepared; execution is disabled."));
    }
}

void ToolchainSetupWidget::setRunning(const bool running) {
    m_profileCombo->setEnabled(!running);
    m_refreshButton->setEnabled(!running);
    m_cancelButton->setEnabled(running);
    m_progress->setRange(0, running ? 0 : 100);
    if (!running) {
        m_progress->setValue(0);
    }
    refreshCommandPreview();
}

} // namespace fgl::studio
