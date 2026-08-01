#pragma once

#include "fabgl/core/result.h"

#include <map>
#include <memory>
#include <string>
#include <string_view>
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

struct AnimationTransition final {
    std::string fromState;
    std::string toState;
    std::string booleanParameter;
    bool expectedValue = true;
    float minimumNormalizedTime = 0.0F;
};

struct AnimationFrame final {
    std::string state;
    float localTime = 0.0F;
    bool transitioned = false;
    std::map<std::string, float> values;
    std::vector<std::string> events;
};

class AnimatorController final {
  public:
    [[nodiscard]] Result<void> addState(std::string name,
                                        std::shared_ptr<const AnimationClip> clip);
    [[nodiscard]] Result<void> addTransition(AnimationTransition transition);
    void setBoolean(std::string name, bool value);
    [[nodiscard]] Result<void> play(std::string_view state);
    [[nodiscard]] Result<AnimationFrame> update(float deltaSeconds);

    [[nodiscard]] const std::string& currentState() const noexcept {
        return currentState_;
    }

  private:
    struct State final {
        std::shared_ptr<const AnimationClip> clip;
    };

    std::map<std::string, State> states_;
    std::vector<AnimationTransition> transitions_;
    std::map<std::string, bool> booleans_;
    std::string currentState_;
    float stateTime_ = 0.0F;
};

} // namespace fabgl
