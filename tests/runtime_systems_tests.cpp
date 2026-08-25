#include "test_harness.h"

#include "fabgl/audio/audio_mixer.h"
#include "fabgl/navigation/grid_navigation.h"
#include "fabgl/particles/particle_system.h"
#include "fabgl/profiling/profiler.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

using namespace fabgl;

namespace {

class CapturingAudioBackend final : public IAudioOutputBackend {
  public:
    Result<void> submitStereo(const float* interleavedSamples, std::size_t frameCount,
                              std::uint32_t sampleRate) override {
        samples.assign(interleavedSamples, interleavedSamples + frameCount * 2U);
        frames = frameCount;
        rate = sampleRate;
        return Result<void>::success();
    }

    std::vector<float> samples;
    std::size_t frames = 0;
    std::uint32_t rate = 0;
};

AudioClipView monoClip(const float* samples, std::size_t frames, std::uint32_t sampleRate) {
    return {samples, frames, 1, sampleRate};
}

struct StreamingFixture final {
    const float* samples = nullptr;
    std::size_t frames = 0U;
    std::size_t reads = 0U;
};

std::size_t readStreamingFixture(const void* context, const std::size_t firstFrame, float* output,
                                 const std::size_t frameCount) noexcept {
    auto* fixture = static_cast<StreamingFixture*>(const_cast<void*>(context));
    if (fixture == nullptr || output == nullptr || firstFrame >= fixture->frames) {
        return 0U;
    }
    ++fixture->reads;
    const auto count = std::min(frameCount, fixture->frames - firstFrame);
    std::copy_n(fixture->samples + firstFrame, count, output);
    return count;
}

} // namespace

FGL_TEST(audio_mixer_applies_bus_voice_pan_volume_and_backend_clipping) {
    AudioMixer mixer({4, 2, 2, 4});
    const AudioBusId effects{1};
    FGL_CHECK(mixer.createBus(effects, {0.5F, -1.0F, false}));

    const std::array<float, 4> mono = {1.0F, 1.0F, 1.0F, 1.0F};
    AudioVoiceSettings settings;
    settings.bus = effects;
    settings.volume = 0.5F;
    auto voice = mixer.play(monoClip(mono.data(), mono.size(), 4), settings);
    FGL_CHECK(voice);

    std::array<float, 2> output{};
    FGL_CHECK(mixer.mixTo(output.data(), 1));
    FGL_CHECK_NEAR(output[0], 0.25F, 0.0001F);
    FGL_CHECK_NEAR(output[1], 0.0F, 0.0001F);

    FGL_CHECK(mixer.setBusMuted(effects, true));
    FGL_CHECK(mixer.mixTo(output.data(), 1));
    FGL_CHECK_NEAR(output[0], 0.0F, 0.0001F);
    FGL_CHECK_NEAR(output[1], 0.0F, 0.0001F);
    FGL_CHECK(mixer.stop(voice.value()));

    const std::array<float, 2> loudStereo = {2.0F, -2.0F};
    auto loudVoice = mixer.play({loudStereo.data(), 1, 2, 4});
    FGL_CHECK(loudVoice);
    CapturingAudioBackend backend;
    mixer.setOutputBackend(&backend);
    FGL_CHECK(mixer.render(1));
    FGL_CHECK(backend.frames == 1);
    FGL_CHECK(backend.rate == 4);
    FGL_CHECK(backend.samples.size() == 2);
    FGL_CHECK_NEAR(backend.samples[0], 1.0F, 0.0001F);
    FGL_CHECK_NEAR(backend.samples[1], -1.0F, 0.0001F);
    auto oversized = mixer.render(5);
    FGL_CHECK(!oversized);
    FGL_CHECK(oversized.error().code() == ErrorCode::CapacityExceeded);
}

FGL_TEST(audio_mixer_resamples_pitch_and_steals_only_lower_priority_voices) {
    AudioMixer pitchMixer({4, 1, 1, 4});
    const std::array<float, 3> ramp = {0.0F, 1.0F, 0.0F};
    AudioVoiceSettings pitched;
    pitched.pitch = 0.5F;
    auto pitchedVoice = pitchMixer.play(monoClip(ramp.data(), ramp.size(), 4), pitched);
    FGL_CHECK(pitchedVoice);
    std::array<float, 6> pitchedOutput{};
    FGL_CHECK(pitchMixer.mixTo(pitchedOutput.data(), 3));
    FGL_CHECK_NEAR(pitchedOutput[0], 0.0F, 0.0001F);
    FGL_CHECK_NEAR(pitchedOutput[2], 0.5F, 0.0001F);
    FGL_CHECK_NEAR(pitchedOutput[4], 1.0F, 0.0001F);

    AudioMixer mixer({4, 2, 1, 4});
    const std::array<float, 2> sample = {0.25F, 0.25F};
    AudioVoiceSettings low;
    low.loop = true;
    low.priority = 1;
    AudioVoiceSettings medium = low;
    medium.priority = 2;
    auto lowVoice = mixer.play(monoClip(sample.data(), sample.size(), 4), low);
    auto mediumVoice = mixer.play(monoClip(sample.data(), sample.size(), 4), medium);
    FGL_CHECK(lowVoice && mediumVoice);

    AudioVoiceSettings rejected = low;
    rejected.priority = 0;
    auto rejectedVoice = mixer.play(monoClip(sample.data(), sample.size(), 4), rejected);
    FGL_CHECK(!rejectedVoice);
    FGL_CHECK(rejectedVoice.error().code() == ErrorCode::CapacityExceeded);

    AudioVoiceSettings high = low;
    high.priority = 3;
    auto highVoice = mixer.play(monoClip(sample.data(), sample.size(), 4), high);
    FGL_CHECK(highVoice);
    FGL_CHECK(!mixer.isPlaying(lowVoice.value()));
    FGL_CHECK(mixer.isPlaying(mediumVoice.value()));
    FGL_CHECK(mixer.isPlaying(highVoice.value()));
    const auto stats = mixer.stats();
    FGL_CHECK(stats.activeVoices == 2);
    FGL_CHECK(stats.voicesStarted == 3);
    FGL_CHECK(stats.voicesStolen == 1);
    FGL_CHECK(stats.voicesRejected == 1);
}

FGL_TEST(audio_mixer_rejects_invalid_clips_buses_and_voice_parameters) {
    AudioMixer mixer({48'000, 1, 1, 8});
    FGL_CHECK(!mixer.play({}));
    const float sample = 0.0F;
    FGL_CHECK(!mixer.play({&sample, 1, 3, 48'000}));
    FGL_CHECK(!mixer.setBusPan(MasterAudioBus, 1.1F));
    FGL_CHECK(!mixer.setBusVolume(MasterAudioBus, -1.0F));
    FGL_CHECK(!mixer.createBus(AudioBusId{1}));
    auto noBackend = mixer.render(1);
    FGL_CHECK(!noBackend);
    FGL_CHECK(noBackend.error().code() == ErrorCode::InvalidState);
}

FGL_TEST(audio_mixer_reads_streaming_clips_through_a_bounded_per_voice_cache) {
    std::array<float, 260U> samples{};
    for (std::size_t index = 0U; index < samples.size(); ++index) {
        samples[index] = static_cast<float>(index % 17U) / 16.0F;
    }
    StreamingFixture fixture{samples.data(), samples.size(), 0U};
    AudioClipView clip;
    clip.frameCount = samples.size();
    clip.channelCount = 1U;
    clip.sampleRate = 4U;
    clip.readerContext = &fixture;
    clip.frameReader = &readStreamingFixture;

    AudioMixer mixer({4U, 1U, 1U, 256U});
    FGL_CHECK(mixer.play(clip));
    std::array<float, 400U> output{};
    FGL_CHECK(mixer.mixTo(output.data(), 200U));
    FGL_CHECK_NEAR(output[0U], samples[0U], 0.0001F);
    FGL_CHECK_NEAR(output[2U * 129U], samples[129U], 0.0001F);
    const auto stats = mixer.stats();
    FGL_CHECK(fixture.reads == 2U);
    FGL_CHECK(stats.streamCacheRefills == 2U);
    FGL_CHECK(stats.streamedFrames == 256U);
    FGL_CHECK(stats.streamUnderruns == 0U);
}

FGL_TEST(particle_system_is_bounded_and_reuses_slots_without_reviving_stale_handles) {
    ParticleSystem particles(2);
    ParticleSpawn first;
    first.position = {1.0F, 2.0F};
    auto firstHandle = particles.spawn(first);
    auto secondHandle = particles.spawn({});
    FGL_CHECK(firstHandle && secondHandle);
    FGL_CHECK(firstHandle.value().index == 0);
    FGL_CHECK(secondHandle.value().index == 1);
    FGL_CHECK(particles.activeCount() == 2);

    auto overflow = particles.spawn({});
    FGL_CHECK(!overflow);
    FGL_CHECK(overflow.error().code() == ErrorCode::CapacityExceeded);
    FGL_CHECK(particles.destroy(firstHandle.value()));
    FGL_CHECK(!particles.isAlive(firstHandle.value()));

    auto reused = particles.spawn({});
    FGL_CHECK(reused);
    FGL_CHECK(reused.value().index == firstHandle.value().index);
    FGL_CHECK(reused.value().generation != firstHandle.value().generation);
    FGL_CHECK(particles.get(firstHandle.value()) == nullptr);
    FGL_CHECK(particles.handleAtSlot(reused.value().index) == reused.value());
    FGL_CHECK(particles.particleAtSlot(reused.value().index) != nullptr);

    const auto stats = particles.stats();
    FGL_CHECK(stats.capacity == 2);
    FGL_CHECK(stats.activeParticles == 2);
    FGL_CHECK(stats.totalSpawned == 3);
    FGL_CHECK(stats.totalDestroyed == 1);
    FGL_CHECK(stats.rejectedSpawns == 1);
}

FGL_TEST(particle_system_integrates_motion_expires_and_clears_deterministically) {
    ParticleSystem particles(2);
    ParticleSpawn spawn;
    spawn.velocity = {2.0F, 0.0F};
    spawn.acceleration = {2.0F, 4.0F};
    spawn.lifetimeSeconds = 1.0F;
    auto handle = particles.spawn(spawn);
    FGL_CHECK(handle);
    FGL_CHECK(particles.update(0.5F));
    const auto* particle = particles.get(handle.value());
    FGL_CHECK(particle != nullptr);
    FGL_CHECK_NEAR(particle->position.x, 1.25F, 0.0001F);
    FGL_CHECK_NEAR(particle->position.y, 0.5F, 0.0001F);
    FGL_CHECK_NEAR(particle->velocity.x, 3.0F, 0.0001F);
    FGL_CHECK_NEAR(particle->velocity.y, 2.0F, 0.0001F);
    FGL_CHECK_NEAR(particle->normalizedAge(), 0.5F, 0.0001F);

    FGL_CHECK(particles.update(0.5F));
    FGL_CHECK(!particles.isAlive(handle.value()));
    FGL_CHECK(particles.stats().totalExpired == 1);
    FGL_CHECK(!particles.update(-0.1F));

    auto another = particles.spawn({});
    FGL_CHECK(another);
    particles.clear();
    FGL_CHECK(particles.activeCount() == 0);
    FGL_CHECK(!particles.isAlive(another.value()));
}

FGL_TEST(grid_navigation_finds_deterministic_four_neighbor_paths) {
    GridNavigation navigation(3, 3);
    auto path = navigation.findPath({0, 0}, {2, 2});
    FGL_CHECK(path);
    const std::vector<GridPosition> expected = {
        {0, 0}, {1, 0}, {2, 0}, {2, 1}, {2, 2},
    };
    FGL_CHECK(path.value() == expected);

    auto sameCell = navigation.findPath({1, 1}, {1, 1});
    FGL_CHECK(sameCell);
    FGL_CHECK(sameCell.value().size() == 1);
    FGL_CHECK(sameCell.value()[0] == GridPosition{1, 1});
}

FGL_TEST(grid_navigation_obeys_obstacles_costs_and_reports_unreachable_paths) {
    GridNavigation weighted(3, 2);
    FGL_CHECK(weighted.setTraversalCost({1, 0}, 10));
    auto path = weighted.findPath({0, 0}, {2, 0});
    FGL_CHECK(path);
    const std::vector<GridPosition> expected = {
        {0, 0}, {0, 1}, {1, 1}, {2, 1}, {2, 0},
    };
    FGL_CHECK(path.value() == expected);

    GridNavigation blocked(3, 3);
    FGL_CHECK(blocked.setBlocked({1, 0}, true));
    FGL_CHECK(blocked.setBlocked({1, 1}, true));
    FGL_CHECK(blocked.setBlocked({1, 2}, true));
    auto noPath = blocked.findPath({0, 1}, {2, 1});
    FGL_CHECK(!noPath);
    FGL_CHECK(noPath.error().code() == ErrorCode::NotFound);
    FGL_CHECK(!blocked.setTraversalCost({0, 0}, 0));
    FGL_CHECK(!blocked.setWalkable({-1, 0}, true));
    blocked.clearObstacles();
    FGL_CHECK(blocked.findPath({0, 1}, {2, 1}));
}

FGL_TEST(profiler_keeps_measured_and_estimated_samples_distinct_and_checks_budgets) {
    Profiler profiler({8, 2, 2});
    FGL_CHECK(profiler.setBudget("frame", 10.0, ProfilerUnit::Milliseconds));
    FGL_CHECK(profiler.recordMeasured("frame", 8.0, ProfilerUnit::Milliseconds));
    FGL_CHECK(profiler.recordMeasured("frame", 12.0, ProfilerUnit::Milliseconds));
    FGL_CHECK(profiler.recordEstimated("frame", 7.0, ProfilerUnit::Milliseconds));

    auto measured = profiler.summary("frame", ProfilerSampleSource::MeasuredPc);
    FGL_CHECK(measured);
    FGL_CHECK(measured.value().sampleCount == 2);
    FGL_CHECK_NEAR(measured.value().minimum, 8.0F, 0.0001F);
    FGL_CHECK_NEAR(measured.value().maximum, 12.0F, 0.0001F);
    FGL_CHECK_NEAR(measured.value().average, 10.0F, 0.0001F);
    FGL_CHECK(measured.value().hasBudget);
    FGL_CHECK(measured.value().budgetExceeded);

    auto estimated = profiler.summary("frame", ProfilerSampleSource::EstimatedEsp32);
    FGL_CHECK(estimated);
    FGL_CHECK(estimated.value().sampleCount == 1);
    FGL_CHECK(!estimated.value().budgetExceeded);
    FGL_CHECK(profiler.latestSample("frame", ProfilerSampleSource::MeasuredPc)->value == 12.0);

    auto wrongUnit = profiler.recordMeasured("frame", 1.0, ProfilerUnit::Count);
    FGL_CHECK(!wrongUnit);
    FGL_CHECK(wrongUnit.error().code() == ErrorCode::TypeMismatch);
    FGL_CHECK(!profiler.recordMeasured("frame", 1.0, ProfilerUnit::Milliseconds,
                                       ProfilerSampleSource::EstimatedEsp32));
}

FGL_TEST(profiler_history_is_bounded_and_timed_measurements_are_validated) {
    Profiler ring({2, 1, 1});
    FGL_CHECK(ring.recordMeasured("counter", 1.0, ProfilerUnit::Count));
    FGL_CHECK(ring.recordMeasured("counter", 2.0, ProfilerUnit::Count));
    FGL_CHECK(ring.recordMeasured("counter", 3.0, ProfilerUnit::Count));
    FGL_CHECK(ring.sampleCount() == 2);
    FGL_CHECK(ring.sampleAt(0) != nullptr && ring.sampleAt(0)->value == 2.0);
    FGL_CHECK(ring.sampleAt(1) != nullptr && ring.sampleAt(1)->value == 3.0);
    FGL_CHECK(ring.sampleAt(2) == nullptr);

    auto measurement = ring.beginMeasurement("update");
    FGL_CHECK(measurement);
    auto capacityFailure = ring.beginMeasurement("nested");
    FGL_CHECK(!capacityFailure);
    FGL_CHECK(capacityFailure.error().code() == ErrorCode::CapacityExceeded);
    auto elapsed = ring.endMeasurement(measurement.value());
    FGL_CHECK(elapsed);
    FGL_CHECK(elapsed.value() >= 0.0);
    const auto* timed = ring.latestSample("update", ProfilerSampleSource::MeasuredPc);
    FGL_CHECK(timed != nullptr);
    FGL_CHECK(timed->unit == ProfilerUnit::Milliseconds);
    FGL_CHECK(!ring.endMeasurement(measurement.value()));

    auto cancelled = ring.beginMeasurement("cancelled", ProfilerSampleSource::MeasuredEsp32);
    FGL_CHECK(cancelled);
    FGL_CHECK(ring.cancelMeasurement(cancelled.value()));
    FGL_CHECK(!ring.cancelMeasurement(cancelled.value()));
    ring.clearSamples();
    FGL_CHECK(ring.sampleCount() == 0);

    Profiler noStorage({0, 0, 0});
    auto noCapacity = noStorage.recordEstimated("estimate", 1.0, ProfilerUnit::Count);
    FGL_CHECK(!noCapacity);
    FGL_CHECK(noCapacity.error().code() == ErrorCode::CapacityExceeded);
    FGL_CHECK(!noStorage.setBudget("count", 1.0, ProfilerUnit::Count));
}
