#include "SceneDocument.h"

#include <fabgl/scene/builtin_components.h>
#include <fabgl/scene/entity.h>
#include <fabgl/scene/transform_component.h>
#include <fabgl/serialization/scene_serializer.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <string>
#include <utility>

namespace fgl::studio {

SceneDocument::SceneDocument(QObject* parent)
    : QObject(parent), m_scene(std::make_unique<fabgl::Scene>("Main Scene")) {
    (void)fabgl::registerBuiltinComponentTypes(m_reflectionRegistry);
}

fabgl::Scene& SceneDocument::scene() noexcept {
    return *m_scene;
}

const fabgl::Scene& SceneDocument::scene() const noexcept {
    return *m_scene;
}

QString SceneDocument::filePath() const {
    return m_filePath;
}

bool SceneDocument::isModified() const noexcept {
    return m_modified;
}

void SceneDocument::createDefault(const QString& sceneName) {
    m_scene = std::make_unique<fabgl::Scene>(sceneName.toStdString());
    auto camera = m_scene->createEntity("Main Camera");
    if (camera) {
        camera.value()->transform().setLocalPosition({0.0F, 0.0F, 0.0F});
    }
    auto player = m_scene->createEntity("Player");
    if (player) {
        player.value()->transform().setLocalPosition({2.0F, 1.0F, 0.0F});
    }
    m_filePath.clear();
    setModified(false);
    emit sceneReset();
}

bool SceneDocument::load(const QString& filePath, QString& errorMessage) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        errorMessage = tr("Cannot open scene %1: %2")
                           .arg(QDir::toNativeSeparators(filePath), file.errorString());
        return false;
    }
    const QByteArray bytes = file.readAll();
    auto parsed = fabgl::SceneSerializer::deserialize(
        std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    if (!parsed) {
        errorMessage = tr("Cannot deserialize scene %1: %2")
                           .arg(QDir::toNativeSeparators(filePath), errorText(parsed.error()));
        return false;
    }
    m_scene = std::move(parsed.value());
    m_filePath = QFileInfo(filePath).absoluteFilePath();
    setModified(false);
    emit sceneReset();
    return true;
}

bool SceneDocument::save(QString& errorMessage) {
    if (m_filePath.isEmpty()) {
        errorMessage = tr("The scene does not have a file path.");
        return false;
    }
    return saveAs(m_filePath, errorMessage);
}

bool SceneDocument::saveAs(const QString& filePath, QString& errorMessage) {
    const auto bytes = serialized(errorMessage);
    if (bytes.isNull()) {
        return false;
    }

    const auto sceneDirectory = QFileInfo(filePath).absolutePath();
    if (!QDir().mkpath(sceneDirectory)) {
        errorMessage =
            tr("Cannot create scene directory %1.").arg(QDir::toNativeSeparators(sceneDirectory));
        return false;
    }
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        errorMessage = tr("Cannot write scene %1: %2")
                           .arg(QDir::toNativeSeparators(filePath), file.errorString());
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        errorMessage = tr("Could not completely write scene %1: %2")
                           .arg(QDir::toNativeSeparators(filePath), file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        errorMessage = tr("Could not atomically replace scene %1: %2")
                           .arg(QDir::toNativeSeparators(filePath), file.errorString());
        return false;
    }

    m_filePath = QFileInfo(filePath).absoluteFilePath();
    setModified(false);
    return true;
}

QByteArray SceneDocument::serialized(QString& errorMessage) const {
    const auto serializedScene = fabgl::SceneSerializer::serialize(*m_scene);
    if (!serializedScene) {
        errorMessage = tr("Scene serialization failed: %1").arg(errorText(serializedScene.error()));
        return {};
    }
    const auto& text = serializedScene.value();
    return QByteArray(text.data(), static_cast<qsizetype>(text.size()));
}

bool SceneDocument::restoreSerialized(const QByteArray& bytes, QString& errorMessage,
                                      const bool markModified) {
    auto parsed = fabgl::SceneSerializer::deserialize(
        std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    if (!parsed) {
        errorMessage = tr("Scene snapshot restore failed: %1").arg(errorText(parsed.error()));
        return false;
    }
    m_scene = std::move(parsed.value());
    setModified(markModified);
    emit sceneReset();
    return true;
}

std::unique_ptr<fabgl::Scene> SceneDocument::cloneScene(QString& errorMessage) const {
    const auto bytes = serialized(errorMessage);
    if (bytes.isNull()) {
        return nullptr;
    }
    auto parsed = fabgl::SceneSerializer::deserialize(
        std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    if (!parsed) {
        errorMessage = tr("Could not clone the scene: %1").arg(errorText(parsed.error()));
        return nullptr;
    }
    return std::move(parsed.value());
}

const fabgl::ReflectionRegistry& SceneDocument::reflectionRegistry() const noexcept {
    return m_reflectionRegistry;
}

std::optional<ComponentSnapshot>
SceneDocument::componentSnapshot(const fabgl::EntityGuid entityId,
                                 const fabgl::ComponentTypeGuid typeId,
                                 QString& errorMessage) const {
    const auto* entity = m_scene->findEntity(entityId);
    if (entity == nullptr) {
        errorMessage = tr("Entity %1 was not found.").arg(guidString(entityId));
        return std::nullopt;
    }
    const auto* component = entity->getComponent(typeId);
    if (component == nullptr) {
        errorMessage = tr("Component %1 was not found on entity %2.")
                           .arg(QString::fromStdString(typeId.toString()), guidString(entityId));
        return std::nullopt;
    }

    ComponentSnapshot result{
        typeId, QString::fromUtf8(component->typeName()), component->enabled(), {}};
    const auto* metadata = component->metadata();
    if (metadata == nullptr) {
        return result;
    }
    result.properties.reserve(metadata->properties.size());
    for (const auto& property : metadata->properties) {
        const auto value = property.read(component);
        if (value) {
            result.properties.emplace_back(property.name, value.value());
        }
    }
    return result;
}

bool SceneDocument::addBuiltinComponent(const fabgl::EntityGuid entityId, const QString& typeName,
                                        QString& errorMessage) {
    auto* entity = m_scene->findEntity(entityId);
    if (entity == nullptr) {
        errorMessage = tr("Entity %1 was not found.").arg(guidString(entityId));
        return false;
    }
    auto created = fabgl::createBuiltinDataComponent(m_reflectionRegistry, typeName.toStdString());
    if (!created) {
        errorMessage = errorText(created.error());
        return false;
    }
    auto added = entity->addComponent(std::move(created.value()));
    if (!added) {
        errorMessage = errorText(added.error());
        return false;
    }
    setModified(true);
    emit entityChanged(guidString(entityId));
    return true;
}

bool SceneDocument::restoreComponent(const fabgl::EntityGuid entityId,
                                     const ComponentSnapshot& snapshot, QString& errorMessage) {
    auto* entity = m_scene->findEntity(entityId);
    if (entity == nullptr) {
        errorMessage = tr("Entity %1 was not found.").arg(guidString(entityId));
        return false;
    }
    auto created = fabgl::createBuiltinDataComponent(m_reflectionRegistry, snapshot.typeId);
    if (!created) {
        errorMessage = errorText(created.error());
        return false;
    }
    created.value()->setEnabled(snapshot.enabled);
    for (const auto& [propertyName, value] : snapshot.properties) {
        const auto written = created.value()->set(propertyName, value);
        if (!written && written.error().code() != fabgl::ErrorCode::InvalidState) {
            errorMessage = errorText(written.error());
            return false;
        }
    }
    auto added = entity->addComponent(std::move(created.value()));
    if (!added) {
        errorMessage = errorText(added.error());
        return false;
    }
    setModified(true);
    emit entityChanged(guidString(entityId));
    return true;
}

bool SceneDocument::removeComponent(const fabgl::EntityGuid entityId,
                                    const fabgl::ComponentTypeGuid typeId, QString& errorMessage) {
    auto* entity = m_scene->findEntity(entityId);
    if (entity == nullptr) {
        errorMessage = tr("Entity %1 was not found.").arg(guidString(entityId));
        return false;
    }
    const auto removed = entity->removeComponent(typeId);
    if (!removed) {
        errorMessage = errorText(removed.error());
        return false;
    }
    setModified(true);
    emit entityChanged(guidString(entityId));
    return true;
}

std::optional<fabgl::PropertyValue>
SceneDocument::componentProperty(const fabgl::EntityGuid entityId,
                                 const fabgl::ComponentTypeGuid typeId,
                                 const std::string& propertyName, QString& errorMessage) const {
    const auto* entity = m_scene->findEntity(entityId);
    const auto* component = entity != nullptr ? entity->getComponent(typeId) : nullptr;
    if (component == nullptr) {
        errorMessage =
            tr("Component %1 was not found.").arg(QString::fromStdString(typeId.toString()));
        return std::nullopt;
    }
    const auto* metadata = component->metadata();
    const auto* property = metadata != nullptr ? metadata->findProperty(propertyName) : nullptr;
    if (property == nullptr) {
        errorMessage = tr("Property %1 was not found.").arg(QString::fromStdString(propertyName));
        return std::nullopt;
    }
    const auto value = property->read(component);
    if (!value) {
        errorMessage = errorText(value.error());
        return std::nullopt;
    }
    return value.value();
}

bool SceneDocument::setComponentProperty(const fabgl::EntityGuid entityId,
                                         const fabgl::ComponentTypeGuid typeId,
                                         const std::string& propertyName,
                                         const fabgl::PropertyValue& value, QString& errorMessage,
                                         const bool markModified) {
    auto* entity = m_scene->findEntity(entityId);
    auto* component = entity != nullptr ? entity->getComponent(typeId) : nullptr;
    if (component == nullptr) {
        errorMessage =
            tr("Component %1 was not found.").arg(QString::fromStdString(typeId.toString()));
        return false;
    }
    const auto* metadata = component->metadata();
    const auto* property = metadata != nullptr ? metadata->findProperty(propertyName) : nullptr;
    if (property == nullptr) {
        errorMessage = tr("Property %1 was not found.").arg(QString::fromStdString(propertyName));
        return false;
    }
    const auto written = property->write(component, value);
    if (!written) {
        errorMessage = errorText(written.error());
        return false;
    }
    if (markModified) {
        setModified(true);
    }
    emit entityChanged(guidString(entityId));
    return true;
}

std::optional<EntitySnapshot> SceneDocument::snapshot(const fabgl::EntityGuid id) const {
    const auto* entity = m_scene->findEntity(id);
    if (entity == nullptr) {
        return std::nullopt;
    }
    const auto& transform = entity->transform();
    std::vector<ComponentSnapshot> components;
    for (const auto* component : entity->components()) {
        if (component->typeId() == fabgl::TransformComponent::staticTypeId()) {
            continue;
        }
        QString ignored;
        auto componentState = componentSnapshot(id, component->typeId(), ignored);
        if (componentState) {
            components.push_back(std::move(*componentState));
        }
    }
    return EntitySnapshot{id,
                          QString::fromStdString(entity->name()),
                          entity->active(),
                          transform.localPosition(),
                          transform.localRotation(),
                          transform.localScale(),
                          transform.parent(),
                          transform.children(),
                          std::move(components)};
}

bool SceneDocument::restoreEntity(const EntitySnapshot& entitySnapshot, QString& errorMessage) {
    auto created = m_scene->createEntity(entitySnapshot.name.toStdString(), entitySnapshot.id);
    if (!created) {
        errorMessage = errorText(created.error());
        return false;
    }
    auto* entity = created.value();
    entity->setActive(entitySnapshot.active);
    entity->transform().setLocalPosition(entitySnapshot.position);
    entity->transform().setLocalRotation(entitySnapshot.rotation);
    entity->transform().setLocalScale(entitySnapshot.scale);
    for (const auto& component : entitySnapshot.components) {
        if (!restoreComponent(entitySnapshot.id, component, errorMessage)) {
            (void)m_scene->destroyEntity(entitySnapshot.id);
            return false;
        }
    }
    if (entitySnapshot.parent && m_scene->findEntity(*entitySnapshot.parent) != nullptr) {
        const auto parented = m_scene->setParent(entitySnapshot.id, *entitySnapshot.parent);
        if (!parented) {
            errorMessage = errorText(parented.error());
            (void)m_scene->destroyEntity(entitySnapshot.id);
            return false;
        }
    }
    for (const auto child : entitySnapshot.children) {
        if (m_scene->findEntity(child) != nullptr) {
            (void)m_scene->setParent(child, entitySnapshot.id);
        }
    }
    setModified(true);
    emit entityAdded(guidString(entitySnapshot.id));
    return true;
}

bool SceneDocument::removeEntity(const fabgl::EntityGuid id, QString& errorMessage) {
    const auto removed = m_scene->destroyEntity(id);
    if (!removed) {
        errorMessage = errorText(removed.error());
        return false;
    }
    setModified(true);
    emit entityRemoved(guidString(id));
    return true;
}

bool SceneDocument::applySnapshot(const EntitySnapshot& entitySnapshot, QString& errorMessage,
                                  const bool markModified) {
    auto* entity = m_scene->findEntity(entitySnapshot.id);
    if (entity == nullptr) {
        errorMessage = tr("Entity %1 was not found.").arg(guidString(entitySnapshot.id));
        return false;
    }
    entity->setName(entitySnapshot.name.toStdString());
    entity->setActive(entitySnapshot.active);
    entity->transform().setLocalPosition(entitySnapshot.position);
    entity->transform().setLocalRotation(entitySnapshot.rotation);
    entity->transform().setLocalScale(entitySnapshot.scale);
    if (entity->transform().parent() != entitySnapshot.parent) {
        const auto parented = m_scene->setParent(entitySnapshot.id, entitySnapshot.parent);
        if (!parented) {
            errorMessage = errorText(parented.error());
            return false;
        }
    }
    if (markModified) {
        setModified(true);
    }
    emit entityChanged(guidString(entitySnapshot.id));
    return true;
}

void SceneDocument::previewPosition(const fabgl::EntityGuid id, const fabgl::Vec3 position) {
    if (auto* entity = m_scene->findEntity(id)) {
        entity->transform().setLocalPosition(position);
        emit entityChanged(guidString(id));
    }
}

void SceneDocument::previewRotation(const fabgl::EntityGuid id, const fabgl::Vec3 rotation) {
    if (auto* entity = m_scene->findEntity(id)) {
        entity->transform().setLocalRotation(rotation);
        emit entityChanged(guidString(id));
    }
}

void SceneDocument::previewScale(const fabgl::EntityGuid id, const fabgl::Vec3 scale) {
    if (auto* entity = m_scene->findEntity(id)) {
        entity->transform().setLocalScale(scale);
        emit entityChanged(guidString(id));
    }
}

void SceneDocument::setModified(const bool modified) {
    if (m_modified == modified) {
        return;
    }
    m_modified = modified;
    emit modifiedChanged(m_modified);
}

QString SceneDocument::guidString(const fabgl::EntityGuid id) {
    return QString::fromStdString(id.toString());
}

std::optional<fabgl::EntityGuid> SceneDocument::parseEntityGuid(const QString& text) {
    const auto parsed = fabgl::EntityGuid::parse(text.toStdString());
    if (!parsed) {
        return std::nullopt;
    }
    return parsed.value();
}

QString SceneDocument::errorText(const fabgl::Error& error) {
    QString text = QString::fromStdString(error.message());
    for (const auto& context : error.context()) {
        text +=
            QStringLiteral(" [%1=%2]")
                .arg(QString::fromStdString(context.key), QString::fromStdString(context.value));
    }
    return text;
}

} // namespace fgl::studio
