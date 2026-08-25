#pragma once

#include "ProjectDocument.h"

#include <QString>
#include <QWidget>

#include <functional>
#include <utility>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTreeWidget;
class QTreeWidgetItem;
class QUndoStack;

namespace fgl::studio {

class InputMapEditorWidget final : public QWidget {
    Q_OBJECT

  public:
    explicit InputMapEditorWidget(QWidget* parent = nullptr);

    void setProjectContext(const QString& projectFilePath, const ProjectData& project);
    [[nodiscard]] ProjectData projectData() const;
    [[nodiscard]] QString projectFilePath() const;
    [[nodiscard]] bool hasValidationErrors() const noexcept;
    [[nodiscard]] QString validationText() const;
    [[nodiscard]] bool save(QString& errorMessage);

  signals:
    void projectDataChanged(const ProjectData& data);
    void projectSaved(const ProjectData& data);
    void statusMessage(const QString& message);

  private:
    enum class ValueKind { Action, Axis };

    void buildUi();
    void connectUi();
    void refreshUi();
    void refreshContextEditor();
    void refreshTree();
    void refreshBindingEditor(QTreeWidgetItem* item);
    void validate();
    void applySnapshot(const ProjectData& project);
    void runEdit(const QString& description, const std::function<void(ProjectData&)>& edit);
    [[nodiscard]] int currentContextIndex() const;
    [[nodiscard]] std::pair<ValueKind, int> selectedValue() const;
    [[nodiscard]] QString bindingControlFromUi() const;

    QString m_projectFilePath;
    ProjectData m_data;
    bool m_hasProject = false;
    bool m_dirty = false;
    bool m_refreshing = false;
    bool m_hasValidationErrors = false;
    QString m_validationText;
    QUndoStack* m_undoStack = nullptr;

    QComboBox* m_contextCombo = nullptr;
    QLineEdit* m_contextNameEdit = nullptr;
    QSpinBox* m_contextPrioritySpin = nullptr;
    QCheckBox* m_contextEnabledCheck = nullptr;
    QPushButton* m_addContextButton = nullptr;
    QPushButton* m_applyContextButton = nullptr;
    QPushButton* m_removeContextButton = nullptr;
    QTreeWidget* m_tree = nullptr;
    QComboBox* m_valueKindCombo = nullptr;
    QLineEdit* m_valueNameEdit = nullptr;
    QPushButton* m_addValueButton = nullptr;
    QComboBox* m_deviceCombo = nullptr;
    QLineEdit* m_controlEdit = nullptr;
    QDoubleSpinBox* m_scaleSpin = nullptr;
    QDoubleSpinBox* m_deadzoneSpin = nullptr;
    QPushButton* m_addBindingButton = nullptr;
    QPushButton* m_applyBindingButton = nullptr;
    QPushButton* m_removeSelectedButton = nullptr;
    QPushButton* m_saveButton = nullptr;
    QPushButton* m_undoButton = nullptr;
    QPushButton* m_redoButton = nullptr;
    QLabel* m_validationLabel = nullptr;
};

} // namespace fgl::studio
