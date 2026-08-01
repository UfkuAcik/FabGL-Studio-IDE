#include "fabgl/profiling/profiler.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace fabgl {

Profiler::Profiler(ProfilerConfig config)
    : samples_(config.maximumSamples), activeMeasurements_(config.maximumActiveMeasurements),
      maximumBudgets_(config.maximumBudgets) {
    budgets_.reserve(config.maximumBudgets);
}

Result<void> Profiler::setBudget(std::string metric, double maximum, ProfilerUnit unit) {
    if (metric.empty()) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "profiler budget metric cannot be empty"));
    }
    if (!std::isfinite(maximum)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "profiler budget must be finite"));
    }
    for (std::size_t index = 0; index < sampleCount_; ++index) {
        const auto* sample = sampleAt(index);
        if (sample != nullptr && sample->metric == metric && sample->unit != unit) {
            return Result<void>::failure(Error(
                ErrorCode::TypeMismatch, "profiler budget unit differs from existing samples"));
        }
    }

    auto* existing = findBudget(metric);
    if (existing != nullptr) {
        existing->maximum = maximum;
        existing->unit = unit;
        return Result<void>::success();
    }
    if (budgets_.size() >= maximumBudgets_) {
        return Result<void>::failure(
            Error(ErrorCode::CapacityExceeded, "profiler budget capacity has been reached"));
    }
    budgets_.push_back({std::move(metric), maximum, unit});
    return Result<void>::success();
}

bool Profiler::removeBudget(std::string_view metric) noexcept {
    const auto iterator =
        std::find_if(budgets_.begin(), budgets_.end(), [metric](const ProfilerBudget& candidate) {
            return candidate.metric == metric;
        });
    if (iterator == budgets_.end()) {
        return false;
    }
    budgets_.erase(iterator);
    return true;
}

const ProfilerBudget* Profiler::budget(std::string_view metric) const noexcept {
    return findBudget(metric);
}

Result<void> Profiler::recordMeasured(std::string metric, double value, ProfilerUnit unit,
                                      ProfilerSampleSource source) {
    if (source == ProfilerSampleSource::EstimatedEsp32) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "estimated samples cannot be recorded as measured"));
    }
    return recordSample({0, std::move(metric), value, unit, source});
}

Result<void> Profiler::recordEstimated(std::string metric, double value, ProfilerUnit unit) {
    return recordSample({0, std::move(metric), value, unit, ProfilerSampleSource::EstimatedEsp32});
}

Result<void> Profiler::recordSample(ProfilerSample sample) {
    if (sample.metric.empty()) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "profiler sample metric cannot be empty"));
    }
    if (!std::isfinite(sample.value)) {
        return Result<void>::failure(
            Error(ErrorCode::InvalidArgument, "profiler sample value must be finite"));
    }
    if (samples_.empty()) {
        return Result<void>::failure(
            Error(ErrorCode::CapacityExceeded, "profiler sample capacity is zero"));
    }
    const auto* metricBudget = findBudget(sample.metric);
    if (metricBudget != nullptr && metricBudget->unit != sample.unit) {
        return Result<void>::failure(
            Error(ErrorCode::TypeMismatch, "profiler sample unit differs from its budget"));
    }
    for (std::size_t index = 0; index < sampleCount_; ++index) {
        const auto* existing = sampleAt(index);
        if (existing != nullptr && existing->metric == sample.metric &&
            existing->unit != sample.unit) {
            return Result<void>::failure(
                Error(ErrorCode::TypeMismatch, "profiler metric changed units"));
        }
    }

    sample.sequence = ++nextSampleSequence_;
    std::size_t writeIndex = 0;
    if (sampleCount_ < samples_.size()) {
        writeIndex = (sampleHead_ + sampleCount_) % samples_.size();
        ++sampleCount_;
    } else {
        writeIndex = sampleHead_;
        sampleHead_ = (sampleHead_ + 1U) % samples_.size();
    }
    samples_[writeIndex] = std::move(sample);
    return Result<void>::success();
}

Result<ProfilerMeasurement> Profiler::beginMeasurement(std::string metric,
                                                       ProfilerSampleSource source) {
    if (metric.empty()) {
        return Result<ProfilerMeasurement>::failure(
            Error(ErrorCode::InvalidArgument, "profiler measurement metric cannot be empty"));
    }
    if (source == ProfilerSampleSource::EstimatedEsp32) {
        return Result<ProfilerMeasurement>::failure(
            Error(ErrorCode::InvalidArgument, "timed measurements cannot be estimated"));
    }

    for (auto& measurement : activeMeasurements_) {
        if (!measurement.active) {
            measurement.id = allocateMeasurementId();
            measurement.metric = std::move(metric);
            measurement.source = source;
            measurement.started = Clock::now();
            measurement.active = true;
            ++activeMeasurementCount_;
            return Result<ProfilerMeasurement>::success(measurement.id);
        }
    }
    return Result<ProfilerMeasurement>::failure(Error(
        ErrorCode::CapacityExceeded, "active profiler measurement capacity has been reached"));
}

Result<double> Profiler::endMeasurement(ProfilerMeasurement measurement) {
    if (!measurement.valid()) {
        return Result<double>::failure(
            Error(ErrorCode::InvalidArgument, "profiler measurement ID is invalid"));
    }
    for (auto& active : activeMeasurements_) {
        if (active.active && active.id == measurement) {
            const auto ended = Clock::now();
            const auto duration = std::chrono::duration<double, std::milli>(ended - active.started);
            const double elapsedMilliseconds = duration.count();
            auto metric = std::move(active.metric);
            const auto source = active.source;
            active.active = false;
            active.id = {};
            --activeMeasurementCount_;
            auto recorded = recordMeasured(std::move(metric), elapsedMilliseconds,
                                           ProfilerUnit::Milliseconds, source);
            if (!recorded) {
                return Result<double>::failure(recorded.error());
            }
            return Result<double>::success(elapsedMilliseconds);
        }
    }
    return Result<double>::failure(
        Error(ErrorCode::NotFound, "active profiler measurement was not found"));
}

bool Profiler::cancelMeasurement(ProfilerMeasurement measurement) noexcept {
    if (!measurement.valid()) {
        return false;
    }
    for (auto& active : activeMeasurements_) {
        if (active.active && active.id == measurement) {
            active.active = false;
            active.id = {};
            active.metric.clear();
            --activeMeasurementCount_;
            return true;
        }
    }
    return false;
}

const ProfilerSample* Profiler::sampleAt(std::size_t chronologicalIndex) const noexcept {
    if (chronologicalIndex >= sampleCount_ || samples_.empty()) {
        return nullptr;
    }
    const auto index = (sampleHead_ + chronologicalIndex) % samples_.size();
    return &samples_[index];
}

const ProfilerSample* Profiler::latestSample(std::string_view metric,
                                             ProfilerSampleSource source) const noexcept {
    for (std::size_t offset = sampleCount_; offset > 0; --offset) {
        const auto* sample = sampleAt(offset - 1U);
        if (sample != nullptr && sample->metric == metric && sample->source == source) {
            return sample;
        }
    }
    return nullptr;
}

Result<ProfilerSummary> Profiler::summary(std::string_view metric,
                                          ProfilerSampleSource source) const {
    ProfilerSummary result;
    result.metric = std::string(metric);
    result.source = source;
    double sum = 0.0;

    for (std::size_t index = 0; index < sampleCount_; ++index) {
        const auto* sample = sampleAt(index);
        if (sample == nullptr || sample->metric != metric || sample->source != source) {
            continue;
        }
        if (result.sampleCount == 0) {
            result.unit = sample->unit;
            result.minimum = sample->value;
            result.maximum = sample->value;
        } else {
            result.minimum = std::min(result.minimum, sample->value);
            result.maximum = std::max(result.maximum, sample->value);
        }
        result.latest = sample->value;
        sum += sample->value;
        ++result.sampleCount;
    }
    if (result.sampleCount == 0) {
        return Result<ProfilerSummary>::failure(
            Error(ErrorCode::NotFound, "profiler metric has no samples for this source"));
    }

    result.average = sum / static_cast<double>(result.sampleCount);
    const auto* metricBudget = findBudget(metric);
    if (metricBudget != nullptr && metricBudget->unit == result.unit) {
        result.hasBudget = true;
        result.budgetMaximum = metricBudget->maximum;
        result.budgetExceeded = result.maximum > metricBudget->maximum;
    }
    return Result<ProfilerSummary>::success(std::move(result));
}

void Profiler::clearSamples() noexcept {
    sampleHead_ = 0;
    sampleCount_ = 0;
}

ProfilerBudget* Profiler::findBudget(std::string_view metric) noexcept {
    for (auto& candidate : budgets_) {
        if (candidate.metric == metric) {
            return &candidate;
        }
    }
    return nullptr;
}

const ProfilerBudget* Profiler::findBudget(std::string_view metric) const noexcept {
    for (const auto& candidate : budgets_) {
        if (candidate.metric == metric) {
            return &candidate;
        }
    }
    return nullptr;
}

ProfilerMeasurement Profiler::allocateMeasurementId() noexcept {
    for (;;) {
        const ProfilerMeasurement candidate{nextMeasurementId_++};
        if (nextMeasurementId_ == 0) {
            nextMeasurementId_ = 1;
        }
        bool alreadyActive = false;
        for (const auto& measurement : activeMeasurements_) {
            if (measurement.active && measurement.id == candidate) {
                alreadyActive = true;
                break;
            }
        }
        if (candidate.valid() && !alreadyActive) {
            return candidate;
        }
    }
}

} // namespace fabgl
