/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "BuffersConfig.h"
#include "CharacterizationTestSupport.h"
#include "DeterministicAudioTestSupport.h"
#include "ServerMixScheduler.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using jammernetz::test::SampleIndex;
using jammernetz::test::ScenarioTrace;
using jammernetz::test::SyntheticAudioSource;

constexpr std::size_t warmupFrames = 16;
constexpr std::size_t recoveryFrames = 64;
constexpr std::size_t coherentRecoveryWindow = 8;

const char* triggerName(const ServerMixTrigger trigger)
{
	switch (trigger) {
	case ServerMixTrigger::None: return "none";
	case ServerMixTrigger::SingleClient: return "single_client";
	case ServerMixTrigger::AllClientsReady: return "all_clients_ready";
	case ServerMixTrigger::MaximumBufferPressure: return "maximum_buffer_pressure";
	case ServerMixTrigger::AllClientsReadyAndMaximumBufferPressure:
		return "all_clients_ready_and_maximum_buffer_pressure";
	}
	return "unknown";
}

JammerNetzChannelSetup monoSetup(const JammerNetzChannelTarget target)
{
	JammerNetzChannelSetup setup(true);
	setup.channels.push_back(JammerNetzSingleChannelSetup(static_cast<uint8>(target)));
	return setup;
}

JammerNetzChannelSetup stereoMixdown()
{
	return JammerNetzChannelSetup(false, {
		JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Left),
		JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Right)
	});
}

std::shared_ptr<JammerNetzAudioData> makePacket(const std::uint32_t sourceId,
	const std::uint64_t frameIndex,
	const JammerNetzChannelSetup& setup)
{
	auto audio = std::make_shared<AudioBuffer<float>>(1, SAMPLE_BUFFER_SIZE);
	for (int sample = 0; sample < SAMPLE_BUFFER_SIZE; ++sample) {
		audio->setSample(0, sample, SyntheticAudioSource::valueAt(sourceId, 0,
			frameIndex * SAMPLE_BUFFER_SIZE + static_cast<std::uint64_t>(sample)));
	}
	const auto timestamp = 1000.0 * static_cast<double>(frameIndex * SAMPLE_BUFFER_SIZE)
		/ static_cast<double>(SAMPLE_RATE);
	return std::make_shared<JammerNetzAudioData>(100 + frameIndex, timestamp, setup,
		SAMPLE_RATE, 0.0f, MidiSignal_None, std::move(audio), nullptr);
}

nlohmann::json queueJson(const std::map<std::string, ServerQueueObservation>& queues)
{
	nlohmann::json result = nlohmann::json::object();
	for (const auto& [name, queue] : queues) {
		result[name] = {
			{ "state", jammernetz::test::connectionStateName(queue.state) },
			{ "size", queue.size },
			{ "activity_generation", queue.activityGeneration }
		};
	}
	return result;
}

struct HoldFlushResult {
	std::size_t holdFrames { 0 };
	bool flushHeldBeforeCurrent { false };
	std::size_t mixCount { 0 };
	std::size_t maximumPressureMixes { 0 };
	std::size_t singleSourceMixes { 0 };
	std::size_t skewedMixes { 0 };
	std::uint64_t maximumSourceSkew { 0 };
	std::size_t maximumQueueA { 0 };
	std::size_t maximumQueueB { 0 };
	std::optional<std::size_t> recoveryMixesAfterFlush;
	std::size_t underrunTransitions { 0 };

	bool operator==(const HoldFlushResult&) const = default;
};

nlohmann::json toJson(const HoldFlushResult& result)
{
	return {
		{ "hold_frames", result.holdFrames },
		{ "flush_order", result.flushHeldBeforeCurrent ? "held_before_current" : "current_before_held" },
		{ "mix_count", result.mixCount },
		{ "maximum_pressure_mixes", result.maximumPressureMixes },
		{ "single_source_mixes", result.singleSourceMixes },
		{ "skewed_mixes", result.skewedMixes },
		{ "maximum_source_skew_frames", result.maximumSourceSkew },
		{ "maximum_queue_a", result.maximumQueueA },
		{ "maximum_queue_b", result.maximumQueueB },
		{ "recovery_mixes_after_flush", result.recoveryMixesAfterFlush
			? nlohmann::json(*result.recoveryMixesAfterFlush) : nlohmann::json(nullptr) },
		{ "underrun_transitions", result.underrunTransitions }
	};
}

class HoldFlushScenario {
public:
	HoldFlushScenario(const std::size_t holdFrames, const bool flushHeldBeforeCurrent)
		: clientA_(std::make_shared<ClientState>("client-a"))
		, clientB_(std::make_shared<ClientState>("client-b"))
		, scheduler_(stereoMixdown(), {
			SERVER_INCOMING_JITTER_BUFFER,
			SERVER_INCOMING_MAXIMUM_BUFFER,
			BUFFER_PREFILL_ON_CONNECT
		})
		, setupA_(monoSetup(JammerNetzChannelTarget::Left))
		, setupB_(monoSetup(JammerNetzChannelTarget::Right))
	{
		result_.holdFrames = holdFrames;
		result_.flushHeldBeforeCurrent = flushHeldBeforeCurrent;
		clients_.emplace("client-a", clientA_);
		clients_.emplace("client-b", clientB_);
	}

	HoldFlushResult run()
	{
		deliver(0, 0, "paced", false);
		deliver(1, 0, "paced", false);
		processWakeups(2);
		for (std::uint64_t frame = 1; frame <= warmupFrames; ++frame) {
			deliver(0, frame, "paced");
			deliver(1, frame, "paced");
		}

		std::vector<std::shared_ptr<JammerNetzAudioData>> held;
		const auto firstHeldFrame = static_cast<std::uint64_t>(warmupFrames + 1);
		for (std::uint64_t offset = 0; offset < result_.holdFrames; ++offset) {
			const auto frame = firstHeldFrame + offset;
			deliver(0, frame, "paced");
			held.push_back(makePacket(2, frame, setupB_));
			recordDelivery("client-b", frame, "held");
		}

		const auto currentFrame = firstHeldFrame + result_.holdFrames;
		if (!result_.flushHeldBeforeCurrent) {
			deliver(0, currentFrame, "paced");
		}
		for (const auto& packet : held) {
			pushPacket(1, packet, "flushed");
		}
		if (result_.flushHeldBeforeCurrent) {
			deliver(0, currentFrame, "paced");
		}
		deliver(1, currentFrame, "paced");
		flushComplete_ = true;
		mixCountAtFlush_ = result_.mixCount;

		for (std::uint64_t offset = 1; offset <= recoveryFrames; ++offset) {
			const auto frame = currentFrame + offset;
			deliver(0, frame, "paced");
			deliver(1, frame, "paced");
		}
		return result_;
	}

	const ScenarioTrace& trace() const noexcept { return trace_; }

private:
	ClientState::TimePoint now() const
	{
		const auto micros = static_cast<std::int64_t>(virtualSample_ * 1000000ULL / SAMPLE_RATE);
		return ClientState::TimePoint{} + std::chrono::microseconds(micros);
	}

	void recordDelivery(const std::string& client, const std::uint64_t frame, const std::string& action)
	{
		trace_.record({ virtualSample_, traceSequence_++, "delivery", {
			{ "client", client }, { "frame", frame }, { "action", action }
		} });
	}

	void deliver(const std::size_t clientIndex, const std::uint64_t frame,
		const std::string& action, const bool process = true)
	{
		const auto& setup = clientIndex == 0 ? setupA_ : setupB_;
		pushPacket(clientIndex, makePacket(static_cast<std::uint32_t>(clientIndex + 1), frame, setup),
			action, process);
		virtualSample_ += SAMPLE_BUFFER_SIZE / 2U;
	}

	void pushPacket(const std::size_t clientIndex,
		const std::shared_ptr<JammerNetzAudioData>& packet,
		const std::string& action,
		const bool process = true)
	{
		auto& client = clientIndex == 0 ? clientA_ : clientB_;
		const auto push = client->push(packet, BUFFER_PREFILL_ON_CONNECT, now());
		recordDelivery(clientIndex == 0 ? "client-a" : "client-b",
			packet->messageCounter() - 100U, action);
		if (!push.queued) {
			throw std::runtime_error("The hold-and-flush transport rejected a generated packet");
		}
		if (process) {
			processWakeups(1);
		}
	}

	void processWakeups(std::size_t count)
	{
		constexpr std::size_t maximumSteps = 4096;
		std::size_t steps = 0;
		while (count > 0) {
			--count;
			if (++steps > maximumSteps) {
				throw std::runtime_error("The scheduler kept requesting wake-ups without settling");
			}
			const auto step = scheduler_.process(clients_, now());
			recordStep(step);
			if (step.shouldWakeAgain) {
				++count;
			}
		}
	}

	void recordStep(const ServerScheduledMixResult& step)
	{
		if (step.mix.outgoing.size() != step.incoming.size()) {
			throw std::runtime_error("The server mixer did not produce one routed packet per input");
		}
		if (!step.mix.diagnostics.empty()) {
			throw std::runtime_error("The characterization input produced an invalid mixer diagnostic");
		}

		for (const auto& [name, queue] : step.queuesBefore) {
			if (name == "client-a") {
				result_.maximumQueueA = std::max(result_.maximumQueueA, queue.size);
			}
			else if (name == "client-b") {
				result_.maximumQueueB = std::max(result_.maximumQueueB, queue.size);
			}
		}
		result_.underrunTransitions += step.underrunClients.size();
		if (step.trigger == ServerMixTrigger::MaximumBufferPressure
			|| step.trigger == ServerMixTrigger::AllClientsReadyAndMaximumBufferPressure) {
			++result_.maximumPressureMixes;
		}

		nlohmann::json selected = nlohmann::json::object();
		for (const auto& [name, packet] : step.incoming) {
			selected[name] = packet->messageCounter();
		}
		trace_.record({ virtualSample_, traceSequence_++, "server_mix_decision", {
			{ "trigger", triggerName(step.trigger) },
			{ "queues_before", queueJson(step.queuesBefore) },
			{ "queues_after", queueJson(step.queuesAfter) },
			{ "selected_counters", selected },
			{ "underrun_clients", step.underrunClients }
		} });

		if (step.incoming.empty()) {
			return;
		}
		++result_.mixCount;
		bool coherent = false;
		if (step.incoming.size() == 1U) {
			++result_.singleSourceMixes;
		}
		else if (step.incoming.size() == 2U) {
			const auto a = step.incoming.at("client-a")->messageCounter();
			const auto b = step.incoming.at("client-b")->messageCounter();
			const auto skew = a > b ? a - b : b - a;
			result_.maximumSourceSkew = std::max(result_.maximumSourceSkew, skew);
			coherent = skew == 0;
			if (!coherent) {
				++result_.skewedMixes;
			}
		}

		if (!flushComplete_ || result_.recoveryMixesAfterFlush) {
			return;
		}
		coherentRun_ = coherent ? coherentRun_ + 1U : 0U;
		if (coherentRun_ >= coherentRecoveryWindow) {
			result_.recoveryMixesAfterFlush = result_.mixCount - mixCountAtFlush_;
		}
	}

	HoldFlushResult result_;
	TPacketStreamBundle clients_;
	std::shared_ptr<ClientState> clientA_;
	std::shared_ptr<ClientState> clientB_;
	ServerMixScheduler scheduler_;
	JammerNetzChannelSetup setupA_;
	JammerNetzChannelSetup setupB_;
	ScenarioTrace trace_;
	SampleIndex virtualSample_ { 0 };
	std::uint64_t traceSequence_ { 0 };
	bool flushComplete_ { false };
	std::size_t mixCountAtFlush_ { 0 };
	std::size_t coherentRun_ { 0 };
};

TEST(NetworkImpairmentCharacterizationTest, HoldAndFlushSweepProducesDeterministicBoundaryReport)
{
	const std::array<std::size_t, 6> holdSizes { 1, 2, 4, 8, 16, 32 };
	nlohmann::json summary {
		{ "scenario", "hold_and_flush" },
		{ "sample_rate", SAMPLE_RATE },
		{ "frame_samples", SAMPLE_BUFFER_SIZE },
		{ "server_jitter_frames", SERVER_INCOMING_JITTER_BUFFER },
		{ "server_maximum_frames", SERVER_INCOMING_MAXIMUM_BUFFER },
		{ "results", nlohmann::json::array() }
	};
	const juce::File artifactDirectory(JAMMERNETZ_TEST_ARTIFACT_DIR);
	const auto scenarioDirectory = artifactDirectory.getChildFile("hold-and-flush");

	for (const auto holdFrames : holdSizes) {
		for (const bool heldBeforeCurrent : { false, true }) {
			HoldFlushScenario firstScenario(holdFrames, heldBeforeCurrent);
			const auto first = firstScenario.run();
			HoldFlushScenario replayScenario(holdFrames, heldBeforeCurrent);
			const auto replay = replayScenario.run();
			EXPECT_EQ(first, replay) << "hold frames=" << holdFrames
				<< ", held before current=" << heldBeforeCurrent;
			EXPECT_GT(first.mixCount, warmupFrames);
			EXPECT_LE(first.maximumQueueA, holdFrames + BUFFER_PREFILL_ON_CONNECT + 2U);
			EXPECT_LE(first.maximumQueueB, holdFrames + BUFFER_PREFILL_ON_CONNECT + 2U);

			auto row = toJson(first);
			summary["results"].push_back(row);
			const auto traceName = "hold-" + std::to_string(holdFrames)
				+ (heldBeforeCurrent ? "-held-first.jsonl" : "-current-first.jsonl");
			firstScenario.trace().writeJsonLines(scenarioDirectory.getChildFile(traceName));
		}
	}
	jammernetz::test::writeJsonArtifact(scenarioDirectory.getChildFile("summary.json"),
		summary, "characterization");
	RecordProperty("characterization_summary", summary.dump());
}

} // namespace
