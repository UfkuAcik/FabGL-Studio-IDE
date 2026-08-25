#include "test_harness.h"

#include <ProjectRuntime.h>
#include <ProjectSaveRuntime.h>
#include <ProjectScriptRuntime.h>
#include <esp32_export.h>
#include <fabgl/assets/asset_pack.h>
#include <fabgl/assets/image_pipeline.h>
#include <fabgl/reflection/reflection.h>
#include <fabgl/rendering/racer_track.h>
#include <fabgl/rendering/raycast_map_asset.h>
#include <fabgl/scene/builtin_components.h>
#include <fabgl/scene/entity.h>
#include <fabgl/scene/scene.h>
#include <fabgl/serialization/scene_serializer.h>
#include <project_format.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

struct VectorReader final {
    const std::vector<std::uint8_t>* bytes = nullptr;

    [[nodiscard]] std::size_t size() const noexcept {
        return bytes->size();
    }
    [[nodiscard]] std::uint8_t byte(const std::size_t offset) const noexcept {
        return (*bytes)[offset];
    }
};

void appendU16(std::vector<std::uint8_t>& output, const std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

std::vector<std::uint8_t> wrapped(const std::string& path,
                                  const std::vector<std::uint8_t>& content) {
    FGL_CHECK(path.size() <= 65535U);
    std::vector<std::uint8_t> output{'F', 'G', 'L', 'A'};
    appendU16(output, 1U);
    appendU16(output, static_cast<std::uint16_t>(path.size()));
    output.insert(output.end(), path.begin(), path.end());
    output.insert(output.end(), content.begin(), content.end());
    return output;
}

fabgl::DataComponent* addBuiltin(fabgl::Entity& entity,
                                 const fabgl::ReflectionRegistry& registry,
                                 const char* name) {
    auto component = fabgl::createBuiltinDataComponent(registry, name);
    FGL_CHECK(component);
    auto* result = component.value().get();
    FGL_CHECK(entity.addComponent(std::move(component.value())));
    return result;
}

fabgl::rendering::RacerTrackAsset testTrack(const fabgl::AssetGuid guid) {
    fabgl::rendering::RacerTrackAsset track;
    track.guid = guid;
    track.name = "Embedded Track";
    track.segmentLength = 12.0F;
    track.startSegment = 0U;
    track.finishSegment = 3U;
    track.weather = {fabgl::rendering::RacerWeatherKind::Clear, 0.0F, 1.0F, 0.0F,
                     {255U, 255U, 255U, 255U}, 7U};
    track.segments.resize(4U);
    track.segments[0].curve = 0.1F;
    track.segments[1].curve = -0.2F;
    track.segments[2].hill = 0.15F;
    track.segments[3].width = 0.85F;
    track.checkpoints = {{0U, "Start"}};
    return track;
}

std::uint32_t scriptStarts = 0U;
std::uint32_t scriptUpdates = 0U;

void startPortableScript(fabgl_project_runtime::RuntimeProject& project) noexcept {
    ++scriptStarts;
    if (project.scene.entityCount != 0U)
        project.scene.entities[0].x = 10.0F;
}

void updatePortableScript(fabgl_project_runtime::RuntimeProject& project,
                          const float deltaSeconds) noexcept {
    ++scriptUpdates;
    if (project.scene.entityCount != 0U)
        project.scene.entities[0].x += deltaSeconds * 20.0F;
}

const fabgl_project_scripts::Descriptor PortableDescriptors[]{
    {sizeof(fabgl_project_scripts::Descriptor), "test.portable", &startPortableScript,
     &updatePortableScript}};

bool portableModule(fabgl_project_scripts::ModuleView* output) noexcept {
    if (output == nullptr)
        return false;
    output->structureSize = sizeof(*output);
    output->abiVersion = fabgl_project_scripts::kAbiVersion;
    output->descriptors = PortableDescriptors;
    output->descriptorCount = 1U;
    return true;
}

fabgl_project_runtime::Guid saveTestGuid(const std::uint8_t seed) {
    fabgl_project_runtime::Guid result;
    for (std::size_t index = 0U; index < sizeof(result.bytes); ++index)
        result.bytes[index] = static_cast<std::uint8_t>(seed + index);
    return result;
}

struct MemorySaveStorage final {
    std::map<std::string, std::vector<std::uint8_t>> files;
    bool failStage = false;
    bool failCommitAfterBackup = false;
    std::size_t discarded = 0U;

    fabgl_project_save::StorageCallbacks callbacks() {
        fabgl_project_save::StorageCallbacks result;
        result.context = this;
        result.read = &readThunk;
        result.stage = &stageThunk;
        result.commit = &commitThunk;
        result.discard = &discardThunk;
        result.remove = &removeThunk;
        return result;
    }

    static fabgl_project_save::StorageStatus
    readThunk(void* context, const char* path, std::uint8_t* output,
              const std::size_t capacity, std::size_t* size) noexcept {
        auto& self = *static_cast<MemorySaveStorage*>(context);
        const auto found = self.files.find(path);
        if (found == self.files.end())
            return fabgl_project_save::StorageStatus::NotFound;
        if (found->second.size() > capacity)
            return fabgl_project_save::StorageStatus::CapacityExceeded;
        std::memcpy(output, found->second.data(), found->second.size());
        *size = found->second.size();
        return fabgl_project_save::StorageStatus::Ok;
    }

    static fabgl_project_save::StorageStatus
    stageThunk(void* context, const char* path, const std::uint8_t* data,
               const std::size_t size) noexcept {
        auto& self = *static_cast<MemorySaveStorage*>(context);
        if (self.failStage)
            return fabgl_project_save::StorageStatus::IoFailure;
        self.files[path] = std::vector<std::uint8_t>(data, data + size);
        return fabgl_project_save::StorageStatus::Ok;
    }

    static fabgl_project_save::StorageStatus
    commitThunk(void* context, const char* live, const char* temporary,
                const char* backup) noexcept {
        auto& self = *static_cast<MemorySaveStorage*>(context);
        const auto staged = self.files.find(temporary);
        if (staged == self.files.end())
            return fabgl_project_save::StorageStatus::CommitFailed;
        const auto existing = self.files.find(live);
        if (existing != self.files.end()) {
            self.files[backup] = existing->second;
            self.files.erase(existing);
        } else {
            self.files.erase(backup);
        }
        if (self.failCommitAfterBackup) {
            const auto previous = self.files.find(backup);
            if (previous != self.files.end()) {
                self.files[live] = previous->second;
                self.files.erase(previous);
            }
            return fabgl_project_save::StorageStatus::CommitFailed;
        }
        self.files[live] = staged->second;
        self.files.erase(staged);
        return fabgl_project_save::StorageStatus::Ok;
    }

    static void discardThunk(void* context, const char* path) noexcept {
        auto& self = *static_cast<MemorySaveStorage*>(context);
        self.files.erase(path);
        ++self.discarded;
    }

    static fabgl_project_save::StorageStatus removeThunk(void* context,
                                                         const char* path) noexcept {
        auto& self = *static_cast<MemorySaveStorage*>(context);
        return self.files.erase(path) == 0U ? fabgl_project_save::StorageStatus::NotFound
                                           : fabgl_project_save::StorageStatus::Ok;
    }
};

fabgl_project_runtime::RuntimeProject saveRuntimeFixture() {
    fabgl_project_runtime::RuntimeProject project;
    project.loaded = true;
    project.scene.guid = saveTestGuid(1U);
    FGL_CHECK(fabgl_project_save::detail::assignText(project.scene.name, "Save Scene"));
    project.scene.entityCount = 2U;
    auto& player = project.scene.entities[0];
    player.guid = saveTestGuid(33U);
    FGL_CHECK(fabgl_project_save::detail::assignText(player.name, "Player One"));
    player.active = true;
    player.components = fabgl_project_runtime::Component::Character |
                        fabgl_project_runtime::Component::Sprite;
    player.movementMode = 1;
    player.x = 12.5F;
    player.y = -4.0F;
    player.z = 7.25F;
    player.rotationZ = 0.75F;
    player.scaleX = 1.5F;
    player.scaleY = 0.5F;
    player.velocityY = -9.0F;
    player.vehicleSpeed = 3.0F;
    auto& door = project.scene.entities[1];
    door.guid = saveTestGuid(65U);
    FGL_CHECK(fabgl_project_save::detail::assignText(door.name, "Exit Door"));
    door.active = false;
    door.x = 99.0F;
    door.y = 24.0F;
    return project;
}

bool migrateSaveV1ToV2(void* context, const std::uint32_t from,
                       fabgl_project_save::Document& document) noexcept {
    auto* count = static_cast<std::uint32_t*>(context);
    ++*count;
    return from == 1U &&
           document.putPrimitive("migration", fabgl_project_save::Value::unsigned32(2U));
}

struct PersistenceProbe final {
    std::size_t saves = 0U;
    std::size_t loads = 0U;
    fabgl_project_save::Document* document = nullptr;
    std::uint8_t error = 17U;
};

bool probeSave(void* context, fabgl_project_runtime::RuntimeProject&, const char* slot,
               const fabgl_project_runtime::Guid*,
               fabgl_project_save::Document* document) noexcept {
    auto& probe = *static_cast<PersistenceProbe*>(context);
    ++probe.saves;
    probe.document = document;
    return slot != nullptr && std::strcmp(slot, "probe") == 0;
}

bool probeLoad(void* context, fabgl_project_runtime::RuntimeProject&, const char* slot,
               const fabgl_project_runtime::Guid*,
               fabgl_project_save::Document* document) noexcept {
    auto& probe = *static_cast<PersistenceProbe*>(context);
    ++probe.loads;
    probe.document = document;
    return slot != nullptr && std::strcmp(slot, "probe") == 0;
}

std::uint8_t probeError(const void* context) noexcept {
    return static_cast<const PersistenceProbe*>(context)->error;
}

} // namespace

FGL_TEST(esp32_runtime_persistence_is_bound_and_invoked_only_by_explicit_gameplay_calls) {
    auto runtime = saveRuntimeFixture();
    fabgl_project_save::Document state;
    const auto player = runtime.scene.entities[0].guid;
    FGL_CHECK(!runtime.persistenceAvailable());
    FGL_CHECK(!runtime.saveSlot("probe", &player, &state));
    PersistenceProbe probe;
    runtime.bindPersistence(&probe, &probeSave, &probeLoad, &probeError);
    FGL_CHECK(runtime.persistenceAvailable());
    FGL_CHECK(probe.saves == 0U && probe.loads == 0U);
    FGL_CHECK(runtime.saveSlot("probe", &player, &state));
    FGL_CHECK(runtime.loadSlot("probe", &player, &state));
    FGL_CHECK(probe.saves == 1U && probe.loads == 1U && probe.document == &state);
    FGL_CHECK(runtime.lastPersistenceError() == 17U);
}

FGL_TEST(esp32_gameplay_save_round_trips_primitives_vectors_strings_scene_entities_and_player) {
    using namespace fabgl_project_save;
    auto runtime = saveRuntimeFixture();
    const auto playerGuid = runtime.scene.entities[0].guid;
    const auto doorGuid = runtime.scene.entities[1].guid;
    Document source;
    FGL_CHECK(captureRuntime(runtime, &playerGuid, source, 2U, 41U));
    FGL_CHECK(source.putPrimitive("alive", Value::boolean(true)));
    FGL_CHECK(source.putPrimitive("score", Value::signed32(-120)));
    FGL_CHECK(source.putPrimitive("coins", Value::unsigned32(9001U)));
    FGL_CHECK(source.putPrimitive("time", Value::real(12.25F)));
    FGL_CHECK(source.putPrimitive("cursor", Value::vector2(3.0F, -2.0F)));
    FGL_CHECK(source.putPrimitive("spawn", Value::vector3(4.0F, 5.0F, 6.0F)));
    FGL_CHECK(source.putPrimitive("chapter", Value::string("Citadel")));
    FGL_CHECK(source.putPrimitive("door", Value::entity(doorGuid)));
    FGL_CHECK(!source.putPrimitive("overflow", Value::unsigned32(1U)));
    FGL_CHECK(source.putScene("weather", Value::string("rain")));
    FGL_CHECK(source.putPlayer("health", Value::unsigned32(73U)));

    MemorySaveStorage memory;
    SaveService saves(memory.callbacks(), "/fabglstudio/saves", 2U);
    FGL_CHECK(saves.configured());
    std::vector<std::uint8_t> working(kMaximumFileBytes);
    FGL_CHECK(saves.save("campaign-1", source, working.data(), working.size()));
    FGL_CHECK(saves.save("checkpoint_2", source, working.data(), working.size()));

    Document loaded;
    LoadInfo info;
    FGL_CHECK(saves.load("campaign-1", loaded, working.data(), working.size(), &info));
    FGL_CHECK(info.storedSchemaVersion == 2U && !info.migrated &&
              !info.recoveredFromBackup);
    FGL_CHECK(loaded.sequence == 41U && loaded.entityCount == 2U);
    FGL_CHECK(loaded.primitive("alive")->payload.booleanValue);
    FGL_CHECK(loaded.primitive("score")->payload.signedValue == -120);
    FGL_CHECK(loaded.primitive("coins")->payload.unsignedValue == 9001U);
    FGL_CHECK(loaded.primitive("time")->payload.floatValue == 12.25F);
    FGL_CHECK(loaded.primitive("cursor")->payload.vector2Value.x == 3.0F);
    FGL_CHECK(loaded.primitive("spawn")->payload.vector3Value.z == 6.0F);
    FGL_CHECK(std::strcmp(loaded.primitive("chapter")->payload.stringValue, "Citadel") == 0);
    FGL_CHECK(std::memcmp(loaded.primitive("door")->payload.guidValue, doorGuid.bytes,
                          sizeof(doorGuid.bytes)) == 0);
    FGL_CHECK(std::strcmp(loaded.scene("weather")->payload.stringValue, "rain") == 0);
    FGL_CHECK(loaded.player("health")->payload.unsignedValue == 73U);

    runtime.scene.entities[0].x = -500.0F;
    runtime.scene.entities[0].active = false;
    runtime.scene.entities[1].x = -800.0F;
    FGL_CHECK(restoreRuntime(loaded, runtime, &playerGuid));
    FGL_CHECK(runtime.scene.entities[0].x == 12.5F);
    FGL_CHECK(runtime.scene.entities[0].active);
    FGL_CHECK(runtime.scene.entities[0].velocityY == -9.0F);
    FGL_CHECK(runtime.scene.entities[1].x == 99.0F);
    FGL_CHECK(!runtime.scene.entities[1].active);
    const auto wrongPlayer = saveTestGuid(111U);
    FGL_CHECK(restoreRuntime(loaded, runtime, &wrongPlayer).error == Error::PlayerMismatch);

    FGL_CHECK(saves.removeSlot("checkpoint_2"));
    FGL_CHECK(saves.load("checkpoint_2", loaded, working.data(), working.size()).error ==
              Error::NotFound);
}

FGL_TEST(esp32_gameplay_save_checks_versions_checksums_paths_capacity_and_migration) {
    using namespace fabgl_project_save;
    auto runtime = saveRuntimeFixture();
    const auto playerGuid = runtime.scene.entities[0].guid;
    Document versionOne;
    FGL_CHECK(captureRuntime(runtime, &playerGuid, versionOne, 1U, 9U));
    FGL_CHECK(versionOne.putPrimitive("chapter", Value::string("one")));
    MemorySaveStorage memory;
    std::vector<std::uint8_t> working(kMaximumFileBytes);
    SaveService oldSaves(memory.callbacks(), "/saves", 1U);
    FGL_CHECK(oldSaves.save("migration", versionOne, working.data(), working.size()));

    SaveService current(memory.callbacks(), "/saves", 2U);
    Document migrated;
    FGL_CHECK(current.load("migration", migrated, working.data(), working.size()).error ==
              Error::MigrationMissing);
    std::uint32_t migrationCount = 0U;
    MigrationCallbacks migrations;
    migrations.context = &migrationCount;
    migrations.step = &migrateSaveV1ToV2;
    LoadInfo info;
    FGL_CHECK(current.load("migration", migrated, working.data(), working.size(), &info,
                           migrations));
    FGL_CHECK(migrationCount == 1U && info.migrated && info.storedSchemaVersion == 1U);
    FGL_CHECK(migrated.schemaVersion == 2U);
    FGL_CHECK(migrated.primitive("migration")->payload.unsignedValue == 2U);

    FGL_CHECK(!oldSaves.save("../escape", versionOne, working.data(), working.size()));
    FGL_CHECK(oldSaves.lastError() == Error::InvalidArgument);
    SaveService relativeRoot(memory.callbacks(), "relative/saves", 1U);
    FGL_CHECK(!relativeRoot.configured());
    FGL_CHECK(relativeRoot.save("slot", versionOne, working.data(), working.size()).error ==
              Error::StorageUnavailable);
    std::uint8_t tiny[64]{};
    FGL_CHECK(encode(versionOne, tiny, sizeof(tiny)).error == Error::CapacityExceeded);
    auto excessiveRuntime = saveRuntimeFixture();
    excessiveRuntime.scene.entityCount = kMaximumSaveEntities + 1U;
    Document excessive;
    FGL_CHECK(captureRuntime(excessiveRuntime, nullptr, excessive, 1U).error ==
              Error::CapacityExceeded);
    memory.files["/saves/oversized.fglsave"] =
        std::vector<std::uint8_t>(kMaximumFileBytes + 1U, 0U);
    FGL_CHECK(oldSaves.load("oversized", excessive, working.data(), working.size()).error ==
              Error::CapacityExceeded);

    FGL_CHECK(oldSaves.save("corrupt", versionOne, working.data(), working.size()));
    auto& corrupt = memory.files["/saves/corrupt.fglsave"];
    corrupt.back() ^= 0x55U;
    Document rejected;
    FGL_CHECK(oldSaves.load("corrupt", rejected, working.data(), working.size()).error ==
              Error::ChecksumMismatch);

    FGL_CHECK(oldSaves.save("format", versionOne, working.data(), working.size()));
    auto& unsupported = memory.files["/saves/format.fglsave"];
    unsupported[4] = 2U;
    FGL_CHECK(oldSaves.load("format", rejected, working.data(), working.size()).error ==
              Error::UnsupportedVersion);

    SaveService olderRuntime(memory.callbacks(), "/saves", 1U);
    Document schemaTwo = versionOne;
    schemaTwo.schemaVersion = 2U;
    SaveService schemaTwoWriter(memory.callbacks(), "/saves", 2U);
    FGL_CHECK(schemaTwoWriter.save("future", schemaTwo, working.data(), working.size()));
    FGL_CHECK(olderRuntime.load("future", rejected, working.data(), working.size()).error ==
              Error::SchemaTooNew);

    auto missingEntityRuntime = saveRuntimeFixture();
    missingEntityRuntime.scene.entityCount = 1U;
    FGL_CHECK(restoreRuntime(versionOne, missingEntityRuntime, &playerGuid).error ==
              Error::EntityMissing);
    auto wrongSceneRuntime = saveRuntimeFixture();
    wrongSceneRuntime.scene.guid = saveTestGuid(220U);
    FGL_CHECK(restoreRuntime(versionOne, wrongSceneRuntime, &playerGuid).error ==
              Error::SceneMismatch);
}

FGL_TEST(esp32_gameplay_save_write_failures_preserve_the_previous_live_slot) {
    using namespace fabgl_project_save;
    auto runtime = saveRuntimeFixture();
    const auto playerGuid = runtime.scene.entities[0].guid;
    Document state;
    FGL_CHECK(captureRuntime(runtime, &playerGuid, state, 1U, 1U));
    FGL_CHECK(state.putPrimitive("revision", Value::unsigned32(1U)));
    MemorySaveStorage memory;
    SaveService saves(memory.callbacks(), "/saves", 1U);
    std::vector<std::uint8_t> working(kMaximumFileBytes);
    FGL_CHECK(saves.save("atomic", state, working.data(), working.size()));

    FGL_CHECK(state.putPrimitive("revision", Value::unsigned32(2U)));
    state.sequence = 2U;
    memory.failStage = true;
    FGL_CHECK(saves.save("atomic", state, working.data(), working.size()).error ==
              Error::IoFailure);
    memory.failStage = false;
    Document loaded;
    FGL_CHECK(saves.load("atomic", loaded, working.data(), working.size()));
    FGL_CHECK(loaded.sequence == 1U);
    FGL_CHECK(loaded.primitive("revision")->payload.unsignedValue == 1U);

    memory.failCommitAfterBackup = true;
    FGL_CHECK(saves.save("atomic", state, working.data(), working.size()).error ==
              Error::CommitFailed);
    memory.failCommitAfterBackup = false;
    FGL_CHECK(saves.load("atomic", loaded, working.data(), working.size()));
    FGL_CHECK(loaded.sequence == 1U);
    FGL_CHECK(loaded.primitive("revision")->payload.unsignedValue == 1U);
    FGL_CHECK(memory.discarded >= 2U);

    // A later successful generation retains revision 1 as .bak. If the new live bytes are
    // corrupted, load validates and returns the complete previous generation explicitly.
    FGL_CHECK(saves.save("atomic", state, working.data(), working.size()));
    memory.files["/saves/atomic.fglsave"].back() ^= 0x80U;
    LoadInfo recovered;
    FGL_CHECK(saves.load("atomic", loaded, working.data(), working.size(), &recovered));
    FGL_CHECK(recovered.recoveredFromBackup);
    FGL_CHECK(loaded.sequence == 1U);
    FGL_CHECK(loaded.primitive("revision")->payload.unsignedValue == 1U);
}

FGL_TEST(esp32_portable_script_runtime_validates_abi_and_runs_bounded_callbacks) {
    fabgl_project_runtime::RuntimeProject project;
    project.scene.entityCount = 1U;
    scriptStarts = 0U;
    scriptUpdates = 0U;
    fabgl_project_scripts::Runtime scripts;
    FGL_CHECK(scripts.initialize(&portableModule, project, true));
    FGL_CHECK(scripts.initialized() && scripts.count() == 1U);
    FGL_CHECK(scriptStarts == 1U && project.scene.entities[0].x == 10.0F);
    scripts.update(project, 0.25F);
    FGL_CHECK(scriptUpdates == 1U);
    FGL_CHECK(project.scene.entities[0].x == 12.0F);
    FGL_CHECK(scripts.updateCount() == 1U);

    fabgl_project_scripts::Runtime optional;
    FGL_CHECK(optional.initialize(nullptr, project, false));
    FGL_CHECK(optional.count() == 0U);
    fabgl_project_scripts::Runtime required;
    FGL_CHECK(!required.initialize(nullptr, project, true));
    FGL_CHECK(required.error() == fabgl_project_scripts::Error::MissingEntryPoint);
}

FGL_TEST(esp32_soak_workload_is_fixed_capacity_and_exercises_every_required_churn_path) {
    fabgl_project_runtime::SoakWorkload soak;
    FGL_CHECK(soak.invariantHolds());

    for (std::size_t iteration = 0U;
         iteration < fabgl_project_runtime::kSoakEntityCapacity * 4U; ++iteration) {
        const auto step = soak.advance(true);
        FGL_CHECK(step.projectSceneActive == soak.projectSceneActive);
        FGL_CHECK(step.assetResident == soak.assetResident);
        FGL_CHECK(step.entityCreated != step.entityDestroyed);
        FGL_CHECK(step.entitySlot < fabgl_project_runtime::kSoakEntityCapacity);
        FGL_CHECK(soak.invariantHolds());
    }

    FGL_CHECK(soak.iterations == fabgl_project_runtime::kSoakEntityCapacity * 4U);
    FGL_CHECK(soak.sceneTransitions == soak.iterations);
    FGL_CHECK(soak.assetLoads == soak.iterations / 2U);
    FGL_CHECK(soak.assetUnloads == soak.iterations / 2U);
    FGL_CHECK(soak.audioPlays == soak.iterations);
    FGL_CHECK(soak.entityCreates == fabgl_project_runtime::kSoakEntityCapacity * 2U);
    FGL_CHECK(soak.entityDestroys == fabgl_project_runtime::kSoakEntityCapacity * 2U);
    FGL_CHECK(soak.liveEntities == 0U);

    fabgl_project_runtime::SoakWorkload noAsset;
    for (std::size_t iteration = 0U; iteration < 8U; ++iteration)
        static_cast<void>(noAsset.advance(false));
    FGL_CHECK(noAsset.assetLoads == 0U && noAsset.assetUnloads == 0U);
    FGL_CHECK(noAsset.invariantHolds());
}

FGL_TEST(esp32_runtime_streams_desktop_serializers_and_updates_script_free_components) {
    const auto spriteGuid = fabgl::AssetGuid::fromStableName("tests.esp32-runtime.sprite");
    const auto raycastGuid = fabgl::AssetGuid::fromStableName("tests.esp32-runtime.raycast");
    const auto trackGuid = fabgl::AssetGuid::fromStableName("tests.esp32-runtime.track");

    fabgl::assets::IndexedImage image;
    image.width = 2;
    image.height = 2;
    image.palette = {{0U, 0U, 0U, 0U}, {255U, 190U, 40U, 255U}};
    image.indices = {1U, 1U, 0U, 1U};
    image.transparentIndex = 0U;
    const auto imageBytes = fabgl::assets::encodeIndexedImage(image);
    FGL_CHECK(!imageBytes.empty());

    fabgl::rendering::RaycastMapAsset raycast;
    raycast.guid = raycastGuid;
    raycast.map = fabgl::rendering::makeDemoRaycastMap();
    auto raycastText = fabgl::rendering::serializeRaycastMapAsset(raycast);
    FGL_CHECK(raycastText);
    auto trackText = fabgl::rendering::serializeRacerTrack(testTrack(trackGuid));
    FGL_CHECK(trackText);

    fabgl::ReflectionRegistry registry;
    FGL_CHECK(fabgl::registerBuiltinComponentTypes(registry));
    fabgl::Scene scene("Embedded Runtime",
                       fabgl::SceneGuid::fromStableName("tests.esp32-runtime.scene"));
    auto character = scene.createEntity(
        "Character", fabgl::EntityGuid::fromStableName("tests.esp32-runtime.character"));
    auto vehicle = scene.createEntity(
        "Vehicle", fabgl::EntityGuid::fromStableName("tests.esp32-runtime.vehicle"));
    auto map = scene.createEntity(
        "Map", fabgl::EntityGuid::fromStableName("tests.esp32-runtime.map"));
    auto camera = scene.createEntity(
        "Camera", fabgl::EntityGuid::fromStableName("tests.esp32-runtime.camera"));
    FGL_CHECK(character && vehicle && map && camera);
    character.value()->transform().setLocalPosition({32.0F, 188.0F, 0.0F});
    auto* sprite = addBuiltin(*character.value(), registry, "SpriteRenderer");
    auto* body = addBuiltin(*character.value(), registry, "CharacterBody2D");
    FGL_CHECK(sprite->set("sprite", spriteGuid));
    FGL_CHECK(body->set("moveSpeed", 20.0));
    auto* vehicleComponent = addBuiltin(*vehicle.value(), registry, "VehicleController");
    FGL_CHECK(vehicleComponent->set("track", trackGuid));
    FGL_CHECK(vehicleComponent->set("acceleration", 12.0));
    auto* mapComponent = addBuiltin(*map.value(), registry, "RaycastMap");
    FGL_CHECK(mapComponent->set("map", raycastGuid));
    auto* firstPerson = addBuiltin(*camera.value(), registry, "FirstPersonController");
    FGL_CHECK(firstPerson->set("moveSpeed", 5.0));
    camera.value()->transform().setLocalPosition({2.5F, 2.5F, 0.0F});
    auto sceneText = fabgl::SceneSerializer::serialize(scene);
    FGL_CHECK(sceneText);

    fabgl::project::Manifest manifest;
    manifest.projectGuid =
        fabgl::AssetGuid::fromStableName("tests.esp32-runtime.project").toString();
    manifest.name = "ESP32 Script-Free Runtime";
    manifest.assets = {{spriteGuid, "Images/Hero.fgli", "image", {}},
                       {raycastGuid, "Maps/Main.fglray", "raycast.map", {}},
                       {trackGuid, "Tracks/Main.fgltrack", "racer.track", {}}};
    fabgl::project::InputContextDefinition context;
    context.name = "gameplay";
    context.priority = 4;
    context.actions = {{"Jump", {{"Key.Space", 1.0F, 0.5F}}},
                       {"Brake", {{"Key.Space", 1.0F, 0.5F}}}};
    context.axes = {{"MoveX", {{"Key.A", -1.0F, 0.5F}, {"Key.D", 1.0F, 0.5F}}},
                    {"MoveY", {{"Key.S", -1.0F, 0.5F}, {"Key.W", 1.0F, 0.5F}}},
                    {"Steer", {{"Key.A", -1.0F, 0.5F}, {"Key.D", 1.0F, 0.5F}}},
                    {"Throttle", {{"Key.W", 1.0F, 0.5F}}},
                    {"LookX", {{"Mouse.X", 0.75F, 0.0F}}}};
    manifest.inputContexts.push_back(std::move(context));
    auto manifestText = fabgl::project::serializeManifest(manifest);
    FGL_CHECK(manifestText);

    std::vector<fabgl::assets::PackInput> inputs;
    inputs.push_back({fabgl::AssetGuid::fromStableName("tests.esp32-runtime.manifest"),
                      fabgl::project::Esp32ManifestPayloadType,
                      fabgl::assets::StorageClass::Flash,
                      {manifestText.value().begin(), manifestText.value().end()}});
    inputs.push_back({fabgl::AssetGuid::fromStableName("tests.esp32-runtime.scene-payload"),
                      fabgl::project::Esp32ScenePayloadType,
                      fabgl::assets::StorageClass::Flash,
                      {sceneText.value().begin(), sceneText.value().end()}});
    inputs.push_back({spriteGuid, fabgl::project::Esp32AssetPayloadType,
                      fabgl::assets::StorageClass::Flash,
                      wrapped("Images/Hero.fgli", imageBytes)});
    inputs.push_back({raycastGuid, fabgl::project::Esp32AssetPayloadType,
                      fabgl::assets::StorageClass::Flash,
                      wrapped("Maps/Main.fglray",
                              {raycastText.value().begin(), raycastText.value().end()})});
    inputs.push_back({trackGuid, fabgl::project::Esp32AssetPayloadType,
                      fabgl::assets::StorageClass::Flash,
                      wrapped("Tracks/Main.fgltrack",
                              {trackText.value().begin(), trackText.value().end()})});
    auto pack = fabgl::assets::buildPack(std::move(inputs));
    FGL_CHECK(pack);
    VectorReader reader{&pack.value().bytes};
    fabgl_project_runtime::RuntimeProject runtime;
    fabgl_project_runtime::Failure failure;
    const auto payloadChecksum = fabgl::assets::checksum64(pack.value().bytes.data(),
                                                           pack.value().bytes.size());
    if (!fabgl_project_runtime::loadProject(
            reader, 3U, payloadChecksum, pack.value().buildChecksum,
            "olimex-esp32-sbc-fabgl-revb", runtime, failure)) {
        throw fabgl::tests::AssertionFailure(
            "ESP32 runtime load failed: code=" +
            std::to_string(static_cast<unsigned int>(failure.code)) +
            " offset=" + std::to_string(failure.offset) + " detail=" + failure.detail);
    }
    FGL_CHECK(runtime.loaded);
    FGL_CHECK(runtime.scene.entityCount == 4U);
    FGL_CHECK(runtime.manifest.assetCount == 3U);
    FGL_CHECK(runtime.manifest.inputBindingCount == 10U);
    fabgl_project_runtime::Guid runtimeRaycast;
    fabgl_project_runtime::Guid runtimeTrack;
    FGL_CHECK(fabgl_project_runtime::detail::parseGuidText(raycastGuid.toString().c_str(),
                                                           runtimeRaycast));
    FGL_CHECK(fabgl_project_runtime::detail::parseGuidText(trackGuid.toString().c_str(),
                                                           runtimeTrack));
    FGL_CHECK(runtime.raycastMap.valid && runtime.raycastMap.guid == runtimeRaycast);
    FGL_CHECK(runtime.racerTrack.valid && runtime.racerTrack.guid == runtimeTrack);
    FGL_CHECK(runtime.racerTrack.segmentCount == 4U);

    fabgl_project_runtime::IndexedImageView imageView;
    fabgl_project_runtime::Failure imageFailure;
    fabgl_project_runtime::Guid runtimeSprite;
    FGL_CHECK(fabgl_project_runtime::detail::parseGuidText(spriteGuid.toString().c_str(),
                                                           runtimeSprite));
    FGL_CHECK(fabgl_project_runtime::inspectIndexedImage(reader, runtime, runtimeSprite, imageView,
                                                         imageFailure));
    FGL_CHECK(imageView.width == 2U && imageView.height == 2U && imageView.pixelCount == 4U);

    const auto characterX = runtime.scene.entities[0].x;
    FGL_CHECK(runtime.setControl("Key.D", 1.0F));
    FGL_CHECK(runtime.setControl("Key.W", 1.0F));
    FGL_CHECK(runtime.setControl("Mouse.X", 0.5F, true));
    runtime.update(0.1F);
    FGL_CHECK(runtime.scene.entities[0].x > characterX);
    FGL_CHECK(runtime.scene.entities[1].vehicleSpeed > 0.0F);
    FGL_CHECK(runtime.scene.entities[3].rotationZ > 0.0F);
    runtime.update(0.1F);
    const auto secondRotation = runtime.scene.entities[3].rotationZ;
    runtime.update(0.1F);
    FGL_CHECK(runtime.scene.entities[3].rotationZ == secondRotation);
}

FGL_TEST(esp32_runtime_rejects_corrupt_pack_before_reading_scene) {
    std::vector<std::uint8_t> bytes(32U, 0U);
    bytes[0] = 'F';
    bytes[1] = 'G';
    bytes[2] = 'L';
    bytes[3] = 'P';
    VectorReader reader{&bytes};
    fabgl_project_runtime::RuntimeProject runtime;
    fabgl_project_runtime::Failure failure;
    FGL_CHECK(!fabgl_project_runtime::loadProject(reader, 0U, 0U, 0U,
                                                  "olimex-esp32-sbc-fabgl-revb", runtime,
                                                  failure));
    FGL_CHECK(failure.code == fabgl_project_runtime::ErrorCode::InvalidPack);
    FGL_CHECK(!runtime.loaded);
}
