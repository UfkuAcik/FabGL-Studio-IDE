#pragma once

#include "fabgl/core/guid.h"
#include "fabgl/core/result.h"
#include "fabgl/save/save_system.h"

#include <optional>

namespace fabgl {

class Scene;

struct SceneSaveCaptureOptions final {
    std::optional<EntityGuid> playerEntity;
    bool failOnUnsupportedProperties = false;
};

struct SceneSaveRestoreOptions final {
    bool requireSceneIdentity = true;
    bool requireEntities = true;
    bool requireComponents = true;
    bool requireProperties = false;
    bool restoreHierarchy = true;
};

// Captures mutable gameplay state through reflection without using the project/scene
// serializer. Entity and component identities are stable GUIDs, so runtime EntityId
// reuse cannot redirect a save to a different object after a scene reload.
class SceneSaveState final {
  public:
    [[nodiscard]] static Result<SaveDocument>
    capture(const Scene& scene, const SceneSaveCaptureOptions& options = {});

    // Replaces the scene/player/entity sections and preserves caller-owned primitive
    // values, which are intended for game-specific progression flags and counters.
    [[nodiscard]] static Result<void>
    captureInto(const Scene& scene, SaveDocument& document,
                const SceneSaveCaptureOptions& options = {});

    [[nodiscard]] static Result<void>
    restore(Scene& scene, const SaveDocument& document,
            const SceneSaveRestoreOptions& options = {});
};

} // namespace fabgl
