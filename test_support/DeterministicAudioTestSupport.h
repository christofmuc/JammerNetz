/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <queue>
#include <string>
#include <vector>

namespace jammernetz::test {

using SampleIndex = std::uint64_t;

class SyntheticAudioSource {
public:
	SyntheticAudioSource(std::uint32_t sourceId, int channelCount, SampleIndex firstSample = 0);

	[[nodiscard]] juce::AudioBuffer<float> render(int sampleCount);
	void renderInto(juce::AudioBuffer<float>& destination, int destinationStartSample, int sampleCount);

	[[nodiscard]] std::uint32_t sourceId() const noexcept;
	[[nodiscard]] int channelCount() const noexcept;
	[[nodiscard]] SampleIndex nextSample() const noexcept;
	[[nodiscard]] static float valueAt(std::uint32_t sourceId, int channel, SampleIndex absoluteSample);

private:
	std::uint32_t sourceId_;
	int channelCount_;
	SampleIndex nextSample_;
};

class CapturedAudio {
public:
	void append(const juce::AudioBuffer<float>& source, int sourceStartSample = 0, int sampleCount = -1);

	[[nodiscard]] int channelCount() const noexcept;
	[[nodiscard]] std::size_t sampleCount() const noexcept;
	[[nodiscard]] float sample(int channel, std::size_t sampleIndex) const;
	[[nodiscard]] const std::vector<std::vector<float>>& channels() const noexcept;

private:
	std::vector<std::vector<float>> channels_;
};

struct DiscrepancySpan {
	int channel{};
	SampleIndex firstSample{};
	SampleIndex lastSample{};
	float expectedAtFirst{};
	float observedAtFirst{};
	float maximumAbsoluteError{};
};

class SignalOracle {
public:
	[[nodiscard]] static std::vector<DiscrepancySpan> compare(
		const CapturedAudio& expected,
		const CapturedAudio& observed,
		float epsilon,
		SampleIndex firstSample = 0);
};

struct TraceEvent {
	SampleIndex virtualSample{};
	std::uint64_t sequence{};
	std::string kind;
	nlohmann::json details = nlohmann::json::object();
};

class ScenarioTrace {
public:
	void record(TraceEvent event);

	[[nodiscard]] const std::vector<TraceEvent>& events() const noexcept;
	[[nodiscard]] std::string toJsonLines() const;
	void writeJsonLines(const juce::File& path) const;

private:
	std::vector<TraceEvent> events_;
};

class ScenarioScheduler {
public:
	using Action = std::function<void()>;

	explicit ScenarioScheduler(std::uint64_t seed);

	std::uint64_t scheduleAt(SampleIndex virtualSample,
		std::string kind,
		Action action,
		nlohmann::json details = nlohmann::json::object());
	bool runNext();
	void runUntilIdle(std::size_t maximumEvents = 100000);

	[[nodiscard]] SampleIndex now() const noexcept;
	[[nodiscard]] std::uint64_t seed() const noexcept;
	[[nodiscard]] std::uint64_t nextRandom() noexcept;
	[[nodiscard]] bool empty() const noexcept;
	[[nodiscard]] const ScenarioTrace& trace() const noexcept;

private:
	struct ScheduledEvent {
		SampleIndex virtualSample{};
		std::uint64_t sequence{};
		std::string kind;
		Action action;
		nlohmann::json details;
	};

	struct LaterEvent {
		bool operator()(const ScheduledEvent& left, const ScheduledEvent& right) const noexcept;
	};

	std::uint64_t seed_;
	std::uint64_t randomState_;
	SampleIndex now_{};
	std::uint64_t nextSequence_{};
	std::priority_queue<ScheduledEvent, std::vector<ScheduledEvent>, LaterEvent> events_;
	ScenarioTrace trace_;
};

} // namespace jammernetz::test
