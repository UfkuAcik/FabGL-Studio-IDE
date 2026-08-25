#pragma once

#include "fabgl/core/result.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fabgl {

enum class CurveInterpolation {
    Step,
    Linear,
    CubicHermite,
};

struct AnimationKey final {
    float time = 0.0F;
    float value = 0.0F;
    float inTangent = 0.0F;
    float outTangent = 0.0F;
    CurveInterpolation interpolation = CurveInterpolation::Linear;
};

class AnimationCurve final {
  public:
    [[nodiscard]] Result<void> addKey(AnimationKey key);
    [[nodiscard]] float sample(float time) const noexcept;
    [[nodiscard]] const std::vector<AnimationKey>& keys() const noexcept {
        return keys_;
    }

  private:
    std::vector<AnimationKey> keys_;
};

struct AnimationEvent final {
    float time = 0.0F;
    std::string name;
};

class AnimationClip final {
  public:
    AnimationClip(std::string name, float durationSeconds, bool looping = false);

    [[nodiscard]] const std::string& name() const noexcept {
        return name_;
    }
    [[nodiscard]] float duration() const noexcept {
        return durationSeconds_;
    }
    [[nodiscard]] bool looping() const noexcept {
        return looping_;
    }
    [[nodiscard]] bool valid() const noexcept;

    [[nodiscard]] Result<void> addTrack(std::string propertyPath, AnimationCurve curve);
    [[nodiscard]] Result<void> addEvent(AnimationEvent event);
    [[nodiscard]] std::map<std::string, float> sample(float time) const;
    [[nodiscard]] std::vector<std::string> eventsCrossed(float previousTime,
                                                         float currentTime) const;

  private:
    [[nodiscard]] float localTime(float time) const noexcept;

    std::string name_;
    float durationSeconds_ = 0.0F;
    bool looping_ = false;
    std::map<std::string, AnimationCurve> tracks_;
    std::vector<AnimationEvent> events_;
};

enum class AnimationConditionMode {
    BooleanEquals,
    IntegerEquals,
    IntegerNotEquals,
    IntegerGreater,
    IntegerLess,
    FloatGreater,
    FloatLess,
    TriggerSet,
};

struct AnimationCondition final {
    std::string parameter;
    AnimationConditionMode mode = AnimationConditionMode::BooleanEquals;
    bool booleanValue = true;
    std::int64_t integerValue = 0;
    float floatValue = 0.0F;
};

struct AnimationTransition final {
    AnimationTransition() = default;
    AnimationTransition(std::string from, std::string to, std::string booleanCondition,
                        bool booleanExpected, float normalizedTime)
        : fromState(std::move(from)), toState(std::move(to)),
          booleanParameter(std::move(booleanCondition)), expectedValue(booleanExpected),
          minimumNormalizedTime(normalizedTime) {}

    std::string fromState;
    std::string toState;
    // Legacy boolean condition kept for source compatibility. More expressive
    // transitions should use conditions below.
    std::string booleanParameter;
    bool expectedValue = true;
    float minimumNormalizedTime = 0.0F;
    std::vector<AnimationCondition> conditions;
    bool hasExitTime = false;
    float exitTime = 0.0F;
    float blendDurationSeconds = 0.0F;
};

struct AnimationFrame final {
    std::string state;
    float localTime = 0.0F;
    bool transitioned = false;
    float blendWeight = 1.0F;
    std::map<std::string, float> values;
    std::vector<std::string> events;
};

class AnimatorController final {
  public:
    [[nodiscard]] Result<void> addState(std::string name,
                                        std::shared_ptr<const AnimationClip> clip);
    [[nodiscard]] Result<void> addTransition(AnimationTransition transition);
    void setBoolean(std::string name, bool value);
    void setInteger(std::string name, std::int64_t value);
    void setFloat(std::string name, float value);
    void setTrigger(std::string name);
    void resetTrigger(std::string_view name);
    [[nodiscard]] Result<void> play(std::string_view state);
    [[nodiscard]] Result<AnimationFrame> update(float deltaSeconds);

    [[nodiscard]] const std::string& currentState() const noexcept {
        return currentState_;
    }

  private:
    struct State final {
        std::shared_ptr<const AnimationClip> clip;
    };

    struct ActiveBlend final {
        std::shared_ptr<const AnimationClip> source;
        float sourceTime = 0.0F;
        float elapsed = 0.0F;
        float duration = 0.0F;
    };

    [[nodiscard]] bool conditionSatisfied(const AnimationCondition& condition) const noexcept;
    void consumeTriggers(const AnimationTransition& transition);

    std::map<std::string, State> states_;
    std::vector<AnimationTransition> transitions_;
    std::map<std::string, bool> booleans_;
    std::map<std::string, std::int64_t> integers_;
    std::map<std::string, float> floats_;
    std::set<std::string> triggers_;
    std::string currentState_;
    float stateTime_ = 0.0F;
    std::optional<ActiveBlend> activeBlend_;
};

} // namespace fabgl
