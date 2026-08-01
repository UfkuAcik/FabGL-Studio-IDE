#include "fabgl/scene/scene.h"

#include <algorithm>
#include <cmath>

namespace fabgl {

Scene::Scene(std::string name, SceneGuid id) : id_(id), name_(std::move(name)) {}

Scene::~Scene() {
    shutdown();
}

Result<Entity*> Scene::createEntity(std::string name, EntityGuid id) {
    if (shutDown_) {
        return Result<Entity*>::failure(
            Error(ErrorCode::InvalidState, "cannot create an entity after scene shutdown"));
    }
    if (id.isNil()) {
        return Result<Entity*>::failure(
            Error(ErrorCode::InvalidArgument, "entity GUID cannot be nil"));
    }
    if (entityIndex_.find(id) != entityIndex_.end()) {
        return Result<Entity*>::failure(
            Error(ErrorCode::AlreadyExists, "entity GUID already exists")
                .addContext("entity", id.toString()));
    }

    auto entity = std::unique_ptr<Entity>(new Entity(*this, id, std::move(name)));
    auto* raw = entity.get();
    entities_.push_back(std::move(entity));
    entityIndex_.emplace(id, raw);
    for (const auto& component : raw->components_) {
        raw->initializeComponent(*component, started_);
    }
    return Result<Entity*>::success(raw);
}

Result<void> Scene::destroyEntity(EntityGuid id) {
    auto required = requireEntity(id, "entity");
    if (!required) {
        return Result<void>::failure(required.error());
    }
    auto* entity = required.value();

    const auto children = entity->transform().children_;
    for (const auto child : children) {
        auto cleared = clearParent(child);
        if (!cleared) {
            return cleared;
        }
    }
    if (entity->transform().parent_) {
        auto cleared = clearParent(id);
        if (!cleared) {
            return cleared;
        }
    }

    entity->shutdownComponents();
    entityIndex_.erase(id);
    const auto iterator = std::find_if(entities_.begin(), entities_.end(),
                                       [id](const auto& value) { return value->id() == id; });
    if (iterator != entities_.end()) {
        entities_.erase(iterator);
    }
    return Result<void>::success();
}

Entity* Scene::findEntity(EntityGuid id) noexcept {
    const auto iterator = entityIndex_.find(id);
    return iterator == entityIndex_.end() ? nullptr : iterator->second;
}

const Entity* Scene::findEntity(EntityGuid id) const noexcept {
    const auto iterator = entityIndex_.find(id);
    return iterator == entityIndex_.end() ? nullptr : iterator->second;
}

std::vector<Entity*> Scene::entities() noexcept {
    std::vector<Entity*> result;
    result.reserve(entities_.size());
    for (const auto& entity : entities_)
        result.push_back(entity.get());
    return result;
}

std::vector<const Entity*> Scene::entities() const {
    std::vector<const Entity*> result;
    result.reserve(entities_.size());
    for (const auto& entity : entities_)
        result.push_back(entity.get());
    return result;
}

Result<void> Scene::setParent(EntityGuid childId, std::optional<EntityGuid> parentId) {
    auto childResult = requireEntity(childId, "child");
    if (!childResult) {
        return Result<void>::failure(childResult.error());
    }
    auto* child = childResult.value();

    Entity* parent = nullptr;
    if (parentId) {
        auto parentResult = requireEntity(*parentId, "parent");
        if (!parentResult) {
            return Result<void>::failure(parentResult.error());
        }
        parent = parentResult.value();
        if (parent == child) {
            return Result<void>::failure(
                Error(ErrorCode::CycleDetected, "an entity cannot parent itself"));
        }

        auto cursor = parent;
        while (cursor != nullptr) {
            if (cursor->id() == childId) {
                return Result<void>::failure(
                    Error(ErrorCode::CycleDetected, "transform parenting would create a cycle")
                        .addContext("child", childId.toString())
                        .addContext("parent", parentId->toString()));
            }
            const auto& cursorParent = cursor->transform().parent_;
            cursor = cursorParent ? findEntity(*cursorParent) : nullptr;
        }
    }

    if (child->transform().parent_ == parentId) {
        return Result<void>::success();
    }

    if (child->transform().parent_) {
        if (auto* previousParent = findEntity(*child->transform().parent_)) {
            auto& siblings = previousParent->transform().children_;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), childId), siblings.end());
        }
    }
    child->transform().parent_ = parentId;
    if (parent != nullptr) {
        parent->transform().children_.push_back(childId);
    }
    markTransformDirtyRecursive(*child);
    return Result<void>::success();
}

Result<Mat4> Scene::worldTransform(EntityGuid entityId) const {
    auto required = requireEntity(entityId, "entity");
    if (!required) {
        return Result<Mat4>::failure(required.error());
    }
    return resolveWorldTransform(*required.value());
}

Result<Mat4> Scene::resolveWorldTransform(const Entity& entity) const {
    auto& transform = const_cast<TransformComponent&>(entity.transform());
    if (!transform.worldDirty_) {
        return Result<Mat4>::success(transform.cachedWorld_);
    }

    auto world = transform.localMatrix();
    if (transform.parent_) {
        const auto* parent = findEntity(*transform.parent_);
        if (parent == nullptr) {
            return Result<Mat4>::failure(
                Error(ErrorCode::NotFound, "transform parent does not exist")
                    .addContext("entity", entity.id().toString())
                    .addContext("parent", transform.parent_->toString()));
        }
        auto parentWorld = resolveWorldTransform(*parent);
        if (!parentWorld) {
            return Result<Mat4>::failure(parentWorld.error());
        }
        world = parentWorld.value() * world;
    }
    transform.cachedWorld_ = world;
    transform.worldDirty_ = false;
    return Result<Mat4>::success(world);
}

void Scene::markTransformDirty(EntityGuid id) {
    if (auto* entity = findEntity(id)) {
        markTransformDirtyRecursive(*entity);
    }
}

void Scene::markTransformDirtyRecursive(Entity& entity) {
    entity.transform().worldDirty_ = true;
    for (const auto childId : entity.transform().children_) {
        if (auto* child = findEntity(childId)) {
            markTransformDirtyRecursive(*child);
        }
    }
}

Result<void> Scene::start() {
    if (shutDown_) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "cannot start a shut down scene"));
    }
    if (started_) {
        return Result<void>::success();
    }
    started_ = true;
    for (const auto& entity : entities_) {
        entity->startComponents();
    }
    return Result<void>::success();
}

Result<void> Scene::ensureRunningOperation(float deltaTime, const char* operation) const {
    if (!started_ || shutDown_) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "scene update requires a started scene")
                .addContext("operation", operation));
    }
    if (!std::isfinite(deltaTime) || deltaTime < 0.0F) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "delta time must be finite and non-negative")
                .addContext("operation", operation));
    }
    return Result<void>::success();
}

Result<void> Scene::fixedUpdate(float deltaTime) {
    auto valid = ensureRunningOperation(deltaTime, "fixedUpdate");
    if (!valid)
        return valid;
    for (const auto& entity : entities_)
        if (entity->active())
            entity->fixedUpdate(deltaTime);
    return Result<void>::success();
}

Result<void> Scene::update(float deltaTime) {
    auto valid = ensureRunningOperation(deltaTime, "update");
    if (!valid)
        return valid;
    for (const auto& entity : entities_)
        if (entity->active())
            entity->update(deltaTime);
    return Result<void>::success();
}

Result<void> Scene::lateUpdate(float deltaTime) {
    auto valid = ensureRunningOperation(deltaTime, "lateUpdate");
    if (!valid)
        return valid;
    for (const auto& entity : entities_)
        if (entity->active())
            entity->lateUpdate(deltaTime);
    return Result<void>::success();
}

Result<void> Scene::dispatchCollisionEnter(EntityGuid receiver, EntityGuid other) {
    auto entity = requireEntity(receiver, "receiver");
    if (!entity)
        return Result<void>::failure(entity.error());
    entity.value()->dispatchCollisionEnter(other);
    return Result<void>::success();
}
Result<void> Scene::dispatchCollisionStay(EntityGuid receiver, EntityGuid other) {
    auto entity = requireEntity(receiver, "receiver");
    if (!entity)
        return Result<void>::failure(entity.error());
    entity.value()->dispatchCollisionStay(other);
    return Result<void>::success();
}
Result<void> Scene::dispatchCollisionExit(EntityGuid receiver, EntityGuid other) {
    auto entity = requireEntity(receiver, "receiver");
    if (!entity)
        return Result<void>::failure(entity.error());
    entity.value()->dispatchCollisionExit(other);
    return Result<void>::success();
}
Result<void> Scene::dispatchTriggerEnter(EntityGuid receiver, EntityGuid other) {
    auto entity = requireEntity(receiver, "receiver");
    if (!entity)
        return Result<void>::failure(entity.error());
    entity.value()->dispatchTriggerEnter(other);
    return Result<void>::success();
}
Result<void> Scene::dispatchTriggerExit(EntityGuid receiver, EntityGuid other) {
    auto entity = requireEntity(receiver, "receiver");
    if (!entity)
        return Result<void>::failure(entity.error());
    entity.value()->dispatchTriggerExit(other);
    return Result<void>::success();
}

void Scene::shutdown() {
    if (shutDown_)
        return;
    shutDown_ = true;
    for (const auto& entity : entities_)
        entity->shutdownComponents();
}

void Scene::clear() {
    for (const auto& entity : entities_)
        entity->shutdownComponents();
    entityIndex_.clear();
    entities_.clear();
    started_ = false;
    shutDown_ = false;
}

Result<Entity*> Scene::requireEntity(EntityGuid id, const char* role) {
    auto* entity = findEntity(id);
    if (entity == nullptr) {
        return Result<Entity*>::failure(
            Error(ErrorCode::NotFound, "entity was not found").addContext(role, id.toString()));
    }
    return Result<Entity*>::success(entity);
}

Result<const Entity*> Scene::requireEntity(EntityGuid id, const char* role) const {
    const auto* entity = findEntity(id);
    if (entity == nullptr) {
        return Result<const Entity*>::failure(
            Error(ErrorCode::NotFound, "entity was not found").addContext(role, id.toString()));
    }
    return Result<const Entity*>::success(entity);
}

} // namespace fabgl
