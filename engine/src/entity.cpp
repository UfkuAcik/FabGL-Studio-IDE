#include "fabgl/scene/entity.h"

#include "fabgl/scene/scene.h"

#include <algorithm>

namespace fabgl {

Entity::Entity(Scene& scene, EntityGuid id, std::string name)
    : scene_(&scene), id_(id), name_(std::move(name)) {
    auto transform = std::make_unique<TransformComponent>();
    transform_ = transform.get();
    components_.push_back(std::move(transform));
}

Entity::~Entity() {
    shutdownComponents();
}

void Entity::setActive(bool active) {
    if (active_ == active || destroyed_) {
        return;
    }
    active_ = active;
    for (const auto& component : components_) {
        refreshComponentState(*component);
    }
}

Result<Component*> Entity::addComponent(std::unique_ptr<Component> component) {
    auto* raw = component.get();
    auto attached = addComponentInternal(std::move(component));
    if (!attached) {
        return Result<Component*>::failure(attached.error());
    }
    return Result<Component*>::success(raw);
}

Result<void> Entity::removeComponent(ComponentTypeGuid typeId) {
    if (typeId == TransformComponent::staticTypeId()) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "Transform cannot be removed from an entity")
                .addContext("entity", id_.toString()));
    }
    if (destroyed_) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "cannot remove a component from a destroyed entity")
                .addContext("entity", id_.toString()));
    }
    const auto iterator = std::find_if(components_.begin(), components_.end(),
                                       [typeId](const std::unique_ptr<Component>& component) {
                                           return component->typeId() == typeId;
                                       });
    if (iterator == components_.end()) {
        return Result<void>::failure(Error(ErrorCode::NotFound, "entity component was not found")
                                         .addContext("entity", id_.toString())
                                         .addContext("type_id", typeId.toString()));
    }

    auto& component = **iterator;
    if (component.active_) {
        component.active_ = false;
        component.onDisable();
    }
    if (component.created_ && !component.destroyed_) {
        component.destroyed_ = true;
        component.onDestroy();
    }
    component.owner_ = nullptr;
    components_.erase(iterator);
    return Result<void>::success();
}

Result<void> Entity::addComponentInternal(std::unique_ptr<Component> component) {
    if (!component) {
        return Result<void>::failure(Error(ErrorCode::InvalidArgument, "component cannot be null"));
    }
    if (destroyed_) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidState, "cannot add a component to a destroyed entity"));
    }
    if (getComponent(component->typeId()) != nullptr) {
        return Result<void>::failure(
            Error(ErrorCode::AlreadyExists, "entity already has this component type")
                .addContext("entity", id_.toString())
                .addContext("component", std::string(component->typeName())));
    }

    auto* raw = component.get();
    components_.push_back(std::move(component));
    initializeComponent(*raw, scene_->started());
    return Result<void>::success();
}

void Entity::initializeComponent(Component& component, bool sceneStarted) {
    component.owner_ = this;
    if (!component.created_) {
        component.created_ = true;
        component.onCreate();
    }
    refreshComponentState(component);
    if (sceneStarted && component.active_ && !component.started_) {
        component.started_ = true;
        component.onStart();
    }
}

void Entity::refreshComponentState(Component& component) {
    if (!component.created_ || component.destroyed_) {
        return;
    }
    const bool shouldBeActive = active_ && component.enabled_ && !destroyed_;
    if (shouldBeActive && !component.active_) {
        component.active_ = true;
        component.onEnable();
        if (scene_->started() && !component.started_) {
            component.started_ = true;
            component.onStart();
        }
    } else if (!shouldBeActive && component.active_) {
        component.active_ = false;
        component.onDisable();
    }
}

void Entity::startComponents() {
    for (const auto& component : components_) {
        if (component->active_ && !component->started_) {
            component->started_ = true;
            component->onStart();
        }
    }
}

void Entity::shutdownComponents() {
    if (destroyed_) {
        return;
    }
    destroyed_ = true;
    for (auto iterator = components_.rbegin(); iterator != components_.rend(); ++iterator) {
        auto& component = **iterator;
        if (component.active_) {
            component.active_ = false;
            component.onDisable();
        }
        if (component.created_ && !component.destroyed_) {
            component.destroyed_ = true;
            component.onDestroy();
        }
    }
}

Component* Entity::getComponent(ComponentTypeGuid typeId) noexcept {
    for (const auto& component : components_) {
        if (component->typeId() == typeId) {
            return component.get();
        }
    }
    return nullptr;
}

const Component* Entity::getComponent(ComponentTypeGuid typeId) const noexcept {
    for (const auto& component : components_) {
        if (component->typeId() == typeId) {
            return component.get();
        }
    }
    return nullptr;
}

std::vector<Component*> Entity::components() noexcept {
    std::vector<Component*> result;
    result.reserve(components_.size());
    for (const auto& component : components_) {
        result.push_back(component.get());
    }
    return result;
}

std::vector<const Component*> Entity::components() const {
    std::vector<const Component*> result;
    result.reserve(components_.size());
    for (const auto& component : components_) {
        result.push_back(component.get());
    }
    return result;
}

void Entity::fixedUpdate(float deltaTime) {
    for (const auto& component : components_) {
        if (component->active_) {
            component->onFixedUpdate(deltaTime);
        }
    }
}

void Entity::update(float deltaTime) {
    for (const auto& component : components_) {
        if (component->active_) {
            component->onUpdate(deltaTime);
        }
    }
}

void Entity::lateUpdate(float deltaTime) {
    for (const auto& component : components_) {
        if (component->active_) {
            component->onLateUpdate(deltaTime);
        }
    }
}

void Entity::dispatchCollisionEnter(EntityGuid other) {
    for (const auto& component : components_)
        if (component->active_)
            component->onCollisionEnter(other);
}
void Entity::dispatchCollisionStay(EntityGuid other) {
    for (const auto& component : components_)
        if (component->active_)
            component->onCollisionStay(other);
}
void Entity::dispatchCollisionExit(EntityGuid other) {
    for (const auto& component : components_)
        if (component->active_)
            component->onCollisionExit(other);
}
void Entity::dispatchTriggerEnter(EntityGuid other) {
    for (const auto& component : components_)
        if (component->active_)
            component->onTriggerEnter(other);
}
void Entity::dispatchTriggerExit(EntityGuid other) {
    for (const auto& component : components_)
        if (component->active_)
            component->onTriggerExit(other);
}

} // namespace fabgl
