#pragma once

#include "BuildRunner.h"

#include <QString>
#include <QStringList>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

namespace fgl::studio {

class ToolchainSetupWidget final : public QWidget {
    Q_OBJECT

  public:
    explicit ToolchainSetupWidget(QWidget* parent = nullptr);

    void setRepositoryRoot(const QString& repositoryRoot);
    void setCommandExecutionEnabled(bool enabled) noexcept;
    void refreshStatus();
    [[nodiscard]] QString repositoryRoot() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] QString commandPreview() const;
    [[nodiscard]] QString commandProgram() const;
    [[nodiscard]] QStringList commandArguments(bool repair = false) const;
    [[nodiscard]] bool selectedProfileInstalled() const noexcept;

  signals:
    void commandPrepared(const QString& program, const QStringList& arguments,
                         const QString& workingDirectory);
    void statusMessage(const QString& message);

  private:
    enum class Profile { Desktop, Esp32 };

    [[nodiscard]] Profile selectedProfile() const;
    [[nodiscard]] QString manifestPath(Profile profile) const;
    [[nodiscard]] QString scriptPath(Profile profile) const;
    [[nodiscard]] bool commandReady(QString& reason) const;
    void browseOfflineDirectory();
    void refreshCommandPreview();
    void start(bool repair);
    void setRunning(bool running);

    QString m_repositoryRoot;
    QString m_statusText;
    QStringList m_missing;
    bool m_selectedInstalled = false;
    bool m_commandExecutionEnabled = true;
    BuildRunner* m_runner = nullptr;
    QComboBox* m_profileCombo = nullptr;
    QLabel* m_versionLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_fallbackLabel = nullptr;
    QCheckBox* m_offlineCheck = nullptr;
    QLineEdit* m_offlineDirectoryEdit = nullptr;
    QPushButton* m_browseOfflineButton = nullptr;
    QLineEdit* m_commandPreviewEdit = nullptr;
    QPushButton* m_refreshButton = nullptr;
    QPushButton* m_installButton = nullptr;
    QPushButton* m_repairButton = nullptr;
    QPushButton* m_cancelButton = nullptr;
    QProgressBar* m_progress = nullptr;
    QPlainTextEdit* m_log = nullptr;
};

} // namespace fgl::studio
