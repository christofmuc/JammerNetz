/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "BuffersConfig.h"
#include "CharacterizationTestSupport.h"
#include "ServerMixScheduler.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

constexpr std::uint64_t boundaryFirstCounter = 1000;
constexpr std::size_t boundaryWarmupFrames = 32;
constexpr std::size_t boundaryImpairmentFrames = 640;
constexpr std::size_t boundaryRecoveryFrames = 96;

JammerNetzChannelSetup boundaryMonoSetup(const std::size_t participant)
{
	JammerNetzChannelSetup setup(true);
	const auto target = participant % 2U == 0U
		? JammerNetzChannelTarget::Left : JammerNetzChannelTarget::Right;
	setup.channels.push_back(JammerNetzSingleChannelSetup(static_cast<uint8>(target)));
	return setup;
}

JammerNetzChannelSetup boundaryStereoMixdown()
{
	return JammerNetzChannelSetup(false, {
		JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Left),
		JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Right)
	});
}

std::shared_ptr<JammerNetzAudioData> makeBoundaryPacket(const std::size_t participant,
	const std::uint64_t frame, const JammerNetzChannelSetup& setup)
{
	auto audio = std::make_shared<AudioBuffer<float>>(1, SAMPLE_BUFFER_SIZE);
	const auto amplitude = 0.01f * static_cast<float>(participant + 1U);
	for (int sample = 0; sample < SAMPLE_BUFFER_SIZE; ++sample) {
		audio->setSample(0, sample, amplitude);
	}
	const auto timestamp = 1000.0 * static_cast<double>(frame * SAMPLE_BUFFER_SIZE)
		/ static_cast<double>(SAMPLE_RATE);
	return std::make_shared<JammerNetzAudioData>(boundaryFirstCounter + frame,
		timestamp, setup, SAMPLE_RATE, 0.0f, MidiSignal_None, std::move(audio), nullptr);
}

ServerQueueObservation boundaryObservation(const ClientQueueSnapshot& snapshot)
{
	return { snapshot.state, snapshot.size, snapshot.activityGeneration };
}

// Frozen test-only reference for the all-clients-ready scheduler shipped before
// upload isolation. Keeping it here makes comparisons reproducible without a
// production compatibility switch.
class LegacyAllReadyScheduler {
public:
	LegacyAllReadyScheduler(JammerNetzChannelSetup mixdownSetup,
		const ServerBufferConfig bufferConfig)
		: mixerCore_(std::move(mixdownSetup)), bufferConfig_(bufferConfig)
	{
	}

	ServerScheduledMixResult process(TPacketStreamBundle& clients,
		const ClientState::TimePoint now)
	{
		ServerScheduledMixResult result;
		int clientCount = 0;
		int available = 0;
		std::map<std::string, ServerQueueObservation> queuesAfterFastForward;
		const auto maximumQueueDepth = static_cast<std::size_t>(
			std::max(0, bufferConfig_.serverIncomingMaximumBuffer));
		const auto targetQueueDepth = std::min(maximumQueueDepth,
			static_cast<std::size_t>(std::max(0, bufferConfig_.serverIncomingJitterBuffer)));

		for (auto& client : clients) {
			if (!client.second) {
				continue;
			}
			if (client.second->disconnectIfGraceExpired(now)) {
				result.disconnectedClients.push_back(client.first);
			}
			auto pressure = client.second->applyQueuePressure(maximumQueueDepth, targetQueueDepth);
			result.queuesBefore.emplace(client.first, boundaryObservation(pressure.before));
			auto snapshot = pressure.after;
			if (snapshot.state == ClientConnectionState::Disconnected) {
				queuesAfterFastForward.emplace(client.first, boundaryObservation(snapshot));
				continue;
			}
			if (pressure.fastForward.discardedPackets > 0U) {
				result.fastForwardedClients.emplace(client.first, std::move(pressure.fastForward));
			}
			queuesAfterFastForward.emplace(client.first, boundaryObservation(snapshot));
			++clientCount;
			if (static_cast<int>(snapshot.size) > bufferConfig_.serverIncomingJitterBuffer) {
				++available;
			}
		}

		const bool allClientsReady = clientCount > 0 && clientCount == available;
		if (!allClientsReady && clientCount > 1) {
			result.trigger = ServerMixTrigger::None;
			result.queuesAfter = std::move(queuesAfterFastForward);
			return result;
		}
		result.trigger = clientCount == 1
			? ServerMixTrigger::SingleClient : ServerMixTrigger::AllClientsReady;

		std::map<std::string, std::uint64_t> observedActivity;
		for (auto& client : clients) {
			if (!client.second) {
				continue;
			}
			std::shared_ptr<JammerNetzAudioData> popped;
			bool isFillIn = false;
			std::uint64_t activityGeneration = 0;
			if (client.second->tryPop(popped, isFillIn, activityGeneration)) {
				result.incoming.emplace(client.first, std::move(popped));
				if (isFillIn) {
					result.fillInClients.push_back(client.first);
					result.contributions.emplace(client.first, ServerSourceContribution::Concealment);
					result.shouldWakeAgain = true;
				}
				else {
					result.contributions.emplace(client.first, ServerSourceContribution::Packet);
				}
			}
			else if (client.second->snapshot().state != ClientConnectionState::Disconnected) {
				observedActivity.emplace(client.first, activityGeneration);
				result.missingClients.push_back(client.first);
				result.contributions.emplace(client.first, ServerSourceContribution::Silence);
			}
		}

		for (auto& client : clients) {
			if (!client.second) {
				continue;
			}
			const auto observation = observedActivity.find(client.first);
			if (observation != observedActivity.end()
				&& client.second->markUnderrun(observation->second, now)) {
				result.underrunClients.push_back(client.first);
			}
			result.queuesAfter.emplace(client.first, boundaryObservation(client.second->snapshot()));
		}
		result.mix = mixerCore_.mix(result.incoming);
		return result;
	}

private:
	ServerMixerCore mixerCore_;
	ServerBufferConfig bufferConfig_;
};

enum class BoundaryTopology {
	OneOutlier,
	AllUnhealthy
};

const char* topologyName(const BoundaryTopology topology)
{
	return topology == BoundaryTopology::OneOutlier ? "one_outlier" : "all_unhealthy";
}

struct BoundarySeverity {
	std::string name;
	std::size_t jitterFrames { 0 };
	std::size_t slotHoldFrames { 0 };
	std::size_t duplicateCopies { 0 };
};

const std::vector<BoundarySeverity>& boundarySeverities()
{
	static const std::vector<BoundarySeverity> severities {
		{ "clean", 0, 0, 0 },
		{ "jitter-2", 2, 0, 0 },
		{ "jitter-2_hold-4", 2, 4, 0 },
		{ "jitter-4_hold-8_duplicate-1", 4, 8, 1 },
		{ "jitter-8_hold-16_duplicate-2", 8, 16, 2 },
		{ "jitter-16_hold-32_duplicate-3", 16, 32, 3 }
	};
	return severities;
}

struct BoundaryScenario {
	std::size_t participantCount { 0 };
	BoundaryTopology topology { BoundaryTopology::OneOutlier };
	BoundarySeverity severity;
	std::uint64_t seed { 0x4a616d6d65724e65ULL };
};

struct PacketArrival {
	std::uint64_t tick { 0 };
	std::size_t participant { 0 };
	std::uint64_t frame { 0 };
	bool duplicate { false };
};

std::uint64_t mixBits(std::uint64_t value)
{
	value ^= value >> 30U;
	value *= 0xbf58476d1ce4e5b9ULL;
	value ^= value >> 27U;
	value *= 0x94d049bb133111ebULL;
	return value ^ (value >> 31U);
}

std::size_t scaledImpairment(const std::size_t maximum, const std::size_t participant,
	const std::size_t participantCount)
{
	if (maximum == 0U) {
		return 0U;
	}
	return std::max<std::size_t>(1U, maximum * (participant + 1U) / participantCount);
}

std::size_t arrivalDelay(const BoundaryScenario& scenario, const std::size_t participant,
	const std::uint64_t frame)
{
	const bool impairedParticipant = scenario.topology == BoundaryTopology::AllUnhealthy
		|| participant + 1U == scenario.participantCount;
	if (!impairedParticipant) {
		return 0U;
	}
	const auto jitter = scenario.topology == BoundaryTopology::AllUnhealthy
		? scaledImpairment(scenario.severity.jitterFrames, participant, scenario.participantCount)
		: scenario.severity.jitterFrames;
	const auto hold = scenario.topology == BoundaryTopology::AllUnhealthy
		? scaledImpairment(scenario.severity.slotHoldFrames, participant, scenario.participantCount)
		: scenario.severity.slotHoldFrames;
	const auto random = mixBits(scenario.seed
		^ (static_cast<std::uint64_t>(participant + 1U) * 0x9e3779b97f4a7c15ULL)
		^ (frame * 0xd1b54a32d192ed03ULL));
	std::size_t delay = jitter == 0U ? 0U
		: static_cast<std::size_t>(random % static_cast<std::uint64_t>(jitter + 1U));
	if (hold > 0U) {
		const auto cycle = hold * 3U;
		const auto phase = (participant * 5U) % cycle;
		const auto position = (static_cast<std::size_t>(frame) + phase) % cycle;
		if (position < hold) {
			delay += hold - position;
		}
	}
	return delay;
}

std::vector<PacketArrival> buildArrivals(const BoundaryScenario& scenario)
{
	std::vector<PacketArrival> arrivals;
	const auto generatedFrames = boundaryWarmupFrames + boundaryImpairmentFrames
		+ boundaryRecoveryFrames;
	const auto impairedEnd = boundaryWarmupFrames + boundaryImpairmentFrames;
	arrivals.reserve(generatedFrames * scenario.participantCount);
	for (std::uint64_t frame = 0; frame < generatedFrames; ++frame) {
		const bool impaired = frame >= boundaryWarmupFrames && frame < impairedEnd;
		for (std::size_t participant = 0; participant < scenario.participantCount; ++participant) {
			const auto delay = impaired ? arrivalDelay(scenario, participant, frame) : 0U;
			arrivals.push_back({ frame + delay, participant, frame, false });
			const bool duplicateBurst = impaired && scenario.severity.duplicateCopies > 0U
				&& frame % 37U == 0U
				&& (scenario.topology == BoundaryTopology::AllUnhealthy
					|| participant + 1U == scenario.participantCount);
			for (std::size_t copy = 0; duplicateBurst && copy < scenario.severity.duplicateCopies;
				++copy) {
				arrivals.push_back({ frame + delay, participant, frame, true });
			}
		}
	}
	std::stable_sort(arrivals.begin(), arrivals.end(), [](const auto& left, const auto& right) {
		return std::tie(left.tick, left.participant, left.frame, left.duplicate)
			< std::tie(right.tick, right.participant, right.frame, right.duplicate);
	});
	return arrivals;
}

struct BoundaryModelResult {
	std::size_t acceptedPackets { 0 };
	std::size_t rejectedPackets { 0 };
	std::size_t wakeups { 0 };
	std::size_t idleWakeups { 0 };
	std::size_t mixes { 0 };
	std::size_t outgoingPackets { 0 };
	std::size_t packetContributions { 0 };
	std::size_t concealmentContributions { 0 };
	std::size_t silenceContributions { 0 };
	std::size_t incompleteMixes { 0 };
	std::size_t skewedMixes { 0 };
	std::uint64_t maximumEpochSkewFrames { 0 };
	std::size_t fastForwardEvents { 0 };
	std::size_t fastForwardedPackets { 0 };
	std::size_t maximumQueueDepth { 0 };
	std::size_t underrunTransitions { 0 };
	std::size_t disconnects { 0 };
	std::size_t cadenceAssignments { 0 };
	std::size_t cadenceSwitches { 0 };
	std::size_t longestCadenceResidencyMixes { 0 };
	std::size_t maximumMixBurst { 0 };
	std::uint64_t longestMixGapFrames { 0 };
	std::uint64_t stalledFramesAfterStart { 0 };
	std::size_t sequenceErrors { 0 };

	bool operator==(const BoundaryModelResult&) const = default;
};

nlohmann::json modelJson(const BoundaryModelResult& result, const std::size_t generatedFrames)
{
	const auto mixDelta = static_cast<std::int64_t>(result.mixes)
		- static_cast<std::int64_t>(generatedFrames);
	return {
		{ "mixes", result.mixes },
		{ "nominal_mix_delta", mixDelta },
		{ "output_rate_percent_of_nominal", generatedFrames == 0U ? 0.0
			: 100.0 * static_cast<double>(result.mixes) / static_cast<double>(generatedFrames) },
		{ "accepted_packets", result.acceptedPackets },
		{ "rejected_packets", result.rejectedPackets },
		{ "wakeups", result.wakeups },
		{ "idle_wakeups", result.idleWakeups },
		{ "outgoing_packets", result.outgoingPackets },
		{ "packet_contributions", result.packetContributions },
		{ "concealment_contributions", result.concealmentContributions },
		{ "silence_contributions", result.silenceContributions },
		{ "incomplete_mixes", result.incompleteMixes },
		{ "skewed_mixes", result.skewedMixes },
		{ "maximum_epoch_skew_frames", result.maximumEpochSkewFrames },
		{ "fast_forward_events", result.fastForwardEvents },
		{ "fast_forwarded_packets", result.fastForwardedPackets },
		{ "maximum_queue_depth", result.maximumQueueDepth },
		{ "underrun_transitions", result.underrunTransitions },
		{ "disconnects", result.disconnects },
		{ "cadence_assignments", result.cadenceAssignments },
		{ "cadence_switches", result.cadenceSwitches },
		{ "cadence_switches_per_100_mixes", result.mixes == 0U ? 0.0
			: 100.0 * static_cast<double>(result.cadenceSwitches)
				/ static_cast<double>(result.mixes) },
		{ "longest_cadence_residency_mixes", result.longestCadenceResidencyMixes },
		{ "maximum_mix_burst", result.maximumMixBurst },
		{ "longest_mix_gap_frames", result.longestMixGapFrames },
		{ "stalled_frames_after_start", result.stalledFramesAfterStart },
		{ "sequence_errors", result.sequenceErrors }
	};
}

struct BoundaryFrontier {
	std::optional<std::string> legacyRateDeviation;
	std::optional<std::string> cadenceRateDeviation;
	std::optional<std::string> cadenceOverspeed;
	std::optional<std::string> legacyLongGap;
	std::optional<std::string> cadenceLongGap;
	std::optional<std::string> cadenceChurn;
	std::optional<std::string> cadenceEpochSkew;
};

void setFirst(std::optional<std::string>& frontier, const std::string& severity)
{
	if (!frontier) {
		frontier = severity;
	}
}

nlohmann::json optionalStringJson(const std::optional<std::string>& value)
{
	return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

nlohmann::json frontierJson(const BoundaryScenario& scenario, const BoundaryFrontier& frontier)
{
	return {
		{ "participants", scenario.participantCount },
		{ "topology", topologyName(scenario.topology) },
		{ "legacy_first_rate_deviation", optionalStringJson(frontier.legacyRateDeviation) },
		{ "cadence_first_rate_deviation", optionalStringJson(frontier.cadenceRateDeviation) },
		{ "cadence_first_overspeed", optionalStringJson(frontier.cadenceOverspeed) },
		{ "legacy_first_gap_above_server_jitter", optionalStringJson(frontier.legacyLongGap) },
		{ "cadence_first_gap_above_server_jitter", optionalStringJson(frontier.cadenceLongGap) },
		{ "cadence_first_donor_switch", optionalStringJson(frontier.cadenceChurn) },
		{ "cadence_first_epoch_skew", optionalStringJson(frontier.cadenceEpochSkew) }
	};
}

class BoundaryModelRunner {
public:
	BoundaryModelRunner(const std::size_t participantCount, const bool legacy)
		: participantCount_(participantCount), legacy_(legacy)
	{
		const ServerBufferConfig config {
			SERVER_INCOMING_JITTER_BUFFER,
			SERVER_INCOMING_MAXIMUM_BUFFER,
			BUFFER_PREFILL_ON_CONNECT
		};
		if (legacy_) {
			legacyScheduler_ = std::make_unique<LegacyAllReadyScheduler>(boundaryStereoMixdown(), config);
		}
		else {
			cadenceScheduler_ = std::make_unique<ServerMixScheduler>(boundaryStereoMixdown(), config);
		}
		for (std::size_t participant = 0; participant < participantCount_; ++participant) {
			const auto name = clientName(participant);
			clients_.insert(std::make_pair(name, std::make_shared<ClientState>(name)));
			setups_.push_back(boundaryMonoSetup(participant));
		}
	}

	BoundaryModelResult run(const std::vector<PacketArrival>& arrivals)
	{
		std::size_t cursor = 0;
		while (cursor < arrivals.size()) {
			const auto tick = arrivals[cursor].tick;
			std::size_t acceptedAtTick = 0;
			while (cursor < arrivals.size() && arrivals[cursor].tick == tick) {
				const auto& arrival = arrivals[cursor++];
				auto client = clients_.find(clientName(arrival.participant));
				if (client == clients_.end()) {
					throw std::runtime_error("Synthetic session referenced an unknown participant");
				}
				const auto push = client->second->push(makeBoundaryPacket(arrival.participant,
					arrival.frame, setups_.at(arrival.participant)), BUFFER_PREFILL_ON_CONNECT,
					timeAt(tick));
				if (push.queued) {
					++result_.acceptedPackets;
					++acceptedAtTick;
				}
				else {
					++result_.rejectedPackets;
				}
			}
			processWakeups(tick, acceptedAtTick);
		}
		finishCadenceResidency();
		return result_;
	}

private:
	static std::string clientName(const std::size_t participant)
	{
		return "client-" + std::to_string(participant + 1U);
	}

	static ClientState::TimePoint timeAt(const std::uint64_t tick)
	{
		const auto micros = static_cast<std::int64_t>(tick * SAMPLE_BUFFER_SIZE * 1000000ULL
			/ SAMPLE_RATE);
		return ClientState::TimePoint{} + std::chrono::microseconds(micros);
	}

	ServerScheduledMixResult processOne(const std::uint64_t tick)
	{
		return legacy_ ? legacyScheduler_->process(clients_, timeAt(tick))
			: cadenceScheduler_->process(clients_, timeAt(tick));
	}

	void processWakeups(const std::uint64_t tick, const std::size_t initialWakeups)
	{
		std::size_t pending = initialWakeups;
		std::size_t steps = 0;
		while (pending > 0U) {
			--pending;
			if (++steps > 16384U) {
				throw std::runtime_error("Synthetic mixer wake-ups did not settle");
			}
			const auto step = processOne(tick);
			++result_.wakeups;
			if (step.shouldWakeAgain) {
				++pending;
			}
			recordStep(step, tick);
		}
	}

	void recordStep(const ServerScheduledMixResult& step, const std::uint64_t tick)
	{
		for (const auto& [name, queue] : step.queuesBefore) {
			static_cast<void>(name);
			result_.maximumQueueDepth = std::max(result_.maximumQueueDepth, queue.size);
		}
		result_.fastForwardEvents += step.fastForwardedClients.size();
		for (const auto& [name, fastForward] : step.fastForwardedClients) {
			static_cast<void>(name);
			result_.fastForwardedPackets += fastForward.discardedPackets;
		}
		result_.underrunTransitions += step.underrunClients.size();
		result_.disconnects += step.disconnectedClients.size();
		if (!step.cadenceClient.empty() && step.cadenceClient != lastCadenceClient_) {
			++result_.cadenceAssignments;
			if (!lastCadenceClient_.empty()) {
				++result_.cadenceSwitches;
			}
			finishCadenceResidency();
			lastCadenceClient_ = step.cadenceClient;
		}
		if (step.incoming.empty()) {
			++result_.idleWakeups;
			return;
		}
		if (!step.mix.diagnostics.empty()) {
			throw std::runtime_error("Synthetic session produced invalid mixer diagnostics");
		}
		++result_.mixes;
		result_.outgoingPackets += step.mix.outgoing.size();
		if (step.mix.mixSequence != result_.mixes) {
			++result_.sequenceErrors;
		}
		for (const auto& [name, contribution] : step.contributions) {
			static_cast<void>(name);
			switch (contribution) {
			case ServerSourceContribution::Packet: ++result_.packetContributions; break;
			case ServerSourceContribution::Concealment: ++result_.concealmentContributions; break;
			case ServerSourceContribution::Silence: ++result_.silenceContributions; break;
			}
		}
		if (step.contributions.size() < participantCount_
			|| step.missingClients.size() > 0U) {
			++result_.incompleteMixes;
		}
		std::optional<std::uint64_t> minimumCounter;
		std::optional<std::uint64_t> maximumCounter;
		for (const auto& [name, packet] : step.incoming) {
			static_cast<void>(name);
			const auto counter = static_cast<std::uint64_t>(packet->messageCounter());
			minimumCounter = minimumCounter ? std::min(*minimumCounter, counter) : counter;
			maximumCounter = maximumCounter ? std::max(*maximumCounter, counter) : counter;
		}
		if (minimumCounter && maximumCounter && *minimumCounter != *maximumCounter) {
			++result_.skewedMixes;
			result_.maximumEpochSkewFrames = std::max(result_.maximumEpochSkewFrames,
				*maximumCounter - *minimumCounter);
		}
		if (!lastCadenceClient_.empty()) {
			++currentCadenceResidencyMixes_;
		}
		if (!lastMixTick_ || tick != *lastMixTick_) {
			mixesAtCurrentTick_ = 0;
		}
		++mixesAtCurrentTick_;
		result_.maximumMixBurst = std::max(result_.maximumMixBurst, mixesAtCurrentTick_);
		if (lastMixTick_ && tick > *lastMixTick_) {
			const auto distance = tick - *lastMixTick_;
			result_.longestMixGapFrames = std::max(result_.longestMixGapFrames, distance);
			if (distance > 1U) {
				result_.stalledFramesAfterStart += distance - 1U;
			}
		}
		lastMixTick_ = tick;
	}

	void finishCadenceResidency()
	{
		result_.longestCadenceResidencyMixes = std::max(result_.longestCadenceResidencyMixes,
			currentCadenceResidencyMixes_);
		currentCadenceResidencyMixes_ = 0;
	}

	std::size_t participantCount_ { 0 };
	bool legacy_ { false };
	TPacketStreamBundle clients_;
	std::vector<JammerNetzChannelSetup> setups_;
	std::unique_ptr<LegacyAllReadyScheduler> legacyScheduler_;
	std::unique_ptr<ServerMixScheduler> cadenceScheduler_;
	BoundaryModelResult result_;
	std::string lastCadenceClient_;
	std::size_t currentCadenceResidencyMixes_ { 0 };
	std::optional<std::uint64_t> lastMixTick_;
	std::size_t mixesAtCurrentTick_ { 0 };
};

nlohmann::json scenarioJson(const BoundaryScenario& scenario)
{
	return {
		{ "participants", scenario.participantCount },
		{ "topology", topologyName(scenario.topology) },
		{ "severity", scenario.severity.name },
		{ "jitter_frames", scenario.severity.jitterFrames },
		{ "slot_hold_frames", scenario.severity.slotHoldFrames },
		{ "duplicate_copies", scenario.severity.duplicateCopies },
		{ "seed", scenario.seed }
	};
}

BoundaryModelResult runModel(const BoundaryScenario& scenario,
	const std::vector<PacketArrival>& arrivals, const bool legacy)
{
	BoundaryModelRunner runner(scenario.participantCount, legacy);
	return runner.run(arrivals);
}

TEST(MixerCadenceBoundaryRegressionTest, NoHealthyDonorSweepIsDeterministicAndBounded)
{
	const BoundaryScenario scenario {
		6, BoundaryTopology::AllUnhealthy, boundarySeverities().back()
	};
	const auto arrivals = buildArrivals(scenario);
	const auto first = runModel(scenario, arrivals, false);
	const auto replay = runModel(scenario, arrivals, false);
	EXPECT_EQ(first, replay);
	EXPECT_GT(first.mixes, boundaryWarmupFrames);
	EXPECT_EQ(first.sequenceErrors, 0U);
	EXPECT_EQ(first.disconnects, 0U);
	EXPECT_GT(first.cadenceSwitches, 0U);
	EXPECT_LE(first.maximumQueueDepth,
		scenario.participantCount * (scenario.severity.slotHoldFrames + 1U));
}

TEST(MixerCadenceBoundaryCharacterizationTest, TwoToSixParticipantSweepComparesLegacyAndCadenceModels)
{
	const auto generatedFrames = boundaryWarmupFrames + boundaryImpairmentFrames
		+ boundaryRecoveryFrames;
	nlohmann::json summary {
		{ "scenario", "mixer_cadence_boundary" },
		{ "description", "Deterministic long-session comparison against the pre-upload-isolation all-ready scheduler" },
		{ "arrival_batching", "Packets with the same synthetic arrival tick are queued before their accepted mixer wake-ups are processed" },
		{ "sample_rate", SAMPLE_RATE },
		{ "frame_samples", SAMPLE_BUFFER_SIZE },
		{ "warmup_frames", boundaryWarmupFrames },
		{ "impairment_frames", boundaryImpairmentFrames },
		{ "recovery_frames", boundaryRecoveryFrames },
		{ "server_jitter_frames", SERVER_INCOMING_JITTER_BUFFER },
		{ "server_maximum_frames", SERVER_INCOMING_MAXIMUM_BUFFER },
		{ "rate_deviation_threshold_percent", 1.0 },
		{ "long_gap_threshold_frames", SERVER_INCOMING_JITTER_BUFFER },
		{ "results", nlohmann::json::array() },
		{ "frontiers", nlohmann::json::array() }
	};
	const auto rateTolerance = generatedFrames / 100U;

	for (std::size_t participants = 2; participants <= 6; ++participants) {
		for (const auto topology : { BoundaryTopology::OneOutlier, BoundaryTopology::AllUnhealthy }) {
			BoundaryFrontier frontier;
			for (const auto& severity : boundarySeverities()) {
				const BoundaryScenario scenario { participants, topology, severity };
				const auto arrivals = buildArrivals(scenario);
				const auto legacy = runModel(scenario, arrivals, true);
				const auto cadence = runModel(scenario, arrivals, false);

				EXPECT_GT(legacy.mixes, 0U) << participants << " participants, "
					<< topologyName(topology) << ", " << severity.name;
				EXPECT_GT(cadence.mixes, 0U) << participants << " participants, "
					<< topologyName(topology) << ", " << severity.name;
				EXPECT_EQ(legacy.sequenceErrors, 0U);
				EXPECT_EQ(cadence.sequenceErrors, 0U);
				EXPECT_EQ(legacy.disconnects, 0U);
				EXPECT_EQ(cadence.disconnects, 0U);
				if (severity.name == "clean") {
					EXPECT_EQ(cadence.mixes, legacy.mixes);
					EXPECT_EQ(cadence.longestMixGapFrames, legacy.longestMixGapFrames);
					EXPECT_EQ(cadence.skewedMixes, legacy.skewedMixes);
					EXPECT_EQ(cadence.fastForwardedPackets, legacy.fastForwardedPackets);
				}
				if (topology == BoundaryTopology::OneOutlier) {
					EXPECT_GE(cadence.mixes, legacy.mixes);
					EXPECT_LE(cadence.longestMixGapFrames, legacy.longestMixGapFrames);
				}
				const auto legacyRateDelta = legacy.mixes > generatedFrames
					? legacy.mixes - generatedFrames : generatedFrames - legacy.mixes;
				const auto cadenceRateDelta = cadence.mixes > generatedFrames
					? cadence.mixes - generatedFrames : generatedFrames - cadence.mixes;
				if (legacyRateDelta > rateTolerance) {
					setFirst(frontier.legacyRateDeviation, severity.name);
				}
				if (cadenceRateDelta > rateTolerance) {
					setFirst(frontier.cadenceRateDeviation, severity.name);
				}
				if (cadence.mixes > generatedFrames + rateTolerance) {
					setFirst(frontier.cadenceOverspeed, severity.name);
				}
				if (legacy.longestMixGapFrames
					> static_cast<std::uint64_t>(SERVER_INCOMING_JITTER_BUFFER)) {
					setFirst(frontier.legacyLongGap, severity.name);
				}
				if (cadence.longestMixGapFrames
					> static_cast<std::uint64_t>(SERVER_INCOMING_JITTER_BUFFER)) {
					setFirst(frontier.cadenceLongGap, severity.name);
				}
				if (cadence.cadenceSwitches > 0U) {
					setFirst(frontier.cadenceChurn, severity.name);
				}
				if (cadence.skewedMixes > 0U) {
					setFirst(frontier.cadenceEpochSkew, severity.name);
				}

				auto row = scenarioJson(scenario);
				row["legacy_all_ready"] = modelJson(legacy, generatedFrames);
				row["cadence_donor"] = modelJson(cadence, generatedFrames);
				row["comparison"] = {
					{ "mix_count_delta", static_cast<std::int64_t>(cadence.mixes)
						- static_cast<std::int64_t>(legacy.mixes) },
					{ "longest_gap_delta_frames", static_cast<std::int64_t>(cadence.longestMixGapFrames)
						- static_cast<std::int64_t>(legacy.longestMixGapFrames) },
					{ "skewed_mix_delta", static_cast<std::int64_t>(cadence.skewedMixes)
						- static_cast<std::int64_t>(legacy.skewedMixes) },
					{ "cadence_has_better_continuity", cadence.longestMixGapFrames < legacy.longestMixGapFrames
						|| cadence.mixes > legacy.mixes },
					{ "legacy_has_more_coherent_epochs", legacy.skewedMixes < cadence.skewedMixes },
					{ "legacy_has_lower_burst_peak", legacy.maximumMixBurst < cadence.maximumMixBurst }
				};
				summary["results"].push_back(std::move(row));
			}
			const BoundaryScenario frontierScenario {
				participants, topology, boundarySeverities().front()
			};
			summary["frontiers"].push_back(frontierJson(frontierScenario, frontier));
		}
	}

	const juce::File artifactDirectory(JAMMERNETZ_TEST_ARTIFACT_DIR);
	const auto scenarioDirectory = artifactDirectory.getChildFile("mixer-cadence-boundary");
	jammernetz::test::writeJsonArtifact(scenarioDirectory.getChildFile("summary.json"),
		summary, "mixer cadence boundary");
	RecordProperty("characterization_summary", summary.dump());
}

} // namespace
