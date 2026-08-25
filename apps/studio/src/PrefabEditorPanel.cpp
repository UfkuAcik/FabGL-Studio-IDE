#include "PrefabEditorPanel.h"

#include <fabgl/scene/builtin_components.h>
#include <fabgl/scene/entity.h>
#include <fabgl/scene/transform_component.h>
#include <fabgl/serialization/prefab_instance_serializer.h>
#include <fabgl/serialization/prefab_serializer.h>

#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <set>
#include <string>
#include <type_traits>
#include <utility>

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

class AppliedCallbackCommand final : public QUndoCommand {
  public:
    AppliedCallbackCommand(QString description, std::function<void()> undo,
                           std::function<void()> redo)
        : QUndoCommand(std::move(description)), undo_(std::move(undo)), redo_(std::move(redo)) {}

    void undo() override {
        undo_();
    }

    void redo() override {
        if (firstRedo_) {
            firstRedo_ = false;
            return;
        }
        redo_();
    }

  private:
    std::function<void()> undo_;
    std::function<void()> redo_;
    bool firstRedo_ = true;
};

fabgl::PrefabComponentData componentData(const ComponentSnapshot& snapshot) {
    fabgl::PrefabComponentData component{snapshot.typeId, snapshot.typeName.toStdString(), {}};
    for (const auto& [name, value] : snapshot.properties) {
        component.properties.emplace(name, value);
    }
    return component;
}

fabgl::PrefabComponentData transformData(const EntitySnapshot& snapshot) {
    fabgl::PrefabComponentData component{
        fabgl::TransformComponent::staticTypeId(), "fabgl.Transform", {}};
    component.properties.emplace("localPosition", snapshot.position);
    component.properties.emplace(
        "localRotation",
        fabgl::EulerAngles{snapshot.rotation.x, snapshot.rotation.y, snapshot.rotation.z});
    component.properties.emplace("localScale", snapshot.scale);
    return component;
}

void applyTransformData(const fabgl::PrefabComponentData& component, EntitySnapshot& snapshot) {
    if (const auto position = component.properties.find("localPosition");
        position != component.properties.cend()) {
        if (const auto* value = std::get_if<fabgl::Vec3>(&position->second)) {
            snapshot.position = *value;
        }
    }
    if (const auto rotation = component.properties.find("localRotation");
        rotation != component.properties.cend()) {
        if (const auto* euler = std::get_if<fabgl::EulerAngles>(&rotation->second)) {
            snapshot.rotation = {euler->x, euler->y, euler->z};
        } else if (const auto* vector = std::get_if<fabgl::Vec3>(&rotation->second)) {
            snapshot.rotation = *vector;
        }
    }
    if (const auto scale = component.properties.find("localScale");
        scale != component.properties.cend()) {
        if (const auto* value = std::get_if<fabgl::Vec3>(&scale->second)) {
            snapshot.scale = *value;
        }
    }
}

ComponentSnapshot componentSnapshot(const fabgl::PrefabComponentData& component) {
    ComponentSnapshot snapshot{
        component.typeId, QString::fromStdString(component.typeName), true, {}};
    snapshot.properties.reserve(component.properties.size());
    for (const auto& [name, value] : component.properties) {
        snapshot.properties.emplace_back(name, value);
    }
    return snapshot;
}

QString propertyValueText(const fabgl::PropertyValue& value) {
    return std::visit(
        [](const auto& typed) -> QString {
            using Value = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<Value, bool>) {
                return typed ? QStringLiteral("true") : QStringLiteral("false");
            } else if constexpr (std::is_same_v<Value, std::int64_t> ||
                                 std::is_same_v<Value, std::uint64_t>) {
                return QString::number(typed);
            } else if constexpr (std::is_same_v<Value, double>) {
                return QString::number(typed, 'g', 15);
            } else if constexpr (std::is_same_v<Value, fabgl::Fixed>) {
                return QString::number(typed.toFloat(), 'g', 8);
            } else if constexpr (std::is_same_v<Value, std::string>) {
                return QString::fromStdString(typed);
            } else if constexpr (std::is_same_v<Value, fabgl::Vec2>) {
                return QStringLiteral("%1, %2").arg(typed.x).arg(typed.y);
            } else if constexpr (std::is_same_v<Value, fabgl::Vec3> ||
                                 std::is_same_v<Value, fabgl::EulerAngles>) {
                return QStringLiteral("%1, %2, %3").arg(typed.x).arg(typed.y).arg(typed.z);
            } else if constexpr (std::is_same_v<Value, fabgl::Quaternion>) {
                return QStringLiteral("%1, %2, %3, %4")
                    .arg(typed.x)
                    .arg(typed.y)
                    .arg(typed.z)
                    .arg(typed.w);
            } else if constexpr (std::is_same_v<Value, fabgl::Rect>) {
                return QStringLiteral("%1, %2, %3, %4")
                    .arg(typed.x)
                    .arg(typed.y)
                    .arg(typed.width)
                    .arg(typed.height);
            } else if constexpr (std::is_same_v<Value, fabgl::Color>) {
                return QStringLiteral("%1, %2, %3, %4")
                    .arg(static_cast<unsigned int>(typed.r))
                    .arg(static_cast<unsigned int>(typed.g))
                    .arg(static_cast<unsigned int>(typed.b))
                    .arg(static_cast<unsigned int>(typed.a));
            } else if constexpr (std::is_same_v<Value, fabgl::AssetGuid> ||
                                 std::is_same_v<Value, fabgl::EntityGuid>) {
                return QString::fromStdString(typed.toString());
            } else if constexpr (std::is_same_v<Value, fabgl::ComponentReference>) {
                return QStringLiteral("%1, %2").arg(
                    QString::fromStdString(typed.entity.toString()),
                    QString::fromStdString(typed.component.toString()));
            } else if constexpr (std::is_same_v<Value, fabgl::ActionReference> ||
                                 std::is_same_v<Value, fabgl::EventReference>) {
                return QString::fromStdString(typed.name);
            }
            return QStringLiteral("<structured value>");
        },
        value);
}

QStringList numericParts(const QString& text) {
    auto parts = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (auto& part : parts) {
        part = part.trimmed();
    }
    return parts;
}

std::optional<fabgl::PropertyValue> parsedPropertyValue(const fabgl::PropertyValue& prototype,
                                                        const QString& text,
                                                        QString& errorMessage) {
    const auto invalid = [&errorMessage]() -> std::optional<fabgl::PropertyValue> {
        errorMessage = QObject::tr("The override value does not match the property type.");
        return std::nullopt;
    };
    if (std::holds_alternative<bool>(prototype)) {
        const auto normalized = text.trimmed().toLower();
        if (normalized == QStringLiteral("true") || normalized == QStringLiteral("1")) {
            return fabgl::PropertyValue{true};
        }
        if (normalized == QStringLiteral("false") || normalized == QStringLiteral("0")) {
            return fabgl::PropertyValue{false};
        }
        return invalid();
    }
    if (std::holds_alternative<std::int64_t>(prototype)) {
        bool ok = false;
        const auto value = text.trimmed().toLongLong(&ok);
        return ok ? std::optional<fabgl::PropertyValue>(
                        fabgl::PropertyValue{static_cast<std::int64_t>(value)})
                  : invalid();
    }
    if (std::holds_alternative<std::uint64_t>(prototype)) {
        bool ok = false;
        const auto value = text.trimmed().toULongLong(&ok);
        return ok ? std::optional<fabgl::PropertyValue>(
                        fabgl::PropertyValue{static_cast<std::uint64_t>(value)})
                  : invalid();
    }
    if (std::holds_alternative<double>(prototype)) {
        bool ok = false;
        const auto value = text.trimmed().toDouble(&ok);
        return ok ? std::optional<fabgl::PropertyValue>(fabgl::PropertyValue{value}) : invalid();
    }
    if (std::holds_alternative<fabgl::Fixed>(prototype)) {
        bool ok = false;
        const auto value = text.trimmed().toFloat(&ok);
        return ok ? std::optional<fabgl::PropertyValue>(
                        fabgl::PropertyValue{fabgl::Fixed::fromFloat(value)})
                  : invalid();
    }
    if (std::holds_alternative<std::string>(prototype)) {
        return fabgl::PropertyValue{text.toStdString()};
    }
    const auto parts = numericParts(text);
    const auto number = [&parts](const qsizetype index, bool& ok) {
        if (index >= parts.size()) {
            ok = false;
            return 0.0F;
        }
        return parts[index].toFloat(&ok);
    };
    if (std::holds_alternative<fabgl::Vec2>(prototype) && parts.size() == 2) {
        bool xOk = false;
        bool yOk = false;
        const auto x = number(0, xOk);
        const auto y = number(1, yOk);
        return xOk && yOk
                   ? std::optional<fabgl::PropertyValue>(fabgl::PropertyValue{fabgl::Vec2{x, y}})
                   : invalid();
    }
    if ((std::holds_alternative<fabgl::Vec3>(prototype) ||
         std::holds_alternative<fabgl::EulerAngles>(prototype)) &&
        parts.size() == 3) {
        bool xOk = false;
        bool yOk = false;
        bool zOk = false;
        const auto x = number(0, xOk);
        const auto y = number(1, yOk);
        const auto z = number(2, zOk);
        if (!(xOk && yOk && zOk)) {
            return invalid();
        }
        if (std::holds_alternative<fabgl::EulerAngles>(prototype)) {
            return fabgl::PropertyValue{fabgl::EulerAngles{x, y, z}};
        }
        return fabgl::PropertyValue{fabgl::Vec3{x, y, z}};
    }
    if (std::holds_alternative<fabgl::Quaternion>(prototype) && parts.size() == 4) {
        bool xOk = false;
        bool yOk = false;
        bool zOk = false;
        bool wOk = false;
        const auto x = number(0, xOk);
        const auto y = number(1, yOk);
        const auto z = number(2, zOk);
        const auto w = number(3, wOk);
        return xOk && yOk && zOk && wOk ? std::optional<fabgl::PropertyValue>(
                                              fabgl::PropertyValue{fabgl::Quaternion{x, y, z, w}})
                                        : invalid();
    }
    if (std::holds_alternative<fabgl::Rect>(prototype) && parts.size() == 4) {
        bool xOk = false;
        bool yOk = false;
        bool widthOk = false;
        bool heightOk = false;
        const auto x = number(0, xOk);
        const auto y = number(1, yOk);
        const auto width = number(2, widthOk);
        const auto height = number(3, heightOk);
        return xOk && yOk && widthOk && heightOk
                   ? std::optional<fabgl::PropertyValue>(
                         fabgl::PropertyValue{fabgl::Rect{x, y, width, height}})
                   : invalid();
    }
    if (std::holds_alternative<fabgl::Color>(prototype) && parts.size() == 4) {
        std::array<std::uint8_t, 4> channels{};
        for (qsizetype index = 0; index < parts.size(); ++index) {
            bool ok = false;
            const auto channel = parts[index].toUInt(&ok);
            if (!ok || channel > std::numeric_limits<std::uint8_t>::max()) {
                return invalid();
            }
            channels[static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(channel);
        }
        return fabgl::PropertyValue{
            fabgl::Color{channels[0], channels[1], channels[2], channels[3]}};
    }
    if (std::holds_alternative<fabgl::AssetGuid>(prototype)) {
        const auto parsed = fabgl::AssetGuid::parse(text.trimmed().toStdString());
        return parsed ? std::optional<fabgl::PropertyValue>(fabgl::PropertyValue{parsed.value()})
                      : invalid();
    }
    if (std::holds_alternative<fabgl::EntityGuid>(prototype)) {
        const auto parsed = fabgl::EntityGuid::parse(text.trimmed().toStdString());
        return parsed ? std::optional<fabgl::PropertyValue>(fabgl::PropertyValue{parsed.value()})
                      : invalid();
    }
    if (std::holds_alternative<fabgl::ComponentReference>(prototype) && parts.size() == 2) {
        const auto entity = fabgl::EntityGuid::parse(parts[0].toStdString());
        const auto component = fabgl::ComponentTypeGuid::parse(parts[1].toStdString());
        return entity && component
                   ? std::optional<fabgl::PropertyValue>(fabgl::PropertyValue{
                         fabgl::ComponentReference{entity.value(), component.value()}})
                   : invalid();
    }
    if (std::holds_alternative<fabgl::ActionReference>(prototype)) {
        return fabgl::PropertyValue{fabgl::ActionReference{text.toStdString()}};
    }
    if (std::holds_alternative<fabgl::EventReference>(prototype)) {
        return fabgl::PropertyValue{fabgl::EventReference{text.toStdString()}};
    }
    errorMessage = QObject::tr("This structured property type is not editable as text.");
    return std::nullopt;
}

bool writeBytesAtomically(const QString& path, const QByteArray& bytes, QString& errorMessage) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        errorMessage = QObject::tr("Cannot create directory %1.")
                           .arg(QDir::toNativeSeparators(QFileInfo(path).absolutePath()));
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        errorMessage = QObject::tr("Cannot write %1: %2")
                           .arg(QDir::toNativeSeparators(path), file.errorString());
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        errorMessage = QObject::tr("Could not completely write %1: %2")
                           .arg(QDir::toNativeSeparators(path), file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        errorMessage = QObject::tr("Could not atomically replace %1: %2")
                           .arg(QDir::toNativeSeparators(path), file.errorString());
        return false;
    }
    return true;
}

} // namespace

PrefabEditorPanel::PrefabEditorPanel(SceneDocument* document, QUndoStack* undoStack,
                                     QWidget* parent)
    : QWidget(parent), m_document(document),
      m_undoStack(undoStack != nullptr ? undoStack : new QUndoStack(this)) {
    setObjectName(QStringLiteral("prefabEditorPanel"));
    buildUi();
    connectUi();
    refreshUi();
}

void PrefabEditorPanel::buildUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);

    auto* assetGroup = new QGroupBox(tr("Prefab Asset"), this);
    auto* assetLayout = new QFormLayout(assetGroup);
    m_pathEdit = new QLineEdit(QStringLiteral("Assets/NewPrefab.fglprefab"), assetGroup);
    m_pathEdit->setObjectName(QStringLiteral("prefabPathEdit"));
    m_nameEdit = new QLineEdit(QStringLiteral("New Prefab"), assetGroup);
    m_nameEdit->setObjectName(QStringLiteral("prefabNameEdit"));
    m_guidEdit = new QLineEdit(assetGroup);
    m_guidEdit->setObjectName(QStringLiteral("prefabGuidEdit"));
    m_guidEdit->setReadOnly(true);
    m_nestedBaseEdit = new QLineEdit(assetGroup);
    m_nestedBaseEdit->setObjectName(QStringLiteral("prefabNestedBaseEdit"));
    m_nestedBaseEdit->setPlaceholderText(tr("Optional base Asset GUID"));
    assetLayout->addRow(tr("Project path"), m_pathEdit);
    assetLayout->addRow(tr("Name"), m_nameEdit);
    assetLayout->addRow(tr("Asset GUID"), m_guidEdit);
    assetLayout->addRow(tr("Nested base"), m_nestedBaseEdit);
    auto* assetButtons = new QWidget(assetGroup);
    auto* assetButtonsLayout = new QHBoxLayout(assetButtons);
    assetButtonsLayout->setContentsMargins(0, 0, 0, 0);
    m_createButton = new QPushButton(tr("Create from Selection"), assetButtons);
    m_createButton->setObjectName(QStringLiteral("prefabCreateButton"));
    m_openButton = new QPushButton(tr("Open"), assetButtons);
    m_openButton->setObjectName(QStringLiteral("prefabOpenButton"));
    m_saveButton = new QPushButton(tr("Save"), assetButtons);
    m_saveButton->setObjectName(QStringLiteral("prefabSaveButton"));
    assetButtonsLayout->addWidget(m_createButton);
    assetButtonsLayout->addWidget(m_openButton);
    assetButtonsLayout->addWidget(m_saveButton);
    assetLayout->addRow(assetButtons);
    layout->addWidget(assetGroup);

    m_dependencyTree = new QTreeWidget(this);
    m_dependencyTree->setObjectName(QStringLiteral("prefabDependencyTree"));
    m_dependencyTree->setHeaderLabels({tr("Dependency"), tr("Asset GUID / path")});
    m_dependencyTree->header()->setStretchLastSection(true);
    m_dependencyTree->setMaximumHeight(130);
    layout->addWidget(m_dependencyTree);
    m_diagnosticLabel = new QLabel(this);
    m_diagnosticLabel->setObjectName(QStringLiteral("prefabDiagnosticStatus"));
    m_diagnosticLabel->setWordWrap(true);
    layout->addWidget(m_diagnosticLabel);

    m_instantiateButton = new QPushButton(tr("Instantiate in Scene"), this);
    m_instantiateButton->setObjectName(QStringLiteral("prefabInstantiateButton"));
    layout->addWidget(m_instantiateButton);
    m_instancesTree = new QTreeWidget(this);
    m_instancesTree->setObjectName(QStringLiteral("prefabInstancesTree"));
    m_instancesTree->setHeaderLabels({tr("Scene instance"), tr("State")});
    m_instancesTree->setMaximumHeight(150);
    layout->addWidget(m_instancesTree);

    auto* overrideGroup = new QGroupBox(tr("Selected Instance Overrides"), this);
    auto* overrideLayout = new QFormLayout(overrideGroup);
    m_overrideComponentCombo = new QComboBox(overrideGroup);
    m_overrideComponentCombo->setObjectName(QStringLiteral("prefabOverrideComponentCombo"));
    m_overridePropertyCombo = new QComboBox(overrideGroup);
    m_overridePropertyCombo->setObjectName(QStringLiteral("prefabOverridePropertyCombo"));
    m_overrideValueEdit = new QLineEdit(overrideGroup);
    m_overrideValueEdit->setObjectName(QStringLiteral("prefabOverrideValueEdit"));
    m_setPropertyButton = new QPushButton(tr("Set Property Override"), overrideGroup);
    m_setPropertyButton->setObjectName(QStringLiteral("prefabSetPropertyOverrideButton"));
    overrideLayout->addRow(tr("Component"), m_overrideComponentCombo);
    overrideLayout->addRow(tr("Property"), m_overridePropertyCombo);
    overrideLayout->addRow(tr("Value"), m_overrideValueEdit);
    overrideLayout->addRow(m_setPropertyButton);
    m_addComponentCombo = new QComboBox(overrideGroup);
    m_addComponentCombo->setObjectName(QStringLiteral("prefabAddComponentCombo"));
    for (const auto& name : fabgl::builtinComponentNames()) {
        if (name != "Transform" && name != "PrefabInstanceLink") {
            m_addComponentCombo->addItem(QString::fromStdString(name));
        }
    }
    auto* componentButtons = new QWidget(overrideGroup);
    auto* componentButtonsLayout = new QHBoxLayout(componentButtons);
    componentButtonsLayout->setContentsMargins(0, 0, 0, 0);
    m_addComponentButton = new QPushButton(tr("Add Component Override"), componentButtons);
    m_addComponentButton->setObjectName(QStringLiteral("prefabAddComponentOverrideButton"));
    m_removeComponentButton = new QPushButton(tr("Remove Component Override"), componentButtons);
    m_removeComponentButton->setObjectName(QStringLiteral("prefabRemoveComponentOverrideButton"));
    componentButtonsLayout->addWidget(m_addComponentButton);
    componentButtonsLayout->addWidget(m_removeComponentButton);
    overrideLayout->addRow(m_addComponentCombo);
    overrideLayout->addRow(componentButtons);
    auto* instanceButtons = new QWidget(overrideGroup);
    auto* instanceButtonsLayout = new QHBoxLayout(instanceButtons);
    instanceButtonsLayout->setContentsMargins(0, 0, 0, 0);
    m_revertButton = new QPushButton(tr("Revert"), instanceButtons);
    m_revertButton->setObjectName(QStringLiteral("prefabRevertButton"));
    m_applyButton = new QPushButton(tr("Apply to Prefab"), instanceButtons);
    m_applyButton->setObjectName(QStringLiteral("prefabApplyButton"));
    m_unpackButton = new QPushButton(tr("Unpack"), instanceButtons);
    m_unpackButton->setObjectName(QStringLiteral("prefabUnpackButton"));
    instanceButtonsLayout->addWidget(m_revertButton);
    instanceButtonsLayout->addWidget(m_applyButton);
    instanceButtonsLayout->addWidget(m_unpackButton);
    overrideLayout->addRow(instanceButtons);
    layout->addWidget(overrideGroup);

    auto* historyButtons = new QWidget(this);
    auto* historyLayout = new QHBoxLayout(historyButtons);
    historyLayout->setContentsMargins(0, 0, 0, 0);
    m_undoButton = new QPushButton(tr("Undo"), historyButtons);
    m_undoButton->setObjectName(QStringLiteral("prefabUndoButton"));
    m_redoButton = new QPushButton(tr("Redo"), historyButtons);
    m_redoButton->setObjectName(QStringLiteral("prefabRedoButton"));
    historyLayout->addWidget(m_undoButton);
    historyLayout->addWidget(m_redoButton);
    historyLayout->addStretch();
    layout->addWidget(historyButtons);
    layout->addStretch();
}

void PrefabEditorPanel::connectUi() {
    connect(m_createButton, &QPushButton::clicked, this, [this]() {
        QString error;
        std::optional<fabgl::AssetGuid> nested;
        if (!m_nestedBaseEdit->text().trimmed().isEmpty()) {
            const auto parsed =
                fabgl::AssetGuid::parse(m_nestedBaseEdit->text().trimmed().toStdString());
            if (!parsed) {
                reportFailure(tr("Nested base Asset GUID is invalid."));
                return;
            }
            nested = parsed.value();
        }
        if (!createFromSelection(m_pathEdit->text(), m_nameEdit->text(), nested, error)) {
            reportFailure(error);
        }
    });
    connect(m_openButton, &QPushButton::clicked, this, [this]() {
        QString error;
        if (!openPrefabRelativePath(m_pathEdit->text(), error)) {
            reportFailure(error);
        }
    });
    connect(m_saveButton, &QPushButton::clicked, this, [this]() {
        QString error;
        if (!saveCurrentPrefab(error)) {
            reportFailure(error);
        }
    });
    connect(m_instantiateButton, &QPushButton::clicked, this, [this]() {
        QString error;
        if (!instantiateCurrentPrefab(error)) {
            reportFailure(error);
        }
    });
    connect(m_instancesTree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
                m_selectedInstance =
                    current != nullptr
                        ? SceneDocument::parseEntityGuid(current->data(0, Qt::UserRole).toString())
                        : std::nullopt;
                refreshOverrideUi();
                if (m_selectedInstance) {
                    emit sceneSelectionRequested({SceneDocument::guidString(*m_selectedInstance)});
                }
            });
    connect(m_overrideComponentCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { refreshOverrideUi(); });
    connect(m_overridePropertyCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) {
                const auto* record = selectedInstanceRecord();
                if (record == nullptr) {
                    return;
                }
                const auto resolved = record->state.resolve(m_library);
                const auto componentId = fabgl::ComponentTypeGuid::parse(
                    m_overrideComponentCombo->currentData().toString().toStdString());
                if (!resolved || !componentId) {
                    return;
                }
                const auto component = resolved.value().find(componentId.value());
                if (component == resolved.value().cend()) {
                    return;
                }
                const auto property = component->second.properties.find(
                    m_overridePropertyCombo->currentData().toString().toStdString());
                if (property != component->second.properties.cend()) {
                    m_overrideValueEdit->setText(propertyValueText(property->second));
                }
            });
    const auto runEdit = [this](const QString& description,
                                const std::function<bool(QString&)>& callback) {
        QString error;
        if (!runSnapshotEdit(description, {}, callback, error)) {
            reportFailure(error);
        }
    };
    connect(m_setPropertyButton, &QPushButton::clicked, this, [this, runEdit]() {
        runEdit(tr("Set prefab property override"),
                [this](QString& error) { return setPropertyOverrideFromUi(error); });
    });
    connect(m_addComponentButton, &QPushButton::clicked, this, [this, runEdit]() {
        runEdit(tr("Add prefab component override"),
                [this](QString& error) { return addComponentOverrideFromUi(error); });
    });
    connect(m_removeComponentButton, &QPushButton::clicked, this, [this, runEdit]() {
        runEdit(tr("Remove prefab component override"),
                [this](QString& error) { return removeComponentOverrideFromUi(error); });
    });
    connect(m_revertButton, &QPushButton::clicked, this, [this, runEdit]() {
        runEdit(tr("Revert prefab overrides"),
                [this](QString& error) { return revertSelectedInstance(error); });
    });
    connect(m_applyButton, &QPushButton::clicked, this, [this]() {
        QString error;
        const std::vector<QString> files =
            m_currentPath.isEmpty() ? std::vector<QString>{} : std::vector<QString>{m_currentPath};
        if (!runSnapshotEdit(
                tr("Apply prefab overrides"), files,
                [this](QString& operationError) { return applySelectedInstance(operationError); },
                error)) {
            reportFailure(error);
        }
    });
    connect(m_unpackButton, &QPushButton::clicked, this, [this, runEdit]() {
        runEdit(tr("Unpack prefab instance"),
                [this](QString& error) { return unpackSelectedInstance(error); });
    });
    connect(m_undoButton, &QPushButton::clicked, m_undoStack, &QUndoStack::undo);
    connect(m_redoButton, &QPushButton::clicked, m_undoStack, &QUndoStack::redo);
    connect(m_undoStack, &QUndoStack::canUndoChanged, m_undoButton, &QWidget::setEnabled);
    connect(m_undoStack, &QUndoStack::canRedoChanged, m_redoButton, &QWidget::setEnabled);
    m_undoButton->setEnabled(m_undoStack->canUndo());
    m_redoButton->setEnabled(m_undoStack->canRedo());
}

void PrefabEditorPanel::setProjectContext(const QString& projectRoot, const QString& projectGuid,
                                          const QVector<ProjectAssetEntry>& assets) {
    const auto normalizedRoot =
        projectRoot.trimmed().isEmpty() ? QString{} : QFileInfo(projectRoot).absoluteFilePath();
    const auto normalizedGuid = projectGuid.trimmed();
    const bool projectChanged = normalizedRoot != m_projectRoot || normalizedGuid != m_projectGuid;
    m_projectRoot = normalizedRoot;
    m_projectGuid = normalizedGuid;
    m_projectAssets = assets;
    if (projectChanged) {
        m_hasCurrent = false;
        m_currentPrefab = {};
        m_currentPath.clear();
        m_missingGuid.reset();
        m_library = {};
        m_instances.clear();
        m_selectedEntity.reset();
        m_selectedInstance.reset();
    }
    bool sceneBelongsToProject = false;
    if (m_document != nullptr && !m_document->filePath().isEmpty() && !m_projectRoot.isEmpty()) {
        const auto relative =
            QDir(m_projectRoot)
                .relativeFilePath(QFileInfo(m_document->filePath()).absoluteFilePath());
        sceneBelongsToProject = relative != QStringLiteral("..") &&
                                !relative.startsWith(QStringLiteral("../")) &&
                                !QFileInfo(relative).isAbsolute();
    }
    QString libraryError;
    (void)rebuildLibrary(libraryError);
    if (!m_projectRoot.isEmpty() && (!projectChanged || sceneBelongsToProject)) {
        discoverSceneInstances();
    }
    if (!m_hasCurrent && !m_missingGuid) {
        if (m_instances.empty()) {
            m_diagnostic = m_projectRoot.isEmpty()
                               ? tr("Open a project to author prefabs.")
                               : tr("Select an entity subtree to create a prefab.");
        } else if (m_diagnostic.isEmpty()) {
            m_diagnostic = tr("Discovered %1 persisted prefab instance(s) in Scene v2.")
                               .arg(m_instances.size());
        }
    } else {
        (void)rebuildLibrary(libraryError);
    }
    refreshUi();
}

void PrefabEditorPanel::setSelectedEntity(const std::optional<fabgl::EntityGuid> entity) {
    m_selectedEntity = entity;
    refreshUi();
}

void PrefabEditorPanel::setSelectedInstanceRoot(const std::optional<fabgl::EntityGuid> entity) {
    m_selectedInstance = std::nullopt;
    if (entity) {
        const auto iterator = std::find_if(
            m_instances.cbegin(), m_instances.cend(), [entity](const InstanceRecord& record) {
                return std::find(record.entities.cbegin(), record.entities.cend(), *entity) !=
                       record.entities.cend();
            });
        if (iterator != m_instances.cend()) {
            m_selectedInstance = iterator->root;
            if (!iterator->missing && (!m_hasCurrent || m_currentPrefab.id != iterator->prefab)) {
                QString openError;
                if (!openPrefabGuid(iterator->prefab, openError)) {
                    m_diagnostic =
                        tr("Linked prefab source could not be opened: %1").arg(openError);
                }
            }
        }
    }
    refreshInstanceUi();
    refreshOverrideUi();
}

bool PrefabEditorPanel::createFromSelection(const QString& projectRelativePath,
                                            const QString& prefabName,
                                            const std::optional<fabgl::AssetGuid> nestedBase,
                                            QString& errorMessage) {
    auto normalized = QDir::fromNativeSeparators(projectRelativePath.trimmed());
    if (!normalized.endsWith(QStringLiteral(".fglprefab"), Qt::CaseInsensitive)) {
        normalized += QStringLiteral(".fglprefab");
    }
    const auto absolute = safeProjectPath(normalized, errorMessage);
    if (!absolute) {
        return false;
    }
    if (QFileInfo::exists(*absolute) && !assetEntry(normalized)) {
        errorMessage = tr("A file already exists at %1 and is not the mapped prefab asset.")
                           .arg(QDir::toNativeSeparators(*absolute));
        return false;
    }
    if (const auto mapped = assetEntry(normalized);
        mapped && mapped->type.compare(QStringLiteral("prefab"), Qt::CaseInsensitive) != 0) {
        errorMessage =
            tr("The project path is mapped as asset type '%1', not prefab.").arg(mapped->type);
        return false;
    }
    const bool edited = runSnapshotEdit(
        tr("Create prefab from selection"), {*absolute},
        [this, normalized, absolute, prefabName, nestedBase](QString& operationError) {
            fabgl::PrefabAsset prefab;
            if (!buildPrefabFromSelection(prefabName.trimmed(), nestedBase, prefab,
                                          operationError)) {
                return false;
            }
            const auto existing = assetEntry(normalized);
            const auto stableKey = QStringLiteral("fabgl.project.%1.asset.%2")
                                       .arg(m_projectGuid, normalized.toCaseFolded());
            prefab.id = fabgl::AssetGuid::fromStableName(stableKey.toStdString());
            if (existing) {
                const auto mappedGuid = fabgl::AssetGuid::parse(existing->guid.toStdString());
                if (mappedGuid) {
                    prefab.id = mappedGuid.value();
                }
            }
            if (!writePrefab(*absolute, prefab, operationError)) {
                return false;
            }
            if (!existing) {
                m_projectAssets.push_back({QString::fromStdString(prefab.id.toString()),
                                           normalized,
                                           QStringLiteral("prefab"),
                                           QStringLiteral("{}"),
                                           QStringLiteral("flash"),
                                           {},
                                           false});
                emit projectAssetsChanged(m_projectAssets);
            }
            m_currentPrefab = std::move(prefab);
            m_currentPath = *absolute;
            m_hasCurrent = true;
            m_missingGuid.reset();
            QString dependencyError;
            (void)rebuildLibrary(dependencyError);
            return true;
        },
        errorMessage);
    if (edited) {
        m_pathEdit->setText(normalized);
        reportSuccess(tr("Prefab created atomically: %1").arg(normalized));
    }
    return edited;
}

bool PrefabEditorPanel::openPrefabRelativePath(const QString& projectRelativePath,
                                               QString& errorMessage) {
    const auto normalized = QDir::fromNativeSeparators(projectRelativePath.trimmed());
    const auto absolute = safeProjectPath(normalized, errorMessage);
    if (!absolute) {
        return false;
    }
    fabgl::PrefabAsset prefab;
    if (!readPrefab(*absolute, prefab, errorMessage)) {
        m_hasCurrent = false;
        m_missingGuid.reset();
        m_currentPath = *absolute;
        m_diagnostic = tr("Missing or invalid prefab: %1").arg(errorMessage);
        refreshUi();
        return false;
    }
    const auto mapped = assetEntry(prefab.id);
    if (!mapped || mapped->type.compare(QStringLiteral("prefab"), Qt::CaseInsensitive) != 0 ||
        QDir::cleanPath(QDir::fromNativeSeparators(mapped->path)) != QDir::cleanPath(normalized)) {
        errorMessage = tr("Prefab %1 is not resolved by the project Asset GUID mapping for %2.")
                           .arg(QString::fromStdString(prefab.id.toString()), normalized);
        m_hasCurrent = false;
        m_missingGuid = prefab.id;
        m_currentPath = *absolute;
        m_diagnostic = errorMessage;
        refreshUi();
        return false;
    }
    m_currentPrefab = std::move(prefab);
    m_currentPath = *absolute;
    m_hasCurrent = true;
    m_missingGuid.reset();
    QString dependencyError;
    (void)rebuildLibrary(dependencyError);
    refreshUi();
    reportSuccess(tr("Opened prefab %1").arg(normalized));
    return true;
}

bool PrefabEditorPanel::openPrefabGuid(const fabgl::AssetGuid guid, QString& errorMessage) {
    const auto entry = assetEntry(guid);
    if (!entry) {
        m_hasCurrent = false;
        m_currentPath.clear();
        m_missingGuid = guid;
        m_diagnostic = tr("Missing prefab Asset GUID mapping: %1")
                           .arg(QString::fromStdString(guid.toString()));
        errorMessage = m_diagnostic;
        refreshUi();
        return false;
    }
    if (entry->type.compare(QStringLiteral("prefab"), Qt::CaseInsensitive) != 0) {
        m_hasCurrent = false;
        m_currentPath.clear();
        m_missingGuid = guid;
        m_diagnostic = tr("Asset GUID %1 is mapped as type '%2', not prefab.")
                           .arg(QString::fromStdString(guid.toString()), entry->type);
        errorMessage = m_diagnostic;
        refreshUi();
        return false;
    }
    return openPrefabRelativePath(entry->path, errorMessage);
}

bool PrefabEditorPanel::saveCurrentPrefab(QString& errorMessage) {
    if (!m_hasCurrent || m_currentPath.isEmpty()) {
        errorMessage = tr("No prefab asset is open.");
        return false;
    }
    const bool saved = runSnapshotEdit(
        tr("Save prefab"), {m_currentPath},
        [this](QString& operationError) {
            m_currentPrefab.name = m_nameEdit->text().trimmed().toStdString();
            if (m_currentPrefab.name.empty()) {
                operationError = tr("Prefab name cannot be empty.");
                return false;
            }
            const auto nestedText = m_nestedBaseEdit->text().trimmed();
            if (nestedText.isEmpty()) {
                m_currentPrefab.nestedBase.reset();
            } else {
                const auto parsed = fabgl::AssetGuid::parse(nestedText.toStdString());
                if (!parsed) {
                    operationError = tr("Nested base Asset GUID is invalid.");
                    return false;
                }
                m_currentPrefab.nestedBase = parsed.value();
            }
            if (!writePrefab(m_currentPath, m_currentPrefab, operationError)) {
                return false;
            }
            QString dependencyError;
            (void)rebuildLibrary(dependencyError);
            return true;
        },
        errorMessage);
    if (saved) {
        reportSuccess(tr("Prefab saved atomically."));
    }
    return saved;
}

bool PrefabEditorPanel::instantiateCurrentPrefab(QString& errorMessage) {
    if (!m_hasCurrent) {
        if (m_missingGuid) {
            return runSnapshotEdit(
                tr("Instantiate missing prefab placeholder"), {},
                [this](QString& operationError) {
                    return instantiateMissing(*m_missingGuid, operationError);
                },
                errorMessage);
        }
        errorMessage = tr("No prefab asset is open.");
        return false;
    }
    return runSnapshotEdit(
        tr("Instantiate prefab"), {},
        [this](QString& operationError) {
            QString dependencyError;
            if (!rebuildLibrary(dependencyError)) {
                const auto missing = m_currentPrefab.nestedBase.value_or(m_currentPrefab.id);
                m_diagnostic = dependencyError;
                return instantiateMissing(missing, operationError);
            }
            auto resolved = m_library.resolveHierarchy(m_currentPrefab.id);
            if (!resolved) {
                operationError = errorText(resolved.error());
                return false;
            }
            InstanceRecord record(m_currentPrefab.id);
            if (!instantiateResolved(resolved.value(), record, operationError)) {
                return false;
            }
            m_selectedInstance = record.root;
            const auto root = record.root;
            m_instances.push_back(std::move(record));
            emit sceneSelectionRequested({SceneDocument::guidString(root)});
            return true;
        },
        errorMessage);
}

bool PrefabEditorPanel::hasCurrentPrefab() const noexcept {
    return m_hasCurrent;
}

QString PrefabEditorPanel::currentPrefabPath() const {
    return m_currentPath;
}

QString PrefabEditorPanel::currentPrefabGuid() const {
    if (m_hasCurrent) {
        return QString::fromStdString(m_currentPrefab.id.toString());
    }
    return m_missingGuid ? QString::fromStdString(m_missingGuid->toString()) : QString{};
}

QString PrefabEditorPanel::diagnosticText() const {
    return m_diagnostic;
}

qsizetype PrefabEditorPanel::instanceCount() const noexcept {
    return static_cast<qsizetype>(m_instances.size());
}

qsizetype PrefabEditorPanel::dependencyCount() const noexcept {
    return m_dependencyCount;
}

bool PrefabEditorPanel::selectedInstanceLinked() const noexcept {
    const auto* record = selectedInstanceRecord();
    return record != nullptr && !record->missing && !record->state.unpacked();
}

std::optional<fabgl::EntityGuid> PrefabEditorPanel::selectedInstanceRoot() const noexcept {
    return m_selectedInstance;
}

void PrefabEditorPanel::refreshUi() {
    const bool hasProject = !m_projectRoot.isEmpty() && !m_projectGuid.isEmpty();
    m_createButton->setEnabled(hasProject && m_selectedEntity.has_value());
    m_openButton->setEnabled(hasProject && !m_pathEdit->text().trimmed().isEmpty());
    m_saveButton->setEnabled(m_hasCurrent);
    m_instantiateButton->setEnabled(m_hasCurrent || m_missingGuid.has_value());
    if (m_hasCurrent) {
        m_nameEdit->setText(QString::fromStdString(m_currentPrefab.name));
        m_guidEdit->setText(QString::fromStdString(m_currentPrefab.id.toString()));
        m_nestedBaseEdit->setText(
            m_currentPrefab.nestedBase
                ? QString::fromStdString(m_currentPrefab.nestedBase->toString())
                : QString{});
        if (!m_currentPath.isEmpty() && !m_projectRoot.isEmpty()) {
            m_pathEdit->setText(
                QDir::fromNativeSeparators(QDir(m_projectRoot).relativeFilePath(m_currentPath)));
        }
    } else {
        m_guidEdit->setText(m_missingGuid ? QString::fromStdString(m_missingGuid->toString())
                                          : QString{});
    }
    m_diagnosticLabel->setText(m_diagnostic);
    const bool problem = m_diagnostic.contains(QStringLiteral("missing"), Qt::CaseInsensitive) ||
                         m_diagnostic.contains(QStringLiteral("cycle"), Qt::CaseInsensitive) ||
                         m_diagnostic.contains(QStringLiteral("invalid"), Qt::CaseInsensitive);
    m_diagnosticLabel->setStyleSheet(problem ? QStringLiteral("color: #e57373;") : QString{});
    refreshDependencyUi();
    refreshInstanceUi();
    refreshOverrideUi();
}

void PrefabEditorPanel::refreshDependencyUi() {
    m_dependencyTree->clear();
    m_dependencyCount = 0;
    if (!m_hasCurrent) {
        if (m_missingGuid) {
            auto* item = new QTreeWidgetItem(
                {tr("[Missing Prefab]"), QString::fromStdString(m_missingGuid->toString())});
            item->setForeground(0, QColor(QStringLiteral("#e57373")));
            m_dependencyTree->addTopLevelItem(item);
        }
        return;
    }
    auto* root = new QTreeWidgetItem(
        {QString::fromStdString(m_currentPrefab.name),
         QStringLiteral("%1  %2").arg(
             QString::fromStdString(m_currentPrefab.id.toString()),
             QDir::fromNativeSeparators(QDir(m_projectRoot).relativeFilePath(m_currentPath)))});
    m_dependencyTree->addTopLevelItem(root);
    std::set<fabgl::AssetGuid> visited{m_currentPrefab.id};
    auto nested = m_currentPrefab.nestedBase;
    QTreeWidgetItem* parent = root;
    while (nested) {
        ++m_dependencyCount;
        const auto entry = assetEntry(*nested);
        if (!entry) {
            auto* missing = new QTreeWidgetItem(
                {tr("[Missing Prefab]"), QString::fromStdString(nested->toString())});
            missing->setForeground(0, QColor(QStringLiteral("#e57373")));
            parent->addChild(missing);
            break;
        }
        if (entry->type.compare(QStringLiteral("prefab"), Qt::CaseInsensitive) != 0) {
            auto* invalid = new QTreeWidgetItem(
                {tr("[Wrong Asset Type]"), QStringLiteral("%1  %2").arg(entry->guid, entry->type)});
            invalid->setForeground(0, QColor(QStringLiteral("#e57373")));
            parent->addChild(invalid);
            break;
        }
        auto* item = new QTreeWidgetItem({QFileInfo(entry->path).completeBaseName(),
                                          QStringLiteral("%1  %2").arg(entry->guid, entry->path)});
        parent->addChild(item);
        if (!visited.insert(*nested).second) {
            item->setText(0, tr("[Dependency Cycle]"));
            item->setForeground(0, QColor(QStringLiteral("#e57373")));
            break;
        }
        QString pathError;
        const auto absolute = safeProjectPath(entry->path, pathError);
        fabgl::PrefabAsset dependency;
        if (!absolute || !readPrefab(*absolute, dependency, pathError) ||
            dependency.id != *nested) {
            item->setText(0, tr("[Missing / Invalid Prefab]"));
            item->setForeground(0, QColor(QStringLiteral("#e57373")));
            break;
        }
        nested = dependency.nestedBase;
        parent = item;
    }
    m_dependencyTree->expandAll();
}

void PrefabEditorPanel::refreshInstanceUi() {
    const auto requestedSelection = m_selectedInstance;
    const QSignalBlocker blocker(m_instancesTree);
    m_instancesTree->clear();
    QTreeWidgetItem* selectedItem = nullptr;
    for (const auto& record : m_instances) {
        const auto* entity =
            m_document != nullptr ? m_document->scene().findEntity(record.root) : nullptr;
        const auto displayName = entity != nullptr ? QString::fromStdString(entity->name())
                                                   : tr("[Removed Scene Instance]");
        QString state = record.missing ? tr("Missing placeholder") : tr("Linked");
        if (record.state.unpacked()) {
            state = tr("Unpacked");
        } else if (!record.missing) {
            state = tr("Linked — %1 property, %2 added, %3 removed")
                        .arg(record.state.propertyOverrideCount())
                        .arg(record.state.addedComponentCount())
                        .arg(record.state.removedComponentCount());
        }
        auto* item = new QTreeWidgetItem({displayName, state});
        item->setData(0, Qt::UserRole, SceneDocument::guidString(record.root));
        if (record.missing) {
            item->setForeground(0, QColor(QStringLiteral("#e57373")));
        }
        m_instancesTree->addTopLevelItem(item);
        if (requestedSelection && *requestedSelection == record.root) {
            selectedItem = item;
        }
    }
    if (selectedItem != nullptr) {
        m_instancesTree->setCurrentItem(selectedItem);
    }
    m_selectedInstance = requestedSelection;
}

void PrefabEditorPanel::refreshOverrideUi() {
    const auto previousComponent = m_overrideComponentCombo->currentData().toString();
    const auto previousProperty = m_overridePropertyCombo->currentData().toString();
    m_overrideComponentCombo->blockSignals(true);
    m_overridePropertyCombo->blockSignals(true);
    m_overrideComponentCombo->clear();
    m_overridePropertyCombo->clear();
    const auto* record = selectedInstanceRecord();
    const bool linked = record != nullptr && !record->missing && !record->state.unpacked();
    if (linked) {
        const auto resolved = record->state.resolve(m_library);
        if (resolved) {
            const auto linkType = instanceLinkType();
            for (const auto& [typeId, component] : resolved.value()) {
                if (linkType && typeId == *linkType) {
                    continue;
                }
                m_overrideComponentCombo->addItem(QString::fromStdString(component.typeName),
                                                  QString::fromStdString(typeId.toString()));
            }
        }
    }
    auto componentIndex = m_overrideComponentCombo->findData(previousComponent);
    if (componentIndex < 0 && m_overrideComponentCombo->count() > 0) {
        componentIndex = 0;
    }
    m_overrideComponentCombo->setCurrentIndex(componentIndex);
    if (linked && componentIndex >= 0) {
        const auto resolved = record->state.resolve(m_library);
        const auto typeId = fabgl::ComponentTypeGuid::parse(
            m_overrideComponentCombo->currentData().toString().toStdString());
        if (resolved && typeId) {
            const auto component = resolved.value().find(typeId.value());
            if (component != resolved.value().cend()) {
                for (const auto& [name, value] : component->second.properties) {
                    (void)value;
                    m_overridePropertyCombo->addItem(QString::fromStdString(name),
                                                     QString::fromStdString(name));
                }
            }
        }
    }
    auto propertyIndex = m_overridePropertyCombo->findData(previousProperty);
    if (propertyIndex < 0 && m_overridePropertyCombo->count() > 0) {
        propertyIndex = 0;
    }
    m_overridePropertyCombo->setCurrentIndex(propertyIndex);
    m_overrideComponentCombo->blockSignals(false);
    m_overridePropertyCombo->blockSignals(false);
    if (linked && componentIndex >= 0 && propertyIndex >= 0) {
        const auto resolved = record->state.resolve(m_library);
        const auto typeId = fabgl::ComponentTypeGuid::parse(
            m_overrideComponentCombo->currentData().toString().toStdString());
        if (resolved && typeId) {
            const auto component = resolved.value().find(typeId.value());
            if (component != resolved.value().cend()) {
                const auto property = component->second.properties.find(
                    m_overridePropertyCombo->currentData().toString().toStdString());
                if (property != component->second.properties.cend()) {
                    m_overrideValueEdit->setText(propertyValueText(property->second));
                }
            }
        }
    } else {
        m_overrideValueEdit->clear();
    }
    m_overrideComponentCombo->setEnabled(linked);
    m_overridePropertyCombo->setEnabled(linked);
    m_overrideValueEdit->setEnabled(linked);
    m_setPropertyButton->setEnabled(linked && m_overridePropertyCombo->count() > 0);
    m_addComponentCombo->setEnabled(linked);
    m_addComponentButton->setEnabled(linked);
    m_removeComponentButton->setEnabled(linked && m_overrideComponentCombo->count() > 0);
    m_revertButton->setEnabled(linked);
    m_applyButton->setEnabled(linked && record->prefab == m_currentPrefab.id &&
                              !m_currentPath.isEmpty());
    m_unpackButton->setEnabled(linked);
}

void PrefabEditorPanel::reportFailure(const QString& errorMessage) {
    m_diagnostic = errorMessage;
    refreshUi();
    emit statusMessage(tr("Prefab operation failed: %1").arg(errorMessage));
}

void PrefabEditorPanel::reportSuccess(const QString& message) {
    if (m_diagnostic.isEmpty()) {
        m_diagnostic = message;
    }
    refreshUi();
    emit statusMessage(message);
}

std::optional<QString> PrefabEditorPanel::safeProjectPath(const QString& relativePath,
                                                          QString& errorMessage) const {
    if (m_projectRoot.isEmpty()) {
        errorMessage = tr("No project root is configured.");
        return std::nullopt;
    }
    const auto normalized = QDir::cleanPath(QDir::fromNativeSeparators(relativePath.trimmed()));
    if (normalized.isEmpty() || normalized == QStringLiteral(".") ||
        QFileInfo(normalized).isAbsolute() || normalized == QStringLiteral("..") ||
        normalized.startsWith(QStringLiteral("../"))) {
        errorMessage = tr("Prefab path must be a relative path inside the project root.");
        return std::nullopt;
    }
    const auto root = QFileInfo(m_projectRoot).absoluteFilePath();
    const auto target = QFileInfo(QDir(root).absoluteFilePath(normalized)).absoluteFilePath();
    const auto relativeCheck = QDir(root).relativeFilePath(target);
    if (relativeCheck == QStringLiteral("..") || relativeCheck.startsWith(QStringLiteral("../")) ||
        QFileInfo(target).isSymLink()) {
        errorMessage = tr("Prefab path escapes the project root or targets a symbolic link.");
        return std::nullopt;
    }
    auto directory = QFileInfo(target).absoluteDir();
    while (directory.absolutePath() != root && directory.absolutePath() != directory.rootPath()) {
        const QFileInfo info(directory.absolutePath());
        if (info.exists() && info.isSymLink()) {
            errorMessage = tr("Prefab path crosses a symbolic-link directory.");
            return std::nullopt;
        }
        if (!directory.cdUp()) {
            break;
        }
    }
    return target;
}

std::optional<ProjectAssetEntry> PrefabEditorPanel::assetEntry(const fabgl::AssetGuid guid) const {
    const auto guidText = QString::fromStdString(guid.toString());
    const auto iterator =
        std::find_if(m_projectAssets.cbegin(), m_projectAssets.cend(),
                     [&guidText](const ProjectAssetEntry& entry) {
                         return entry.guid.compare(guidText, Qt::CaseInsensitive) == 0;
                     });
    return iterator == m_projectAssets.cend() ? std::nullopt
                                              : std::optional<ProjectAssetEntry>(*iterator);
}

std::optional<ProjectAssetEntry> PrefabEditorPanel::assetEntry(const QString& relativePath) const {
    const auto normalized = QDir::cleanPath(QDir::fromNativeSeparators(relativePath));
    const auto iterator = std::find_if(
        m_projectAssets.cbegin(), m_projectAssets.cend(),
        [&normalized](const ProjectAssetEntry& entry) {
            return QDir::cleanPath(QDir::fromNativeSeparators(entry.path)) == normalized;
        });
    return iterator == m_projectAssets.cend() ? std::nullopt
                                              : std::optional<ProjectAssetEntry>(*iterator);
}

bool PrefabEditorPanel::readPrefab(const QString& absolutePath, fabgl::PrefabAsset& prefab,
                                   QString& errorMessage) const {
    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly)) {
        errorMessage = tr("Cannot open prefab %1: %2")
                           .arg(QDir::toNativeSeparators(absolutePath), file.errorString());
        return false;
    }
    const auto bytes = file.readAll();
    const auto parsed = fabgl::PrefabSerializer::deserialize(
        std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    if (!parsed) {
        errorMessage = tr("Cannot deserialize prefab %1: %2")
                           .arg(QDir::toNativeSeparators(absolutePath), errorText(parsed.error()));
        return false;
    }
    prefab = parsed.value();
    return true;
}

bool PrefabEditorPanel::writePrefab(const QString& absolutePath, const fabgl::PrefabAsset& prefab,
                                    QString& errorMessage) const {
    const auto serialized = fabgl::PrefabSerializer::serialize(prefab);
    if (!serialized) {
        errorMessage = tr("Prefab serialization failed: %1").arg(errorText(serialized.error()));
        return false;
    }
    const auto& text = serialized.value();
    return writeBytesAtomically(
        absolutePath, QByteArray(text.data(), static_cast<qsizetype>(text.size())), errorMessage);
}

bool PrefabEditorPanel::rebuildLibrary(QString& errorMessage) {
    m_library = fabgl::PrefabLibrary{};
    m_dependencyCount = 0;
    errorMessage.clear();
    std::set<fabgl::AssetGuid> loaded;
    if (m_hasCurrent) {
        auto added = m_library.add(m_currentPrefab);
        if (!added) {
            errorMessage = errorText(added.error());
            m_diagnostic = errorMessage;
            return false;
        }
        loaded.insert(m_currentPrefab.id);
    }

    for (const auto& entry : m_projectAssets) {
        if (entry.type.compare(QStringLiteral("prefab"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        const auto mappedGuid = fabgl::AssetGuid::parse(entry.guid.toStdString());
        if (!mappedGuid || mappedGuid.value().isNil() || loaded.contains(mappedGuid.value())) {
            continue;
        }
        QString loadError;
        const auto absolute = safeProjectPath(entry.path, loadError);
        fabgl::PrefabAsset prefab;
        if (!absolute || !readPrefab(*absolute, prefab, loadError) ||
            prefab.id != mappedGuid.value()) {
            if (errorMessage.isEmpty()) {
                errorMessage =
                    loadError.isEmpty()
                        ? tr("Mapped prefab GUID does not match file content: %1").arg(entry.path)
                        : loadError;
            }
            continue;
        }
        auto added = m_library.add(std::move(prefab));
        if (!added) {
            if (errorMessage.isEmpty()) {
                errorMessage = errorText(added.error());
            }
            continue;
        }
        loaded.insert(mappedGuid.value());
    }

    if (!m_hasCurrent) {
        if (m_missingGuid) {
            errorMessage =
                tr("Missing prefab: %1").arg(QString::fromStdString(m_missingGuid->toString()));
            m_diagnostic = errorMessage;
            return false;
        }
        return true;
    }

    auto resolved = m_library.resolveHierarchy(m_currentPrefab.id);
    if (!resolved) {
        errorMessage = errorText(resolved.error());
        m_diagnostic = errorMessage;
        return false;
    }
    auto dependencies = m_library.dependencies(m_currentPrefab.id);
    if (!dependencies) {
        errorMessage = errorText(dependencies.error());
        m_diagnostic = errorMessage;
        return false;
    }
    m_dependencyCount = static_cast<qsizetype>(dependencies.value().size());
    m_diagnostic = tr("Prefab dependencies resolved by explicit Asset GUID mappings.");
    return true;
}

std::optional<fabgl::ComponentTypeGuid> PrefabEditorPanel::instanceLinkType() const {
    if (m_document == nullptr) {
        return std::nullopt;
    }
    const auto* metadata = m_document->reflectionRegistry().find("fabgl.PrefabInstanceLink");
    return metadata != nullptr ? std::optional<fabgl::ComponentTypeGuid>(metadata->typeId)
                               : std::nullopt;
}

bool PrefabEditorPanel::persistInstanceLink(const InstanceRecord& record, QString& errorMessage) {
    if (m_document == nullptr) {
        errorMessage = tr("Scene document is unavailable.");
        return false;
    }
    const auto linkType = instanceLinkType();
    if (!linkType) {
        errorMessage = tr("Prefab instance link component metadata is unavailable.");
        return false;
    }
    auto encoded = fabgl::PrefabInstanceSerializer::serialize(record);
    if (!encoded) {
        errorMessage = errorText(encoded.error());
        return false;
    }
    auto* root = m_document->scene().findEntity(record.root);
    if (root == nullptr) {
        errorMessage = tr("The prefab instance root is missing from the scene.");
        return false;
    }
    if (root->getComponent(*linkType) == nullptr) {
        ComponentSnapshot snapshot{*linkType,
                                   QStringLiteral("fabgl.PrefabInstanceLink"),
                                   true,
                                   {{"state", fabgl::PropertyValue(encoded.value())}}};
        return m_document->restoreComponent(record.root, snapshot, errorMessage);
    }
    return m_document->setComponentProperty(record.root, *linkType, "state",
                                            fabgl::PropertyValue(encoded.value()), errorMessage);
}

bool PrefabEditorPanel::removeInstanceLink(const fabgl::EntityGuid root, QString& errorMessage) {
    if (m_document == nullptr) {
        errorMessage = tr("Scene document is unavailable.");
        return false;
    }
    const auto linkType = instanceLinkType();
    auto* entity = m_document->scene().findEntity(root);
    if (!linkType || entity == nullptr || entity->getComponent(*linkType) == nullptr) {
        errorMessage = tr("The selected entity has no persisted prefab linkage.");
        return false;
    }
    return m_document->removeComponent(root, *linkType, errorMessage);
}

void PrefabEditorPanel::discoverSceneInstances() {
    m_instances.clear();
    m_selectedInstance.reset();
    if (m_document == nullptr) {
        return;
    }
    const auto linkType = instanceLinkType();
    if (!linkType) {
        m_diagnostic = tr("Prefab instance link component metadata is unavailable.");
        return;
    }

    QStringList diagnostics;
    std::set<fabgl::EntityGuid> claimedEntities;
    for (const auto* entity : m_document->scene().entities()) {
        const auto* component = entity->getComponent(*linkType);
        if (component == nullptr || component->metadata() == nullptr) {
            continue;
        }
        const auto* stateProperty = component->metadata()->findProperty("state");
        const auto stateValue =
            stateProperty != nullptr
                ? stateProperty->read(component)
                : fabgl::Result<fabgl::PropertyValue>::failure(
                      fabgl::Error(fabgl::ErrorCode::NotFound, "prefab link state is missing"));
        const auto* encoded = stateValue ? std::get_if<std::string>(&stateValue.value()) : nullptr;
        if (encoded == nullptr) {
            diagnostics.push_back(tr("Entity %1 has an unreadable prefab link.")
                                      .arg(SceneDocument::guidString(entity->id())));
            continue;
        }
        auto decoded = fabgl::PrefabInstanceSerializer::deserialize(*encoded);
        if (!decoded || decoded.value().root != entity->id()) {
            diagnostics.push_back(tr("Entity %1 has an invalid prefab link: %2")
                                      .arg(SceneDocument::guidString(entity->id()),
                                           decoded
                                               ? tr("root GUID does not match the component owner")
                                               : errorText(decoded.error())));
            continue;
        }
        auto record = std::move(decoded.value());
        bool hierarchyMissing = false;
        for (const auto sceneEntity : record.entities) {
            if (m_document->scene().findEntity(sceneEntity) == nullptr ||
                !claimedEntities.insert(sceneEntity).second) {
                hierarchyMissing = true;
            }
        }
        const auto source = m_library.resolveHierarchy(record.prefab);
        if (!source || hierarchyMissing) {
            record.missing = true;
            diagnostics.push_back(
                tr("Prefab instance %1 remains visible but its %2 is missing or invalid.")
                    .arg(SceneDocument::guidString(record.root),
                         hierarchyMissing ? tr("baked hierarchy") : tr("source asset")));
        }
        m_instances.push_back(std::move(record));
    }
    if (!diagnostics.isEmpty()) {
        m_diagnostic = diagnostics.join(QLatin1Char('\n'));
    } else if (!m_instances.empty()) {
        m_diagnostic =
            tr("Discovered %1 persisted prefab instance(s) in Scene v2.").arg(m_instances.size());
    }
}

bool PrefabEditorPanel::buildPrefabFromSelection(const QString& prefabName,
                                                 const std::optional<fabgl::AssetGuid> nestedBase,
                                                 fabgl::PrefabAsset& prefab,
                                                 QString& errorMessage) const {
    if (m_document == nullptr || !m_selectedEntity) {
        errorMessage = tr("Select a hierarchy entity before creating a prefab.");
        return false;
    }
    if (prefabName.isEmpty()) {
        errorMessage = tr("Prefab name cannot be empty.");
        return false;
    }
    const auto rootState = m_document->snapshot(*m_selectedEntity);
    if (!rootState) {
        errorMessage = tr("The selected scene entity no longer exists.");
        return false;
    }
    prefab = {fabgl::AssetGuid{}, prefabName.toStdString(), nestedBase, {}, {}};
    const auto linkType = instanceLinkType();
    for (const auto& component : rootState->components) {
        if (linkType && component.typeId == *linkType) {
            continue;
        }
        const auto prefabComponent = componentData(component);
        prefab.components.emplace(prefabComponent.typeId, prefabComponent);
    }
    std::set<fabgl::EntityGuid> visited;
    std::function<bool(fabgl::EntityGuid, std::optional<fabgl::EntityGuid>)> append =
        [&](const fabgl::EntityGuid id,
            const std::optional<fabgl::EntityGuid> prefabParent) -> bool {
        if (!visited.insert(id).second) {
            errorMessage = tr("The selected hierarchy contains a cycle.");
            return false;
        }
        const auto state = m_document->snapshot(id);
        if (!state) {
            errorMessage = tr("Entity %1 disappeared while creating the prefab.")
                               .arg(SceneDocument::guidString(id));
            return false;
        }
        fabgl::PrefabEntityData entity{
            id, state->name.toStdString(), state->active, prefabParent, {}};
        const auto transform = transformData(*state);
        entity.components.emplace(transform.typeId, transform);
        if (id != *m_selectedEntity) {
            for (const auto& component : state->components) {
                if (linkType && component.typeId == *linkType) {
                    continue;
                }
                const auto prefabComponent = componentData(component);
                entity.components.emplace(prefabComponent.typeId, prefabComponent);
            }
        }
        prefab.entities.push_back(std::move(entity));
        for (const auto child : state->children) {
            if (!append(child, id)) {
                return false;
            }
        }
        return true;
    };
    return append(*m_selectedEntity, std::nullopt);
}

bool PrefabEditorPanel::instantiateResolved(const fabgl::ResolvedPrefab& resolved,
                                            InstanceRecord& record, QString& errorMessage) {
    if (m_document == nullptr) {
        errorMessage = tr("Scene document is unavailable.");
        return false;
    }
    std::vector<fabgl::EntityGuid> ordered;
    std::set<fabgl::EntityGuid> complete;
    std::set<fabgl::EntityGuid> active;
    std::function<bool(fabgl::EntityGuid)> visit = [&](const fabgl::EntityGuid id) {
        if (complete.contains(id)) {
            return true;
        }
        if (!active.insert(id).second) {
            errorMessage = tr("Resolved prefab hierarchy contains a cycle.");
            return false;
        }
        const auto entity = resolved.entities.find(id);
        if (entity == resolved.entities.cend()) {
            errorMessage = tr("Resolved prefab hierarchy references a missing entity.");
            return false;
        }
        if (entity->second.parent && resolved.entities.contains(*entity->second.parent) &&
            !visit(*entity->second.parent)) {
            return false;
        }
        active.erase(id);
        complete.insert(id);
        ordered.push_back(id);
        return true;
    };
    for (const auto& [id, entity] : resolved.entities) {
        (void)entity;
        if (!visit(id)) {
            return false;
        }
    }
    if (ordered.empty()) {
        const auto source = fabgl::EntityGuid::fromStableName(
            std::string("fabgl.prefab.synthetic-root.") + record.prefab.toString());
        ordered.push_back(source);
        record.sourceToScene.emplace(source, fabgl::EntityGuid::generate());
    } else {
        for (const auto source : ordered) {
            record.sourceToScene.emplace(source, fabgl::EntityGuid::generate());
        }
    }
    fabgl::EntityGuid sourceRoot = ordered.front();
    const auto resolvedRoot =
        std::find_if(ordered.cbegin(), ordered.cend(), [&resolved](const fabgl::EntityGuid id) {
            const auto& entity = resolved.entities.at(id);
            return !entity.parent || !resolved.entities.contains(*entity.parent);
        });
    if (resolvedRoot != ordered.cend()) {
        sourceRoot = *resolvedRoot;
    }
    std::set<fabgl::EntityGuid> localEntities;
    for (const auto& entity : m_currentPrefab.entities) {
        localEntities.insert(entity.id);
    }
    const auto localRoot =
        std::find_if(m_currentPrefab.entities.cbegin(), m_currentPrefab.entities.cend(),
                     [&localEntities, &resolved](const fabgl::PrefabEntityData& entity) {
                         return resolved.entities.contains(entity.id) &&
                                (!entity.parent || !localEntities.contains(*entity.parent));
                     });
    if (localRoot != m_currentPrefab.entities.cend()) {
        sourceRoot = localRoot->id;
    }
    record.root = record.sourceToScene.at(sourceRoot);
    record.entities.reserve(ordered.size());
    for (const auto sourceId : ordered) {
        EntitySnapshot snapshot;
        snapshot.id = record.sourceToScene.at(sourceId);
        snapshot.name = QString::fromStdString(m_currentPrefab.name);
        const auto entity = resolved.entities.find(sourceId);
        if (entity != resolved.entities.cend()) {
            snapshot.name = QString::fromStdString(entity->second.name);
            snapshot.active = entity->second.active;
            if (entity->second.parent) {
                const auto mappedParent = record.sourceToScene.find(*entity->second.parent);
                if (mappedParent != record.sourceToScene.cend()) {
                    snapshot.parent = mappedParent->second;
                }
            }
            for (const auto& [typeId, component] : entity->second.components) {
                if (typeId == fabgl::TransformComponent::staticTypeId()) {
                    applyTransformData(component, snapshot);
                } else {
                    snapshot.components.push_back(componentSnapshot(component));
                }
            }
        }
        if (sourceId == sourceRoot) {
            for (const auto& [typeId, component] : resolved.components) {
                if (typeId == fabgl::TransformComponent::staticTypeId()) {
                    applyTransformData(component, snapshot);
                } else {
                    const auto existing =
                        std::find_if(snapshot.components.begin(), snapshot.components.end(),
                                     [typeId](const ComponentSnapshot& candidate) {
                                         return candidate.typeId == typeId;
                                     });
                    if (existing == snapshot.components.end()) {
                        snapshot.components.push_back(componentSnapshot(component));
                    } else {
                        *existing = componentSnapshot(component);
                    }
                }
            }
        }
        if (!m_document->restoreEntity(snapshot, errorMessage)) {
            return false;
        }
        record.entities.push_back(snapshot.id);
    }
    return persistInstanceLink(record, errorMessage);
}

bool PrefabEditorPanel::instantiateMissing(const fabgl::AssetGuid missingGuid,
                                           QString& errorMessage) {
    if (m_document == nullptr) {
        errorMessage = tr("Scene document is unavailable.");
        return false;
    }
    InstanceRecord record(missingGuid);
    record.missing = true;
    record.root = fabgl::EntityGuid::generate();
    record.entities.push_back(record.root);
    EntitySnapshot placeholder;
    placeholder.id = record.root;
    placeholder.name =
        tr("[Missing Prefab] %1").arg(QString::fromStdString(missingGuid.toString()));
    if (!m_document->restoreEntity(placeholder, errorMessage)) {
        return false;
    }
    if (!persistInstanceLink(record, errorMessage)) {
        return false;
    }
    m_selectedInstance = record.root;
    m_instances.push_back(std::move(record));
    m_diagnostic = tr("Instantiated a visible placeholder for missing prefab %1.")
                       .arg(QString::fromStdString(missingGuid.toString()));
    emit sceneSelectionRequested({SceneDocument::guidString(*m_selectedInstance)});
    return true;
}

bool PrefabEditorPanel::applyResolvedRootComponents(InstanceRecord& record, QString& errorMessage) {
    if (m_document == nullptr || record.missing || record.state.unpacked()) {
        errorMessage = tr("The selected prefab instance is not linked.");
        return false;
    }
    const auto resolved = record.state.resolve(m_library);
    if (!resolved) {
        errorMessage = errorText(resolved.error());
        return false;
    }
    auto* entity = m_document->scene().findEntity(record.root);
    if (entity == nullptr) {
        errorMessage = tr("The selected prefab instance root is missing from the scene.");
        return false;
    }
    std::vector<fabgl::ComponentTypeGuid> existing;
    const auto linkType = instanceLinkType();
    for (const auto* component : entity->components()) {
        if (component->typeId() != fabgl::TransformComponent::staticTypeId() &&
            (!linkType || component->typeId() != *linkType)) {
            existing.push_back(component->typeId());
        }
    }
    for (const auto typeId : existing) {
        if (!m_document->removeComponent(record.root, typeId, errorMessage)) {
            return false;
        }
    }
    for (const auto& [typeId, component] : resolved.value()) {
        if (typeId != fabgl::TransformComponent::staticTypeId() &&
            (!linkType || typeId != *linkType) &&
            !m_document->restoreComponent(record.root, componentSnapshot(component),
                                          errorMessage)) {
            return false;
        }
    }
    return persistInstanceLink(record, errorMessage);
}

bool PrefabEditorPanel::setPropertyOverrideFromUi(QString& errorMessage) {
    auto* record = selectedInstanceRecord();
    if (record == nullptr || record->missing || record->state.unpacked()) {
        errorMessage = tr("Select a linked prefab instance.");
        return false;
    }
    const auto componentId = fabgl::ComponentTypeGuid::parse(
        m_overrideComponentCombo->currentData().toString().toStdString());
    const auto propertyName = m_overridePropertyCombo->currentData().toString().toStdString();
    const auto resolved = record->state.resolve(m_library);
    if (!componentId || !resolved || propertyName.empty()) {
        errorMessage = tr("Select a prefab component property.");
        return false;
    }
    const auto component = resolved.value().find(componentId.value());
    const auto property = component != resolved.value().cend()
                              ? component->second.properties.find(propertyName)
                              : std::map<std::string, fabgl::PropertyValue>::const_iterator{};
    if (component == resolved.value().cend() || property == component->second.properties.cend()) {
        errorMessage = tr("The selected prefab property no longer exists.");
        return false;
    }
    const auto value =
        parsedPropertyValue(property->second, m_overrideValueEdit->text(), errorMessage);
    if (!value) {
        return false;
    }
    const auto overridden =
        record->state.setPropertyOverride(componentId.value(), propertyName, *value);
    if (!overridden) {
        errorMessage = errorText(overridden.error());
        return false;
    }
    return applyResolvedRootComponents(*record, errorMessage);
}

bool PrefabEditorPanel::addComponentOverrideFromUi(QString& errorMessage) {
    auto* record = selectedInstanceRecord();
    if (record == nullptr || record->missing || record->state.unpacked()) {
        errorMessage = tr("Select a linked prefab instance.");
        return false;
    }
    const auto name = m_addComponentCombo->currentText().toStdString();
    auto created = fabgl::createBuiltinDataComponent(m_document->reflectionRegistry(), name);
    if (!created) {
        errorMessage = errorText(created.error());
        return false;
    }
    const auto* metadata = created.value()->metadata();
    if (metadata == nullptr) {
        errorMessage = tr("Component metadata is unavailable.");
        return false;
    }
    fabgl::PrefabComponentData component{metadata->typeId, metadata->name, {}};
    for (const auto& property : metadata->properties) {
        if (property.defaultValue) {
            component.properties.emplace(property.name, *property.defaultValue);
        }
    }
    const auto added = record->state.addComponentOverride(std::move(component));
    if (!added) {
        errorMessage = errorText(added.error());
        return false;
    }
    return applyResolvedRootComponents(*record, errorMessage);
}

bool PrefabEditorPanel::removeComponentOverrideFromUi(QString& errorMessage) {
    auto* record = selectedInstanceRecord();
    if (record == nullptr || record->missing || record->state.unpacked()) {
        errorMessage = tr("Select a linked prefab instance.");
        return false;
    }
    const auto componentId = fabgl::ComponentTypeGuid::parse(
        m_overrideComponentCombo->currentData().toString().toStdString());
    const auto linkType = instanceLinkType();
    if (!componentId || componentId.value() == fabgl::TransformComponent::staticTypeId() ||
        (linkType && componentId.value() == *linkType)) {
        errorMessage = tr("Select a removable non-transform component.");
        return false;
    }
    record->state.removeComponentOverride(componentId.value());
    return applyResolvedRootComponents(*record, errorMessage);
}

bool PrefabEditorPanel::revertSelectedInstance(QString& errorMessage) {
    auto* record = selectedInstanceRecord();
    if (record == nullptr || record->missing || record->state.unpacked()) {
        errorMessage = tr("Select a linked prefab instance.");
        return false;
    }
    record->state.revertAll();
    return applyResolvedRootComponents(*record, errorMessage);
}

bool PrefabEditorPanel::applySelectedInstance(QString& errorMessage) {
    auto* record = selectedInstanceRecord();
    if (record == nullptr || record->missing || record->state.unpacked() || !m_hasCurrent ||
        record->prefab != m_currentPrefab.id) {
        errorMessage = tr("Select an instance of the currently open prefab.");
        return false;
    }
    const auto applied = record->state.applyTo(m_currentPrefab);
    if (!applied) {
        errorMessage = errorText(applied.error());
        return false;
    }
    if (!writePrefab(m_currentPath, m_currentPrefab, errorMessage)) {
        return false;
    }
    if (!rebuildLibrary(errorMessage)) {
        return false;
    }
    for (auto& instance : m_instances) {
        if (!instance.missing && !instance.state.unpacked() &&
            instance.prefab == m_currentPrefab.id &&
            !applyResolvedRootComponents(instance, errorMessage)) {
            return false;
        }
    }
    return true;
}

bool PrefabEditorPanel::unpackSelectedInstance(QString& errorMessage) {
    auto* record = selectedInstanceRecord();
    if (record == nullptr || record->missing || record->state.unpacked()) {
        errorMessage = tr("Select a linked prefab instance.");
        return false;
    }
    const auto root = record->root;
    const auto unpacked = record->state.unpack(m_library);
    if (!unpacked) {
        errorMessage = errorText(unpacked.error());
        return false;
    }
    if (!removeInstanceLink(root, errorMessage)) {
        return false;
    }
    m_instances.erase(
        std::remove_if(m_instances.begin(), m_instances.end(),
                       [root](const InstanceRecord& candidate) { return candidate.root == root; }),
        m_instances.end());
    m_selectedInstance.reset();
    return true;
}

PrefabEditorPanel::InstanceRecord* PrefabEditorPanel::selectedInstanceRecord() {
    if (!m_selectedInstance) {
        return nullptr;
    }
    const auto iterator =
        std::find_if(m_instances.begin(), m_instances.end(), [this](const InstanceRecord& record) {
            return record.root == *m_selectedInstance;
        });
    return iterator == m_instances.end() ? nullptr : &*iterator;
}

const PrefabEditorPanel::InstanceRecord* PrefabEditorPanel::selectedInstanceRecord() const {
    if (!m_selectedInstance) {
        return nullptr;
    }
    const auto iterator = std::find_if(
        m_instances.cbegin(), m_instances.cend(),
        [this](const InstanceRecord& record) { return record.root == *m_selectedInstance; });
    return iterator == m_instances.cend() ? nullptr : &*iterator;
}

PrefabEditorPanel::StateSnapshot
PrefabEditorPanel::captureState(const std::vector<QString>& affectedFiles,
                                QString& errorMessage) const {
    StateSnapshot state;
    state.projectRoot = m_projectRoot;
    state.projectGuid = m_projectGuid;
    if (m_document != nullptr) {
        state.scene = m_document->serialized(errorMessage);
        if (state.scene.isNull()) {
            return state;
        }
        state.sceneModified = m_document->isModified();
    }
    state.hasCurrent = m_hasCurrent;
    state.current = m_currentPrefab;
    state.currentPath = m_currentPath;
    state.missingGuid = m_missingGuid;
    state.assets = m_projectAssets;
    state.instances = m_instances;
    state.selectedInstance = m_selectedInstance;
    state.files.reserve(affectedFiles.size());
    for (const auto& path : affectedFiles) {
        FileSnapshot fileState;
        fileState.path = path;
        fileState.existed = QFileInfo::exists(path);
        if (fileState.existed) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                errorMessage = tr("Cannot snapshot %1 for undo: %2")
                                   .arg(QDir::toNativeSeparators(path), file.errorString());
                return {};
            }
            fileState.bytes = file.readAll();
        }
        state.files.push_back(std::move(fileState));
    }
    return state;
}

bool PrefabEditorPanel::restoreState(const StateSnapshot& state, QString& errorMessage) {
    if (m_restoring) {
        return true;
    }
    if (state.projectRoot != m_projectRoot || state.projectGuid != m_projectGuid) {
        errorMessage = tr("Prefab history belongs to a different project and was not restored.");
        return false;
    }
    m_restoring = true;
    const auto guard = qScopeGuard([this]() { m_restoring = false; });
    for (const auto& file : state.files) {
        if (file.existed) {
            if (!writeBytesAtomically(file.path, file.bytes, errorMessage)) {
                return false;
            }
        } else if (QFileInfo::exists(file.path) && !QFile::remove(file.path)) {
            errorMessage = tr("Cannot remove newly created prefab during undo: %1")
                               .arg(QDir::toNativeSeparators(file.path));
            return false;
        }
    }
    if (m_document != nullptr && !state.scene.isNull() &&
        !m_document->restoreSerialized(state.scene, errorMessage, state.sceneModified)) {
        return false;
    }
    m_hasCurrent = state.hasCurrent;
    m_currentPrefab = state.current;
    m_currentPath = state.currentPath;
    m_missingGuid = state.missingGuid;
    m_projectAssets = state.assets;
    m_instances = state.instances;
    m_selectedInstance = state.selectedInstance;
    emit projectAssetsChanged(m_projectAssets);
    QString dependencyError;
    (void)rebuildLibrary(dependencyError);
    refreshUi();
    return true;
}

bool PrefabEditorPanel::runSnapshotEdit(const QString& description,
                                        const std::vector<QString>& affectedFiles,
                                        const std::function<bool(QString&)>& edit,
                                        QString& errorMessage) {
    if (m_restoring) {
        errorMessage = tr("A prefab undo/redo restore is already in progress.");
        return false;
    }
    errorMessage.clear();
    const auto before = captureState(affectedFiles, errorMessage);
    if (!errorMessage.isEmpty()) {
        return false;
    }
    QString operationError;
    if (!edit(operationError)) {
        QString restoreError;
        if (!restoreState(before, restoreError) && !restoreError.isEmpty()) {
            operationError += tr(" Rollback failed: %1").arg(restoreError);
        }
        errorMessage = operationError;
        return false;
    }
    const auto after = captureState(affectedFiles, errorMessage);
    if (!errorMessage.isEmpty()) {
        QString ignored;
        (void)restoreState(before, ignored);
        return false;
    }
    QPointer<PrefabEditorPanel> panel(this);
    m_undoStack->push(new AppliedCallbackCommand(
        description,
        [panel, before]() {
            if (panel == nullptr) {
                return;
            }
            QString error;
            if (!panel->restoreState(before, error)) {
                emit panel->statusMessage(QObject::tr("Prefab undo failed: %1").arg(error));
            }
        },
        [panel, after]() {
            if (panel == nullptr) {
                return;
            }
            QString error;
            if (!panel->restoreState(after, error)) {
                emit panel->statusMessage(QObject::tr("Prefab redo failed: %1").arg(error));
            }
        }));
    refreshUi();
    return true;
}

} // namespace fgl::studio
