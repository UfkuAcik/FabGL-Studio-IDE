#pragma once

#include <fabgl/core/guid.h>

#include <QAbstractListModel>

#include <optional>
#include <vector>

namespace fgl::studio {

class SceneDocument;

class EntityModel final : public QAbstractListModel {
    Q_OBJECT

  public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        ActiveRole,
    };

    explicit EntityModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void setDocument(SceneDocument* document);
    [[nodiscard]] std::optional<fabgl::EntityGuid> entityIdAt(int row) const;
    [[nodiscard]] int rowForId(fabgl::EntityGuid id) const;
    [[nodiscard]] SceneDocument* document() const noexcept;

  private:
    void resetFromScene();
    void entityAdded(const QString& guid);
    void entityRemoved(const QString& guid);
    void entityChanged(const QString& guid);

    SceneDocument* m_document = nullptr;
    std::vector<fabgl::EntityGuid> m_order;
};

} // namespace fgl::studio
