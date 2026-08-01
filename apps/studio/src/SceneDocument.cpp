#include "SceneDocument.h"

#include <fabgl/scene/entity.h>
#include <fabgl/serialization/scene_serializer.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <string>
#include <utility>

namespace fgl::studio {

SceneDocument::SceneDocument(QObject* parent)
    : QObject(parent), m_scene(std::make_unique<fabgl::Scene>("Main Scene")) {}

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

std::optional<EntitySnapshot> SceneDocument::snapshot(const fabgl::EntityGuid id) const {
    const auto* entity = m_scene->findEntity(id);
    if (entity == nullptr) {
        return std::nullopt;
    }
    const auto& transform = entity->transform();
    return EntitySnapshot{id,
                          QString::fromStdString(entity->name()),
                          entity->active(),
                          transform.localPosition(),
                          transform.localRotation(),
                          transform.localScale(),
                          transform.parent(),
                          transform.children()};
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
