#include "fabgl/scene/transform_component.h"

#include "fabgl/reflection/reflection.h"
#include "fabgl/scene/entity.h"
#include "fabgl/scene/scene.h"

namespace fabgl {
namespace {

Result<void> writeVec3Property(void* instance, const PropertyValue& value,
                               void (TransformComponent::*setter)(Vec3), const char* propertyName) {
    const auto* vector = std::get_if<Vec3>(&value);
    if (vector == nullptr) {
        return Result<void>::failure(Error(ErrorCode::TypeMismatch, "property value must be Vec3")
                                         .addContext("property", propertyName));
    }
    (static_cast<TransformComponent*>(instance)->*setter)(*vector);
    return Result<void>::success();
}

Result<void> writeEulerProperty(void* instance, const PropertyValue& value,
                                void (TransformComponent::*setter)(Vec3),
                                const char* propertyName) {
    const auto* euler = std::get_if<EulerAngles>(&value);
    if (euler == nullptr) {
        return Result<void>::failure(
            Error(ErrorCode::TypeMismatch, "property value must be Euler angles")
                .addContext("property", propertyName));
    }
    (static_cast<TransformComponent*>(instance)->*setter)(Vec3{euler->x, euler->y, euler->z});
    return Result<void>::success();
}

TypeMetadata createTransformMetadata() {
    TypeMetadata metadata;
    metadata.typeId = TransformComponent::staticTypeId();
    metadata.name = "fabgl.Transform";
    metadata.displayName = "Transform";

    PropertyMetadata position;
    position.name = "localPosition";
    position.displayName = "Position";
    position.type = PropertyType::Vec3;
    position.flags = PropertyFlags::Serialize | PropertyFlags::RuntimeEditable;
    position.defaultValue = Vec3{};
    position.category = "Transform";
    position.reader = [](const void* instance) {
        return Result<PropertyValue>::success(
            PropertyValue(static_cast<const TransformComponent*>(instance)->localPosition()));
    };
    position.writer = [](void* instance, const PropertyValue& value) {
        return writeVec3Property(instance, value, &TransformComponent::setLocalPosition,
                                 "localPosition");
    };

    PropertyMetadata rotation;
    rotation.name = "localRotation";
    rotation.displayName = "Rotation (Radians)";
    rotation.type = PropertyType::EulerAngles;
    rotation.flags = PropertyFlags::Serialize | PropertyFlags::RuntimeEditable;
    rotation.defaultValue = EulerAngles{};
    rotation.category = "Transform";
    rotation.reader = [](const void* instance) {
        const auto value = static_cast<const TransformComponent*>(instance)->localRotation();
        return Result<PropertyValue>::success(PropertyValue(EulerAngles{value.x, value.y, value.z}));
    };
    rotation.writer = [](void* instance, const PropertyValue& value) {
        return writeEulerProperty(instance, value, &TransformComponent::setLocalRotation,
                                  "localRotation");
    };

    PropertyMetadata scale;
    scale.name = "localScale";
    scale.displayName = "Scale";
    scale.type = PropertyType::Vec3;
    scale.flags = PropertyFlags::Serialize | PropertyFlags::RuntimeEditable;
    scale.defaultValue = Vec3{1.0F, 1.0F, 1.0F};
    scale.category = "Transform";
    scale.reader = [](const void* instance) {
        return Result<PropertyValue>::success(
            PropertyValue(static_cast<const TransformComponent*>(instance)->localScale()));
    };
    scale.writer = [](void* instance, const PropertyValue& value) {
        return writeVec3Property(instance, value, &TransformComponent::setLocalScale, "localScale");
    };

    metadata.properties.push_back(std::move(position));
    metadata.properties.push_back(std::move(rotation));
    metadata.properties.push_back(std::move(scale));
    return metadata;
}

} // namespace

ComponentTypeGuid TransformComponent::staticTypeId() noexcept {
    static const auto id = ComponentTypeGuid::fromStableName("fabgl.component.Transform.v1");
    return id;
}

const TypeMetadata* TransformComponent::metadata() const noexcept {
    static const auto metadata = createTransformMetadata();
    return &metadata;
}

void TransformComponent::setLocalPosition(Vec3 position) {
    if (localPosition_ != position) {
        localPosition_ = position;
        markDirty();
    }
}

void TransformComponent::setLocalRotation(Vec3 eulerRadians) {
    if (localRotation_ != eulerRadians) {
        localRotation_ = eulerRadians;
        markDirty();
    }
}

void TransformComponent::setLocalScale(Vec3 scale) {
    if (localScale_ != scale) {
        localScale_ = scale;
        markDirty();
    }
}

Mat4 TransformComponent::localMatrix() const noexcept {
    return Mat4::trs(localPosition_, localRotation_, localScale_);
}

Result<Mat4> TransformComponent::worldMatrix() const {
    if (owner() == nullptr) {
        return Result<Mat4>::failure(
            Error(ErrorCode::InvalidState, "transform is not attached to an entity"));
    }
    return owner()->scene().worldTransform(owner()->id());
}

void TransformComponent::markDirty() {
    if (owner() != nullptr) {
        owner()->scene().markTransformDirty(owner()->id());
    } else {
        worldDirty_ = true;
    }
}

} // namespace fabgl
