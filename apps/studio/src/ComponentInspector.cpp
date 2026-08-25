#include "ComponentInspector.h"

#include "EntityCommands.h"
#include "SceneDocument.h"

#include <fabgl/scene/builtin_components.h>
#include <fabgl/scene/entity.h>
#include <fabgl/scene/transform_component.h>

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QSlider>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <type_traits>
#include <utility>
#include <vector>

namespace fgl::studio {
namespace {

QString viewText(const std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString componentDisplayName(const fabgl::Component& component) {
    const auto* metadata = component.metadata();
    if (metadata != nullptr && !metadata->displayName.empty()) {
        return QString::fromStdString(metadata->displayName);
    }
    auto name = viewText(component.typeName());
    if (name.startsWith(QStringLiteral("fabgl."))) {
        name.remove(0, 6);
    }
    return name;
}

QString propertyDisplayName(const fabgl::PropertyMetadata& metadata) {
    return QString::fromStdString(metadata.displayName.empty() ? metadata.name
                                                               : metadata.displayName);
}

QString editorObjectName(const QString& componentName, const std::string& propertyName) {
    return QStringLiteral("property.%1.%2")
        .arg(componentName, QString::fromStdString(propertyName));
}

void configureNumberEditor(QDoubleSpinBox& editor, const fabgl::PropertyMetadata& metadata,
                           const double fallbackMinimum, const double fallbackMaximum,
                           const double fallbackStep, const int decimals) {
    const double minimum = metadata.numeric.minimum.value_or(fallbackMinimum);
    const double maximum = metadata.numeric.maximum.value_or(fallbackMaximum);
    editor.setRange(std::min(minimum, maximum), std::max(minimum, maximum));
    editor.setSingleStep(std::max(metadata.numeric.step.value_or(fallbackStep), 0.000001));
    editor.setDecimals(decimals);
    editor.setKeyboardTracking(false);
}

void styleColorButton(QPushButton& button, const QColor& color) {
    button.setText(QStringLiteral("RGBA %1, %2, %3, %4")
                       .arg(color.red())
                       .arg(color.green())
                       .arg(color.blue())
                       .arg(color.alpha()));
    const QColor foreground = color.lightness() < 128 ? QColor(Qt::white) : QColor(Qt::black);
    button.setStyleSheet(QStringLiteral("background-color: rgba(%1,%2,%3,%4); color: %5;")
                             .arg(color.red())
                             .arg(color.green())
                             .arg(color.blue())
                             .arg(color.alpha())
                             .arg(foreground.name()));
    button.setProperty("inspectorColor", color);
}

QString listElementText(const fabgl::PropertyListElement& value) {
    return std::visit(
        [](const auto& typed) -> QString {
            using Value = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<Value, bool>)
                return typed ? QStringLiteral("true") : QStringLiteral("false");
            else if constexpr (std::is_same_v<Value, std::int64_t> ||
                               std::is_same_v<Value, std::uint64_t>)
                return QString::number(typed);
            else if constexpr (std::is_same_v<Value, double>)
                return QString::number(typed, 'g', 15);
            else if constexpr (std::is_same_v<Value, fabgl::Fixed>)
                return QString::number(typed.toFloat(), 'g', 8);
            else if constexpr (std::is_same_v<Value, std::string>)
                return QString::fromStdString(typed);
            else if constexpr (std::is_same_v<Value, fabgl::Vec2>)
                return QStringLiteral("%1, %2").arg(typed.x).arg(typed.y);
            else if constexpr (std::is_same_v<Value, fabgl::Vec3> ||
                               std::is_same_v<Value, fabgl::EulerAngles>)
                return QStringLiteral("%1, %2, %3").arg(typed.x).arg(typed.y).arg(typed.z);
            else if constexpr (std::is_same_v<Value, fabgl::Quaternion>)
                return QStringLiteral("%1, %2, %3, %4")
                    .arg(typed.x)
                    .arg(typed.y)
                    .arg(typed.z)
                    .arg(typed.w);
            else if constexpr (std::is_same_v<Value, fabgl::Rect>)
                return QStringLiteral("%1, %2, %3, %4")
                    .arg(typed.x)
                    .arg(typed.y)
                    .arg(typed.width)
                    .arg(typed.height);
            else if constexpr (std::is_same_v<Value, fabgl::Color>)
                return QStringLiteral("%1, %2, %3, %4")
                    .arg(static_cast<unsigned int>(typed.r))
                    .arg(static_cast<unsigned int>(typed.g))
                    .arg(static_cast<unsigned int>(typed.b))
                    .arg(static_cast<unsigned int>(typed.a));
            else if constexpr (std::is_same_v<Value, fabgl::AssetGuid> ||
                               std::is_same_v<Value, fabgl::EntityGuid>)
                return typed.isNil() ? QString{} : QString::fromStdString(typed.toString());
            else if constexpr (std::is_same_v<Value, fabgl::ComponentReference>)
                return typed.entity.isNil()
                           ? QString{}
                           : QStringLiteral("%1 / %2")
                                 .arg(QString::fromStdString(typed.entity.toString()),
                                      QString::fromStdString(typed.component.toString()));
            else
                return QString::fromStdString(typed.name);
        },
        value);
}

std::optional<fabgl::PropertyListElement> parseListElementText(fabgl::PropertyType type,
                                                               const QString& source) {
    const auto text = source.trimmed();
    const auto parts = text.split(QLatin1Char(','), Qt::KeepEmptyParts);
    const auto doubles = [&parts](const qsizetype expected) -> std::optional<std::vector<double>> {
        if (parts.size() != expected)
            return std::nullopt;
        std::vector<double> result;
        result.reserve(static_cast<std::size_t>(expected));
        for (const auto& part : parts) {
            bool ok = false;
            const double value = part.trimmed().toDouble(&ok);
            if (!ok || !std::isfinite(value))
                return std::nullopt;
            result.push_back(value);
        }
        return result;
    };
    switch (type) {
    case fabgl::PropertyType::Boolean:
        if (text.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0 || text == "1")
            return fabgl::PropertyListElement(true);
        if (text.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0 || text == "0")
            return fabgl::PropertyListElement(false);
        return std::nullopt;
    case fabgl::PropertyType::SignedInteger:
    case fabgl::PropertyType::Enumeration: {
        bool ok = false;
        const auto value = text.toLongLong(&ok);
        return ok ? std::optional<fabgl::PropertyListElement>(
                        fabgl::PropertyListElement(static_cast<std::int64_t>(value)))
                  : std::nullopt;
    }
    case fabgl::PropertyType::UnsignedInteger:
    case fabgl::PropertyType::BitFlags: {
        bool ok = false;
        const auto value = text.toULongLong(&ok, 0);
        return ok ? std::optional<fabgl::PropertyListElement>(
                        fabgl::PropertyListElement(static_cast<std::uint64_t>(value)))
                  : std::nullopt;
    }
    case fabgl::PropertyType::Float: {
        bool ok = false;
        const double value = text.toDouble(&ok);
        return ok && std::isfinite(value)
                   ? std::optional<fabgl::PropertyListElement>(fabgl::PropertyListElement(value))
                   : std::nullopt;
    }
    case fabgl::PropertyType::Fixed: {
        bool ok = false;
        const float value = text.toFloat(&ok);
        return ok && std::isfinite(value)
                   ? std::optional<fabgl::PropertyListElement>(
                         fabgl::PropertyListElement(fabgl::Fixed::fromFloat(value)))
                   : std::nullopt;
    }
    case fabgl::PropertyType::String:
        if (static_cast<std::size_t>(text.toUtf8().size()) > fabgl::MaximumPropertyStringLength)
            return std::nullopt;
        return fabgl::PropertyListElement(text.toStdString());
    case fabgl::PropertyType::Vec2: {
        const auto values = doubles(2);
        return values ? std::optional<fabgl::PropertyListElement>(fabgl::Vec2{
                            static_cast<float>((*values)[0]), static_cast<float>((*values)[1])})
                      : std::nullopt;
    }
    case fabgl::PropertyType::Vec3:
    case fabgl::PropertyType::EulerAngles: {
        const auto values = doubles(3);
        if (!values)
            return std::nullopt;
        if (type == fabgl::PropertyType::Vec3)
            return fabgl::PropertyListElement(fabgl::Vec3{static_cast<float>((*values)[0]),
                                                          static_cast<float>((*values)[1]),
                                                          static_cast<float>((*values)[2])});
        return fabgl::PropertyListElement(fabgl::EulerAngles{
            static_cast<float>((*values)[0]), static_cast<float>((*values)[1]),
            static_cast<float>((*values)[2])});
    }
    case fabgl::PropertyType::Quaternion:
    case fabgl::PropertyType::Rect: {
        const auto values = doubles(4);
        if (!values)
            return std::nullopt;
        if (type == fabgl::PropertyType::Quaternion)
            return fabgl::PropertyListElement(fabgl::Quaternion{
                static_cast<float>((*values)[0]), static_cast<float>((*values)[1]),
                static_cast<float>((*values)[2]), static_cast<float>((*values)[3])});
        return fabgl::PropertyListElement(
            fabgl::Rect{static_cast<float>((*values)[0]), static_cast<float>((*values)[1]),
                        static_cast<float>((*values)[2]), static_cast<float>((*values)[3])});
    }
    case fabgl::PropertyType::Color: {
        if (parts.size() != 4)
            return std::nullopt;
        unsigned int values[4]{};
        for (qsizetype index = 0; index < 4; ++index) {
            bool ok = false;
            values[index] = parts.at(index).trimmed().toUInt(&ok);
            if (!ok || values[index] > 255U)
                return std::nullopt;
        }
        return fabgl::PropertyListElement(fabgl::Color{
            static_cast<std::uint8_t>(values[0]), static_cast<std::uint8_t>(values[1]),
            static_cast<std::uint8_t>(values[2]), static_cast<std::uint8_t>(values[3])});
    }
    case fabgl::PropertyType::AssetReference: {
        if (text.isEmpty())
            return fabgl::PropertyListElement(fabgl::AssetGuid{});
        auto value = fabgl::AssetGuid::parse(text.toStdString());
        return value ? std::optional<fabgl::PropertyListElement>(value.value()) : std::nullopt;
    }
    case fabgl::PropertyType::EntityReference: {
        if (text.isEmpty())
            return fabgl::PropertyListElement(fabgl::EntityGuid{});
        auto value = fabgl::EntityGuid::parse(text.toStdString());
        return value ? std::optional<fabgl::PropertyListElement>(value.value()) : std::nullopt;
    }
    case fabgl::PropertyType::ComponentReference: {
        if (text.isEmpty())
            return fabgl::PropertyListElement(fabgl::ComponentReference{});
        const auto split = text.split(QLatin1Char('/'));
        if (split.size() != 2)
            return std::nullopt;
        auto entity = fabgl::EntityGuid::parse(split.at(0).trimmed().toStdString());
        auto component = fabgl::ComponentTypeGuid::parse(split.at(1).trimmed().toStdString());
        return entity && component
                   ? std::optional<fabgl::PropertyListElement>(
                         fabgl::ComponentReference{entity.value(), component.value()})
                   : std::nullopt;
    }
    case fabgl::PropertyType::ActionReference:
    case fabgl::PropertyType::EventReference:
        if (static_cast<std::size_t>(text.toUtf8().size()) >
            fabgl::MaximumActionOrEventNameLength)
            return std::nullopt;
        if (type == fabgl::PropertyType::ActionReference)
            return fabgl::PropertyListElement(fabgl::ActionReference{text.toStdString()});
        return fabgl::PropertyListElement(fabgl::EventReference{text.toStdString()});
    case fabgl::PropertyType::List:
    case fabgl::PropertyType::Curve:
    case fabgl::PropertyType::AnimationCurve:
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace

ComponentInspector::ComponentInspector(SceneDocument* document, QUndoStack* undoStack,
                                       QWidget* parent)
    : QWidget(parent), m_document(document), m_undoStack(undoStack),
      m_layout(new QVBoxLayout(this)) {
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(8);
    rebuild();
}

void ComponentInspector::setEntity(const std::optional<fabgl::EntityGuid> entityId,
                                   const bool editable) {
    setEntities(entityId ? std::vector<fabgl::EntityGuid>{*entityId}
                         : std::vector<fabgl::EntityGuid>{},
                editable);
}

void ComponentInspector::setEntities(std::vector<fabgl::EntityGuid> entityIds,
                                     const bool editable) {
    std::sort(entityIds.begin(), entityIds.end());
    entityIds.erase(std::unique(entityIds.begin(), entityIds.end()), entityIds.end());
    m_entityIds = std::move(entityIds);
    m_entityId = m_entityIds.empty() ? std::nullopt
                                     : std::optional<fabgl::EntityGuid>(m_entityIds.front());
    m_editable = editable;
    rebuild();
}

void ComponentInspector::setExtensionHooks(CustomInspectorInspectHook inspectHook,
                                           CustomInspectorApplyHook applyHook) {
    m_inspectHook = std::move(inspectHook);
    m_applyHook = std::move(applyHook);
    rebuild();
}

void ComponentInspector::rebuild() {
    while (auto* item = m_layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    if (m_document == nullptr || m_entityIds.empty() || !m_entityId) {
        auto* emptyLabel = new QLabel(tr("Select one or more entities to inspect their common components."), this);
        emptyLabel->setWordWrap(true);
        m_layout->addWidget(emptyLabel);
        m_layout->addStretch();
        return;
    }

    auto* entity = m_document->scene().findEntity(*m_entityId);
    if (entity == nullptr) {
        m_layout->addWidget(new QLabel(tr("The selected entity no longer exists."), this));
        m_layout->addStretch();
        return;
    }

    if (m_entityIds.size() > 1U) {
        auto* selectionLabel =
            new QLabel(tr("%1 entities selected — showing common components and properties.")
                           .arg(static_cast<qulonglong>(m_entityIds.size())),
                       this);
        selectionLabel->setObjectName(QStringLiteral("multiSelectionSummary"));
        selectionLabel->setWordWrap(true);
        m_layout->addWidget(selectionLabel);
    }

    auto* addContainer = new QWidget(this);
    auto* addLayout = new QHBoxLayout(addContainer);
    addLayout->setContentsMargins(0, 0, 0, 0);
    auto* addCombo = new QComboBox(addContainer);
    addCombo->setObjectName(QStringLiteral("addComponentCombo"));
    addCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);

    for (const auto& name : fabgl::builtinComponentNames()) {
        if (name == "Transform" || name == "PrefabInstanceLink") {
            continue;
        }
        const auto* metadata = m_document->reflectionRegistry().find(std::string("fabgl.") + name);
        if (metadata != nullptr && entity->getComponent(metadata->typeId) == nullptr) {
            addCombo->addItem(QString::fromStdString(name));
        }
    }
    auto* addButton = new QPushButton(tr("Add Component"), addContainer);
    addButton->setObjectName(QStringLiteral("addComponentButton"));
    const bool singleSelection = m_entityIds.size() == 1U;
    addButton->setEnabled(m_editable && singleSelection && addCombo->count() > 0);
    addCombo->setEnabled(m_editable && singleSelection && addCombo->count() > 0);
    addLayout->addWidget(addCombo, 1);
    addLayout->addWidget(addButton);
    m_layout->addWidget(addContainer);

    connect(addButton, &QPushButton::clicked, this, [this, addCombo]() {
        if (!m_entityId || m_undoStack == nullptr || addCombo->currentText().isEmpty()) {
            return;
        }
        const auto entityId = *m_entityId;
        const auto typeName = addCombo->currentText();
        QTimer::singleShot(0, this, [this, entityId, typeName]() {
            if (m_undoStack != nullptr) {
                m_undoStack->push(new AddComponentCommand(m_document, entityId, typeName));
            }
        });
    });

    for (auto* component : entity->components()) {
        if (component->typeName() == "fabgl.PrefabInstanceLink") {
            continue;
        }
        const bool commonComponent = std::all_of(
            m_entityIds.cbegin(), m_entityIds.cend(), [this, component](const auto entityId) {
                const auto* selected = m_document->scene().findEntity(entityId);
                return selected != nullptr && selected->getComponent(component->typeId()) != nullptr;
            });
        if (!commonComponent)
            continue;
        const auto* metadata = component->metadata();
        const QString displayName = componentDisplayName(*component);
        auto* componentGroup = new QGroupBox(displayName, this);
        componentGroup->setObjectName(QStringLiteral("component.%1").arg(displayName));
        auto* componentLayout = new QVBoxLayout(componentGroup);

        if (component->typeId() != fabgl::TransformComponent::staticTypeId()) {
            auto* removeRow = new QWidget(componentGroup);
            auto* removeLayout = new QHBoxLayout(removeRow);
            removeLayout->setContentsMargins(0, 0, 0, 0);
            removeLayout->addStretch();
            auto* removeButton = new QToolButton(removeRow);
            removeButton->setText(tr("Remove"));
            removeButton->setToolTip(tr("Remove the %1 component").arg(displayName));
            removeButton->setObjectName(QStringLiteral("removeComponent.%1").arg(displayName));
            removeButton->setEnabled(m_editable && singleSelection);
            removeLayout->addWidget(removeButton);
            componentLayout->addWidget(removeRow);
            const auto typeId = component->typeId();
            connect(removeButton, &QToolButton::clicked, this,
                    [this, entityId = *m_entityId, typeId]() {
                        QTimer::singleShot(0, this, [this, entityId, typeId]() {
                            if (m_undoStack != nullptr) {
                                m_undoStack->push(
                                    new RemoveComponentCommand(m_document, entityId, typeId));
                            }
                        });
                    });
        }

        if (metadata == nullptr) {
            componentLayout->addWidget(new QLabel(tr("No reflected properties."), componentGroup));
            m_layout->addWidget(componentGroup);
            continue;
        }

        std::map<std::string, QFormLayout*> categories;
        for (const auto& property : metadata->properties) {
            if (fabgl::hasFlag(property.flags, fabgl::PropertyFlags::Hidden)) {
                continue;
            }
            const auto value = property.read(component);
            if (!value) {
                emit statusMessage(tr("Could not read %1.%2: %3")
                                       .arg(displayName, QString::fromStdString(property.name),
                                            QString::fromStdString(value.error().message())));
                continue;
            }
            bool mixed = false;
            bool readableForAll = true;
            for (const auto selectedId : m_entityIds) {
                const auto* selectedEntity = m_document->scene().findEntity(selectedId);
                const auto* selectedComponent =
                    selectedEntity != nullptr ? selectedEntity->getComponent(component->typeId())
                                              : nullptr;
                const auto selectedValue =
                    selectedComponent != nullptr ? property.read(selectedComponent)
                                                 : fabgl::Result<fabgl::PropertyValue>::failure(
                                                       fabgl::Error(fabgl::ErrorCode::NotFound,
                                                                    "common component vanished"));
                if (!selectedValue) {
                    readableForAll = false;
                    emit statusMessage(tr("Could not read %1.%2 for every selected entity: %3")
                                           .arg(displayName,
                                                QString::fromStdString(property.name),
                                                QString::fromStdString(
                                                    selectedValue.error().message())));
                    break;
                }
                mixed = mixed || selectedValue.value() != value.value();
            }
            if (!readableForAll)
                continue;

            CustomInspectorPresentation extension;
            if (m_inspectHook) {
                auto inspected = m_inspectHook(m_entityIds, component->typeId(), property,
                                               value.value(), mixed);
                if (!inspected) {
                    extension.handled = true;
                    extension.readOnly = true;
                    extension.tooltip =
                        tr("Extension inspector failed closed: %1")
                            .arg(QString::fromStdString(inspected.error().message()));
                    emit statusMessage(extension.tooltip);
                } else {
                    extension = std::move(inspected.value());
                }
            }
            if (extension.hidden)
                continue;
            const std::string category = property.category.empty() ? "General" : property.category;
            auto categoryIt = categories.find(category);
            if (categoryIt == categories.end()) {
                auto* categoryGroup =
                    new QGroupBox(QString::fromStdString(category), componentGroup);
                auto* form = new QFormLayout(categoryGroup);
                form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
                componentLayout->addWidget(categoryGroup);
                categoryIt = categories.emplace(category, form).first;
            }
            auto* editor = createPropertyEditor(*m_entityId, component->typeId(), displayName,
                                                property, value.value(), m_editable, mixed,
                                                extension);
            editor->setProperty("mixedState", mixed);
            if (mixed) {
                if (auto* check = qobject_cast<QCheckBox*>(editor)) {
                    const QSignalBlocker blocker(check);
                    check->setTristate(true);
                    check->setCheckState(Qt::PartiallyChecked);
                }
            }
            auto labelText = extension.displayName.isEmpty() ? propertyDisplayName(property)
                                                             : extension.displayName;
            if (mixed)
                labelText += tr(" (Mixed)");
            auto* label = new QLabel(labelText, componentGroup);
            label->setProperty("mixedState", mixed);
            const auto tooltip = !extension.tooltip.isEmpty()
                                     ? extension.tooltip
                                     : QString::fromStdString(property.tooltip);
            if (!tooltip.isEmpty()) {
                label->setToolTip(tooltip);
                editor->setToolTip(tooltip);
            }
            categoryIt->second->addRow(label, editor);
        }
        if (categories.empty()) {
            componentLayout->addWidget(new QLabel(tr("No visible properties."), componentGroup));
        }
        m_layout->addWidget(componentGroup);
    }
    m_layout->addStretch();
}

QWidget* ComponentInspector::createPropertyEditor(const fabgl::EntityGuid entityId,
                                                  const fabgl::ComponentTypeGuid typeId,
                                                  const QString& componentName,
                                                  const fabgl::PropertyMetadata& metadata,
                                                  const fabgl::PropertyValue& value,
                                                  const bool editable, const bool mixed,
                                                  const CustomInspectorPresentation& extension) {
    static_cast<void>(entityId);
    static_cast<void>(mixed);
    const bool readOnly = fabgl::hasFlag(metadata.flags, fabgl::PropertyFlags::ReadOnly);
    const bool canEdit = editable && !readOnly && !extension.readOnly;
    const QString objectName = editorObjectName(componentName, metadata.name);
    const auto submit = [this, typeId, componentName, metadata,
                         extension](fabgl::PropertyValue after) {
        submitProperty(typeId, componentName, metadata, std::move(after), extension);
    };

    if (metadata.editorHint == fabgl::PropertyEditorHint::Slider) {
        auto* container = new QWidget(this);
        container->setObjectName(objectName);
        auto* layout = new QHBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* slider = new QSlider(Qt::Horizontal, container);
        slider->setObjectName(objectName + QStringLiteral(".slider"));
        slider->setRange(0, 1000);
        auto* spin = new QDoubleSpinBox(container);
        spin->setObjectName(objectName + QStringLiteral(".spin"));
        configureNumberEditor(*spin, metadata, 0.0, 1.0, 0.01, 6);
        const double minimum = metadata.numeric.minimum.value_or(spin->minimum());
        const double maximum = metadata.numeric.maximum.value_or(spin->maximum());
        double current = 0.0;
        if (const auto* floatingValue = std::get_if<double>(&value))
            current = *floatingValue;
        else if (const auto* fixedValue = std::get_if<fabgl::Fixed>(&value))
            current = fixedValue->toFloat();
        else if (const auto* signedValue = std::get_if<std::int64_t>(&value))
            current = static_cast<double>(*signedValue);
        else if (const auto* unsignedValue = std::get_if<std::uint64_t>(&value))
            current = static_cast<double>(*unsignedValue);
        spin->setValue(current);
        const auto sliderPosition = [minimum, maximum](const double number) {
            return static_cast<int>(std::llround(
                std::clamp((number - minimum) / (maximum - minimum), 0.0, 1.0) * 1000.0));
        };
        slider->setValue(sliderPosition(current));
        slider->setEnabled(canEdit);
        spin->setEnabled(canEdit);
        layout->addWidget(slider, 1);
        layout->addWidget(spin);
        connect(slider, &QSlider::valueChanged, spin,
                [spin, minimum, maximum](const int position) {
                    const QSignalBlocker blocker(spin);
                    spin->setValue(minimum + (maximum - minimum) *
                                                 (static_cast<double>(position) / 1000.0));
                });
        connect(spin, &QDoubleSpinBox::valueChanged, slider,
                [slider, sliderPosition](const double number) {
                    const QSignalBlocker blocker(slider);
                    slider->setValue(sliderPosition(number));
                });
        const auto submitNumber = [spin, submit, propertyType = metadata.type]() {
            if (propertyType == fabgl::PropertyType::Fixed) {
                submit(fabgl::PropertyValue(
                    fabgl::Fixed::fromFloat(static_cast<float>(spin->value()))));
            } else if (propertyType == fabgl::PropertyType::SignedInteger ||
                       propertyType == fabgl::PropertyType::Enumeration) {
                submit(fabgl::PropertyValue(
                    static_cast<std::int64_t>(std::llround(spin->value()))));
            } else if (propertyType == fabgl::PropertyType::UnsignedInteger ||
                       propertyType == fabgl::PropertyType::BitFlags) {
                submit(fabgl::PropertyValue(static_cast<std::uint64_t>(
                    std::max(0.0, std::round(spin->value())))));
            } else {
                submit(fabgl::PropertyValue(spin->value()));
            }
        };
        connect(slider, &QSlider::sliderReleased, this, submitNumber);
        connect(spin, &QDoubleSpinBox::editingFinished, this, submitNumber);
        return container;
    }

    if (metadata.type == fabgl::PropertyType::String &&
        metadata.editorHint == fabgl::PropertyEditorHint::Multiline) {
        auto* container = new QWidget(this);
        container->setObjectName(objectName);
        auto* layout = new QVBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* editor = new QPlainTextEdit(QString::fromStdString(std::get<std::string>(value)),
                                          container);
        editor->setObjectName(objectName + QStringLiteral(".text"));
        editor->setReadOnly(!canEdit);
        editor->setMaximumHeight(120);
        auto* apply = new QPushButton(tr("Apply"), container);
        apply->setObjectName(objectName + QStringLiteral(".apply"));
        apply->setEnabled(canEdit);
        layout->addWidget(editor);
        layout->addWidget(apply, 0, Qt::AlignRight);
        connect(apply, &QPushButton::clicked, this, [this, editor, submit]() {
            const auto text = editor->toPlainText().toUtf8();
            if (static_cast<std::size_t>(text.size()) > fabgl::MaximumPropertyStringLength) {
                emit statusMessage(tr("Multiline property exceeds the %1-byte limit.")
                                       .arg(fabgl::MaximumPropertyStringLength));
                return;
            }
            submit(fabgl::PropertyValue(text.toStdString()));
        });
        return container;
    }

    switch (metadata.type) {
    case fabgl::PropertyType::Boolean: {
        auto* editor = new QCheckBox(this);
        editor->setObjectName(objectName);
        editor->setChecked(std::get<bool>(value));
        editor->setEnabled(canEdit);
        connect(editor, &QCheckBox::toggled, this,
                [submit](const bool checked) { submit(fabgl::PropertyValue(checked)); });
        return editor;
    }
    case fabgl::PropertyType::List: {
        const auto source = std::get<fabgl::PropertyList>(value);
        auto* container = new QWidget(this);
        container->setObjectName(objectName);
        auto* layout = new QVBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* table = new QTableWidget(static_cast<int>(source.values.size()), 1, container);
        table->setObjectName(objectName + QStringLiteral(".table"));
        table->setHorizontalHeaderLabels({tr("Value")});
        table->horizontalHeader()->setStretchLastSection(true);
        table->verticalHeader()->setVisible(false);
        for (std::size_t index = 0; index < source.values.size(); ++index)
            table->setItem(static_cast<int>(index), 0,
                           new QTableWidgetItem(listElementText(source.values.at(index))));
        auto* controls = new QWidget(container);
        auto* controlsLayout = new QHBoxLayout(controls);
        controlsLayout->setContentsMargins(0, 0, 0, 0);
        auto* add = new QPushButton(tr("Add"), controls);
        add->setObjectName(objectName + QStringLiteral(".add"));
        auto* remove = new QPushButton(tr("Remove"), controls);
        remove->setObjectName(objectName + QStringLiteral(".remove"));
        auto* apply = new QPushButton(tr("Apply"), controls);
        apply->setObjectName(objectName + QStringLiteral(".apply"));
        for (auto* button : {add, remove, apply})
            button->setEnabled(canEdit);
        if (!canEdit)
            table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        controlsLayout->addWidget(add);
        controlsLayout->addWidget(remove);
        controlsLayout->addStretch();
        controlsLayout->addWidget(apply);
        layout->addWidget(table);
        layout->addWidget(controls);
        connect(add, &QPushButton::clicked, this, [this, table]() {
            if (static_cast<std::size_t>(table->rowCount()) >= fabgl::MaximumPropertyListItems) {
                emit statusMessage(tr("Property list reached its %1-item limit.")
                                       .arg(fabgl::MaximumPropertyListItems));
                return;
            }
            const int row = table->rowCount();
            table->insertRow(row);
            table->setItem(row, 0, new QTableWidgetItem(QString{}));
            table->setCurrentCell(row, 0);
        });
        connect(remove, &QPushButton::clicked, this, [table]() {
            const int row = table->currentRow();
            if (row >= 0)
                table->removeRow(row);
        });
        connect(apply, &QPushButton::clicked, this,
                [this, table, elementType = source.elementType, submit]() {
                    fabgl::PropertyList result{elementType, {}};
                    result.values.reserve(static_cast<std::size_t>(table->rowCount()));
                    for (int row = 0; row < table->rowCount(); ++row) {
                        const auto* item = table->item(row, 0);
                        const auto parsed = parseListElementText(
                            elementType, item == nullptr ? QString{} : item->text());
                        if (!parsed) {
                            emit statusMessage(
                                tr("List item %1 is invalid for this property type.").arg(row + 1));
                            return;
                        }
                        result.values.push_back(*parsed);
                    }
                    submit(fabgl::PropertyValue(std::move(result)));
                });
        return container;
    }
    case fabgl::PropertyType::Curve:
    case fabgl::PropertyType::AnimationCurve: {
        const bool animation = metadata.type == fabgl::PropertyType::AnimationCurve;
        const int columns = animation ? 4 : 2;
        const int rows = animation
                             ? static_cast<int>(std::get<fabgl::PropertyAnimationCurve>(value)
                                                    .keys.size())
                             : static_cast<int>(std::get<fabgl::Curve>(value).points.size());
        auto* container = new QWidget(this);
        container->setObjectName(objectName);
        auto* layout = new QVBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* table = new QTableWidget(rows, columns, container);
        table->setObjectName(objectName + QStringLiteral(".table"));
        table->setHorizontalHeaderLabels(
            animation ? QStringList{tr("Time"), tr("Value"), tr("In tangent"), tr("Out tangent")}
                      : QStringList{tr("Position"), tr("Value")});
        table->horizontalHeader()->setStretchLastSection(true);
        table->verticalHeader()->setVisible(false);
        const auto setNumber = [table](const int row, const int column, const double number) {
            table->setItem(row, column,
                           new QTableWidgetItem(QString::number(number, 'g', 15)));
        };
        if (animation) {
            const auto& keys = std::get<fabgl::PropertyAnimationCurve>(value).keys;
            for (std::size_t index = 0; index < keys.size(); ++index) {
                const auto& key = keys.at(index);
                setNumber(static_cast<int>(index), 0, key.time);
                setNumber(static_cast<int>(index), 1, key.value);
                setNumber(static_cast<int>(index), 2, key.inTangent);
                setNumber(static_cast<int>(index), 3, key.outTangent);
            }
        } else {
            const auto& points = std::get<fabgl::Curve>(value).points;
            for (std::size_t index = 0; index < points.size(); ++index) {
                setNumber(static_cast<int>(index), 0, points.at(index).position);
                setNumber(static_cast<int>(index), 1, points.at(index).value);
            }
        }
        auto* controls = new QWidget(container);
        auto* controlsLayout = new QHBoxLayout(controls);
        controlsLayout->setContentsMargins(0, 0, 0, 0);
        auto* add = new QPushButton(tr("Add"), controls);
        add->setObjectName(objectName + QStringLiteral(".add"));
        auto* remove = new QPushButton(tr("Remove"), controls);
        remove->setObjectName(objectName + QStringLiteral(".remove"));
        auto* apply = new QPushButton(tr("Apply"), controls);
        apply->setObjectName(objectName + QStringLiteral(".apply"));
        for (auto* button : {add, remove, apply})
            button->setEnabled(canEdit);
        if (!canEdit)
            table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        controlsLayout->addWidget(add);
        controlsLayout->addWidget(remove);
        controlsLayout->addStretch();
        controlsLayout->addWidget(apply);
        layout->addWidget(table);
        layout->addWidget(controls);
        connect(add, &QPushButton::clicked, this, [this, table, columns]() {
            if (static_cast<std::size_t>(table->rowCount()) >= fabgl::MaximumCurvePoints) {
                emit statusMessage(
                    tr("Curve reached its %1-point limit.").arg(fabgl::MaximumCurvePoints));
                return;
            }
            const int row = table->rowCount();
            double position = 0.0;
            if (row > 0) {
                bool ok = false;
                position = table->item(row - 1, 0)->text().toDouble(&ok) + 1.0;
                if (!ok || !std::isfinite(position))
                    position = static_cast<double>(row);
            }
            table->insertRow(row);
            for (int column = 0; column < columns; ++column)
                table->setItem(row, column,
                               new QTableWidgetItem(QString::number(column == 0 ? position : 0.0)));
            table->setCurrentCell(row, 0);
        });
        connect(remove, &QPushButton::clicked, this, [table]() {
            const int row = table->currentRow();
            if (row >= 0)
                table->removeRow(row);
        });
        connect(apply, &QPushButton::clicked, this, [this, table, animation, submit]() {
            const auto cellNumber = [table](const int row, const int column) -> std::optional<double> {
                const auto* item = table->item(row, column);
                bool ok = false;
                const double number = item == nullptr ? 0.0 : item->text().toDouble(&ok);
                return ok && std::isfinite(number) ? std::optional<double>(number) : std::nullopt;
            };
            double previous = -std::numeric_limits<double>::infinity();
            fabgl::Curve curve;
            fabgl::PropertyAnimationCurve animationCurve;
            for (int row = 0; row < table->rowCount(); ++row) {
                const auto position = cellNumber(row, 0);
                const auto itemValue = cellNumber(row, 1);
                if (!position || !itemValue || *position <= previous) {
                    emit statusMessage(tr("Curve positions must be finite and strictly increasing."));
                    return;
                }
                previous = *position;
                if (animation) {
                    const auto inTangent = cellNumber(row, 2);
                    const auto outTangent = cellNumber(row, 3);
                    if (!inTangent || !outTangent) {
                        emit statusMessage(tr("Animation curve tangents must be finite."));
                        return;
                    }
                    animationCurve.keys.push_back(
                        {*position, *itemValue, *inTangent, *outTangent});
                } else {
                    curve.points.push_back({*position, *itemValue});
                }
            }
            if (animation)
                submit(fabgl::PropertyValue(std::move(animationCurve)));
            else
                submit(fabgl::PropertyValue(std::move(curve)));
        });
        return container;
    }
    case fabgl::PropertyType::SignedInteger: {
        auto* editor = new QDoubleSpinBox(this);
        editor->setObjectName(objectName);
        configureNumberEditor(*editor, metadata, -9.0e15, 9.0e15, 1.0, 0);
        editor->setValue(static_cast<double>(std::get<std::int64_t>(value)));
        editor->setEnabled(canEdit);
        connect(editor, &QDoubleSpinBox::editingFinished, this, [editor, submit]() {
            submit(fabgl::PropertyValue(static_cast<std::int64_t>(std::llround(editor->value()))));
        });
        return editor;
    }
    case fabgl::PropertyType::UnsignedInteger: {
        auto* editor = new QDoubleSpinBox(this);
        editor->setObjectName(objectName);
        configureNumberEditor(*editor, metadata, 0.0, 9.0e15, 1.0, 0);
        editor->setValue(static_cast<double>(std::get<std::uint64_t>(value)));
        editor->setEnabled(canEdit);
        connect(editor, &QDoubleSpinBox::editingFinished, this, [editor, submit]() {
            submit(fabgl::PropertyValue(
                static_cast<std::uint64_t>(std::max(0.0, std::round(editor->value())))));
        });
        return editor;
    }
    case fabgl::PropertyType::Float:
    case fabgl::PropertyType::Fixed: {
        auto* editor = new QDoubleSpinBox(this);
        editor->setObjectName(objectName);
        configureNumberEditor(*editor, metadata, -1.0e12, 1.0e12, 0.1, 6);
        const bool fixed = metadata.type == fabgl::PropertyType::Fixed;
        editor->setValue(fixed ? static_cast<double>(std::get<fabgl::Fixed>(value).toFloat())
                               : std::get<double>(value));
        editor->setEnabled(canEdit);
        connect(editor, &QDoubleSpinBox::editingFinished, this, [editor, submit, fixed]() {
            if (fixed) {
                submit(fabgl::PropertyValue(
                    fabgl::Fixed::fromFloat(static_cast<float>(editor->value()))));
            } else {
                submit(fabgl::PropertyValue(editor->value()));
            }
        });
        return editor;
    }
    case fabgl::PropertyType::String: {
        auto* editor = new QLineEdit(QString::fromStdString(std::get<std::string>(value)), this);
        editor->setObjectName(objectName);
        editor->setReadOnly(!canEdit);
        connect(editor, &QLineEdit::editingFinished, this,
                [editor, submit]() { submit(fabgl::PropertyValue(editor->text().toStdString())); });
        return editor;
    }
    case fabgl::PropertyType::Enumeration: {
        auto* editor = new QComboBox(this);
        editor->setObjectName(objectName);
        const auto current = std::get<std::int64_t>(value);
        for (const auto& option : metadata.enumOptions) {
            editor->addItem(QString::fromStdString(option.name),
                            QVariant::fromValue<qlonglong>(option.value));
        }
        if (metadata.enumOptions.empty()) {
            editor->setEditable(true);
            editor->setInsertPolicy(QComboBox::NoInsert);
            editor->lineEdit()->setValidator(new QRegularExpressionValidator(
                QRegularExpression(QStringLiteral("-?[0-9]+")), editor));
            editor->setCurrentText(QString::number(current));
            connect(editor->lineEdit(), &QLineEdit::editingFinished, this, [editor, submit]() {
                bool ok = false;
                const auto selected = editor->currentText().toLongLong(&ok);
                if (ok) {
                    submit(fabgl::PropertyValue(static_cast<std::int64_t>(selected)));
                }
            });
        } else {
            editor->setCurrentIndex(editor->findData(QVariant::fromValue<qlonglong>(current)));
            connect(editor, &QComboBox::activated, this, [editor, submit](const int index) {
                submit(fabgl::PropertyValue(
                    static_cast<std::int64_t>(editor->itemData(index).toLongLong())));
            });
        }
        editor->setEnabled(canEdit);
        return editor;
    }
    case fabgl::PropertyType::BitFlags: {
        const auto current = std::get<std::uint64_t>(value);
        if (metadata.enumOptions.empty()) {
            auto* editor = new QLineEdit(QString::number(current), this);
            editor->setObjectName(objectName);
            editor->setValidator(new QRegularExpressionValidator(
                QRegularExpression(QStringLiteral("(?:0[xX][0-9A-Fa-f]+|[0-9]+)")), editor));
            editor->setReadOnly(!canEdit);
            connect(editor, &QLineEdit::editingFinished, this, [editor, submit]() {
                bool ok = false;
                const auto flags = editor->text().toULongLong(&ok, 0);
                if (ok) {
                    submit(fabgl::PropertyValue(static_cast<std::uint64_t>(flags)));
                }
            });
            return editor;
        }
        auto* container = new QWidget(this);
        container->setObjectName(objectName);
        auto* layout = new QVBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        std::vector<std::pair<QCheckBox*, std::uint64_t>> flags;
        for (const auto& option : metadata.enumOptions) {
            auto* check = new QCheckBox(QString::fromStdString(option.name), container);
            const auto flag = static_cast<std::uint64_t>(option.value);
            check->setChecked(flag == 0U ? current == 0U : (current & flag) == flag);
            check->setEnabled(canEdit);
            flags.emplace_back(check, flag);
            layout->addWidget(check);
        }
        for (const auto& flagEntry : flags) {
            connect(flagEntry.first, &QCheckBox::toggled, this, [flags, submit]() {
                std::uint64_t selected = 0;
                for (const auto& [flagCheck, flag] : flags) {
                    if (flagCheck->isChecked()) {
                        selected |= flag;
                    }
                }
                submit(fabgl::PropertyValue(selected));
            });
        }
        return container;
    }
    case fabgl::PropertyType::Vec2:
    case fabgl::PropertyType::Vec3:
    case fabgl::PropertyType::EulerAngles:
    case fabgl::PropertyType::Quaternion:
    case fabgl::PropertyType::Rect: {
        auto* container = new QWidget(this);
        container->setObjectName(objectName);
        auto* layout = new QHBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        std::vector<QDoubleSpinBox*> editors;
        std::vector<double> values;
        std::vector<QString> axes;
        if (metadata.type == fabgl::PropertyType::Vec2) {
            const auto vector = std::get<fabgl::Vec2>(value);
            values = {vector.x, vector.y};
            axes = {QStringLiteral("X"), QStringLiteral("Y")};
        } else if (metadata.type == fabgl::PropertyType::Vec3) {
            const auto vector = std::get<fabgl::Vec3>(value);
            values = {vector.x, vector.y, vector.z};
            axes = {QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
        } else if (metadata.type == fabgl::PropertyType::EulerAngles) {
            const auto euler = std::get<fabgl::EulerAngles>(value);
            values = {euler.x, euler.y, euler.z};
            axes = {QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
        } else if (metadata.type == fabgl::PropertyType::Quaternion) {
            const auto quaternion = std::get<fabgl::Quaternion>(value);
            values = {quaternion.x, quaternion.y, quaternion.z, quaternion.w};
            axes = {QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z"),
                    QStringLiteral("W")};
        } else {
            const auto rectangle = std::get<fabgl::Rect>(value);
            values = {rectangle.x, rectangle.y, rectangle.width, rectangle.height};
            axes = {QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("W"),
                    QStringLiteral("H")};
        }
        for (std::size_t index = 0; index < values.size(); ++index) {
            auto* editor = new QDoubleSpinBox(container);
            configureNumberEditor(*editor, metadata, -1.0e9, 1.0e9, 0.1, 4);
            editor->setPrefix(axes.at(index) + QStringLiteral(": "));
            editor->setValue(values.at(index));
            editor->setEnabled(canEdit);
            editors.push_back(editor);
            layout->addWidget(editor);
        }
        const auto propertyType = metadata.type;
        for (auto* editor : editors) {
            connect(editor, &QDoubleSpinBox::editingFinished, this,
                    [editors, propertyType, submit]() {
                        if (propertyType == fabgl::PropertyType::Vec2) {
                            submit(fabgl::PropertyValue(
                                fabgl::Vec2{static_cast<float>(editors.at(0)->value()),
                                            static_cast<float>(editors.at(1)->value())}));
                        } else if (propertyType == fabgl::PropertyType::Vec3) {
                            submit(fabgl::PropertyValue(
                                fabgl::Vec3{static_cast<float>(editors.at(0)->value()),
                                            static_cast<float>(editors.at(1)->value()),
                                            static_cast<float>(editors.at(2)->value())}));
                        } else if (propertyType == fabgl::PropertyType::EulerAngles) {
                            submit(fabgl::PropertyValue(fabgl::EulerAngles{
                                static_cast<float>(editors.at(0)->value()),
                                static_cast<float>(editors.at(1)->value()),
                                static_cast<float>(editors.at(2)->value())}));
                        } else if (propertyType == fabgl::PropertyType::Quaternion) {
                            submit(fabgl::PropertyValue(fabgl::Quaternion{
                                static_cast<float>(editors.at(0)->value()),
                                static_cast<float>(editors.at(1)->value()),
                                static_cast<float>(editors.at(2)->value()),
                                static_cast<float>(editors.at(3)->value())}));
                        } else {
                            submit(fabgl::PropertyValue(
                                fabgl::Rect{static_cast<float>(editors.at(0)->value()),
                                            static_cast<float>(editors.at(1)->value()),
                                            static_cast<float>(editors.at(2)->value()),
                                            static_cast<float>(editors.at(3)->value())}));
                        }
                    });
        }
        return container;
    }
    case fabgl::PropertyType::Color: {
        const auto source = std::get<fabgl::Color>(value);
        auto* editor = new QPushButton(this);
        editor->setObjectName(objectName);
        styleColorButton(*editor, QColor(source.r, source.g, source.b, source.a));
        editor->setEnabled(canEdit);
        connect(editor, &QPushButton::clicked, this, [this, editor, submit]() {
            const auto initial = editor->property("inspectorColor").value<QColor>();
            const auto selected = QColorDialog::getColor(initial, this, tr("Choose Color"),
                                                         QColorDialog::ShowAlphaChannel);
            if (!selected.isValid()) {
                return;
            }
            styleColorButton(*editor, selected);
            submit(fabgl::PropertyValue(fabgl::Color{static_cast<std::uint8_t>(selected.red()),
                                                     static_cast<std::uint8_t>(selected.green()),
                                                     static_cast<std::uint8_t>(selected.blue()),
                                                     static_cast<std::uint8_t>(selected.alpha())}));
        });
        return editor;
    }
    case fabgl::PropertyType::AssetReference: {
        auto* container = new QWidget(this);
        auto* layout = new QHBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* editor = new QLineEdit(
            QString::fromStdString(std::get<fabgl::AssetGuid>(value).toString()), container);
        editor->setObjectName(objectName);
        editor->setReadOnly(!canEdit);
        editor->setPlaceholderText(tr("Asset GUID"));
        auto* browse = new QToolButton(container);
        browse->setText(QStringLiteral("..."));
        browse->setToolTip(
            metadata.assetTypeFilter.empty()
                ? tr("Choose an asset")
                : tr("Choose a %1 asset").arg(QString::fromStdString(metadata.assetTypeFilter)));
        browse->setEnabled(canEdit);
        layout->addWidget(editor, 1);
        layout->addWidget(browse);
        const auto parseAndSubmit = [this, editor, submit]() {
            const auto text = editor->text().trimmed();
            if (text.isEmpty()) {
                submit(fabgl::PropertyValue(fabgl::AssetGuid{}));
                return;
            }
            const auto parsed = fabgl::AssetGuid::parse(text.toStdString());
            if (!parsed) {
                emit statusMessage(tr("Invalid asset GUID: %1").arg(text));
                return;
            }
            submit(fabgl::PropertyValue(parsed.value()));
        };
        connect(editor, &QLineEdit::editingFinished, this, parseAndSubmit);
        connect(browse, &QToolButton::clicked, this, [this, editor, submit]() {
            const auto filePath = QFileDialog::getOpenFileName(this, tr("Choose Asset"));
            if (filePath.isEmpty()) {
                return;
            }
            const auto asset = fabgl::AssetGuid::fromStableName(filePath.toStdString());
            editor->setText(QString::fromStdString(asset.toString()));
            submit(fabgl::PropertyValue(asset));
        });
        return container;
    }
    case fabgl::PropertyType::EntityReference: {
        auto* editor = new QComboBox(this);
        editor->setObjectName(objectName);
        editor->addItem(tr("None"), QString{});
        for (const auto* entity : m_document->scene().entities()) {
            editor->addItem(QString::fromStdString(entity->name()),
                            SceneDocument::guidString(entity->id()));
        }
        const auto current = std::get<fabgl::EntityGuid>(value);
        editor->setCurrentIndex(
            editor->findData(current.isNil() ? QString{} : SceneDocument::guidString(current)));
        editor->setEnabled(canEdit);
        connect(editor, &QComboBox::activated, this, [this, editor, submit](const int index) {
            const auto text = editor->itemData(index).toString();
            const auto parsed = SceneDocument::parseEntityGuid(text);
            submit(fabgl::PropertyValue(parsed.value_or(fabgl::EntityGuid{})));
        });
        return editor;
    }
    case fabgl::PropertyType::ComponentReference: {
        const auto current = std::get<fabgl::ComponentReference>(value);
        auto* container = new QWidget(this);
        container->setObjectName(objectName);
        auto* layout = new QHBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* entityEditor = new QComboBox(container);
        entityEditor->setObjectName(objectName + QStringLiteral(".entity"));
        entityEditor->addItem(tr("None"), QString{});
        for (const auto* entity : m_document->scene().entities()) {
            entityEditor->addItem(QString::fromStdString(entity->name()),
                                  SceneDocument::guidString(entity->id()));
        }
        entityEditor->setCurrentIndex(entityEditor->findData(
            current.entity.isNil() ? QString{} : SceneDocument::guidString(current.entity)));
        auto* componentEditor = new QComboBox(container);
        componentEditor->setObjectName(objectName + QStringLiteral(".component"));
        const auto populateComponents = [this, entityEditor, componentEditor](
                                            fabgl::ComponentTypeGuid selected) {
            const QSignalBlocker blocker(componentEditor);
            componentEditor->clear();
            componentEditor->addItem(tr("None"), QString{});
            const auto selectedEntityId =
                SceneDocument::parseEntityGuid(entityEditor->currentData().toString());
            const auto* entity = selectedEntityId
                                     ? m_document->scene().findEntity(*selectedEntityId)
                                     : nullptr;
            if (entity != nullptr) {
                for (const auto* component : entity->components()) {
                    componentEditor->addItem(componentDisplayName(*component),
                                             QString::fromStdString(component->typeId().toString()));
                }
            }
            componentEditor->setCurrentIndex(
                selected.isNil()
                    ? 0
                    : componentEditor->findData(QString::fromStdString(selected.toString())));
        };
        populateComponents(current.component);
        entityEditor->setEnabled(canEdit);
        componentEditor->setEnabled(canEdit);
        layout->addWidget(entityEditor, 1);
        layout->addWidget(componentEditor, 1);
        connect(entityEditor, &QComboBox::activated, this,
                [populateComponents, entityEditor, submit](const int) {
                    populateComponents(fabgl::ComponentTypeGuid{});
                    if (entityEditor->currentData().toString().isEmpty())
                        submit(fabgl::PropertyValue(fabgl::ComponentReference{}));
                });
        connect(componentEditor, &QComboBox::activated, this,
                [this, entityEditor, componentEditor, submit](const int) {
                    const auto entity =
                        SceneDocument::parseEntityGuid(entityEditor->currentData().toString());
                    const auto component = fabgl::ComponentTypeGuid::parse(
                        componentEditor->currentData().toString().toStdString());
                    if (entity && component)
                        submit(fabgl::PropertyValue(
                            fabgl::ComponentReference{*entity, component.value()}));
                });
        return container;
    }
    case fabgl::PropertyType::ActionReference:
    case fabgl::PropertyType::EventReference: {
        const auto text = metadata.type == fabgl::PropertyType::ActionReference
                              ? std::get<fabgl::ActionReference>(value).name
                              : std::get<fabgl::EventReference>(value).name;
        auto* editor = new QLineEdit(QString::fromStdString(text), this);
        editor->setObjectName(objectName);
        editor->setMaxLength(static_cast<int>(fabgl::MaximumActionOrEventNameLength));
        editor->setPlaceholderText(metadata.type == fabgl::PropertyType::ActionReference
                                       ? tr("Input action")
                                       : tr("Event name"));
        editor->setReadOnly(!canEdit);
        connect(editor, &QLineEdit::editingFinished, this,
                [editor, submit, type = metadata.type]() {
                    if (type == fabgl::PropertyType::ActionReference)
                        submit(fabgl::PropertyValue(
                            fabgl::ActionReference{editor->text().toStdString()}));
                    else
                        submit(fabgl::PropertyValue(
                            fabgl::EventReference{editor->text().toStdString()}));
                });
        return editor;
    }
    }

    auto* unsupported = new QLabel(tr("Unsupported property type"), this);
    unsupported->setObjectName(objectName);
    unsupported->setEnabled(false);
    return unsupported;
}

void ComponentInspector::submitProperty(const fabgl::ComponentTypeGuid typeId,
                                        QString componentName,
                                        fabgl::PropertyMetadata metadata,
                                        fabgl::PropertyValue after,
                                        CustomInspectorPresentation extension) {
    if (!m_editable || m_undoStack == nullptr || m_entityIds.empty()) {
        return;
    }
    if (extension.handled) {
        if (!m_applyHook || extension.serviceId.isEmpty()) {
            emit statusMessage(tr("The extension inspector did not provide an applicable mutation hook."));
            return;
        }
        auto transformed = m_applyHook(extension.serviceId, m_entityIds, typeId, metadata, after);
        if (!transformed) {
            emit statusMessage(tr("Extension inspector rejected the edit: %1")
                                   .arg(QString::fromStdString(transformed.error().message())));
            return;
        }
        after = std::move(transformed.value());
    }

    std::vector<ComponentPropertyEdit> edits;
    edits.reserve(m_entityIds.size());
    for (const auto entityId : m_entityIds) {
        QString error;
        auto before =
            m_document->componentProperty(entityId, typeId, metadata.name, error);
        if (!before) {
            emit statusMessage(tr("Multi-edit was cancelled before mutation: %1").arg(error));
            return;
        }
        auto valid = fabgl::validatePropertyValue(metadata, after);
        if (!valid) {
            emit statusMessage(tr("Multi-edit value is invalid: %1")
                                   .arg(QString::fromStdString(valid.error().message())));
            return;
        }
        edits.push_back({entityId, std::move(*before), after});
    }
    const bool changed = std::any_of(edits.cbegin(), edits.cend(), [](const auto& edit) {
        return edit.before != edit.after;
    });
    if (!changed)
        return;

    QTimer::singleShot(
        0, this,
        [this, typeId, componentName = std::move(componentName),
         propertyName = std::move(metadata.name), edits = std::move(edits)]() mutable {
            if (m_undoStack == nullptr) {
                return;
            }
            const QString description =
                tr("Edit %1.%2").arg(componentName, QString::fromStdString(propertyName));
            m_undoStack->push(new EditMultipleComponentPropertiesCommand(
                m_document, typeId, std::move(propertyName), std::move(edits), description));
        });
}

} // namespace fgl::studio
