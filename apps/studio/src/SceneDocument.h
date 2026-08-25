#pragma once

#include <fabgl/core/guid.h>
#include <fabgl/math/types.h>
#include <fabgl/reflection/reflection.h>
#include <fabgl/scene/scene.h>

#include <QObject>
#include <QString>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fgl::studio {

struct ComponentSnapshot final {
    fabgl::ComponentTypeGuid typeId;
    QString typeName;
    bool enabled = true;
    std::vector<std::pair<std::string, fabgl::PropertyValue>> properties;
};

struct EntitySnapshot final {
    fabgl::EntityGuid id;
    QString name;
    bool active = true;
    fabgl::Vec3 position{};
    fabgl::Vec3 rotation{};
    fabgl::Vec3 scale{1.0F, 1.0F, 1.0F};
    std::optional<fabgl::EntityGuid> parent;
    std::vector<fabgl::EntityGuid> children;
    std::vector<ComponentSnapshot> components;
};

class SceneDocument final : public QObject {
    Q_OBJECT

  public:
    explicit SceneDocument(QObject* parent = nullptr);

    [[nodiscard]] fabgl::Scene& scene() noexcept;
    [[nodiscard]] const fabgl::Scene& scene() const noexcept;
    [[nodiscard]] QString filePath() const;
    [[nodiscard]] bool isModified() const noexcept;

    void createDefault(const QString& sceneName = QStringLiteral("Main Scene"));
    [[nodiscard]] bool load(const QString& filePath, QString& errorMessage);
    [[nodiscard]] bool save(QString& errorMessage);
    [[nodiscard]] bool saveAs(const QString& filePath, QString& errorMessage);
    [[nodiscard]] QByteArray serialized(QString& errorMessage) const;
    [[nodiscard]] bool restoreSerialized(const QByteArray& bytes, QString& errorMessage,
                                         bool markModified = true);
    [[nodiscard]] std::unique_ptr<fabgl::Scene> cloneScene(QString& errorMessage) const;

    [[nodiscard]] const fabgl::ReflectionRegistry& reflectionRegistry() const noexcept;
    [[nodiscard]] std::optional<ComponentSnapshot>
    componentSnapshot(fabgl::EntityGuid entityId, fabgl::ComponentTypeGuid typeId,
                      QString& errorMessage) const;
    [[nodiscard]] bool addBuiltinComponent(fabgl::EntityGuid entityId, const QString& typeName,
                                           QString& errorMessage);
    [[nodiscard]] bool restoreComponent(fabgl::EntityGuid entityId,
                                        const ComponentSnapshot& snapshot, QString& errorMessage);
    [[nodiscard]] bool removeComponent(fabgl::EntityGuid entityId, fabgl::ComponentTypeGuid typeId,
                                       QString& errorMessage);
    [[nodiscard]] std::optional<fabgl::PropertyValue>
    componentProperty(fabgl::EntityGuid entityId, fabgl::ComponentTypeGuid typeId,
                      const std::string& propertyName, QString& errorMessage) const;
    [[nodiscard]] bool setComponentProperty(fabgl::EntityGuid entityId,
                                            fabgl::ComponentTypeGuid typeId,
                                            const std::string& propertyName,
                                            const fabgl::PropertyValue& value,
                                            QString& errorMessage, bool markModified = true);

    [[nodiscard]] std::optional<EntitySnapshot> snapshot(fabgl::EntityGuid id) const;
    [[nodiscard]] bool restoreEntity(const EntitySnapshot& snapshot, QString& errorMessage);
    [[nodiscard]] bool removeEntity(fabgl::EntityGuid id, QString& errorMessage);
    [[nodiscard]] bool applySnapshot(const EntitySnapshot& snapshot, QString& errorMessage,
                                     bool markModified = true);
    void previewPosition(fabgl::EntityGuid id, fabgl::Vec3 position);
    void previewRotation(fabgl::EntityGuid id, fabgl::Vec3 rotation);
    void previewScale(fabgl::EntityGuid id, fabgl::Vec3 scale);
    void setModified(bool modified);

    [[nodiscard]] static QString guidString(fabgl::EntityGuid id);
    [[nodiscard]] static std::optional<fabgl::EntityGuid> parseEntityGuid(const QString& text);

  signals:
    void sceneReset();
    void entityAdded(const QString& guid);
    void entityRemoved(const QString& guid);
    void entityChanged(const QString& guid);
    void modifiedChanged(bool modified);

  private:
    [[nodiscard]] static QString errorText(const fabgl::Error& error);

    std::unique_ptr<fabgl::Scene> m_scene;
    fabgl::ReflectionRegistry m_reflectionRegistry;
    QString m_filePath;
    bool m_modified = false;
};

} // namespace fgl::studio
