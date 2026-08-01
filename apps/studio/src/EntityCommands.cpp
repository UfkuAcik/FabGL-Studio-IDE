#include "EntityCommands.h"

#include <QObject>

#include <utility>

namespace fgl::studio {

AddEntityCommand::AddEntityCommand(SceneDocument* document, EntitySnapshot entity,
                                   QUndoCommand* parent)
    : QUndoCommand(parent), m_document(document), m_entity(std::move(entity)) {
    setText(QObject::tr("Add %1").arg(m_entity.name));
}

void AddEntityCommand::redo() {
    if (m_document == nullptr) {
        return;
    }
    QString ignored;
    (void)m_document->restoreEntity(m_entity, ignored);
}

void AddEntityCommand::undo() {
    if (m_document == nullptr) {
        return;
    }
    QString ignored;
    (void)m_document->removeEntity(m_entity.id, ignored);
}

fabgl::EntityGuid AddEntityCommand::entityId() const noexcept {
    return m_entity.id;
}

DeleteEntityCommand::DeleteEntityCommand(SceneDocument* document, const fabgl::EntityGuid id,
                                         QUndoCommand* parent)
    : QUndoCommand(parent), m_document(document) {
    if (m_document != nullptr) {
        m_entity = m_document->snapshot(id).value_or(EntitySnapshot{});
    }
    setText(QObject::tr("Delete %1").arg(m_entity.name));
}

void DeleteEntityCommand::redo() {
    if (m_document == nullptr) {
        return;
    }
    QString ignored;
    (void)m_document->removeEntity(m_entity.id, ignored);
}

void DeleteEntityCommand::undo() {
    if (m_document == nullptr) {
        return;
    }
    QString ignored;
    (void)m_document->restoreEntity(m_entity, ignored);
}

EditEntityCommand::EditEntityCommand(SceneDocument* document, EntitySnapshot before,
                                     EntitySnapshot after, QString description,
                                     QUndoCommand* parent)
    : QUndoCommand(parent), m_document(document), m_before(std::move(before)),
      m_after(std::move(after)) {
    setText(std::move(description));
}

void EditEntityCommand::redo() {
    apply(m_after);
}

void EditEntityCommand::undo() {
    apply(m_before);
}

void EditEntityCommand::apply(const EntitySnapshot& entitySnapshot) {
    if (m_document == nullptr) {
        return;
    }
    QString ignored;
    (void)m_document->applySnapshot(entitySnapshot, ignored);
}

} // namespace fgl::studio
