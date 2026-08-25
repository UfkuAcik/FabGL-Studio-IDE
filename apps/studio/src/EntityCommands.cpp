#include "EntityCommands.h"

#include <QDir>
#include <QFileInfo>
#include <QObject>

#include <utility>

namespace fgl::studio {

MoveAssetCommand::MoveAssetCommand(QString sourcePath, QString destinationPath, Relocate relocate,
                                   ReportError reportError, QUndoCommand* parent)
    : QUndoCommand(parent), m_sourcePath(std::move(sourcePath)),
      m_destinationPath(std::move(destinationPath)), m_relocate(std::move(relocate)),
      m_reportError(std::move(reportError)) {
    setText(
        QObject::tr("Move asset %1 to %2")
            .arg(QFileInfo(m_sourcePath).fileName(), QDir::toNativeSeparators(m_destinationPath)));
}

void MoveAssetCommand::redo() {
    apply(m_sourcePath, m_destinationPath, m_initialRedo);
    m_initialRedo = false;
}

void MoveAssetCommand::undo() {
    apply(m_destinationPath, m_sourcePath, false);
}

void MoveAssetCommand::apply(const QString& sourcePath, const QString& destinationPath,
                             const bool initialRedo) {
    if (!m_relocate) {
        if (initialRedo) {
            setObsolete(true);
        }
        return;
    }
    QString errorMessage;
    if (m_relocate(sourcePath, destinationPath, errorMessage)) {
        return;
    }
    if (initialRedo) {
        setObsolete(true);
    }
    if (m_reportError) {
        m_reportError(errorMessage.isEmpty() ? QObject::tr("Asset relocation failed.")
                                             : errorMessage);
    }
}

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

AddComponentCommand::AddComponentCommand(SceneDocument* document, const fabgl::EntityGuid entityId,
                                         QString typeName, QUndoCommand* parent)
    : QUndoCommand(parent), m_document(document), m_entityId(entityId),
      m_typeName(std::move(typeName)) {
    setText(QObject::tr("Add %1 component").arg(m_typeName));
}

void AddComponentCommand::redo() {
    if (m_document == nullptr) {
        return;
    }
    QString ignored;
    if (m_snapshot) {
        (void)m_document->restoreComponent(m_entityId, *m_snapshot, ignored);
        return;
    }
    if (!m_document->addBuiltinComponent(m_entityId, m_typeName, ignored)) {
        return;
    }
    const auto* metadata = m_document->reflectionRegistry().find(
        (QStringLiteral("fabgl.") + m_typeName).toStdString());
    if (metadata != nullptr) {
        m_snapshot = m_document->componentSnapshot(m_entityId, metadata->typeId, ignored);
    }
}

void AddComponentCommand::undo() {
    if (m_document == nullptr || !m_snapshot) {
        return;
    }
    QString ignored;
    (void)m_document->removeComponent(m_entityId, m_snapshot->typeId, ignored);
}

RemoveComponentCommand::RemoveComponentCommand(SceneDocument* document,
                                               const fabgl::EntityGuid entityId,
                                               const fabgl::ComponentTypeGuid typeId,
                                               QUndoCommand* parent)
    : QUndoCommand(parent), m_document(document), m_entityId(entityId) {
    if (m_document != nullptr) {
        QString ignored;
        m_snapshot =
            m_document->componentSnapshot(entityId, typeId, ignored).value_or(ComponentSnapshot{});
    }
    setText(QObject::tr("Remove %1 component").arg(m_snapshot.typeName));
}

void RemoveComponentCommand::redo() {
    if (m_document == nullptr || m_snapshot.typeId.isNil()) {
        return;
    }
    QString ignored;
    (void)m_document->removeComponent(m_entityId, m_snapshot.typeId, ignored);
}

void RemoveComponentCommand::undo() {
    if (m_document == nullptr || m_snapshot.typeId.isNil()) {
        return;
    }
    QString ignored;
    (void)m_document->restoreComponent(m_entityId, m_snapshot, ignored);
}

EditComponentPropertyCommand::EditComponentPropertyCommand(
    SceneDocument* document, const fabgl::EntityGuid entityId,
    const fabgl::ComponentTypeGuid typeId, std::string propertyName, fabgl::PropertyValue before,
    fabgl::PropertyValue after, QString description, QUndoCommand* parent)
    : QUndoCommand(parent), m_document(document), m_entityId(entityId), m_typeId(typeId),
      m_propertyName(std::move(propertyName)), m_before(std::move(before)),
      m_after(std::move(after)) {
    setText(std::move(description));
}

void EditComponentPropertyCommand::redo() {
    apply(m_after);
}

void EditComponentPropertyCommand::undo() {
    apply(m_before);
}

void EditComponentPropertyCommand::apply(const fabgl::PropertyValue& value) {
    if (m_document == nullptr) {
        return;
    }
    QString ignored;
    (void)m_document->setComponentProperty(m_entityId, m_typeId, m_propertyName, value, ignored);
}

EditMultipleComponentPropertiesCommand::EditMultipleComponentPropertiesCommand(
    SceneDocument* document, const fabgl::ComponentTypeGuid typeId, std::string propertyName,
    std::vector<ComponentPropertyEdit> edits, QString description, QUndoCommand* parent)
    : QUndoCommand(parent), m_document(document), m_typeId(typeId),
      m_propertyName(std::move(propertyName)), m_edits(std::move(edits)) {
    setText(std::move(description));
}

void EditMultipleComponentPropertiesCommand::redo() {
    m_lastApplySucceeded = apply(true);
    m_applied = m_lastApplySucceeded;
}

void EditMultipleComponentPropertiesCommand::undo() {
    if (!m_applied) {
        m_lastApplySucceeded = false;
        return;
    }
    m_lastApplySucceeded = apply(false);
    if (m_lastApplySucceeded)
        m_applied = false;
}

bool EditMultipleComponentPropertiesCommand::lastApplySucceeded() const noexcept {
    return m_lastApplySucceeded;
}

QString EditMultipleComponentPropertiesCommand::lastError() const {
    return m_lastError;
}

bool EditMultipleComponentPropertiesCommand::apply(const bool useAfterValues) {
    if (m_document == nullptr || m_edits.empty()) {
        m_lastError = QObject::tr("The multi-edit command has no document or target entities.");
        return false;
    }

    std::size_t applied = 0U;
    for (; applied < m_edits.size(); ++applied) {
        const auto& edit = m_edits[applied];
        const auto& value = useAfterValues ? edit.after : edit.before;
        QString error;
        if (m_document->setComponentProperty(edit.entityId, m_typeId, m_propertyName, value,
                                             error, false)) {
            continue;
        }

        // Restore in reverse order. These values were read immediately before the command was
        // created, so a restore failure indicates a component writer contract violation. Keep
        // the original error and append the rollback diagnostic without hiding either problem.
        while (applied > 0U) {
            --applied;
            const auto& rollback = m_edits[applied];
            const auto& rollbackValue = useAfterValues ? rollback.before : rollback.after;
            QString rollbackError;
            if (!m_document->setComponentProperty(rollback.entityId, m_typeId, m_propertyName,
                                                  rollbackValue, rollbackError, false) &&
                !rollbackError.isEmpty()) {
                error += QObject::tr(" Rollback failed: %1").arg(rollbackError);
            }
        }
        m_lastError = error;
        return false;
    }

    m_document->setModified(true);
    m_lastError.clear();
    return true;
}

} // namespace fgl::studio
