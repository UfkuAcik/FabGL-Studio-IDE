#pragma once

#include "fabgl/animation/animation.h"
#include "fabgl/core/guid.h"
#include "fabgl/core/result.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fabgl {

struct AnimationClipAsset final {
    AssetGuid guid;
    std::string name;
    float durationSeconds = 1.0F;
    bool looping = false;
    std::map<std::string, AnimationCurve> tracks;
    std::vector<AnimationEvent> events;
};

enum class AnimatorParameterType : std::uint8_t {
    Boolean,
    Integer,
    Float,
    Trigger,
};

struct AnimatorParameterDefinition final {
    AnimatorParameterType type = AnimatorParameterType::Boolean;
    bool booleanDefault = false;
    std::int64_t integerDefault = 0;
    float floatDefault = 0.0F;
};

struct AnimatorStateDefinition final {
    AssetGuid clip;
};

struct AnimatorTransitionDefinition final {
    std::string fromState;
    std::string toState;
    float minimumNormalizedTime = 0.0F;
    bool hasExitTime = false;
    float exitTime = 0.0F;
    float blendDurationSeconds = 0.0F;
    std::vector<AnimationCondition> conditions;
};

struct AnimatorControllerAsset final {
    AssetGuid guid;
    std::string name;
    std::string initialState;
    std::map<std::string, AnimatorParameterDefinition> parameters;
    std::map<std::string, AnimatorStateDefinition> states;
    // Transition order is runtime priority and is therefore preserved by the file format.
    std::vector<AnimatorTransitionDefinition> transitions;
};

struct AnimationClipFormatLimits final {
    std::size_t maximumSourceBytes = 4U * 1024U * 1024U;
    std::size_t maximumTracks = 256U;
    std::size_t maximumKeys = 8192U;
    std::size_t maximumEvents = 1024U;
    std::size_t maximumStringBytes = 1024U;
};

struct AnimatorControllerFormatLimits final {
    std::size_t maximumSourceBytes = 4U * 1024U * 1024U;
    std::size_t maximumParameters = 256U;
    std::size_t maximumStates = 256U;
    std::size_t maximumTransitions = 1024U;
    std::size_t maximumConditions = 4096U;
    std::size_t maximumStringBytes = 1024U;
};

[[nodiscard]] Result<void>
validateAnimationClipAsset(const AnimationClipAsset& asset,
                           const AnimationClipFormatLimits& limits = {});
[[nodiscard]] Result<void>
validateAnimatorControllerAsset(const AnimatorControllerAsset& asset,
                                const AnimatorControllerFormatLimits& limits = {});

[[nodiscard]] Result<std::string>
serializeAnimationClipAsset(const AnimationClipAsset& asset,
                            const AnimationClipFormatLimits& limits = {});
[[nodiscard]] Result<AnimationClipAsset>
deserializeAnimationClipAsset(std::string_view text,
                              const AnimationClipFormatLimits& limits = {});

[[nodiscard]] Result<std::string>
serializeAnimatorControllerAsset(const AnimatorControllerAsset& asset,
                                 const AnimatorControllerFormatLimits& limits = {});
[[nodiscard]] Result<AnimatorControllerAsset>
deserializeAnimatorControllerAsset(std::string_view text,
                                   const AnimatorControllerFormatLimits& limits = {});

using AnimationClipResolver =
    std::function<Result<std::shared_ptr<const AnimationClip>>(AssetGuid clip)>;

[[nodiscard]] Result<std::shared_ptr<const AnimationClip>>
buildAnimationClip(const AnimationClipAsset& asset,
                   const AnimationClipFormatLimits& limits = {});
[[nodiscard]] Result<std::unique_ptr<AnimatorController>>
buildAnimatorController(const AnimatorControllerAsset& asset,
                        const AnimationClipResolver& clipResolver,
                        const AnimatorControllerFormatLimits& limits = {});

} // namespace fabgl
