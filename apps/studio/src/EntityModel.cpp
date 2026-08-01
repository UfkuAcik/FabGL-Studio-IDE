#include "EntityModel.h"

#include "SceneDocument.h"

#include <fabgl/scene/entity.h>

#include <QColor>
#include <QFont>

#include <algorithm>
#include <cstddef>

namespace fgl::studio {

EntityModel::EntityModel(QObject* parent) : QAbstractListModel(parent) {}

int EntityModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_order.size());
}

QVariant EntityModel::data(const QModelIndex& index, const int role) const {
    const auto id = entityIdAt(index.row());
    if (!index.isValid() || !id || m_document == nullptr) {
        return {};
    }
    const auto* entity = m_document->scene().findEntity(*id);
    if (entity == nullptr) {
        return {};
    }

    const auto name = QString::fromStdString(entity->name());
    switch (role) {
    case Qt::DisplayRole: {
        const auto depth = [this, entity]() {
            int value = 0;
            auto parent = entity->transform().parent();
            while (parent && value < 16 && m_document != nullptr) {
                const auto* parentEntity = m_document->scene().findEntity(*parent);
                if (parentEntity == nullptr) {
                    break;
                }
                ++value;
                parent = parentEntity->transform().parent();
            }
            return value;
        }();
        return QString(static_cast<qsizetype>(depth * 2), QLatin1Char(' ')) + name;
    }
    case Qt::EditRole:
    case NameRole:
        return name;
    case IdRole:
        return SceneDocument::guidString(*id);
    case ActiveRole:
        return entity->active();
    case Qt::ToolTipRole:
        return tr("%1\nEntity GUID: %2\nActive: %3")
            .arg(name, SceneDocument::guidString(*id), entity->active() ? tr("yes") : tr("no"));
    case Qt::FontRole: {
        QFont font;
        font.setItalic(!entity->active());
        return font;
    }
    case Qt::ForegroundRole:
        return entity->active() ? QVariant{} : QVariant(QColor(Qt::gray));
    default:
        return {};
    }
}

Qt::ItemFlags EntityModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
}

QHash<int, QByteArray> EntityModel::roleNames() const {
    auto roles = QAbstractListModel::roleNames();
    roles.insert(IdRole, "entityId");
    roles.insert(NameRole, "entityName");
    roles.insert(ActiveRole, "entityActive");
    return roles;
}

void EntityModel::setDocument(SceneDocument* document) {
    if (m_document == document) {
        return;
    }
    if (m_document != nullptr) {
        disconnect(m_document, nullptr, this, nullptr);
    }
    m_document = document;
    if (m_document != nullptr) {
        connect(m_document, &SceneDocument::sceneReset, this, &EntityModel::resetFromScene);
        connect(m_document, &SceneDocument::entityAdded, this, &EntityModel::entityAdded);
        connect(m_document, &SceneDocument::entityRemoved, this, &EntityModel::entityRemoved);
        connect(m_document, &SceneDocument::entityChanged, this, &EntityModel::entityChanged);
    }
    resetFromScene();
}

std::optional<fabgl::EntityGuid> EntityModel::entityIdAt(const int row) const {
    if (row < 0 || row >= rowCount()) {
        return std::nullopt;
    }
    return m_order.at(static_cast<std::size_t>(row));
}

int EntityModel::rowForId(const fabgl::EntityGuid id) const {
    const auto iterator = std::find(m_order.cbegin(), m_order.cend(), id);
    if (iterator == m_order.cend()) {
        return -1;
    }
    return static_cast<int>(std::distance(m_order.cbegin(), iterator));
}

SceneDocument* EntityModel::document() const noexcept {
    return m_document;
}

void EntityModel::resetFromScene() {
    beginResetModel();
    m_order.clear();
    if (m_document != nullptr) {
        const auto entities = m_document->scene().entities();
        m_order.reserve(entities.size());
        for (const auto* entity : entities) {
            m_order.push_back(entity->id());
        }
    }
    endResetModel();
}

void EntityModel::entityAdded(const QString& guid) {
    const auto id = SceneDocument::parseEntityGuid(guid);
    if (!id || rowForId(*id) >= 0) {
        return;
    }
    const int row = rowCount();
    beginInsertRows({}, row, row);
    m_order.push_back(*id);
    endInsertRows();
}

void EntityModel::entityRemoved(const QString& guid) {
    const auto id = SceneDocument::parseEntityGuid(guid);
    if (!id) {
        return;
    }
    const int row = rowForId(*id);
    if (row < 0) {
        return;
    }
    beginRemoveRows({}, row, row);
    m_order.erase(m_order.begin() + static_cast<std::ptrdiff_t>(row));
    endRemoveRows();
}

void EntityModel::entityChanged(const QString& guid) {
    const auto id = SceneDocument::parseEntityGuid(guid);
    const int row = id ? rowForId(*id) : -1;
    if (row < 0) {
        return;
    }
    const auto changedIndex = index(row, 0);
    emit dataChanged(changedIndex, changedIndex);
}

} // namespace fgl::studio
