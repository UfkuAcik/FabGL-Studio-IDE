#pragma once

#include "fabgl/core/result.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fabgl {

enum class ProfilerSampleSource {
    MeasuredPc,
    MeasuredEsp32,
    EstimatedEsp32,
};

enum class ProfilerUnit {
    Milliseconds,
    Bytes,
    Count,
    Percent,
};

struct ProfilerSample final {
    std::uint64_t sequence = 0;
    std::string metric;
    double value = 0.0;
    ProfilerUnit unit = ProfilerUnit::Count;
    ProfilerSampleSource source = ProfilerSampleSource::MeasuredPc;
};

struct ProfilerBudget final {
    std::string metric;
    double maximum = 0.0;
    ProfilerUnit unit = ProfilerUnit::Count;
};

struct ProfilerSummary final {
    std::string metric;
    ProfilerUnit unit = ProfilerUnit::Count;
    ProfilerSampleSource source = ProfilerSampleSource::MeasuredPc;
    std::size_t sampleCount = 0;
    double latest = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    double average = 0.0;
    bool hasBudget = false;
    double budgetMaximum = 0.0;
    bool budgetExceeded = false;
};

struct ProfilerMeasurement final {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return value != 0;
    }
    friend constexpr bool operator==(ProfilerMeasurement lhs, ProfilerMeasurement rhs) noexcept {
        return lhs.value == rhs.value;
    }
    friend constexpr bool operator!=(ProfilerMeasurement lhs, ProfilerMeasurement rhs) noexcept {
        return !(lhs == rhs);
    }
};

struct ProfilerConfig final {
    std::size_t maximumSamples = 256;
    std::size_t maximumBudgets = 32;
    std::size_t maximumActiveMeasurements = 16;
};

class Profiler final {
  public:
    explicit Profiler(ProfilerConfig config = {});

    [[nodiscard]] Result<void> setBudget(std::string metric, double maximum, ProfilerUnit unit);
    [[nodiscard]] bool removeBudget(std::string_view metric) noexcept;
    void clearBudgets() noexcept {
        budgets_.clear();
    }
    [[nodiscard]] const ProfilerBudget* budget(std::string_view metric) const noexcept;
    [[nodiscard]] std::size_t budgetCount() const noexcept {
        return budgets_.size();
    }

    [[nodiscard]] Result<void>
    recordMeasured(std::string metric, double value, ProfilerUnit unit,
                   ProfilerSampleSource source = ProfilerSampleSource::MeasuredPc);
    [[nodiscard]] Result<void> recordEstimated(std::string metric, double value, ProfilerUnit unit);
    [[nodiscard]] Result<void> recordSample(ProfilerSample sample);

    [[nodiscard]] Result<ProfilerMeasurement>
    beginMeasurement(std::string metric,
                     ProfilerSampleSource source = ProfilerSampleSource::MeasuredPc);
    [[nodiscard]] Result<double> endMeasurement(ProfilerMeasurement measurement);
    [[nodiscard]] bool cancelMeasurement(ProfilerMeasurement measurement) noexcept;
    [[nodiscard]] std::size_t activeMeasurementCount() const noexcept {
        return activeMeasurementCount_;
    }

    [[nodiscard]] std::size_t sampleCount() const noexcept {
        return sampleCount_;
    }
    [[nodiscard]] std::size_t sampleCapacity() const noexcept {
        return samples_.size();
    }
    [[nodiscard]] const ProfilerSample* sampleAt(std::size_t chronologicalIndex) const noexcept;
    [[nodiscard]] const ProfilerSample* latestSample(std::string_view metric,
                                                     ProfilerSampleSource source) const noexcept;
    [[nodiscard]] Result<ProfilerSummary> summary(std::string_view metric,
                                                  ProfilerSampleSource source) const;
    void clearSamples() noexcept;

  private:
    using Clock = std::chrono::steady_clock;

    struct ActiveMeasurement final {
        ProfilerMeasurement id{};
        std::string metric;
        ProfilerSampleSource source = ProfilerSampleSource::MeasuredPc;
        Clock::time_point started{};
        bool active = false;
    };

    [[nodiscard]] ProfilerBudget* findBudget(std::string_view metric) noexcept;
    [[nodiscard]] const ProfilerBudget* findBudget(std::string_view metric) const noexcept;
    [[nodiscard]] ProfilerMeasurement allocateMeasurementId() noexcept;

    std::vector<ProfilerSample> samples_;
    std::vector<ProfilerBudget> budgets_;
    std::vector<ActiveMeasurement> activeMeasurements_;
    std::size_t maximumBudgets_ = 0;
    std::size_t sampleHead_ = 0;
    std::size_t sampleCount_ = 0;
    std::size_t activeMeasurementCount_ = 0;
    std::uint64_t nextSampleSequence_ = 0;
    std::uint64_t nextMeasurementId_ = 1;
};

} // namespace fabgl
