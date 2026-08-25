#include <fabgl/project/project_asset_streaming.h>

#include <fabgl/reflection/reflection.h>
#include <fabgl/scene/entity.h>
#include <fabgl/scene/scene.h>

#include <project_format.h>

#include <algorithm>
#include <map>
#include <set>
#include <typeindex>
#include <utility>

namespace fabgl::project {
namespace {

[[nodiscard]] const ProjectAssetEntry* findAsset(const Manifest& manifest,
                                                  const AssetGuid guid) noexcept {
    const auto found = std::find_if(manifest.assets.begin(), manifest.assets.end(),
                                    [guid](const auto& asset) { return asset.guid == guid; });
    return found == manifest.assets.end() ? nullptr : &*found;
}

[[nodiscard]] Result<void> appendSceneAsset(const Manifest& manifest, const AssetGuid asset,
                                            std::vector<AssetGuid>& roots) {
    if (asset.isNil())
        return Result<void>::success();
    const auto* entry = findAsset(manifest, asset);
    if (entry == nullptr) {
        return Result<void>::failure(
            Error(ErrorCode::NotFound, "scene references an asset absent from the manifest")
                .addContext("asset", asset.toString()));
    }
    if (ProjectAssetLibrary::supportsRuntimeType(entry->type))
        roots.push_back(asset);
    return Result<void>::success();
}

template <typename T>
[[nodiscard]] std::shared_ptr<const T> resolvePresentationAsset(
    AssetStreamingManager& manager, const AssetGuid guid,
    const std::function<std::shared_ptr<const T>(const ProjectAssetLibrary&)>& resolver);

} // namespace

struct ProjectAssetStreamingRuntime::State final {
    State(std::string rootValue, Manifest manifestValue, ProjectAssetStreamingConfig configValue,
          AssetStreamingManager managerValue)
        : projectRoot(std::move(rootValue)), manifest(std::move(manifestValue)),
          config(std::move(configValue)), manager(std::move(managerValue)) {}

    std::string projectRoot;
    Manifest manifest;
    ProjectAssetStreamingConfig config;
    AssetStreamingManager manager;
};

namespace {

template <typename T>
[[nodiscard]] std::shared_ptr<const T> resolvePresentationAsset(
    AssetStreamingManager& manager, const AssetGuid guid,
    const std::function<std::shared_ptr<const T>(const ProjectAssetLibrary&)>& resolver) {
    auto library = manager.get<ProjectAssetLibrary>(guid);
    return library ? resolver(*library) : std::shared_ptr<const T>{};
}

} // namespace

Result<ProjectAssetStreamingRuntime>
ProjectAssetStreamingRuntime::create(std::string projectRoot, Manifest manifest,
                                     const ProjectAssetStreamingConfig& config) {
    if (projectRoot.empty() || config.loadsPerUpdate == 0U) {
        return Result<ProjectAssetStreamingRuntime>::failure(
            Error(ErrorCode::InvalidArgument, "project asset streaming configuration is invalid"));
    }
    const auto callbackRoot = projectRoot;
    const auto callbackManifest = manifest;
    const auto decoding = config.decoding;
    AssetStreamingCallbacks callbacks;
    callbacks.dependencies = [callbackRoot, callbackManifest,
                              decoding](const AssetGuid asset) {
        return ProjectAssetLibrary::directDependencies(callbackRoot, callbackManifest, asset,
                                                       decoding);
    };
    callbacks.load = [callbackRoot, callbackManifest, decoding](
                         const AssetGuid asset, const AssetResourceLookup&) {
        auto loaded = ProjectAssetLibrary::loadSelected(callbackRoot, callbackManifest, {asset},
                                                        decoding);
        if (!loaded)
            return Result<AssetLoadPayload>::failure(loaded.error());
        const auto residentBytes =
            std::max<std::size_t>(loaded.value().stats().estimatedResidentBytes, 1U);
        return Result<AssetLoadPayload>::success(AssetLoadPayload::make(
            std::make_shared<ProjectAssetLibrary>(std::move(loaded.value())), residentBytes));
    };
    auto manager = AssetStreamingManager::create(std::move(callbacks), config.residency);
    if (!manager)
        return Result<ProjectAssetStreamingRuntime>::failure(manager.error());
    auto state = std::make_shared<State>(std::move(projectRoot), std::move(manifest), config,
                                         std::move(manager.value()));
    return Result<ProjectAssetStreamingRuntime>::success(
        ProjectAssetStreamingRuntime(std::move(state)));
}

Result<std::vector<AssetGuid>>
ProjectAssetStreamingRuntime::collectSceneRoots(const Scene& scene, const Manifest& manifest) {
    std::vector<AssetGuid> roots;
    for (const auto* entity : scene.entities()) {
        for (const auto* component : entity->components()) {
            const auto* metadata = component->metadata();
            if (metadata == nullptr)
                continue;
            for (const auto& property : metadata->properties) {
                if (property.type != PropertyType::AssetReference &&
                    !(property.type == PropertyType::List &&
                      property.listElementType == PropertyType::AssetReference)) {
                    continue;
                }
                auto value = property.read(component);
                if (!value) {
                    return Result<std::vector<AssetGuid>>::failure(
                        value.error()
                            .withContext("entity", entity->id().toString())
                            .withContext("component", std::string(component->typeName()))
                            .withContext("property", property.name));
                }
                if (const auto* asset = std::get_if<AssetGuid>(&value.value())) {
                    auto appended = appendSceneAsset(manifest, *asset, roots);
                    if (!appended)
                        return Result<std::vector<AssetGuid>>::failure(appended.error());
                } else if (const auto* list = std::get_if<PropertyList>(&value.value())) {
                    for (const auto& element : list->values) {
                        const auto* listAsset = std::get_if<AssetGuid>(&element);
                        if (listAsset == nullptr)
                            continue;
                        auto appended = appendSceneAsset(manifest, *listAsset, roots);
                        if (!appended)
                            return Result<std::vector<AssetGuid>>::failure(appended.error());
                    }
                }
            }
        }
    }
    std::sort(roots.begin(), roots.end());
    roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
    return Result<std::vector<AssetGuid>>::success(std::move(roots));
}

Result<void> ProjectAssetStreamingRuntime::beginTransition(std::vector<AssetGuid> roots) {
    return state_->manager.beginTransition(std::move(roots));
}

Result<void>
ProjectAssetStreamingRuntime::loadTransitionBlocking(std::vector<AssetGuid> roots) {
    auto begun = beginTransition(std::move(roots));
    if (!begun)
        return begun;
    while (state_->manager.transitionState() == AssetTransitionState::Loading) {
        auto pumped = state_->manager.pump(state_->config.loadsPerUpdate);
        if (!pumped) {
            state_->manager.cancelTransition();
            return Result<void>::failure(pumped.error());
        }
        if (pumped.value() == 0U &&
            state_->manager.transitionState() == AssetTransitionState::Loading) {
            state_->manager.cancelTransition();
            return Result<void>::failure(
                Error(ErrorCode::InvalidState, "asset transition made no loading progress"));
        }
    }
    auto committed = state_->manager.commitTransition();
    if (!committed)
        state_->manager.cancelTransition();
    return committed;
}

Result<std::size_t> ProjectAssetStreamingRuntime::update() {
    auto pumped = state_->manager.pump(state_->config.loadsPerUpdate);
    if (!pumped)
        return pumped;
    if (state_->manager.transitionState() == AssetTransitionState::Ready) {
        auto committed = state_->manager.commitTransition();
        if (!committed)
            return Result<std::size_t>::failure(committed.error());
    }
    return pumped;
}

std::size_t ProjectAssetStreamingRuntime::evictUnused() {
    return state_->manager.evictUnused();
}

rendering::ScenePresentationResources ProjectAssetStreamingRuntime::resources() const {
    rendering::ScenePresentationResources result;
    const auto state = state_;
    result.sprite = [state](const AssetGuid guid) {
        return resolvePresentationAsset<rendering::Sprite>(
            state->manager, guid, [guid](const ProjectAssetLibrary& library) {
                return library.resources().sprite(guid);
            });
    };
    result.material = [state](const AssetGuid guid) {
        return resolvePresentationAsset<Material>(
            state->manager, guid, [guid](const ProjectAssetLibrary& library) {
                const auto resources = library.resources();
                return resources.material ? resources.material(guid)
                                          : std::shared_ptr<const Material>{};
            });
    };
    result.tilemap = [state](const AssetGuid guid) {
        return resolvePresentationAsset<rendering::Tilemap>(
            state->manager, guid, [guid](const ProjectAssetLibrary& library) {
                return library.resources().tilemap(guid);
            });
    };
    result.mesh = [state](const AssetGuid guid) {
        return resolvePresentationAsset<rendering::LowPolyMesh>(
            state->manager, guid, [guid](const ProjectAssetLibrary& library) {
                return library.resources().mesh(guid);
            });
    };
    result.racerTrack = [state](const AssetGuid guid) {
        return resolvePresentationAsset<rendering::RacerTrackAsset>(
            state->manager, guid, [guid](const ProjectAssetLibrary& library) {
                return library.resources().racerTrack(guid);
            });
    };
    result.raycastMap = [state](const AssetGuid guid) {
        return resolvePresentationAsset<rendering::RaycastMap>(
            state->manager, guid, [guid](const ProjectAssetLibrary& library) {
                return library.resources().raycastMap(guid);
            });
    };
    return result;
}

Result<std::unique_ptr<AnimatorController>>
ProjectAssetStreamingRuntime::createAnimator(const AssetGuid controller) const {
    auto library = state_->manager.get<ProjectAssetLibrary>(controller);
    if (!library) {
        return Result<std::unique_ptr<AnimatorController>>::failure(
            Error(ErrorCode::NotFound, "animator controller is not resident")
                .addContext("controller", controller.toString()));
    }
    return library->createAnimator(controller);
}

std::shared_ptr<const ProjectAudioClip>
ProjectAssetStreamingRuntime::audioClip(const AssetGuid guid) const noexcept {
    auto library = state_->manager.get<ProjectAssetLibrary>(guid);
    return library ? library->audioClip(guid) : std::shared_ptr<const ProjectAudioClip>{};
}

AssetTransitionState ProjectAssetStreamingRuntime::transitionState() const noexcept {
    return state_->manager.transitionState();
}

std::vector<AssetGuid> ProjectAssetStreamingRuntime::activeRoots() const {
    return state_->manager.activeTransitionRoots();
}

std::vector<AssetGuid> ProjectAssetStreamingRuntime::pendingRoots() const {
    return state_->manager.pendingTransitionRoots();
}

AssetStreamingStats ProjectAssetStreamingRuntime::stats() const noexcept {
    return state_->manager.stats();
}

std::size_t ProjectAssetStreamingRuntime::residentAssetCount(const std::string_view type) const {
    return static_cast<std::size_t>(std::count_if(
        state_->manifest.assets.begin(), state_->manifest.assets.end(),
        [this, type](const ProjectAssetEntry& entry) {
            if (!type.empty() && entry.type != type)
                return false;
            const auto status = state_->manager.status(entry.guid);
            return status && status->state == AssetStreamState::Resident;
        }));
}

std::vector<AssetStreamingDiagnostic> ProjectAssetStreamingRuntime::diagnostics() const {
    return state_->manager.diagnostics();
}

} // namespace fabgl::project
