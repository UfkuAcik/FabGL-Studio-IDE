#pragma once

#include <QDialog>
#include <QString>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace fgl::studio {

enum class ProjectTemplateKind {
    Empty,
    Platformer2D,
    TopDown,
    RaycastFps,
    Pseudo3DRacer,
    ThirdPerson,
    UserInterface,
};

struct ProjectCreationRequest final {
    QString name;
    QString parentDirectory;
    ProjectTemplateKind projectTemplate = ProjectTemplateKind::Empty;
    QString pcProfile = QStringLiteral("pc.default");
    QString esp32Profile = QStringLiteral("olimex-esp32-sbc-fabgl-revb");
};

class ProjectTemplateCreator final {
  public:
    [[nodiscard]] static bool create(const ProjectCreationRequest& request,
                                     QString& projectFilePath, QString& errorMessage);
    [[nodiscard]] static QString displayName(ProjectTemplateKind kind);

  private:
    ProjectTemplateCreator() = delete;
};

class ProjectCreationDialog final : public QDialog {
    Q_OBJECT

  public:
    explicit ProjectCreationDialog(QWidget* parent = nullptr);

    void setInitialParentDirectory(const QString& directory);
    [[nodiscard]] ProjectCreationRequest request() const;
    [[nodiscard]] QString createdProjectPath() const;

  signals:
    void projectCreated(const QString& projectFilePath);

  private:
    void browseForParentDirectory();
    void createProject();
    void refreshSummary();

    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_parentEdit = nullptr;
    QComboBox* m_templateCombo = nullptr;
    QComboBox* m_pcProfileCombo = nullptr;
    QComboBox* m_esp32ProfileCombo = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QLabel* m_errorLabel = nullptr;
    QPushButton* m_createButton = nullptr;
    QString m_createdProjectPath;
};

} // namespace fgl::studio
