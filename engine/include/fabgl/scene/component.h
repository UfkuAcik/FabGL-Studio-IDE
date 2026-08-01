#pragma once

#include "fabgl/core/guid.h"

#include <string_view>

namespace fabgl {

class Entity;
struct TypeMetadata;

class Component {
  public:
    virtual ~Component() = default;

    Component(const Component&) = delete;
    Component& operator=(const Component&) = delete;
    Component(Component&&) = delete;
    Component& operator=(Component&&) = delete;

    [[nodiscard]] virtual ComponentTypeGuid typeId() const noexcept = 0;
    [[nodiscard]] virtual std::string_view typeName() const noexcept = 0;
    [[nodiscard]] virtual const TypeMetadata* metadata() const noexcept {
        return nullptr;
    }

    [[nodiscard]] Entity* owner() noexcept {
        return owner_;
    }
    [[nodiscard]] const Entity* owner() const noexcept {
        return owner_;
    }
    [[nodiscard]] bool enabled() const noexcept {
        return enabled_;
    }
    [[nodiscard]] bool activeAndEnabled() const noexcept {
        return active_;
    }
    [[nodiscard]] bool started() const noexcept {
        return started_;
    }

    void setEnabled(bool enabled);

  protected:
    Component() = default;

    virtual void onCreate() {}
    virtual void onEnable() {}
    virtual void onStart() {}
    virtual void onFixedUpdate(float) {}
    virtual void onUpdate(float) {}
    virtual void onLateUpdate(float) {}
    virtual void onCollisionEnter(EntityGuid) {}
    virtual void onCollisionStay(EntityGuid) {}
    virtual void onCollisionExit(EntityGuid) {}
    virtual void onTriggerEnter(EntityGuid) {}
    virtual void onTriggerExit(EntityGuid) {}
    virtual void onDisable() {}
    virtual void onDestroy() {}

  private:
    friend class Entity;

    Entity* owner_ = nullptr;
    bool enabled_ = true;
    bool created_ = false;
    bool active_ = false;
    bool started_ = false;
    bool destroyed_ = false;
};

} // namespace fabgl
