#include "test_harness.h"

#include "fabgl/reflection/reflection.h"
#include "fabgl/scene/scene.h"
#include "fabgl/serialization/scene_serializer.h"

#include <memory>
#include <string>

using namespace fabgl;

namespace {

struct LifecycleCounts final {
    int create = 0;
    int enable = 0;
    int start = 0;
    int fixedUpdate = 0;
    int update = 0;
    int lateUpdate = 0;
    int collisionEnter = 0;
    int collisionStay = 0;
    int collisionExit = 0;
    int triggerEnter = 0;
    int triggerExit = 0;
    int disable = 0;
    int destroy = 0;
};

class LifecycleProbe final : public Component {
  public:
    explicit LifecycleProbe(std::shared_ptr<LifecycleCounts> counts) : counts_(std::move(counts)) {}

    [[nodiscard]] ComponentTypeGuid typeId() const noexcept override {
        return ComponentTypeGuid::fromStableName("tests.LifecycleProbe");
    }
    [[nodiscard]] std::string_view typeName() const noexcept override {
        return "LifecycleProbe";
    }

  protected:
    void onCreate() override {
        ++counts_->create;
    }
    void onEnable() override {
        ++counts_->enable;
    }
    void onStart() override {
        ++counts_->start;
    }
    void onFixedUpdate(float) override {
        ++counts_->fixedUpdate;
    }
    void onUpdate(float) override {
        ++counts_->update;
    }
    void onLateUpdate(float) override {
        ++counts_->lateUpdate;
    }
    void onCollisionEnter(EntityGuid) override {
        ++counts_->collisionEnter;
    }
    void onCollisionStay(EntityGuid) override {
        ++counts_->collisionStay;
    }
    void onCollisionExit(EntityGuid) override {
        ++counts_->collisionExit;
    }
    void onTriggerEnter(EntityGuid) override {
        ++counts_->triggerEnter;
    }
    void onTriggerExit(EntityGuid) override {
        ++counts_->triggerExit;
    }
    void onDisable() override {
        ++counts_->disable;
    }
    void onDestroy() override {
        ++counts_->destroy;
    }

  private:
    std::shared_ptr<LifecycleCounts> counts_;
};

} // namespace

FGL_TEST(component_lifecycle_is_ordered_and_gated_by_enable_state) {
    Scene scene("Lifecycle");
    auto entityResult = scene.createEntity("Actor");
    FGL_CHECK(entityResult);
    auto* entity = entityResult.value();
    auto counts = std::make_shared<LifecycleCounts>();
    auto componentResult = entity->addComponent<LifecycleProbe>(counts);
    FGL_CHECK(componentResult);
    auto* component = componentResult.value();
    FGL_CHECK(counts->create == 1);
    FGL_CHECK(counts->enable == 1);
    FGL_CHECK(counts->start == 0);

    FGL_CHECK(!scene.update(0.016F));
    FGL_CHECK(scene.start());
    FGL_CHECK(counts->start == 1);
    FGL_CHECK(scene.fixedUpdate(0.02F));
    FGL_CHECK(scene.update(0.016F));
    FGL_CHECK(scene.lateUpdate(0.016F));
    FGL_CHECK(counts->fixedUpdate == 1);
    FGL_CHECK(counts->update == 1);
    FGL_CHECK(counts->lateUpdate == 1);

    const auto other = EntityGuid::generate();
    FGL_CHECK(scene.dispatchCollisionEnter(entity->id(), other));
    FGL_CHECK(scene.dispatchCollisionStay(entity->id(), other));
    FGL_CHECK(scene.dispatchCollisionExit(entity->id(), other));
    FGL_CHECK(scene.dispatchTriggerEnter(entity->id(), other));
    FGL_CHECK(scene.dispatchTriggerExit(entity->id(), other));
    FGL_CHECK(counts->collisionEnter == 1 && counts->collisionStay == 1 &&
              counts->collisionExit == 1);
    FGL_CHECK(counts->triggerEnter == 1 && counts->triggerExit == 1);

    component->setEnabled(false);
    FGL_CHECK(counts->disable == 1);
    FGL_CHECK(scene.update(0.016F));
    FGL_CHECK(counts->update == 1);
    component->setEnabled(true);
    FGL_CHECK(counts->enable == 2);
    FGL_CHECK(counts->start == 1);
    entity->setActive(false);
    FGL_CHECK(counts->disable == 2);
    entity->setActive(true);
    FGL_CHECK(counts->enable == 3);

    auto duplicate = entity->addComponent<LifecycleProbe>(counts);
    FGL_CHECK(!duplicate);
    FGL_CHECK(duplicate.error().code() == ErrorCode::AlreadyExists);

    const auto entityId = entity->id();
    FGL_CHECK(scene.destroyEntity(entityId));
    FGL_CHECK(counts->disable == 3);
    FGL_CHECK(counts->destroy == 1);
    FGL_CHECK(scene.findEntity(entityId) == nullptr);
}

FGL_TEST(transform_hierarchy_prevents_cycles_and_propagates_dirty_world_state) {
    Scene scene("Transforms");
    auto parentResult = scene.createEntity("Parent");
    auto childResult = scene.createEntity("Child");
    auto grandchildResult = scene.createEntity("Grandchild");
    FGL_CHECK(parentResult && childResult && grandchildResult);
    auto* parent = parentResult.value();
    auto* child = childResult.value();
    auto* grandchild = grandchildResult.value();

    parent->transform().setLocalPosition({10.0F, 0.0F, 0.0F});
    parent->transform().setLocalScale({2.0F, 2.0F, 1.0F});
    child->transform().setLocalPosition({1.0F, 0.0F, 0.0F});
    grandchild->transform().setLocalPosition({0.0F, 3.0F, 0.0F});
    FGL_CHECK(scene.setParent(child->id(), parent->id()));
    FGL_CHECK(scene.setParent(grandchild->id(), child->id()));

    auto childWorld = child->transform().worldMatrix();
    auto grandchildWorld = grandchild->transform().worldMatrix();
    FGL_CHECK(childWorld && grandchildWorld);
    FGL_CHECK(nearlyEqual(childWorld.value().transformPoint({}), {12.0F, 0.0F, 0.0F}));
    FGL_CHECK(nearlyEqual(grandchildWorld.value().transformPoint({}), {12.0F, 6.0F, 0.0F}));
    FGL_CHECK(!grandchild->transform().worldTransformDirty());

    parent->transform().setLocalPosition({20.0F, 0.0F, 0.0F});
    FGL_CHECK(child->transform().worldTransformDirty());
    FGL_CHECK(grandchild->transform().worldTransformDirty());
    grandchildWorld = grandchild->transform().worldMatrix();
    FGL_CHECK(grandchildWorld);
    FGL_CHECK(nearlyEqual(grandchildWorld.value().transformPoint({}), {22.0F, 6.0F, 0.0F}));

    auto cycle = scene.setParent(parent->id(), grandchild->id());
    FGL_CHECK(!cycle);
    FGL_CHECK(cycle.error().code() == ErrorCode::CycleDetected);
    FGL_CHECK(!scene.setParent(parent->id(), parent->id()));

    const auto parentId = parent->id();
    const auto childId = child->id();
    FGL_CHECK(scene.destroyEntity(parentId));
    FGL_CHECK(!child->transform().parent());
    FGL_CHECK(scene.findEntity(childId) == child);
}

FGL_TEST(transform_reflection_metadata_reads_and_writes_live_values) {
    Scene scene;
    auto entity = scene.createEntity();
    FGL_CHECK(entity);
    auto& transform = entity.value()->transform();
    const auto* metadata = transform.metadata();
    FGL_CHECK(metadata != nullptr);
    FGL_CHECK(metadata->properties.size() == 3);
    const auto* position = metadata->findProperty("localPosition");
    FGL_CHECK(position != nullptr);
    FGL_CHECK(position->write(&transform, PropertyValue(Vec3{7.0F, 8.0F, 9.0F})));
    FGL_CHECK(transform.localPosition() == Vec3{7.0F, 8.0F, 9.0F});
    auto read = position->read(&transform);
    FGL_CHECK(read);
    FGL_CHECK(std::get<Vec3>(read.value()) == transform.localPosition());
}

FGL_TEST(scene_serialization_round_trips_ids_hierarchy_values_and_unicode) {
    const auto sceneId = SceneGuid::fromStableName("tests.scene.roundtrip");
    const auto rootId = EntityGuid::fromStableName("tests.entity.root");
    const auto childId = EntityGuid::fromStableName("tests.entity.child");
    Scene scene("Sahne \"Bir\"\nTürkçe\\path", sceneId);
    auto root = scene.createEntity("Kök \"Entity\"", rootId);
    auto child = scene.createEntity("Çocuk\nEntity", childId);
    FGL_CHECK(root && child);
    root.value()->transform().setLocalPosition({1.25F, -2.5F, 3.75F});
    root.value()->transform().setLocalRotation({0.1F, 0.2F, 0.3F});
    child.value()->transform().setLocalScale({2.0F, 3.0F, 4.0F});
    child.value()->setActive(false);
    FGL_CHECK(scene.setParent(childId, rootId));

    auto serialized = SceneSerializer::serialize(scene);
    FGL_CHECK(serialized);
    FGL_CHECK(serialized.value().find("fglscene 1") == 0);
    FGL_CHECK(serialized.value().find("Türkçe") != std::string::npos);

    auto loaded = SceneSerializer::deserialize(serialized.value());
    FGL_CHECK(loaded);
    FGL_CHECK(loaded.value()->id() == sceneId);
    FGL_CHECK(loaded.value()->name() == scene.name());
    FGL_CHECK(loaded.value()->entityCount() == 2);
    const auto* loadedRoot = loaded.value()->findEntity(rootId);
    const auto* loadedChild = loaded.value()->findEntity(childId);
    FGL_CHECK(loadedRoot != nullptr && loadedChild != nullptr);
    FGL_CHECK(loadedRoot->name() == "Kök \"Entity\"");
    FGL_CHECK(loadedChild->name() == "Çocuk\nEntity");
    FGL_CHECK(!loadedChild->active());
    FGL_CHECK(loadedChild->transform().parent() && *loadedChild->transform().parent() == rootId);
    FGL_CHECK(loadedRoot->transform().localPosition() == Vec3{1.25F, -2.5F, 3.75F});
    FGL_CHECK(loadedChild->transform().localScale() == Vec3{2.0F, 3.0F, 4.0F});

    auto serializedAgain = SceneSerializer::serialize(*loaded.value());
    FGL_CHECK(serializedAgain);
    FGL_CHECK(serializedAgain.value() == serialized.value());
}

FGL_TEST(scene_deserializer_rejects_bad_versions_missing_parents_and_cycles) {
    auto badVersion = SceneSerializer::deserialize("fglscene 99\n");
    FGL_CHECK(!badVersion);
    FGL_CHECK(badVersion.error().code() == ErrorCode::UnsupportedVersion);

    const auto sceneGuid = SceneGuid::fromStableName("bad.scene").toString();
    const auto first = EntityGuid::fromStableName("bad.first").toString();
    const auto missing = EntityGuid::fromStableName("bad.missing").toString();
    const std::string missingParent =
        "fglscene 1\nscene_guid " + sceneGuid + "\nscene_name \"Bad\"\nentity_begin\nguid " +
        first + "\nname \"First\"\nactive 1\nparent " + missing +
        "\nposition 0 0 0\nrotation 0 0 0\nscale 1 1 1\nentity_end\nscene_end\n";
    auto missingResult = SceneSerializer::deserialize(missingParent);
    FGL_CHECK(!missingResult);
    FGL_CHECK(missingResult.error().code() == ErrorCode::NotFound);

    const auto second = EntityGuid::fromStableName("bad.second").toString();
    const std::string cyclic =
        "fglscene 1\nscene_guid " + sceneGuid + "\nscene_name \"Bad\"\nentity_begin\nguid " +
        first + "\nname \"First\"\nactive 1\nparent " + second +
        "\nposition 0 0 0\nrotation 0 0 0\nscale 1 1 1\nentity_end\nentity_begin\nguid " + second +
        "\nname \"Second\"\nactive 1\nparent " + first +
        "\nposition 0 0 0\nrotation 0 0 0\nscale 1 1 1\nentity_end\nscene_end\n";
    auto cycleResult = SceneSerializer::deserialize(cyclic);
    FGL_CHECK(!cycleResult);
    FGL_CHECK(cycleResult.error().code() == ErrorCode::CycleDetected);

    const std::string trailing =
        "fglscene 1\nscene_guid " + sceneGuid + "\nscene_name \"Bad\"\nscene_end\nunexpected\n";
    auto trailingResult = SceneSerializer::deserialize(trailing);
    FGL_CHECK(!trailingResult);
    FGL_CHECK(trailingResult.error().code() == ErrorCode::InvalidFormat);
}

FGL_TEST(scene_serializer_refuses_to_silently_drop_unknown_components) {
    Scene scene;
    auto entity = scene.createEntity();
    FGL_CHECK(entity);
    auto counts = std::make_shared<LifecycleCounts>();
    FGL_CHECK(entity.value()->addComponent<LifecycleProbe>(counts));
    auto serialized = SceneSerializer::serialize(scene);
    FGL_CHECK(!serialized);
    FGL_CHECK(serialized.error().code() == ErrorCode::SerializationFailed);
}
