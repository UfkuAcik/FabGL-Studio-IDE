#pragma once

#include "fabgl/core/guid.h"
#include "fabgl/core/result.h"
#include "fabgl/math/types.h"
#include "fabgl/scene/entity.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace fabgl {

class Scene final {
  public:
    explicit Scene(std::string name = "Untitled", SceneGuid id = SceneGuid::generate());
    ~Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(Scene&&) = delete;

    [[nodiscard]] SceneGuid id() const noexcept {
        return id_;
    }
    [[nodiscard]] const std::string& name() const noexcept {
        return name_;
    }
    void setName(std::string name) {
        name_ = std::move(name);
    }

    [[nodiscard]] Result<Entity*> createEntity(std::string name = "Entity",
                                               EntityGuid id = EntityGuid::generate());
    [[nodiscard]] Result<void> destroyEntity(EntityGuid id);
    [[nodiscard]] Entity* findEntity(EntityGuid id) noexcept;
    [[nodiscard]] const Entity* findEntity(EntityGuid id) const noexcept;
    [[nodiscard]] std::vector<Entity*> entities() noexcept;
    [[nodiscard]] std::vector<const Entity*> entities() const;
    [[nodiscard]] std::size_t entityCount() const noexcept {
        return entities_.size();
    }

    [[nodiscard]] Result<void> setParent(EntityGuid child, std::optional<EntityGuid> parent);
    [[nodiscard]] Result<void> setParent(EntityGuid child, EntityGuid parent) {
        return setParent(child, std::optional<EntityGuid>(parent));
    }
    [[nodiscard]] Result<void> clearParent(EntityGuid child) {
        return setParent(child, std::nullopt);
    }
    [[nodiscard]] Result<Mat4> worldTransform(EntityGuid entity) const;

    [[nodiscard]] bool started() const noexcept {
        return started_;
    }
    [[nodiscard]] bool shutDown() const noexcept {
        return shutDown_;
    }
    [[nodiscard]] Result<void> start();
    [[nodiscard]] Result<void> fixedUpdate(float deltaTime);
    [[nodiscard]] Result<void> update(float deltaTime);
    [[nodiscard]] Result<void> lateUpdate(float deltaTime);
    [[nodiscard]] Result<void> dispatchCollisionEnter(EntityGuid receiver, EntityGuid other);
    [[nodiscard]] Result<void> dispatchCollisionStay(EntityGuid receiver, EntityGuid other);
    [[nodiscard]] Result<void> dispatchCollisionExit(EntityGuid receiver, EntityGuid other);
    [[nodiscard]] Result<void> dispatchTriggerEnter(EntityGuid receiver, EntityGuid other);
    [[nodiscard]] Result<void> dispatchTriggerExit(EntityGuid receiver, EntityGuid other);
    void shutdown();
    void clear();

  private:
    friend class TransformComponent;

    void markTransformDirty(EntityGuid id);
    void markTransformDirtyRecursive(Entity& entity);
    [[nodiscard]] Result<Mat4> resolveWorldTransform(const Entity& entity) const;
    [[nodiscard]] Result<Entity*> requireEntity(EntityGuid id, const char* role);
    [[nodiscard]] Result<const Entity*> requireEntity(EntityGuid id, const char* role) const;
    [[nodiscard]] Result<void> ensureRunningOperation(float deltaTime, const char* operation) const;

    SceneGuid id_;
    std::string name_;
    bool started_ = false;
    bool shutDown_ = false;
    std::vector<std::unique_ptr<Entity>> entities_;
    std::unordered_map<EntityGuid, Entity*, StrongGuidHash<EntityGuidTag>> entityIndex_;
};

} // namespace fabgl
