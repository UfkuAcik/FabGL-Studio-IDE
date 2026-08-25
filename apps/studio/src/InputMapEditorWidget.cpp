#include "InputMapEditorWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>

namespace fgl::studio {
namespace {

constexpr int KindRole = Qt::UserRole;
constexpr int ValueIndexRole = Qt::UserRole + 1;
constexpr int BindingIndexRole = Qt::UserRole + 2;

class InputStateCommand final : public QUndoCommand {
  public:
    InputStateCommand(QString description, std::function<void()> undo, std::function<void()> redo)
        : QUndoCommand(std::move(description)), m_undo(std::move(undo)), m_redo(std::move(redo)) {}

    void undo() override;
    void redo() override;

  private:
    std::function<void()> m_undo;
    std::function<void()> m_redo;
    bool m_firstRedo = true;
};

QString kindText(const bool axis) {
    return axis ? QObject::tr("Axis") : QObject::tr("Action");
}

QString contextKey(const QString& name) {
    return name.trimmed();
}

} // namespace

InputMapEditorWidget::InputMapEditorWidget(QWidget* parent)
    : QWidget(parent), m_undoStack(new QUndoStack(this)) {
    setObjectName(QStringLiteral("inputMapEditorWidget"));
    buildUi();
    connectUi();
    refreshUi();
}

void InputMapEditorWidget::setProjectContext(const QString& projectFilePath,
                                             const ProjectData& project) {
    const auto absolutePath = projectFilePath.trimmed().isEmpty()
                                  ? QString{}
                                  : QFileInfo(projectFilePath).absoluteFilePath();
    const bool sameProject = m_hasProject && absolutePath == m_projectFilePath &&
                             project.projectGuid == m_data.projectGuid;
    if (sameProject && m_dirty) {
        ProjectData merged = project;
        merged.inputContexts = m_data.inputContexts;
        m_data = std::move(merged);
    } else {
        m_data = project;
        m_dirty = false;
        m_undoStack->clear();
    }
    m_projectFilePath = absolutePath;
    m_hasProject = !m_projectFilePath.isEmpty();
    refreshUi();
}

ProjectData InputMapEditorWidget::projectData() const {
    return m_data;
}

QString InputMapEditorWidget::projectFilePath() const {
    return m_projectFilePath;
}

bool InputMapEditorWidget::hasValidationErrors() const noexcept {
    return m_hasValidationErrors;
}

QString InputMapEditorWidget::validationText() const {
    return m_validationText;
}

bool InputMapEditorWidget::save(QString& errorMessage) {
    validate();
    if (!m_hasProject) {
        errorMessage = tr("Open a project before saving its input map.");
        return false;
    }
    if (m_hasValidationErrors) {
        errorMessage = m_validationText;
        return false;
    }
    ProjectData latest;
    if (!ProjectDocument::load(m_projectFilePath, latest, errorMessage)) {
        return false;
    }
    latest.inputContexts = m_data.inputContexts;
    if (!ProjectDocument::save(m_projectFilePath, latest, errorMessage)) {
        return false;
    }
    m_data = std::move(latest);
    m_dirty = false;
    m_undoStack->setClean();
    emit projectSaved(m_data);
    emit statusMessage(tr("Saved input map atomically to the project manifest."));
    refreshUi();
    return true;
}

void InputMapEditorWidget::buildUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);

    auto* contextGroup = new QGroupBox(tr("Input Context"), this);
    auto* contextLayout = new QFormLayout(contextGroup);
    m_contextCombo = new QComboBox(contextGroup);
    m_contextCombo->setObjectName(QStringLiteral("inputContextCombo"));
    m_contextNameEdit = new QLineEdit(contextGroup);
    m_contextNameEdit->setObjectName(QStringLiteral("inputContextNameEdit"));
    m_contextPrioritySpin = new QSpinBox(contextGroup);
    m_contextPrioritySpin->setObjectName(QStringLiteral("inputContextPrioritySpin"));
    m_contextPrioritySpin->setRange(-1000000, 1000000);
    m_contextEnabledCheck = new QCheckBox(tr("Enabled"), contextGroup);
    m_contextEnabledCheck->setObjectName(QStringLiteral("inputContextEnabledCheck"));
    m_contextEnabledCheck->setChecked(true);
    contextLayout->addRow(tr("Current"), m_contextCombo);
    contextLayout->addRow(tr("Name"), m_contextNameEdit);
    contextLayout->addRow(tr("Priority"), m_contextPrioritySpin);
    contextLayout->addRow(QString{}, m_contextEnabledCheck);
    auto* contextButtons = new QWidget(contextGroup);
    auto* contextButtonsLayout = new QHBoxLayout(contextButtons);
    contextButtonsLayout->setContentsMargins(0, 0, 0, 0);
    m_addContextButton = new QPushButton(tr("Add Context"), contextButtons);
    m_addContextButton->setObjectName(QStringLiteral("inputAddContextButton"));
    m_applyContextButton = new QPushButton(tr("Apply Context"), contextButtons);
    m_applyContextButton->setObjectName(QStringLiteral("inputApplyContextButton"));
    m_removeContextButton = new QPushButton(tr("Remove Context"), contextButtons);
    m_removeContextButton->setObjectName(QStringLiteral("inputRemoveContextButton"));
    contextButtonsLayout->addWidget(m_addContextButton);
    contextButtonsLayout->addWidget(m_applyContextButton);
    contextButtonsLayout->addWidget(m_removeContextButton);
    contextLayout->addRow(contextButtons);
    layout->addWidget(contextGroup);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("inputMapTree"));
    m_tree->setHeaderLabels({tr("Kind"), tr("Name / Control"), tr("Scale"), tr("Dead zone")});
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_tree, 1);

    auto* valueGroup = new QGroupBox(tr("Action or Axis"), this);
    auto* valueLayout = new QHBoxLayout(valueGroup);
    m_valueKindCombo = new QComboBox(valueGroup);
    m_valueKindCombo->setObjectName(QStringLiteral("inputValueKindCombo"));
    m_valueKindCombo->addItem(tr("Action"), static_cast<int>(ValueKind::Action));
    m_valueKindCombo->addItem(tr("Axis"), static_cast<int>(ValueKind::Axis));
    m_valueNameEdit = new QLineEdit(valueGroup);
    m_valueNameEdit->setObjectName(QStringLiteral("inputValueNameEdit"));
    m_valueNameEdit->setPlaceholderText(tr("Jump, MoveX, Accept..."));
    m_addValueButton = new QPushButton(tr("Add"), valueGroup);
    m_addValueButton->setObjectName(QStringLiteral("inputAddValueButton"));
    valueLayout->addWidget(m_valueKindCombo);
    valueLayout->addWidget(m_valueNameEdit, 1);
    valueLayout->addWidget(m_addValueButton);
    layout->addWidget(valueGroup);

    auto* bindingGroup = new QGroupBox(tr("Binding"), this);
    auto* bindingLayout = new QFormLayout(bindingGroup);
    m_deviceCombo = new QComboBox(bindingGroup);
    m_deviceCombo->setObjectName(QStringLiteral("inputBindingDeviceCombo"));
    m_deviceCombo->addItem(tr("Keyboard"), QStringLiteral("Key"));
    m_deviceCombo->addItem(tr("Mouse"), QStringLiteral("Mouse"));
    m_deviceCombo->addItem(tr("Gamepad"), QStringLiteral("Gamepad"));
    m_deviceCombo->addItem(tr("Raw / platform-specific"), QString{});
    m_controlEdit = new QLineEdit(bindingGroup);
    m_controlEdit->setObjectName(QStringLiteral("inputBindingControlEdit"));
    m_controlEdit->setPlaceholderText(tr("Space, Left, A, LeftX..."));
    m_scaleSpin = new QDoubleSpinBox(bindingGroup);
    m_scaleSpin->setObjectName(QStringLiteral("inputBindingScaleSpin"));
    m_scaleSpin->setRange(-16.0, 16.0);
    m_scaleSpin->setDecimals(4);
    m_scaleSpin->setValue(1.0);
    m_deadzoneSpin = new QDoubleSpinBox(bindingGroup);
    m_deadzoneSpin->setObjectName(QStringLiteral("inputBindingDeadzoneSpin"));
    m_deadzoneSpin->setRange(0.0, 1.0);
    m_deadzoneSpin->setDecimals(4);
    m_deadzoneSpin->setValue(0.5);
    bindingLayout->addRow(tr("Device"), m_deviceCombo);
    bindingLayout->addRow(tr("Control"), m_controlEdit);
    bindingLayout->addRow(tr("Scale"), m_scaleSpin);
    bindingLayout->addRow(tr("Dead zone / threshold"), m_deadzoneSpin);
    auto* bindingButtons = new QWidget(bindingGroup);
    auto* bindingButtonsLayout = new QHBoxLayout(bindingButtons);
    bindingButtonsLayout->setContentsMargins(0, 0, 0, 0);
    m_addBindingButton = new QPushButton(tr("Add Binding"), bindingButtons);
    m_addBindingButton->setObjectName(QStringLiteral("inputAddBindingButton"));
    m_applyBindingButton = new QPushButton(tr("Apply Rebind"), bindingButtons);
    m_applyBindingButton->setObjectName(QStringLiteral("inputApplyBindingButton"));
    m_removeSelectedButton = new QPushButton(tr("Remove Selected"), bindingButtons);
    m_removeSelectedButton->setObjectName(QStringLiteral("inputRemoveSelectedButton"));
    bindingButtonsLayout->addWidget(m_addBindingButton);
    bindingButtonsLayout->addWidget(m_applyBindingButton);
    bindingButtonsLayout->addWidget(m_removeSelectedButton);
    bindingLayout->addRow(bindingButtons);
    layout->addWidget(bindingGroup);

    m_validationLabel = new QLabel(this);
    m_validationLabel->setObjectName(QStringLiteral("inputValidationLabel"));
    m_validationLabel->setWordWrap(true);
    layout->addWidget(m_validationLabel);
    auto* footer = new QHBoxLayout();
    m_undoButton = new QPushButton(tr("Undo"), this);
    m_undoButton->setObjectName(QStringLiteral("inputUndoButton"));
    m_redoButton = new QPushButton(tr("Redo"), this);
    m_redoButton->setObjectName(QStringLiteral("inputRedoButton"));
    m_saveButton = new QPushButton(tr("Save Input Map"), this);
    m_saveButton->setObjectName(QStringLiteral("inputSaveButton"));
    footer->addWidget(m_undoButton);
    footer->addWidget(m_redoButton);
    footer->addStretch(1);
    footer->addWidget(m_saveButton);
    layout->addLayout(footer);
}

void InputMapEditorWidget::connectUi() {
    connect(m_contextCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        refreshContextEditor();
        refreshTree();
    });
    connect(m_addContextButton, &QPushButton::clicked, this, [this]() {
        const auto name = contextKey(m_contextNameEdit->text());
        runEdit(tr("Add input context"), [this, name](ProjectData& project) {
            project.inputContexts.push_back(
                {name, m_contextPrioritySpin->value(), m_contextEnabledCheck->isChecked(), {}, {}});
        });
        m_contextCombo->setCurrentIndex(m_contextCombo->count() - 1);
    });
    connect(m_applyContextButton, &QPushButton::clicked, this, [this]() {
        const auto index = currentContextIndex();
        const auto name = contextKey(m_contextNameEdit->text());
        runEdit(tr("Edit input context"), [this, index, name](ProjectData& project) {
            if (index < 0 || index >= project.inputContexts.size()) {
                return;
            }
            auto& context = project.inputContexts[index];
            context.name = name;
            context.priority = m_contextPrioritySpin->value();
            context.enabled = m_contextEnabledCheck->isChecked();
        });
    });
    connect(m_removeContextButton, &QPushButton::clicked, this, [this]() {
        const auto index = currentContextIndex();
        runEdit(tr("Remove input context"), [index](ProjectData& project) {
            if (index >= 0 && index < project.inputContexts.size()) {
                project.inputContexts.removeAt(index);
            }
        });
    });
    connect(m_addValueButton, &QPushButton::clicked, this, [this]() {
        const auto contextIndex = currentContextIndex();
        const auto kind = static_cast<ValueKind>(m_valueKindCombo->currentData().toInt());
        const auto name = m_valueNameEdit->text().trimmed();
        runEdit(kind == ValueKind::Axis ? tr("Add input axis") : tr("Add input action"),
                [contextIndex, kind, name](ProjectData& project) {
                    if (contextIndex < 0 || contextIndex >= project.inputContexts.size()) {
                        return;
                    }
                    auto& values = kind == ValueKind::Axis
                                       ? project.inputContexts[contextIndex].axes
                                       : project.inputContexts[contextIndex].actions;
                    values.push_back({name, {}});
                });
        if (m_tree->topLevelItemCount() > 0) {
            m_tree->setCurrentItem(m_tree->topLevelItem(m_tree->topLevelItemCount() - 1));
        }
    });
    connect(m_tree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current, QTreeWidgetItem*) { refreshBindingEditor(current); });
    connect(m_addBindingButton, &QPushButton::clicked, this, [this]() {
        const auto contextIndex = currentContextIndex();
        const auto [kind, valueIndex] = selectedValue();
        const ProjectInputBinding value{bindingControlFromUi(), m_scaleSpin->value(),
                                        m_deadzoneSpin->value()};
        runEdit(tr("Add input binding"), [contextIndex, kind, valueIndex,
                                          value](ProjectData& project) {
            if (contextIndex < 0 || contextIndex >= project.inputContexts.size()) {
                return;
            }
            auto& values = kind == ValueKind::Axis ? project.inputContexts[contextIndex].axes
                                                   : project.inputContexts[contextIndex].actions;
            if (valueIndex >= 0 && valueIndex < values.size()) {
                values[valueIndex].bindings.push_back(value);
            }
        });
    });
    connect(m_applyBindingButton, &QPushButton::clicked, this, [this]() {
        auto* current = m_tree->currentItem();
        if (current == nullptr || current->parent() == nullptr) {
            return;
        }
        const auto contextIndex = currentContextIndex();
        const auto [kind, valueIndex] = selectedValue();
        const auto bindingIndex = current->data(0, BindingIndexRole).toInt();
        const ProjectInputBinding value{bindingControlFromUi(), m_scaleSpin->value(),
                                        m_deadzoneSpin->value()};
        runEdit(tr("Rebind input"), [contextIndex, kind, valueIndex, bindingIndex,
                                     value](ProjectData& project) {
            if (contextIndex < 0 || contextIndex >= project.inputContexts.size()) {
                return;
            }
            auto& values = kind == ValueKind::Axis ? project.inputContexts[contextIndex].axes
                                                   : project.inputContexts[contextIndex].actions;
            if (valueIndex >= 0 && valueIndex < values.size() && bindingIndex >= 0 &&
                bindingIndex < values[valueIndex].bindings.size()) {
                values[valueIndex].bindings[bindingIndex] = value;
            }
        });
    });
    connect(m_removeSelectedButton, &QPushButton::clicked, this, [this]() {
        auto* current = m_tree->currentItem();
        if (current == nullptr) {
            return;
        }
        const auto contextIndex = currentContextIndex();
        const auto [kind, valueIndex] = selectedValue();
        const bool binding = current->parent() != nullptr;
        const auto bindingIndex = current->data(0, BindingIndexRole).toInt();
        runEdit(binding ? tr("Remove input binding") : tr("Remove input value"),
                [contextIndex, kind, valueIndex, binding, bindingIndex](ProjectData& project) {
                    if (contextIndex < 0 || contextIndex >= project.inputContexts.size()) {
                        return;
                    }
                    auto& values = kind == ValueKind::Axis
                                       ? project.inputContexts[contextIndex].axes
                                       : project.inputContexts[contextIndex].actions;
                    if (valueIndex < 0 || valueIndex >= values.size()) {
                        return;
                    }
                    if (binding && bindingIndex >= 0 &&
                        bindingIndex < values[valueIndex].bindings.size()) {
                        values[valueIndex].bindings.removeAt(bindingIndex);
                    } else if (!binding) {
                        values.removeAt(valueIndex);
                    }
                });
    });
    connect(m_saveButton, &QPushButton::clicked, this, [this]() {
        QString error;
        if (!save(error)) {
            emit statusMessage(tr("Input map save failed: %1").arg(error));
        }
    });
    connect(m_undoButton, &QPushButton::clicked, m_undoStack, &QUndoStack::undo);
    connect(m_redoButton, &QPushButton::clicked, m_undoStack, &QUndoStack::redo);
    connect(m_undoStack, &QUndoStack::canUndoChanged, m_undoButton, &QWidget::setEnabled);
    connect(m_undoStack, &QUndoStack::canRedoChanged, m_redoButton, &QWidget::setEnabled);
}

void InputMapEditorWidget::refreshUi() {
    m_refreshing = true;
    const auto oldContext = m_contextCombo->currentText();
    {
        const QSignalBlocker blocker(m_contextCombo);
        m_contextCombo->clear();
        for (const auto& context : m_data.inputContexts) {
            m_contextCombo->addItem(context.name);
        }
        const auto oldIndex = m_contextCombo->findText(oldContext);
        m_contextCombo->setCurrentIndex(oldIndex >= 0 ? oldIndex
                                                      : (m_contextCombo->count() > 0 ? 0 : -1));
    }
    m_refreshing = false;
    refreshContextEditor();
    refreshTree();
    validate();
    const bool contextSelected = currentContextIndex() >= 0;
    m_contextCombo->setEnabled(m_hasProject);
    m_addContextButton->setEnabled(m_hasProject);
    m_applyContextButton->setEnabled(m_hasProject && contextSelected);
    m_removeContextButton->setEnabled(m_hasProject && contextSelected);
    m_addValueButton->setEnabled(m_hasProject && contextSelected);
    m_saveButton->setEnabled(m_hasProject && !m_hasValidationErrors);
    m_undoButton->setEnabled(m_undoStack->canUndo());
    m_redoButton->setEnabled(m_undoStack->canRedo());
}

void InputMapEditorWidget::refreshContextEditor() {
    if (m_refreshing) {
        return;
    }
    const auto index = currentContextIndex();
    if (index >= 0 && index < m_data.inputContexts.size()) {
        const auto& context = m_data.inputContexts[index];
        m_contextNameEdit->setText(context.name);
        m_contextPrioritySpin->setValue(context.priority);
        m_contextEnabledCheck->setChecked(context.enabled);
    } else {
        m_contextNameEdit->clear();
        m_contextPrioritySpin->setValue(0);
        m_contextEnabledCheck->setChecked(true);
    }
}

void InputMapEditorWidget::refreshTree() {
    m_tree->clear();
    const auto contextIndex = currentContextIndex();
    if (contextIndex < 0 || contextIndex >= m_data.inputContexts.size()) {
        return;
    }
    const auto appendValues = [this](const QVector<ProjectInputValue>& values,
                                     const ValueKind kind) {
        for (qsizetype valueIndex = 0; valueIndex < values.size(); ++valueIndex) {
            const auto& value = values[valueIndex];
            auto* valueItem = new QTreeWidgetItem(
                m_tree, {kindText(kind == ValueKind::Axis), value.name, {}, {}});
            valueItem->setData(0, KindRole, static_cast<int>(kind));
            valueItem->setData(0, ValueIndexRole, valueIndex);
            valueItem->setData(0, BindingIndexRole, -1);
            for (qsizetype bindingIndex = 0; bindingIndex < value.bindings.size(); ++bindingIndex) {
                const auto& inputBinding = value.bindings[bindingIndex];
                auto* bindingItem = new QTreeWidgetItem(
                    valueItem, {tr("Binding"), inputBinding.control,
                                QString::number(inputBinding.scale, 'g', 8),
                                QString::number(inputBinding.threshold, 'g', 8)});
                bindingItem->setData(0, KindRole, static_cast<int>(kind));
                bindingItem->setData(0, ValueIndexRole, valueIndex);
                bindingItem->setData(0, BindingIndexRole, bindingIndex);
            }
            valueItem->setExpanded(true);
        }
    };
    const auto& context = m_data.inputContexts[contextIndex];
    appendValues(context.actions, ValueKind::Action);
    appendValues(context.axes, ValueKind::Axis);
}

void InputMapEditorWidget::refreshBindingEditor(QTreeWidgetItem* item) {
    const bool bindingSelected = item != nullptr && item->parent() != nullptr;
    m_applyBindingButton->setEnabled(bindingSelected);
    m_removeSelectedButton->setEnabled(item != nullptr);
    m_addBindingButton->setEnabled(item != nullptr);
    if (!bindingSelected) {
        return;
    }
    const auto contextIndex = currentContextIndex();
    const auto kind = static_cast<ValueKind>(item->data(0, KindRole).toInt());
    const auto valueIndex = item->data(0, ValueIndexRole).toInt();
    const auto bindingIndex = item->data(0, BindingIndexRole).toInt();
    if (contextIndex < 0 || contextIndex >= m_data.inputContexts.size()) {
        return;
    }
    const auto& values = kind == ValueKind::Axis ? m_data.inputContexts[contextIndex].axes
                                                 : m_data.inputContexts[contextIndex].actions;
    if (valueIndex < 0 || valueIndex >= values.size() || bindingIndex < 0 ||
        bindingIndex >= values[valueIndex].bindings.size()) {
        return;
    }
    const auto& value = values[valueIndex].bindings[bindingIndex];
    const auto separator = value.control.indexOf(QLatin1Char('.'));
    const auto prefix = separator > 0 ? value.control.left(separator) : QString{};
    const auto deviceIndex = m_deviceCombo->findData(prefix);
    m_deviceCombo->setCurrentIndex(deviceIndex >= 0 ? deviceIndex : m_deviceCombo->count() - 1);
    m_controlEdit->setText(deviceIndex >= 0 && separator > 0 ? value.control.mid(separator + 1)
                                                             : value.control);
    m_scaleSpin->setValue(value.scale);
    m_deadzoneSpin->setValue(value.threshold);
}

void InputMapEditorWidget::validate() {
    QStringList issues;
    std::set<QString> contextNames;
    for (const auto& context : m_data.inputContexts) {
        const auto name = contextKey(context.name);
        if (name.isEmpty()) {
            issues.push_back(tr("A context has an empty name."));
        } else if (!contextNames.insert(name).second) {
            issues.push_back(tr("Duplicate context '%1'.").arg(name));
        }
        std::map<QString, QString> controls;
        const auto inspectValues = [&issues, &controls,
                                    &context](const QVector<ProjectInputValue>& values,
                                              const QString& category) {
            std::set<QString> names;
            for (const auto& value : values) {
                const auto valueName = value.name.trimmed();
                if (valueName.isEmpty()) {
                    issues.push_back(QObject::tr("%1 contains an unnamed value.").arg(category));
                } else if (!names.insert(valueName).second) {
                    issues.push_back(QObject::tr("Duplicate %1 '%2' in context '%3'.")
                                         .arg(category, valueName, context.name));
                }
                if (value.bindings.isEmpty()) {
                    issues.push_back(
                        QObject::tr("%1 '%2' has no binding.").arg(category, valueName));
                }
                std::set<QString> localControls;
                for (const auto& inputBinding : value.bindings) {
                    const auto control = inputBinding.control.trimmed();
                    if (control.isEmpty() || !std::isfinite(inputBinding.scale) ||
                        std::abs(inputBinding.scale) > 16.0 ||
                        !std::isfinite(inputBinding.threshold) || inputBinding.threshold < 0.0 ||
                        inputBinding.threshold > 1.0) {
                        issues.push_back(QObject::tr("Binding on '%1' has invalid control, scale, "
                                                     "or dead zone.")
                                             .arg(valueName));
                        continue;
                    }
                    if (!localControls.insert(control).second) {
                        issues.push_back(
                            QObject::tr("Duplicate binding '%1' on '%2'.").arg(control, valueName));
                    }
                    const auto previous = controls.find(control);
                    if (previous != controls.end() && previous->second != valueName) {
                        issues.push_back(
                            QObject::tr("Binding conflict: '%1' is assigned to '%2' "
                                        "and '%3' in context '%4'.")
                                .arg(control, previous->second, valueName, context.name));
                    } else {
                        controls.emplace(control, valueName);
                    }
                }
            }
        };
        inspectValues(context.actions, tr("action"));
        inspectValues(context.axes, tr("axis"));
    }
    issues.removeDuplicates();
    m_hasValidationErrors = !issues.isEmpty();
    m_validationText = m_hasValidationErrors
                           ? issues.join(QLatin1Char('\n'))
                           : tr("Input map is valid. Keyboard, mouse, gamepad and raw controls are "
                                "stored in manifest v2.");
    m_validationLabel->setText(m_validationText);
    m_validationLabel->setStyleSheet(m_hasValidationErrors ? QStringLiteral("color: #c43b3b;")
                                                           : QStringLiteral("color: #2f8f46;"));
    m_saveButton->setEnabled(m_hasProject && !m_hasValidationErrors);
}

void InputMapEditorWidget::applySnapshot(const ProjectData& project) {
    m_data = project;
    m_dirty = true;
    refreshUi();
    emit projectDataChanged(m_data);
}

void InputMapEditorWidget::runEdit(const QString& description,
                                   const std::function<void(ProjectData&)>& edit) {
    if (!m_hasProject) {
        emit statusMessage(tr("Open a project before editing its input map."));
        return;
    }
    auto after = m_data;
    edit(after);
    if (after.inputContexts == m_data.inputContexts) {
        return;
    }
    const auto before = m_data;
    applySnapshot(after);
    QPointer<InputMapEditorWidget> editor(this);
    m_undoStack->push(new InputStateCommand(
        description,
        [editor, before]() {
            if (editor != nullptr) {
                editor->applySnapshot(before);
            }
        },
        [editor, after]() {
            if (editor != nullptr) {
                editor->applySnapshot(after);
            }
        }));
}

int InputMapEditorWidget::currentContextIndex() const {
    return m_contextCombo->currentIndex();
}

std::pair<InputMapEditorWidget::ValueKind, int> InputMapEditorWidget::selectedValue() const {
    auto* item = m_tree->currentItem();
    if (item == nullptr) {
        return {ValueKind::Action, -1};
    }
    return {static_cast<ValueKind>(item->data(0, KindRole).toInt()),
            item->data(0, ValueIndexRole).toInt()};
}

QString InputMapEditorWidget::bindingControlFromUi() const {
    const auto control = m_controlEdit->text().trimmed();
    const auto prefix = m_deviceCombo->currentData().toString();
    return prefix.isEmpty() || control.startsWith(prefix + QLatin1Char('.'))
               ? control
               : prefix + QLatin1Char('.') + control;
}

void InputStateCommand::undo() {
    m_undo();
}

void InputStateCommand::redo() {
    if (m_firstRedo) {
        m_firstRedo = false;
        return;
    }
    m_redo();
}

} // namespace fgl::studio
