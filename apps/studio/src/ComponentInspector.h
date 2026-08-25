#pragma once

#include <fabgl/core/guid.h>
#include <fabgl/reflection/reflection.h>

#include <QWidget>
#include <QString>

#include <optional>
#include <functional>
#include <string>
#include <vector>

class QUndoStack;
class QVBoxLayout;

namespace fgl::studio {

class SceneDocument;

struct CustomInspectorPresentation final {
    bool handled = false;
    bool hidden = false;
    bool readOnly = false;
    QString serviceId;
    QString displayName;
    QString tooltip;
};

using CustomInspectorInspectHook = std::function<fabgl::Result<CustomInspectorPresentation>(
    const std::vector<fabgl::EntityGuid>&, fabgl::ComponentTypeGuid,
    const fabgl::PropertyMetadata&, const fabgl::PropertyValue&, bool)>;
using CustomInspectorApplyHook = std::function<fabgl::Result<fabgl::PropertyValue>(
    const QString&, const std::vector<fabgl::EntityGuid>&, fabgl::ComponentTypeGuid,
    const fabgl::PropertyMetadata&, const fabgl::PropertyValue&)>;

class ComponentInspector final : public QWidget {
    Q_OBJECT

  public:
    explicit ComponentInspector(SceneDocument* document, QUndoStack* undoStack,
                                QWidget* parent = nullptr);

    void setEntity(std::optional<fabgl::EntityGuid> entityId, bool editable);
    void setEntities(std::vector<fabgl::EntityGuid> entityIds, bool editable);
    void setExtensionHooks(CustomInspectorInspectHook inspectHook,
                           CustomInspectorApplyHook applyHook);

  signals:
    void statusMessage(const QString& message);

  private:
    [[nodiscard]] QWidget* createPropertyEditor(fabgl::EntityGuid entityId,
                                                fabgl::ComponentTypeGuid typeId,
                                                const QString& componentName,
                                                const fabgl::PropertyMetadata& metadata,
                                                const fabgl::PropertyValue& value, bool editable,
                                                bool mixed,
                                                const CustomInspectorPresentation& extension);
    void submitProperty(fabgl::ComponentTypeGuid typeId, QString componentName,
                        fabgl::PropertyMetadata metadata, fabgl::PropertyValue after,
                        CustomInspectorPresentation extension);
    void rebuild();

    SceneDocument* m_document = nullptr;
    QUndoStack* m_undoStack = nullptr;
    QVBoxLayout* m_layout = nullptr;
    std::optional<fabgl::EntityGuid> m_entityId;
    std::vector<fabgl::EntityGuid> m_entityIds;
    CustomInspectorInspectHook m_inspectHook;
    CustomInspectorApplyHook m_applyHook;
    bool m_editable = false;
};

} // namespace fgl::studio
