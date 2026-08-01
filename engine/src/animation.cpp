#include "fabgl/animation/animation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace fabgl {

Result<void> AnimationCurve::addKey(AnimationKey key) {
    if (!std::isfinite(key.time) || !std::isfinite(key.value) || !std::isfinite(key.inTangent) ||
        !std::isfinite(key.outTangent) || key.time < 0.0F) {
        return Result<void>::failure(Error(ErrorCode::InvalidArgument, "animation key is invalid"));
    }
    const auto iterator = std::lower_bound(
        keys_.begin(), keys_.end(), key.time,
        [](const AnimationKey& existing, float time) { return existing.time < time; });
    if (iterator != keys_.end() && iterator->time == key.time) {
        *iterator = key;
    } else {
        keys_.insert(iterator, key);
    }
    return Result<void>::success();
}

float AnimationCurve::sample(float time) const noexcept {
    if (keys_.empty())
        return 0.0F;
    if (!std::isfinite(time) || time <= keys_.front().time)
        return keys_.front().value;
    if (time >= keys_.back().time)
        return keys_.back().value;
    const auto upper = std::upper_bound(
        keys_.begin(), keys_.end(), time,
        [](float sampleTime, const AnimationKey& key) { return sampleTime < key.time; });
    const auto& right = *upper;
    const auto& left = *(upper - 1);
    if (left.interpolation == CurveInterpolation::Step)
        return left.value;
    const auto duration = right.time - left.time;
    const auto normalized = (time - left.time) / duration;
    if (left.interpolation == CurveInterpolation::Linear) {
        return left.value + (right.value - left.value) * normalized;
    }
    const auto squared = normalized * normalized;
    const auto cubed = squared * normalized;
    const auto h00 = 2.0F * cubed - 3.0F * squared + 1.0F;
    const auto h10 = cubed - 2.0F * squared + normalized;
    const auto h01 = -2.0F * cubed + 3.0F * squared;
    const auto h11 = cubed - squared;
    return h00 * left.value + h10 * duration * left.outTangent + h01 * right.value +
           h11 * duration * right.inTangent;
}

AnimationClip::AnimationClip(std::string name, float durationSeconds, bool looping)
    : name_(std::move(name)), durationSeconds_(durationSeconds), looping_(looping) {}

bool AnimationClip::valid() const noexcept {
    return !name_.empty() && std::isfinite(durationSeconds_) && durationSeconds_ > 0.0F;
}

Result<void> AnimationClip::addTrack(std::string propertyPath, AnimationCurve curve) {
    if (!valid() || propertyPath.empty() || curve.keys().empty()) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "animation track is invalid"));
    }
    if (curve.keys().back().time > durationSeconds_) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "animation key exceeds clip duration"));
    }
    if (tracks_.find(propertyPath) != tracks_.end()) {
        return Result<void>::failure(
            Error(ErrorCode::AlreadyExists, "animation property track already exists")
                .addContext("property", propertyPath));
    }
    tracks_.emplace(std::move(propertyPath), std::move(curve));
    return Result<void>::success();
}

Result<void> AnimationClip::addEvent(AnimationEvent event) {
    if (!valid() || event.name.empty() || !std::isfinite(event.time) || event.time < 0.0F ||
        event.time > durationSeconds_) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "animation event is invalid"));
    }
    const auto iterator =
        std::lower_bound(events_.begin(), events_.end(), event,
                         [](const AnimationEvent& lhs, const AnimationEvent& rhs) {
                             if (lhs.time != rhs.time)
                                 return lhs.time < rhs.time;
                             return lhs.name < rhs.name;
                         });
    events_.insert(iterator, std::move(event));
    return Result<void>::success();
}

float AnimationClip::localTime(float time) const noexcept {
    if (!valid() || !std::isfinite(time))
        return 0.0F;
    if (!looping_)
        return std::clamp(time, 0.0F, durationSeconds_);
    auto wrapped = std::fmod(std::max(time, 0.0F), durationSeconds_);
    if (wrapped < 0.0F)
        wrapped += durationSeconds_;
    return wrapped;
}

std::map<std::string, float> AnimationClip::sample(float time) const {
    std::map<std::string, float> values;
    const auto sampleTime = localTime(time);
    for (const auto& track : tracks_)
        values.emplace(track.first, track.second.sample(sampleTime));
    return values;
}

std::vector<std::string> AnimationClip::eventsCrossed(float previousTime, float currentTime) const {
    std::vector<std::string> crossed;
    if (!valid() || !std::isfinite(previousTime) || !std::isfinite(currentTime) ||
        currentTime < previousTime) {
        return crossed;
    }
    if (!looping_) {
        const auto end = std::min(currentTime, durationSeconds_);
        for (const auto& event : events_) {
            if (event.time > previousTime && event.time <= end)
                crossed.push_back(event.name);
        }
        return crossed;
    }

    std::vector<std::pair<float, std::string>> occurrences;
    for (const auto& event : events_) {
        auto cycle =
            static_cast<long long>(std::floor((previousTime - event.time) / durationSeconds_)) +
            1LL;
        auto occurrence = event.time + static_cast<float>(cycle) * durationSeconds_;
        std::size_t guard = 0;
        while (occurrence <= currentTime && guard < 1024U) {
            if (occurrence >= 0.0F)
                occurrences.push_back({occurrence, event.name});
            ++cycle;
            occurrence = event.time + static_cast<float>(cycle) * durationSeconds_;
            ++guard;
        }
    }
    std::sort(occurrences.begin(), occurrences.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.first != rhs.first)
            return lhs.first < rhs.first;
        return lhs.second < rhs.second;
    });
    for (const auto& occurrence : occurrences)
        crossed.push_back(occurrence.second);
    return crossed;
}

Result<void> AnimatorController::addState(std::string name,
                                          std::shared_ptr<const AnimationClip> clip) {
    if (name.empty() || !clip || !clip->valid()) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "animator state is invalid"));
    }
    if (states_.find(name) != states_.end()) {
        return Result<void>::failure(
            Error(ErrorCode::AlreadyExists, "animator state already exists")
                .addContext("state", name));
    }
    states_.emplace(std::move(name), State{std::move(clip)});
    return Result<void>::success();
}

Result<void> AnimatorController::addTransition(AnimationTransition transition) {
    if (states_.find(transition.fromState) == states_.end() ||
        states_.find(transition.toState) == states_.end() || transition.booleanParameter.empty() ||
        !std::isfinite(transition.minimumNormalizedTime) ||
        transition.minimumNormalizedTime < 0.0F) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "animator transition is invalid"));
    }
    transitions_.push_back(std::move(transition));
    return Result<void>::success();
}

void AnimatorController::setBoolean(std::string name, bool value) {
    booleans_[std::move(name)] = value;
}

Result<void> AnimatorController::play(std::string_view state) {
    const auto iterator = states_.find(std::string(state));
    if (iterator == states_.end()) {
        return Result<void>::failure(Error(ErrorCode::NotFound, "animator state was not found")
                                         .addContext("state", std::string(state)));
    }
    currentState_ = iterator->first;
    stateTime_ = 0.0F;
    return Result<void>::success();
}

Result<AnimationFrame> AnimatorController::update(float deltaSeconds) {
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0F) {
        return Result<AnimationFrame>::failure(
            Error(ErrorCode::InvalidArgument, "animation delta is invalid"));
    }
    const auto current = states_.find(currentState_);
    if (current == states_.end()) {
        return Result<AnimationFrame>::failure(
            Error(ErrorCode::InvalidState, "animator has no current state"));
    }

    const auto previousTime = stateTime_;
    stateTime_ += deltaSeconds;
    AnimationFrame frame;
    frame.state = currentState_;
    frame.events = current->second.clip->eventsCrossed(previousTime, stateTime_);

    const auto local = current->second.clip->looping()
                           ? std::fmod(stateTime_, current->second.clip->duration())
                           : std::min(stateTime_, current->second.clip->duration());
    const auto normalized = local / current->second.clip->duration();
    for (const auto& transition : transitions_) {
        if (transition.fromState != currentState_ || normalized < transition.minimumNormalizedTime)
            continue;
        const auto parameter = booleans_.find(transition.booleanParameter);
        const bool value = parameter != booleans_.end() && parameter->second;
        if (value != transition.expectedValue)
            continue;
        currentState_ = transition.toState;
        stateTime_ = 0.0F;
        frame.transitioned = true;
        frame.state = currentState_;
        break;
    }

    const auto finalState = states_.find(currentState_);
    frame.localTime = finalState->second.clip->looping()
                          ? std::fmod(stateTime_, finalState->second.clip->duration())
                          : std::min(stateTime_, finalState->second.clip->duration());
    frame.values = finalState->second.clip->sample(stateTime_);
    return Result<AnimationFrame>::success(std::move(frame));
}

} // namespace fabgl
