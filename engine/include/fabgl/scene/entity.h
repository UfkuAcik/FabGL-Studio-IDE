#pragma once

#include "fabgl/core/guid.h"
#include "fabgl/core/result.h"
#include "fabgl/scene/component.h"
#include "fabgl/scene/transform_component.h"

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace fabgl {

class Scene;

class Entity final {
  public:
    ~Entity();

    Entity(const Entity&) = delete;
    Entity& operator=(const Entity&) = delete;
    Entity(Entity&&) = delete;
    Entity& operator=(Entity&&) = delete;

    [[nodiscard]] EntityGuid id() const noexcept {
        return id_;
    }
    [[nodiscard]] const std::string& name() const noexcept {
        return name_;
    }
    void setName(std::string name) {
        name_ = std::move(name);
    }

    [[nodiscard]] bool active() const noexcept {
        return active_;
    }
    void setActive(bool active);

    [[nodiscard]] Scene& scene() noexcept {
        return *scene_;
    }
    [[nodiscard]] const Scene& scene() const noexcept {
        return *scene_;
    }
    [[nodiscard]] TransformComponent& transform() noexcept {
        return *transform_;
    }
    [[nodiscard]] const TransformComponent& transform() const noexcept {
        return *transform_;
    }

    template <typename T, typename... Arguments>
    [[nodiscard]] Result<T*> addComponent(Arguments&&... arguments) {
        static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
        auto component = std::make_unique<T>(std::forward<Arguments>(arguments)...);
        auto* raw = component.get();
        auto attached = addComponentInternal(std::move(component));
        if (!attached) {
            return Result<T*>::failure(attached.error());
        }
        return Result<T*>::success(raw);
    }

    template <typename T> [[nodiscard]] T* getComponent() noexcept {
        static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
        for (const auto& component : components_) {
            if (auto* typed = dynamic_cast<T*>(component.get())) {
                return typed;
            }
        }
        return nullptr;
    }

    template <typename T> [[nodiscard]] const T* getComponent() const noexcept {
        static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
        for (const auto& component : components_) {
            if (const auto* typed = dynamic_cast<const T*>(component.get())) {
                return typed;
            }
        }
        return nullptr;
    }

    [[nodiscard]] Component* getComponent(ComponentTypeGuid typeId) noexcept;
    [[nodiscard]] const Component* getComponent(ComponentTypeGuid typeId) const noexcept;
    [[nodiscard]] std::vector<Component*> components() noexcept;
    [[nodiscard]] std::vector<const Component*> components() const;

  private:
    friend class Component;
    friend class Scene;

    Entity(Scene& scene, EntityGuid id, std::string name);

    [[nodiscard]] Result<void> addComponentInternal(std::unique_ptr<Component> component);
    void initializeComponent(Component& component, bool sceneStarted);
    void refreshComponentState(Component& component);
    void startComponents();
    void shutdownComponents();
    void fixedUpdate(float deltaTime);
    void update(float deltaTime);
    void lateUpdate(float deltaTime);
    void dispatchCollisionEnter(EntityGuid other);
    void dispatchCollisionStay(EntityGuid other);
    void dispatchCollisionExit(EntityGuid other);
    void dispatchTriggerEnter(EntityGuid other);
    void dispatchTriggerExit(EntityGuid other);

    Scene* scene_ = nullptr;
    EntityGuid id_;
    std::string name_;
    bool active_ = true;
    bool destroyed_ = false;
    std::vector<std::unique_ptr<Component>> components_;
    TransformComponent* transform_ = nullptr;
};

} // namespace fabgl
