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
#include <tuple>
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
			const auto a = static_cast<std::uint64_t>(
				step.incoming.at("client-a")->messageCounter());
			const auto b = static_cast<std::uint64_t>(
				step.incoming.at("client-b")->messageCounter());
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

struct ImpairmentProfile {
	ImpairmentProfile(std::string profileFamily, std::string profileName)
		: family(std::move(profileFamily)), name(std::move(profileName))
	{
	}

	std::string family;
	std::string name;
	std::uint64_t seed { 0x4a616d6d65724e65ULL };
	std::size_t fixedDelayFrames { 0 };
	std::size_t jitterFrames { 0 };
	std::size_t dropEveryFrames { 0 };
	std::size_t dropBurstFrames { 1 };
	std::size_t duplicateEveryFrames { 0 };
	std::size_t duplicateCopies { 1 };
	std::size_t reorderEveryFrames { 0 };
	std::size_t reorderDelayFrames { 0 };
	std::size_t holdEveryFrames { 0 };
	std::size_t holdFrames { 0 };
};

struct ImpairmentRunResult {
	std::string profile;
	std::size_t generatedPackets { 0 };
	std::size_t primaryPacketsDelivered { 0 };
	std::size_t injectedDrops { 0 };
	std::size_t injectedDuplicates { 0 };
	std::size_t delayedPackets { 0 };
	std::size_t rejectedPackets { 0 };
	std::size_t mixCount { 0 };
	std::size_t maximumPressureMixes { 0 };
	std::size_t singleSourceMixes { 0 };
	std::size_t skewedMixes { 0 };
	std::uint64_t maximumSourceSkew { 0 };
	std::size_t maximumQueueA { 0 };
	std::size_t maximumQueueB { 0 };
	std::size_t underrunTransitions { 0 };
	std::optional<std::size_t> recoveryMixes;
	ClientConnectionState finalStateA { ClientConnectionState::Disconnected };
	ClientConnectionState finalStateB { ClientConnectionState::Disconnected };
	std::uint64_t queueDrops { 0 };
	std::uint64_t queueDuplicates { 0 };
	std::uint64_t queueOutOfOrder { 0 };
	std::uint64_t queueTooLateOrDuplicate { 0 };
	std::uint64_t queueDropsHealed { 0 };
	std::uint64_t queueMaximumGap { 0 };
	std::uint64_t queueMaximumReorderSpan { 0 };

	[[nodiscard]] bool serverRecovered() const noexcept
	{
		return finalStateA == ClientConnectionState::Connected
			&& finalStateB == ClientConnectionState::Connected
			&& recoveryMixes.has_value();
	}

	bool operator==(const ImpairmentRunResult&) const = default;
};

nlohmann::json profileJson(const ImpairmentProfile& profile)
{
	return {
		{ "family", profile.family },
		{ "name", profile.name },
		{ "seed", profile.seed },
		{ "fixed_delay_frames", profile.fixedDelayFrames },
		{ "jitter_frames", profile.jitterFrames },
		{ "drop_every_frames", profile.dropEveryFrames },
		{ "drop_burst_frames", profile.dropBurstFrames },
		{ "duplicate_every_frames", profile.duplicateEveryFrames },
		{ "duplicate_copies", profile.duplicateCopies },
		{ "reorder_every_frames", profile.reorderEveryFrames },
		{ "reorder_delay_frames", profile.reorderDelayFrames },
		{ "hold_every_frames", profile.holdEveryFrames },
		{ "hold_frames", profile.holdFrames }
	};
}

nlohmann::json resultJson(const ImpairmentRunResult& result)
{
	return {
		{ "profile", result.profile },
		{ "server_recovered", result.serverRecovered() },
		{ "generated_packets", result.generatedPackets },
		{ "primary_packets_delivered", result.primaryPacketsDelivered },
		{ "injected_drops", result.injectedDrops },
		{ "injected_duplicates", result.injectedDuplicates },
		{ "delayed_packets", result.delayedPackets },
		{ "rejected_packets", result.rejectedPackets },
		{ "mix_count", result.mixCount },
		{ "maximum_pressure_mixes", result.maximumPressureMixes },
		{ "single_source_mixes", result.singleSourceMixes },
		{ "skewed_mixes", result.skewedMixes },
		{ "maximum_source_skew_frames", result.maximumSourceSkew },
		{ "maximum_queue_a", result.maximumQueueA },
		{ "maximum_queue_b", result.maximumQueueB },
		{ "underrun_transitions", result.underrunTransitions },
		{ "recovery_mixes", result.recoveryMixes
			? nlohmann::json(*result.recoveryMixes) : nlohmann::json(nullptr) },
		{ "final_state_a", jammernetz::test::connectionStateName(result.finalStateA) },
		{ "final_state_b", jammernetz::test::connectionStateName(result.finalStateB) },
		{ "queue_drops", result.queueDrops },
		{ "queue_duplicates", result.queueDuplicates },
		{ "queue_out_of_order", result.queueOutOfOrder },
		{ "queue_too_late_or_duplicate", result.queueTooLateOrDuplicate },
		{ "queue_drops_healed", result.queueDropsHealed },
		{ "queue_maximum_gap", result.queueMaximumGap },
		{ "queue_maximum_reorder_span", result.queueMaximumReorderSpan }
	};
}

class ProgressiveImpairmentScenario {
public:
	explicit ProgressiveImpairmentScenario(ImpairmentProfile profile)
		: profile_(std::move(profile))
		, clientA_(std::make_shared<ClientState>("client-a"))
		, clientB_(std::make_shared<ClientState>("client-b"))
		, scheduler_(stereoMixdown(), {
			SERVER_INCOMING_JITTER_BUFFER,
			SERVER_INCOMING_MAXIMUM_BUFFER,
			BUFFER_PREFILL_ON_CONNECT
		})
		, setupA_(monoSetup(JammerNetzChannelTarget::Left))
		, setupB_(monoSetup(JammerNetzChannelTarget::Right))
		, events_(profile_.seed)
	{
		result_.profile = profile_.name;
		clients_.emplace("client-a", clientA_);
		clients_.emplace("client-b", clientB_);
	}

	ImpairmentRunResult run()
	{
		constexpr std::size_t impairmentFrames = 96;
		const std::size_t maximumDelayFrames = profile_.fixedDelayFrames
			+ profile_.jitterFrames + profile_.reorderDelayFrames + profile_.holdFrames;
		const std::uint64_t firstImpairedFrame = warmupFrames + 1U;
		const std::uint64_t firstRecoveryFrame = firstImpairedFrame + impairmentFrames;
		const std::uint64_t finalFrame = firstRecoveryFrame + recoveryFrames
			+ maximumDelayFrames;
		recoveryEligibleSample_ = (firstRecoveryFrame + maximumDelayFrames + 1U)
			* static_cast<std::uint64_t>(SAMPLE_BUFFER_SIZE);

		for (std::uint64_t frame = 0; frame <= finalFrame; ++frame) {
			scheduleDelivery(0, frame, 0, "paced", false);
			++result_.generatedPackets;
			const bool impaired = frame >= firstImpairedFrame && frame < firstRecoveryFrame;
			if (!impaired) {
				scheduleDelivery(1, frame, 0, "paced", false);
				continue;
			}

			const std::size_t relativeFrame = static_cast<std::size_t>(frame - firstImpairedFrame);
			if (shouldDrop(relativeFrame)) {
				scheduleDrop(frame);
				continue;
			}

			const std::size_t delayFrames = deliveryDelay(relativeFrame);
			const std::string action = delayFrames == 0 ? "paced" : "delayed";
			scheduleDelivery(1, frame, delayFrames, action, false);
			if (profile_.duplicateEveryFrames > 0
				&& relativeFrame % profile_.duplicateEveryFrames == 0) {
				for (std::size_t copy = 0; copy < profile_.duplicateCopies; ++copy) {
					scheduleDelivery(1, frame, delayFrames, "duplicated", true, copy + 1U);
				}
			}
		}

		events_.runUntilIdle();
		const auto snapshotA = clientA_->snapshot();
		const auto snapshotB = clientB_->snapshot();
		result_.finalStateA = snapshotA.state;
		result_.finalStateB = snapshotB.state;
		JammerNetzStreamQualityInfo quality;
		if (clientB_->qualityInfo(quality)) {
			result_.queueDrops = static_cast<std::uint64_t>(std::max<std::int64_t>(0, quality.droppedPacketCounter));
			result_.queueDuplicates = static_cast<std::uint64_t>(std::max<std::int64_t>(0, quality.duplicatePacketCounter));
			result_.queueOutOfOrder = static_cast<std::uint64_t>(std::max<std::int64_t>(0, quality.outOfOrderPacketCounter));
			result_.queueTooLateOrDuplicate = quality.tooLateOrDuplicate;
			result_.queueDropsHealed = quality.dropsHealed;
			result_.queueMaximumGap = quality.maxLengthOfGap;
			result_.queueMaximumReorderSpan = quality.maxWrongOrderSpan;
		}
		return result_;
	}

	[[nodiscard]] const ScenarioTrace& trace() const noexcept { return trace_; }

private:
	[[nodiscard]] bool shouldDrop(const std::size_t relativeFrame) const noexcept
	{
		return profile_.dropEveryFrames > 0
			&& relativeFrame % profile_.dropEveryFrames < profile_.dropBurstFrames;
	}

	[[nodiscard]] std::size_t deliveryDelay(const std::size_t relativeFrame) const noexcept
	{
		std::size_t delay = profile_.fixedDelayFrames;
		if (profile_.jitterFrames > 0) {
			const std::uint64_t mixed = (static_cast<std::uint64_t>(relativeFrame) + 1U)
				* 0x9e3779b97f4a7c15ULL + profile_.seed;
			delay += static_cast<std::size_t>((mixed ^ (mixed >> 29U))
				% (profile_.jitterFrames + 1U));
		}
		if (profile_.reorderEveryFrames > 0 && relativeFrame % profile_.reorderEveryFrames == 0) {
			delay += profile_.reorderDelayFrames;
		}
		if (profile_.holdEveryFrames > 0 && profile_.holdFrames > 0) {
			const std::size_t holdPosition = relativeFrame % profile_.holdEveryFrames;
			if (holdPosition < profile_.holdFrames) {
				delay += profile_.holdFrames - holdPosition;
			}
		}
		return delay;
	}

	[[nodiscard]] ClientState::TimePoint now() const
	{
		const auto micros = static_cast<std::int64_t>(events_.now() * 1000000ULL / SAMPLE_RATE);
		return ClientState::TimePoint{} + std::chrono::microseconds(micros);
	}

	void scheduleDrop(const std::uint64_t frame)
	{
		const auto sample = frame * static_cast<std::uint64_t>(SAMPLE_BUFFER_SIZE);
		events_.scheduleAt(sample, "packet_drop", [this, frame] {
			++result_.injectedDrops;
			record("delivery", {
				{ "client", "client-b" }, { "frame", frame }, { "action", "dropped" }
			});
		}, { { "client", "client-b" }, { "frame", frame } });
	}

	void scheduleDelivery(const std::size_t clientIndex,
		const std::uint64_t frame,
		const std::size_t delayFrames,
		const std::string& action,
		const bool duplicate,
		const std::size_t duplicateOffsetSamples = 0)
	{
		const auto& setup = clientIndex == 0 ? setupA_ : setupB_;
		auto packet = makePacket(static_cast<std::uint32_t>(clientIndex + 1U), frame, setup);
		const auto deliverySample = (frame + delayFrames)
			* static_cast<std::uint64_t>(SAMPLE_BUFFER_SIZE) + duplicateOffsetSamples;
		events_.scheduleAt(deliverySample, "packet_delivery",
			[this, clientIndex, frame, capturedPacket = std::move(packet), action, duplicate, delayFrames] {
				if (duplicate) {
					++result_.injectedDuplicates;
				}
				else if (clientIndex == 1U) {
					++result_.primaryPacketsDelivered;
				}
				if (clientIndex == 1U && delayFrames > 0) {
					++result_.delayedPackets;
				}
				deliver(clientIndex, frame, capturedPacket, action);
			}, {
				{ "client", clientIndex == 0 ? "client-a" : "client-b" },
				{ "frame", frame }, { "action", action }, { "delay_frames", delayFrames }
			});
	}

	void deliver(const std::size_t clientIndex,
		const std::uint64_t frame,
		const std::shared_ptr<JammerNetzAudioData>& packet,
		const std::string& action)
	{
		auto& client = clientIndex == 0 ? clientA_ : clientB_;
		const auto push = client->push(packet, BUFFER_PREFILL_ON_CONNECT, now());
		record("delivery", {
			{ "client", clientIndex == 0 ? "client-a" : "client-b" },
			{ "frame", frame }, { "action", action }, { "queued", push.queued }
		});
		if (!push.queued) {
			++result_.rejectedPackets;
			return;
		}
		// Register both initial clients before the first mixer wake-up. Otherwise
		// client A is briefly treated as a one-client session and loses one
		// prefill packet before client B exists, invalidating the clean control.
		if (frame == 0 && clientIndex == 0) {
			return;
		}
		processWakeups();
	}

	void processWakeups()
	{
		constexpr std::size_t maximumSteps = 4096;
		std::size_t steps = 0;
		bool wakeAgain = true;
		while (wakeAgain) {
			if (++steps > maximumSteps) {
				throw std::runtime_error("The impairment scheduler kept requesting wake-ups without settling");
			}
			const auto step = scheduler_.process(clients_, now());
			recordStep(step);
			wakeAgain = step.shouldWakeAgain;
		}
	}

	void record(const std::string& kind, nlohmann::json details)
	{
		trace_.record({ events_.now(), traceSequence_++, kind, std::move(details) });
	}

	void recordStep(const ServerScheduledMixResult& step)
	{
		if (step.mix.outgoing.size() != step.incoming.size()) {
			throw std::runtime_error("The server mixer did not produce one routed packet per impairment input");
		}
		if (!step.mix.diagnostics.empty()) {
			throw std::runtime_error("The impairment input produced an invalid mixer diagnostic");
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
		record("server_mix_decision", {
			{ "trigger", triggerName(step.trigger) },
			{ "queues_before", queueJson(step.queuesBefore) },
			{ "queues_after", queueJson(step.queuesAfter) },
			{ "selected_counters", selected },
			{ "underrun_clients", step.underrunClients }
		});

		if (step.incoming.empty()) {
			return;
		}
		++result_.mixCount;
		bool coherent = false;
		if (step.incoming.size() == 1U) {
			++result_.singleSourceMixes;
		}
		else if (step.incoming.size() == 2U) {
			const auto counterA = static_cast<std::uint64_t>(
				step.incoming.at("client-a")->messageCounter());
			const auto counterB = static_cast<std::uint64_t>(
				step.incoming.at("client-b")->messageCounter());
			const auto skew = counterA > counterB ? counterA - counterB : counterB - counterA;
			result_.maximumSourceSkew = std::max(result_.maximumSourceSkew, skew);
			coherent = skew == 0;
			if (!coherent) {
				++result_.skewedMixes;
			}
		}

		if (events_.now() < recoveryEligibleSample_ || result_.recoveryMixes) {
			return;
		}
		if (recoveryObservationMix_ == 0) {
			recoveryObservationMix_ = result_.mixCount;
		}
		coherentRun_ = coherent ? coherentRun_ + 1U : 0U;
		if (coherentRun_ >= coherentRecoveryWindow) {
			result_.recoveryMixes = result_.mixCount - recoveryObservationMix_ + 1U;
		}
	}

	ImpairmentProfile profile_;
	ImpairmentRunResult result_;
	TPacketStreamBundle clients_;
	std::shared_ptr<ClientState> clientA_;
	std::shared_ptr<ClientState> clientB_;
	ServerMixScheduler scheduler_;
	JammerNetzChannelSetup setupA_;
	JammerNetzChannelSetup setupB_;
	jammernetz::test::ScenarioScheduler events_;
	ScenarioTrace trace_;
	std::uint64_t traceSequence_ { 0 };
	SampleIndex recoveryEligibleSample_ { 0 };
	std::size_t recoveryObservationMix_ { 0 };
	std::size_t coherentRun_ { 0 };
};

struct ServerRecoveryBoundary {
	std::optional<std::string> lastRecovered;
	std::optional<std::string> firstFailure;
};

nlohmann::json boundaryJson(const ServerRecoveryBoundary& boundary)
{
	return {
		{ "last_server_recovered_profile", boundary.lastRecovered
			? nlohmann::json(*boundary.lastRecovered) : nlohmann::json(nullptr) },
		{ "first_server_recovery_failure_profile", boundary.firstFailure
			? nlohmann::json(*boundary.firstFailure) : nlohmann::json(nullptr) }
	};
}

ImpairmentRunResult runAndRecord(const ImpairmentProfile& profile,
	const juce::File& traceDirectory,
	nlohmann::json& rows)
{
	ProgressiveImpairmentScenario firstScenario(profile);
	const auto first = firstScenario.run();
	ProgressiveImpairmentScenario replayScenario(profile);
	const auto replay = replayScenario.run();
	EXPECT_EQ(first, replay) << profile.name;
	EXPECT_EQ(first.generatedPackets, first.primaryPacketsDelivered + first.injectedDrops)
		<< profile.name;
	EXPECT_GT(first.mixCount, warmupFrames) << profile.name;
	if (profile.dropEveryFrames > 0) {
		EXPECT_GT(first.injectedDrops, 0U) << profile.name;
	}
	if (profile.duplicateEveryFrames > 0) {
		EXPECT_GT(first.injectedDuplicates, 0U) << profile.name;
	}
	if (profile.fixedDelayFrames > 0 || profile.jitterFrames > 0
		|| profile.reorderDelayFrames > 0 || profile.holdFrames > 0) {
		EXPECT_GT(first.delayedPackets, 0U) << profile.name;
	}
	firstScenario.trace().writeJsonLines(traceDirectory.getChildFile(profile.name + ".jsonl"));
	rows.push_back({ { "profile", profileJson(profile) }, { "result", resultJson(first) } });
	return first;
}

std::vector<ImpairmentProfile> isolatedProfiles()
{
	std::vector<ImpairmentProfile> profiles;
	profiles.push_back({ "control", "clean" });
	for (const std::size_t frames : { 1U, 2U, 4U, 8U, 16U, 32U }) {
		ImpairmentProfile profile { "lag", "lag-" + std::to_string(frames) + "-frames" };
		profile.fixedDelayFrames = frames;
		profiles.push_back(profile);
	}
	for (const std::size_t frames : { 1U, 2U, 4U, 8U, 16U, 32U }) {
		ImpairmentProfile profile { "jitter", "jitter-0-to-" + std::to_string(frames) + "-frames" };
		profile.jitterFrames = frames;
		profiles.push_back(profile);
	}
	for (const std::size_t every : { 64U, 32U, 16U, 8U, 4U, 2U }) {
		ImpairmentProfile profile { "drop-rate", "drop-every-" + std::to_string(every) };
		profile.dropEveryFrames = every;
		profiles.push_back(profile);
	}
	for (const std::size_t burst : { 1U, 2U, 3U, 4U, 8U }) {
		ImpairmentProfile profile { "drop-burst", "drop-burst-" + std::to_string(burst) };
		profile.dropEveryFrames = 32;
		profile.dropBurstFrames = burst;
		profiles.push_back(profile);
	}
	for (const std::size_t every : { 64U, 32U, 16U, 8U, 4U, 2U }) {
		ImpairmentProfile profile { "duplicate", "duplicate-every-" + std::to_string(every) };
		profile.duplicateEveryFrames = every;
		profiles.push_back(profile);
	}
	for (const std::size_t displacement : { 1U, 2U, 4U, 8U, 16U }) {
		ImpairmentProfile profile { "reorder", "reorder-" + std::to_string(displacement) + "-frames" };
		profile.reorderEveryFrames = 8;
		profile.reorderDelayFrames = displacement;
		profiles.push_back(profile);
	}
	for (const std::size_t frames : { 1U, 2U, 4U, 8U, 16U, 32U }) {
		ImpairmentProfile profile { "throttle", "throttle-hold-" + std::to_string(frames)
			+ "-every-32" };
		profile.holdEveryFrames = 32;
		profile.holdFrames = frames;
		profiles.push_back(profile);
	}
	return profiles;
}

std::vector<ImpairmentProfile> combinedProfiles()
{
	std::vector<ImpairmentProfile> profiles;
	for (const auto [jitter, dropEvery] : std::array<std::pair<std::size_t, std::size_t>, 3> {
		std::pair { 2U, 64U }, std::pair { 4U, 32U }, std::pair { 8U, 16U }
	}) {
		ImpairmentProfile profile { "jitter+drop", "jitter-" + std::to_string(jitter)
			+ "_drop-every-" + std::to_string(dropEvery) };
		profile.jitterFrames = jitter;
		profile.dropEveryFrames = dropEvery;
		profiles.push_back(profile);
	}
	for (const auto [jitter, displacement] : std::array<std::pair<std::size_t, std::size_t>, 3> {
		std::pair { 2U, 1U }, std::pair { 4U, 2U }, std::pair { 8U, 4U }
	}) {
		ImpairmentProfile profile { "jitter+reorder", "jitter-" + std::to_string(jitter)
			+ "_reorder-" + std::to_string(displacement) };
		profile.jitterFrames = jitter;
		profile.reorderEveryFrames = 8;
		profile.reorderDelayFrames = displacement;
		profiles.push_back(profile);
	}
	for (const auto [hold, duplicateEvery] : std::array<std::pair<std::size_t, std::size_t>, 3> {
		std::pair { 2U, 16U }, std::pair { 4U, 8U }, std::pair { 8U, 4U }
	}) {
		ImpairmentProfile profile { "hold+duplicate", "hold-" + std::to_string(hold)
			+ "_duplicate-every-" + std::to_string(duplicateEvery) };
		profile.holdEveryFrames = 32;
		profile.holdFrames = hold;
		profile.duplicateEveryFrames = duplicateEvery;
		profiles.push_back(profile);
	}
	for (const auto [lag, dropEvery] : std::array<std::pair<std::size_t, std::size_t>, 3> {
		std::pair { 2U, 32U }, std::pair { 4U, 16U }, std::pair { 8U, 8U }
	}) {
		ImpairmentProfile profile { "lag+drop", "lag-" + std::to_string(lag)
			+ "_drop-every-" + std::to_string(dropEvery) };
		profile.fixedDelayFrames = lag;
		profile.dropEveryFrames = dropEvery;
		profiles.push_back(profile);
	}
	for (const auto [jitter, dropEvery, duplicateEvery] :
		std::array<std::tuple<std::size_t, std::size_t, std::size_t>, 3> {
			std::tuple { 2U, 64U, 32U }, std::tuple { 4U, 32U, 16U }, std::tuple { 8U, 16U, 8U }
		}) {
		ImpairmentProfile profile { "jitter+drop+duplicate", "jitter-" + std::to_string(jitter)
			+ "_drop-every-" + std::to_string(dropEvery)
			+ "_duplicate-every-" + std::to_string(duplicateEvery) };
		profile.jitterFrames = jitter;
		profile.dropEveryFrames = dropEvery;
		profile.duplicateEveryFrames = duplicateEvery;
		profiles.push_back(profile);
	}
	for (const auto [jitter, dropEvery, displacement] :
		std::array<std::tuple<std::size_t, std::size_t, std::size_t>, 3> {
			std::tuple { 2U, 64U, 1U }, std::tuple { 4U, 32U, 2U }, std::tuple { 8U, 16U, 4U }
		}) {
		ImpairmentProfile profile { "jitter+drop+reorder", "jitter-" + std::to_string(jitter)
			+ "_drop-every-" + std::to_string(dropEvery)
			+ "_reorder-" + std::to_string(displacement) };
		profile.jitterFrames = jitter;
		profile.dropEveryFrames = dropEvery;
		profile.reorderEveryFrames = 8;
		profile.reorderDelayFrames = displacement;
		profiles.push_back(profile);
	}
	return profiles;
}

TEST(NetworkImpairmentCharacterizationTest, IsolatedClumsyStyleSweepsReportServerRecoveryBoundaries)
{
	nlohmann::json summary {
		{ "scenario", "isolated_progressive_impairments" },
		{ "sample_rate", SAMPLE_RATE },
		{ "frame_samples", SAMPLE_BUFFER_SIZE },
		{ "server_jitter_frames", SERVER_INCOMING_JITTER_BUFFER },
		{ "server_maximum_frames", SERVER_INCOMING_MAXIMUM_BUFFER },
		{ "server_recovery_definition", "both server-side client states connected and eight consecutive coherent server mixes after impairment" },
		{ "results", nlohmann::json::array() },
		{ "boundaries", nlohmann::json::object() }
	};
	const juce::File artifactDirectory(JAMMERNETZ_TEST_ARTIFACT_DIR);
	const auto scenarioDirectory = artifactDirectory.getChildFile("network-impairments").getChildFile("isolated");
	std::map<std::string, ServerRecoveryBoundary> boundaries;
	for (const auto& profile : isolatedProfiles()) {
		const auto result = runAndRecord(profile, scenarioDirectory, summary["results"]);
		auto& boundary = boundaries[profile.family];
		if (result.serverRecovered() && !boundary.firstFailure) {
			boundary.lastRecovered = profile.name;
		}
		else if (!result.serverRecovered() && !boundary.firstFailure) {
			boundary.firstFailure = profile.name;
		}
	}
	for (const auto& [family, boundary] : boundaries) {
		summary["boundaries"][family] = boundaryJson(boundary);
	}
	jammernetz::test::writeJsonArtifact(scenarioDirectory.getChildFile("summary.json"),
		summary, "characterization");
	RecordProperty("isolated_impairment_summary", summary.dump());
}

TEST(NetworkImpairmentCharacterizationTest, CombinedClumsyStyleProfilesRemainDeterministic)
{
	nlohmann::json summary {
		{ "scenario", "combined_impairments" },
		{ "sample_rate", SAMPLE_RATE },
		{ "frame_samples", SAMPLE_BUFFER_SIZE },
		{ "server_recovery_definition", "both server-side client states connected and eight consecutive coherent server mixes after impairment" },
		{ "results", nlohmann::json::array() },
		{ "boundaries", nlohmann::json::object() }
	};
	const juce::File artifactDirectory(JAMMERNETZ_TEST_ARTIFACT_DIR);
	const auto scenarioDirectory = artifactDirectory.getChildFile("network-impairments").getChildFile("combined");
	std::map<std::string, ServerRecoveryBoundary> boundaries;
	for (const auto& profile : combinedProfiles()) {
		const auto result = runAndRecord(profile, scenarioDirectory, summary["results"]);
		auto& boundary = boundaries[profile.family];
		if (result.serverRecovered() && !boundary.firstFailure) {
			boundary.lastRecovered = profile.name;
		}
		else if (!result.serverRecovered() && !boundary.firstFailure) {
			boundary.firstFailure = profile.name;
		}
	}
	for (const auto& [family, boundary] : boundaries) {
		summary["boundaries"][family] = boundaryJson(boundary);
	}
	jammernetz::test::writeJsonArtifact(scenarioDirectory.getChildFile("summary.json"),
		summary, "characterization");
	RecordProperty("combined_impairment_summary", summary.dump());
}

} // namespace
