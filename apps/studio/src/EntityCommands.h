#pragma once

#include "SceneDocument.h"

#include <QUndoCommand>

namespace fgl::studio {

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

} // namespace fgl::studio
