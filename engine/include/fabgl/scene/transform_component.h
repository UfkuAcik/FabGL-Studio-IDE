#pragma once

#include "fabgl/core/result.h"
#include "fabgl/math/types.h"
#include "fabgl/scene/component.h"

#include <optional>
#include <vector>

namespace fabgl {

class Scene;

class TransformComponent final : public Component {
  public:
    [[nodiscard]] static ComponentTypeGuid staticTypeId() noexcept;
    [[nodiscard]] ComponentTypeGuid typeId() const noexcept override {
        return staticTypeId();
    }
    [[nodiscard]] std::string_view typeName() const noexcept override {
        return "Transform";
    }
    [[nodiscard]] const TypeMetadata* metadata() const noexcept override;

    [[nodiscard]] Vec3 localPosition() const noexcept {
        return localPosition_;
    }
    [[nodiscard]] Vec3 localRotation() const noexcept {
        return localRotation_;
    }
    [[nodiscard]] Vec3 localScale() const noexcept {
        return localScale_;
    }
    [[nodiscard]] const std::optional<EntityGuid>& parent() const noexcept {
        return parent_;
    }
    [[nodiscard]] const std::vector<EntityGuid>& children() const noexcept {
        return children_;
    }
    [[nodiscard]] bool worldTransformDirty() const noexcept {
        return worldDirty_;
    }

    void setLocalPosition(Vec3 position);
    void setLocalRotation(Vec3 eulerRadians);
    void setLocalScale(Vec3 scale);

    [[nodiscard]] Mat4 localMatrix() const noexcept;
    [[nodiscard]] Result<Mat4> worldMatrix() const;

  private:
    friend class Scene;

    void markDirty();

    Vec3 localPosition_{};
    Vec3 localRotation_{};
    Vec3 localScale_{1.0F, 1.0F, 1.0F};
    std::optional<EntityGuid> parent_;
    std::vector<EntityGuid> children_;
    mutable Mat4 cachedWorld_ = Mat4::identity();
    mutable bool worldDirty_ = true;
};

} // namespace fabgl
