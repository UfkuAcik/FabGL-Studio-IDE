#include "test_harness.h"

#include "fabgl/reflection/reflection.h"
#include "fabgl/scene/builtin_components.h"
#include "fabgl/scene/scene.h"
#include "fabgl/scripting/script_component.h"
#include "fabgl/serialization/scene_serializer.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

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

class UnsupportedSceneScript final : public scripting::ScriptComponent {
  public:
    UnsupportedSceneScript()
        : ScriptComponent(
              scripting::makeScriptMetadata("tests.UnsupportedSceneScript", "Unsupported")) {}
};

DataComponent* addBuiltin(Entity& entity, const ReflectionRegistry& registry,
                          std::string_view shortName) {
    auto component = createBuiltinDataComponent(registry, shortName);
    FGL_CHECK(component);
    auto attached = entity.addComponent(std::move(component.value()));
    FGL_CHECK(attached);
    auto* data = dynamic_cast<DataComponent*>(attached.value());
    FGL_CHECK(data != nullptr);
    return data;
}

DataComponent* findBuiltin(Entity& entity, const ReflectionRegistry& registry,
                           std::string_view shortName) {
    const auto* metadata = registry.find(std::string("fabgl.") + std::string(shortName));
    FGL_CHECK(metadata != nullptr);
    auto* data = dynamic_cast<DataComponent*>(entity.getComponent(metadata->typeId));
    FGL_CHECK(data != nullptr);
    return data;
}

template <typename Value>
Value propertyValue(const DataComponent& component, std::string_view propertyName) {
    auto value = component.get(propertyName);
    FGL_CHECK(value);
    FGL_CHECK(std::holds_alternative<Value>(value.value()));
    return std::get<Value>(value.value());
}

void replaceOnce(std::string& text, const std::string& from, const std::string& to) {
    const auto position = text.find(from);
    FGL_CHECK(position != std::string::npos);
    text.replace(position, from.size(), to);
}

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

FGL_TEST(entity_component_removal_runs_lifecycle_and_preserves_transform) {
    Scene scene("Removal");
    auto entityResult = scene.createEntity("Actor");
    FGL_CHECK(entityResult);
    auto* entity = entityResult.value();
    auto counts = std::make_shared<LifecycleCounts>();
    auto component = entity->addComponent<LifecycleProbe>(counts);
    FGL_CHECK(component);
    const auto probeType = component.value()->typeId();

    auto transformRemoval = entity->removeComponent(TransformComponent::staticTypeId());
    FGL_CHECK(!transformRemoval);
    FGL_CHECK(transformRemoval.error().code() == ErrorCode::InvalidArgument);
    FGL_CHECK(entity->getComponent(TransformComponent::staticTypeId()) != nullptr);

    FGL_CHECK(entity->removeComponent(probeType));
    FGL_CHECK(counts->disable == 1);
    FGL_CHECK(counts->destroy == 1);
    FGL_CHECK(entity->getComponent(probeType) == nullptr);
    auto missing = entity->removeComponent(probeType);
    FGL_CHECK(!missing);
    FGL_CHECK(missing.error().code() == ErrorCode::NotFound);
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
    const auto* rotation = metadata->findProperty("localRotation");
    FGL_CHECK(rotation != nullptr && rotation->type == PropertyType::EulerAngles);
    FGL_CHECK(rotation->write(&transform, PropertyValue(EulerAngles{0.25F, 0.5F, 0.75F})));
    FGL_CHECK(transform.localRotation() == Vec3{0.25F, 0.5F, 0.75F});
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
    FGL_CHECK(serialized.value().find("fglscene 2") == 0);
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

FGL_TEST(scene_component_properties_and_enabled_state_round_trip_through_version_two) {
    ReflectionRegistry registry;
    FGL_CHECK(registerBuiltinComponentTypes(registry));

    const auto sourceId = EntityGuid::fromStableName("tests.component-roundtrip.source");
    const auto targetId = EntityGuid::fromStableName("tests.component-roundtrip.target");
    const auto assetId = AssetGuid::fromStableName("tests.component-roundtrip.asset");
    Scene scene("Components", SceneGuid::fromStableName("tests.component-roundtrip.scene"));
    auto sourceResult = scene.createEntity("Source", sourceId);
    auto targetResult = scene.createEntity("Target", targetId);
    FGL_CHECK(sourceResult && targetResult);
    auto& source = *sourceResult.value();
    source.transform().setLocalPosition({3.0F, 4.0F, 5.0F});

    auto* camera = addBuiltin(source, registry, "Camera");
    camera->setEnabled(false);
    FGL_CHECK(camera->set("enabled", PropertyValue(true)));
    FGL_CHECK(camera->set("orthographic", PropertyValue(false)));
    FGL_CHECK(camera->set("size", PropertyValue(8.125)));
    FGL_CHECK(camera->set("clearColor", PropertyValue(Color{12, 34, 56, 78})));
    FGL_CHECK(camera->set("viewport", PropertyValue(Rect{0.1F, 0.2F, 0.7F, 0.6F})));
    FGL_CHECK(camera->set("projection", PropertyValue(std::int64_t{-2})));

    auto* sprite = addBuiltin(source, registry, "SpriteRenderer");
    FGL_CHECK(sprite->set("sprite", PropertyValue(assetId)));
    auto* collider2d = addBuiltin(source, registry, "Collider2D");
    FGL_CHECK(collider2d->set("size", PropertyValue(Vec2{2.5F, 7.5F})));
    FGL_CHECK(collider2d->set("layer", PropertyValue(std::uint64_t{0x8000000000000001ULL})));
    auto* collider3d = addBuiltin(source, registry, "Collider3D");
    FGL_CHECK(collider3d->set("size", PropertyValue(Vec3{9.0F, 8.0F, 7.0F})));
    const Quaternion orientation{0.0F, 0.0F, 0.707F, 0.707F};
    FGL_CHECK(collider3d->set("orientation", PropertyValue(orientation)));
    auto* raycastMap = addBuiltin(source, registry, "RaycastMap");
    FGL_CHECK(raycastMap->set("cellSize", PropertyValue(Fixed::fromRaw(-12345))));
    auto* particles = addBuiltin(source, registry, "ParticleEmitter");
    FGL_CHECK(particles->set("maxParticles", PropertyValue(std::uint64_t{1234567890123ULL})));
    auto* health = addBuiltin(source, registry, "Health");
    FGL_CHECK(health->set("current", PropertyValue(std::int64_t{-17})));
    auto* text = addBuiltin(source, registry, "UIText");
    const std::string textValue = "Metin \"bir\"\npath\\tail";
    FGL_CHECK(text->set("text", PropertyValue(textValue)));
    auto* navigation = addBuiltin(source, registry, "NavigationAgent");
    FGL_CHECK(navigation->set("target", PropertyValue(targetId)));
    const ComponentReference targetTransform{targetId, TransformComponent::staticTypeId()};
    FGL_CHECK(navigation->set("targetComponent", PropertyValue(targetTransform)));
    auto* script = addBuiltin(source, registry, "ScriptComponent");
    const std::string notes = "first line\nsecond line";
    FGL_CHECK(script->set("notes", PropertyValue(notes)));
    auto* visualScript = addBuiltin(source, registry, "VisualScriptComponent");
    FGL_CHECK(visualScript->set("triggerAction", PropertyValue(ActionReference{"Jump"})));
    FGL_CHECK(
        visualScript->set("completionEvent", PropertyValue(EventReference{"Completed"})));
    auto* image = addBuiltin(source, registry, "UIImage");
    const PropertyList frames{PropertyType::AssetReference, {assetId}};
    FGL_CHECK(image->set("frames", PropertyValue(frames)));
    auto* light = addBuiltin(source, registry, "Light");
    FGL_CHECK(light->set("intensity", PropertyValue(2.5)));
    const Curve falloff{{CurvePoint{0.0, 1.0}, CurvePoint{0.5, 0.75}, CurvePoint{1.0, 0.0}}};
    FGL_CHECK(light->set("falloff", PropertyValue(falloff)));
    auto* damage = addBuiltin(source, registry, "DamageReceiver");
    const PropertyAnimationCurve response{
        {AnimationCurveKey{0.0, 0.0, 0.0, 0.25}, AnimationCurveKey{1.0, 1.0, -0.25, 0.0}}};
    FGL_CHECK(damage->set("responseCurve", PropertyValue(response)));

    auto serialized = SceneSerializer::serialize(scene);
    FGL_CHECK(serialized);
    FGL_CHECK(serialized.value().find("fglscene 2") == 0);
    auto loaded = SceneSerializer::deserialize(serialized.value());
    FGL_CHECK(loaded);
    auto* loadedSource = loaded.value()->findEntity(sourceId);
    FGL_CHECK(loadedSource != nullptr);
    FGL_CHECK(loadedSource->transform().localPosition() == Vec3{3.0F, 4.0F, 5.0F});

    camera = findBuiltin(*loadedSource, registry, "Camera");
    FGL_CHECK(!camera->enabled());
    FGL_CHECK(propertyValue<bool>(*camera, "enabled"));
    FGL_CHECK(!propertyValue<bool>(*camera, "orthographic"));
    FGL_CHECK(propertyValue<double>(*camera, "size") == 8.125);
    FGL_CHECK(propertyValue<Color>(*camera, "clearColor") == Color{12, 34, 56, 78});
    FGL_CHECK(propertyValue<Rect>(*camera, "viewport") == Rect{0.1F, 0.2F, 0.7F, 0.6F});
    FGL_CHECK(propertyValue<std::int64_t>(*camera, "projection") == -2);
    FGL_CHECK(propertyValue<AssetGuid>(*findBuiltin(*loadedSource, registry, "SpriteRenderer"),
                                       "sprite") == assetId);
    FGL_CHECK(propertyValue<Vec2>(*findBuiltin(*loadedSource, registry, "Collider2D"), "size") ==
              Vec2{2.5F, 7.5F});
    FGL_CHECK(propertyValue<std::uint64_t>(*findBuiltin(*loadedSource, registry, "Collider2D"),
                                           "layer") == std::uint64_t{0x8000000000000001ULL});
    FGL_CHECK(propertyValue<Vec3>(*findBuiltin(*loadedSource, registry, "Collider3D"), "size") ==
              Vec3{9.0F, 8.0F, 7.0F});
    FGL_CHECK(propertyValue<Quaternion>(*findBuiltin(*loadedSource, registry, "Collider3D"),
                                        "orientation") == orientation);
    FGL_CHECK(propertyValue<Fixed>(*findBuiltin(*loadedSource, registry, "RaycastMap"),
                                   "cellSize") == Fixed::fromRaw(-12345));
    FGL_CHECK(propertyValue<std::uint64_t>(*findBuiltin(*loadedSource, registry, "ParticleEmitter"),
                                           "maxParticles") == std::uint64_t{1234567890123ULL});
    FGL_CHECK(propertyValue<std::int64_t>(*findBuiltin(*loadedSource, registry, "Health"),
                                          "current") == -17);
    FGL_CHECK(propertyValue<std::string>(*findBuiltin(*loadedSource, registry, "UIText"), "text") ==
              textValue);
    FGL_CHECK(propertyValue<EntityGuid>(*findBuiltin(*loadedSource, registry, "NavigationAgent"),
                                        "target") == targetId);
    FGL_CHECK(propertyValue<ComponentReference>(
                  *findBuiltin(*loadedSource, registry, "NavigationAgent"),
                  "targetComponent") == targetTransform);
    FGL_CHECK(propertyValue<std::string>(*findBuiltin(*loadedSource, registry, "ScriptComponent"),
                                          "notes") == notes);
    FGL_CHECK(propertyValue<ActionReference>(
                  *findBuiltin(*loadedSource, registry, "VisualScriptComponent"),
                  "triggerAction") == ActionReference{"Jump"});
    FGL_CHECK(propertyValue<EventReference>(
                  *findBuiltin(*loadedSource, registry, "VisualScriptComponent"),
                  "completionEvent") == EventReference{"Completed"});
    FGL_CHECK(propertyValue<PropertyList>(*findBuiltin(*loadedSource, registry, "UIImage"),
                                          "frames") == frames);
    FGL_CHECK(propertyValue<Curve>(*findBuiltin(*loadedSource, registry, "Light"), "falloff") ==
              falloff);
    FGL_CHECK(propertyValue<PropertyAnimationCurve>(
                  *findBuiltin(*loadedSource, registry, "DamageReceiver"),
                  "responseCurve") == response);

    auto serializedAgain = SceneSerializer::serialize(*loaded.value());
    FGL_CHECK(serializedAgain);
    FGL_CHECK(serializedAgain.value() == serialized.value());
}

FGL_TEST(scene_deserializer_migrates_version_one_transform_records_to_version_two) {
    const auto sceneId = SceneGuid::fromStableName("tests.v1-migration.scene");
    const auto rootId = EntityGuid::fromStableName("tests.v1-migration.root");
    const auto childId = EntityGuid::fromStableName("tests.v1-migration.child");
    const std::string legacy =
        "fglscene 1\nscene_guid " + sceneId.toString() +
        "\nscene_name \"Legacy\"\nentity_begin\nguid " + rootId.toString() +
        "\nname \"Root\"\nactive 1\nparent nil\nposition 1 2 3\nrotation 0.1 0.2 "
        "0.3\nscale 1 1 1\nentity_end\nentity_begin\nguid " +
        childId.toString() + "\nname \"Child\"\nactive 0\nparent " + rootId.toString() +
        "\nposition -1 -2 -3\nrotation 0 0 0\nscale 2 3 4\nentity_end\nscene_end\n";

    auto loaded = SceneSerializer::deserialize(legacy);
    FGL_CHECK(loaded);
    const auto* root = loaded.value()->findEntity(rootId);
    const auto* child = loaded.value()->findEntity(childId);
    FGL_CHECK(root != nullptr && child != nullptr);
    FGL_CHECK(root->transform().localPosition() == Vec3{1.0F, 2.0F, 3.0F});
    FGL_CHECK(child->transform().localScale() == Vec3{2.0F, 3.0F, 4.0F});
    FGL_CHECK(!child->active());
    FGL_CHECK(child->transform().parent() && *child->transform().parent() == rootId);

    auto migrated = SceneSerializer::serialize(*loaded.value());
    FGL_CHECK(migrated);
    FGL_CHECK(migrated.value().find("fglscene 2") == 0);
    FGL_CHECK(migrated.value().find("component_begin") != std::string::npos);
}

FGL_TEST(scene_v2_accepts_legacy_vec3_rotation_and_writes_explicit_euler_type) {
    Scene scene("Euler migration", SceneGuid::fromStableName("tests.euler-migration.scene"));
    auto entity = scene.createEntity("Rotated", EntityGuid::fromStableName("tests.euler-migration.entity"));
    FGL_CHECK(entity);
    entity.value()->transform().setLocalRotation({0.25F, -0.5F, 1.0F});
    auto canonical = SceneSerializer::serialize(scene);
    FGL_CHECK(canonical);
    FGL_CHECK(canonical.value().find("property \"localRotation\" euler 0.25 -0.5 1") !=
              std::string::npos);

    auto legacy = canonical.value();
    replaceOnce(legacy, "property \"localRotation\" euler",
                "property \"localRotation\" vec3");
    auto loaded = SceneSerializer::deserialize(legacy);
    FGL_CHECK(loaded);
    FGL_CHECK(loaded.value()->entities().front()->transform().localRotation() ==
              Vec3{0.25F, -0.5F, 1.0F});
    auto migrated = SceneSerializer::serialize(*loaded.value());
    FGL_CHECK(migrated);
    FGL_CHECK(migrated.value().find("property \"localRotation\" euler") !=
              std::string::npos);
}

FGL_TEST(scene_deserializer_reports_unknown_types_properties_values_and_references) {
    ReflectionRegistry registry;
    FGL_CHECK(registerBuiltinComponentTypes(registry));
    const auto sourceId = EntityGuid::fromStableName("tests.invalid-v2.source");
    const auto targetId = EntityGuid::fromStableName("tests.invalid-v2.target");
    const auto missingId = EntityGuid::fromStableName("tests.invalid-v2.missing");
    const auto assetId = AssetGuid::fromStableName("tests.invalid-v2.asset");
    Scene scene("Invalid v2", SceneGuid::fromStableName("tests.invalid-v2.scene"));
    auto source = scene.createEntity("Source", sourceId);
    FGL_CHECK(source);
    FGL_CHECK(scene.createEntity("Target", targetId));
    auto* camera = addBuiltin(*source.value(), registry, "Camera");
    FGL_CHECK(camera->set("size", PropertyValue(8.125)));
    FGL_CHECK(camera->set("clearColor", PropertyValue(Color{12, 34, 56, 78})));
    auto* sprite = addBuiltin(*source.value(), registry, "SpriteRenderer");
    FGL_CHECK(sprite->set("sprite", PropertyValue(assetId)));
    auto* navigation = addBuiltin(*source.value(), registry, "NavigationAgent");
    FGL_CHECK(navigation->set("target", PropertyValue(targetId)));
    auto serialized = SceneSerializer::serialize(scene);
    FGL_CHECK(serialized);

    auto unknownTypeText = serialized.value();
    replaceOnce(unknownTypeText, "type_id " + camera->typeId().toString(),
                "type_id " + ComponentTypeGuid::fromStableName("tests.unknown-type").toString());
    auto unknownType = SceneSerializer::deserialize(unknownTypeText);
    FGL_CHECK(!unknownType);
    FGL_CHECK(unknownType.error().code() == ErrorCode::NotFound);

    auto unknownPropertyText = serialized.value();
    replaceOnce(unknownPropertyText, "property \"orthographic\" bool",
                "property \"notRegistered\" bool");
    auto unknownProperty = SceneSerializer::deserialize(unknownPropertyText);
    FGL_CHECK(!unknownProperty);
    FGL_CHECK(unknownProperty.error().code() == ErrorCode::NotFound);

    auto mismatchedTypeText = serialized.value();
    replaceOnce(mismatchedTypeText, "property \"size\" float", "property \"size\" string");
    auto mismatchedType = SceneSerializer::deserialize(mismatchedTypeText);
    FGL_CHECK(!mismatchedType);
    FGL_CHECK(mismatchedType.error().code() == ErrorCode::TypeMismatch);

    auto badColorText = serialized.value();
    replaceOnce(badColorText, "property \"clearColor\" color 12 34 56 78",
                "property \"clearColor\" color 999 34 56 78");
    auto badColor = SceneSerializer::deserialize(badColorText);
    FGL_CHECK(!badColor);
    FGL_CHECK(badColor.error().code() == ErrorCode::InvalidFormat);

    auto badAssetText = serialized.value();
    replaceOnce(badAssetText, "property \"sprite\" asset " + assetId.toString(),
                "property \"sprite\" asset not-a-guid");
    auto badAsset = SceneSerializer::deserialize(badAssetText);
    FGL_CHECK(!badAsset);
    FGL_CHECK(badAsset.error().code() == ErrorCode::InvalidFormat);

    auto missingReferenceText = serialized.value();
    replaceOnce(missingReferenceText, "property \"target\" entity " + targetId.toString(),
                "property \"target\" entity " + missingId.toString());
    auto missingReference = SceneSerializer::deserialize(missingReferenceText);
    FGL_CHECK(!missingReference);
    FGL_CHECK(missingReference.error().code() == ErrorCode::NotFound);
    FGL_CHECK(!missingReference.error().context().empty());
}

FGL_TEST(scene_serializer_reports_unsupported_gameplay_script_components) {
    Scene scene;
    auto entity = scene.createEntity();
    FGL_CHECK(entity);
    FGL_CHECK(entity.value()->addComponent<UnsupportedSceneScript>());
    auto serialized = SceneSerializer::serialize(scene);
    FGL_CHECK(!serialized);
    FGL_CHECK(serialized.error().code() == ErrorCode::SerializationFailed);
    FGL_CHECK(serialized.error().message().find("gameplay-script") != std::string::npos);
}
