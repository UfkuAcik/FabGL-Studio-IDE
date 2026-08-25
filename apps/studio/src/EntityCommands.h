#pragma once

#include "SceneDocument.h"

#include <QUndoCommand>

#include <functional>
#include <string>
#include <vector>

namespace fgl::studio {

class MoveAssetCommand final : public QUndoCommand {
  public:
    using Relocate = std::function<bool(const QString&, const QString&, QString&)>;
    using ReportError = std::function<void(const QString&)>;

    MoveAssetCommand(QString sourcePath, QString destinationPath, Relocate relocate,
                     ReportError reportError, QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;

  private:
    void apply(const QString& sourcePath, const QString& destinationPath, bool initialRedo);

    QString m_sourcePath;
    QString m_destinationPath;
    Relocate m_relocate;
    ReportError m_reportError;
    bool m_initialRedo = true;
};

class AddEntityCommand final : public QUndoCommand {
  public:
    AddEntityCommand(SceneDocument* document, EntitySnapshot entity,
                     QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;
    [[nodiscard]] fabgl::EntityGuid entityId() const noexcept;

  private:
    SceneDocument* m_document = nullptr;
    EntitySnapshot m_entity;
};

class DeleteEntityCommand final : public QUndoCommand {
  public:
    DeleteEntityCommand(SceneDocument* document, fabgl::EntityGuid id,
                        QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;

  private:
    SceneDocument* m_document = nullptr;
    EntitySnapshot m_entity;
};

class EditEntityCommand final : public QUndoCommand {
  public:
    EditEntityCommand(SceneDocument* document, EntitySnapshot before, EntitySnapshot after,
                      QString description, QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;

  private:
    void apply(const EntitySnapshot& snapshot);

    SceneDocument* m_document = nullptr;
    EntitySnapshot m_before;
    EntitySnapshot m_after;
};

class AddComponentCommand final : public QUndoCommand {
  public:
    AddComponentCommand(SceneDocument* document, fabgl::EntityGuid entityId, QString typeName,
                        QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;

  private:
    SceneDocument* m_document = nullptr;
    fabgl::EntityGuid m_entityId;
    QString m_typeName;
    std::optional<ComponentSnapshot> m_snapshot;
};

class RemoveComponentCommand final : public QUndoCommand {
  public:
    RemoveComponentCommand(SceneDocument* document, fabgl::EntityGuid entityId,
                           fabgl::ComponentTypeGuid typeId, QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;

  private:
    SceneDocument* m_document = nullptr;
    fabgl::EntityGuid m_entityId;
    ComponentSnapshot m_snapshot;
};

class EditComponentPropertyCommand final : public QUndoCommand {
  public:
    EditComponentPropertyCommand(SceneDocument* document, fabgl::EntityGuid entityId,
                                 fabgl::ComponentTypeGuid typeId, std::string propertyName,
                                 fabgl::PropertyValue before, fabgl::PropertyValue after,
                                 QString description, QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;

  private:
    void apply(const fabgl::PropertyValue& value);

    SceneDocument* m_document = nullptr;
    fabgl::EntityGuid m_entityId;
    fabgl::ComponentTypeGuid m_typeId;
    std::string m_propertyName;
    fabgl::PropertyValue m_before;
    fabgl::PropertyValue m_after;
};

struct ComponentPropertyEdit final {
    fabgl::EntityGuid entityId;
    fabgl::PropertyValue before;
    fabgl::PropertyValue after;
};

// Applies one reflected property edit to every selected entity as one undo step. Both redo and
// undo are transactional: if any writer rejects the value, already-written entities are restored
// before returning. This matters for custom component writers whose validation can depend on
// other component state and therefore cannot be completely preflighted.
class EditMultipleComponentPropertiesCommand final : public QUndoCommand {
  public:
    EditMultipleComponentPropertiesCommand(SceneDocument* document,
                                           fabgl::ComponentTypeGuid typeId,
                                           std::string propertyName,
                                           std::vector<ComponentPropertyEdit> edits,
                                           QString description,
                                           QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;
    [[nodiscard]] bool lastApplySucceeded() const noexcept;
    [[nodiscard]] QString lastError() const;

  private:
    [[nodiscard]] bool apply(bool useAfterValues);

    SceneDocument* m_document = nullptr;
    fabgl::ComponentTypeGuid m_typeId;
    std::string m_propertyName;
    std::vector<ComponentPropertyEdit> m_edits;
    QString m_lastError;
    bool m_applied = false;
    bool m_lastApplySucceeded = false;
};

} // namespace fgl::studio
