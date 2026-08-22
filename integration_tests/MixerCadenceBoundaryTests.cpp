/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "BuffersConfig.h"
#include "CharacterizationTestSupport.h"
#include "DeterministicAudioTestSupport.h"
#include "JammerNetzAudioEngine.h"
#include "ServerMixScheduler.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
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
constexpr float boundaryAudioEpsilon = 1.0e-5f;
constexpr std::uint64_t nominalSampleRateMilliHz = SAMPLE_RATE * 1000ULL;
constexpr std::uint64_t departingReferenceTick = 2048;
constexpr auto disconnectGraceMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
	ClientState::DisconnectGracePeriod).count();
constexpr std::uint64_t disconnectGraceFrames =
	(static_cast<std::uint64_t>(disconnectGraceMilliseconds) * SAMPLE_RATE
		+ 1000ULL * SAMPLE_BUFFER_SIZE - 1ULL)
	/ (1000ULL * SAMPLE_BUFFER_SIZE);
constexpr std::uint64_t departingReferenceObservationFrames = departingReferenceTick
	+ disconnectGraceFrames + boundaryRecoveryFrames;

using jammernetz::test::SyntheticAudioSource;

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
	for (int sample = 0; sample < SAMPLE_BUFFER_SIZE; ++sample) {
		audio->setSample(0, sample, SyntheticAudioSource::valueAt(
			static_cast<std::uint32_t>(participant + 1U), 0,
			frame * SAMPLE_BUFFER_SIZE + static_cast<std::uint64_t>(sample)));
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
	std::vector<std::uint64_t> participantSampleRatesMilliHz {};
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

std::uint64_t sampleRateFor(const BoundaryScenario& scenario, const std::size_t participant)
{
	return scenario.participantSampleRatesMilliHz.empty()
		? nominalSampleRateMilliHz : scenario.participantSampleRatesMilliHz.at(participant);
}

std::uint64_t clockEventTick(const std::uint64_t event,
	const std::uint64_t sampleRateMilliHz)
{
	return event * nominalSampleRateMilliHz / sampleRateMilliHz;
}

std::size_t clockEventsAtTick(const std::uint64_t tick,
	const std::uint64_t sampleRateMilliHz)
{
	const auto first = (tick * sampleRateMilliHz + nominalSampleRateMilliHz - 1U)
		/ nominalSampleRateMilliHz;
	const auto after = ((tick + 1U) * sampleRateMilliHz + nominalSampleRateMilliHz - 1U)
		/ nominalSampleRateMilliHz;
	return static_cast<std::size_t>(after - first);
}

std::size_t scheduledClockEvents(const std::uint64_t frames,
	const std::uint64_t sampleRateMilliHz)
{
	return static_cast<std::size_t>((frames * sampleRateMilliHz
		+ nominalSampleRateMilliHz - 1U) / nominalSampleRateMilliHz);
}

struct ClockDriftCase {
	std::string name;
	std::vector<std::uint64_t> participantSampleRatesMilliHz;
	std::uint64_t observationFrames { 0 };
	BoundarySeverity network;
	std::optional<std::size_t> departingParticipant {};
	std::uint64_t departureTick { 0 };
};

std::vector<PacketArrival> buildClockDriftArrivals(const BoundaryScenario& scenario,
	const ClockDriftCase& testCase)
{
	std::vector<PacketArrival> arrivals;
	for (std::size_t participant = 0; participant < scenario.participantCount; ++participant) {
		const auto rate = sampleRateFor(scenario, participant);
		for (std::uint64_t frame = 0;; ++frame) {
			const auto producedAt = clockEventTick(frame, rate);
			if (producedAt >= testCase.observationFrames) {
				break;
			}
			if (testCase.departingParticipant == participant
				&& producedAt >= testCase.departureTick) {
				break;
			}
			const bool impaired = producedAt >= boundaryWarmupFrames;
			const auto delay = impaired ? arrivalDelay(scenario, participant, frame) : 0U;
			const auto arrivalTick = producedAt + delay;
			if (arrivalTick >= testCase.observationFrames) {
				continue;
			}
			arrivals.push_back({ arrivalTick, participant, frame, false });
			const bool duplicateBurst = impaired && scenario.severity.duplicateCopies > 0U
				&& frame % 37U == 0U;
			for (std::size_t copy = 0; duplicateBurst
				&& copy < scenario.severity.duplicateCopies; ++copy) {
				arrivals.push_back({ arrivalTick, participant, frame, true });
			}
		}
	}
	std::stable_sort(arrivals.begin(), arrivals.end(), [](const auto& left, const auto& right) {
		return std::tie(left.tick, left.participant, left.frame, left.duplicate)
			< std::tie(right.tick, right.participant, right.frame, right.duplicate);
	});
	return arrivals;
}

std::string boundaryClientName(const std::size_t participant)
{
	return "client-" + std::to_string(participant + 1U);
}

bool audioBuffersMatch(const AudioBuffer<float>& expected, const AudioBuffer<float>& observed)
{
	if (expected.getNumChannels() != observed.getNumChannels()
		|| expected.getNumSamples() != observed.getNumSamples()) {
		return false;
	}
	for (int channel = 0; channel < expected.getNumChannels(); ++channel) {
		for (int sample = 0; sample < expected.getNumSamples(); ++sample) {
			if (std::abs(expected.getSample(channel, sample) - observed.getSample(channel, sample))
				> boundaryAudioEpsilon) {
				return false;
			}
		}
	}
	return true;
}

bool audioIsSilent(const AudioBuffer<float>& audio)
{
	float magnitude = 0.0f;
	for (int channel = 0; channel < audio.getNumChannels(); ++channel) {
		magnitude = std::max(magnitude, audio.getMagnitude(channel, 0, audio.getNumSamples()));
	}
	return magnitude <= boundaryAudioEpsilon;
}

using AudioSignature = std::pair<std::int32_t, std::int32_t>;

std::int32_t quantizedSample(const float sample)
{
	return static_cast<std::int32_t>(std::lround(sample / boundaryAudioEpsilon));
}

AudioSignature audioSignature(const AudioBuffer<float>& audio)
{
	if (audio.getNumChannels() < 2 || audio.getNumSamples() == 0) {
		return {};
	}
	return { quantizedSample(audio.getSample(0, 0)), quantizedSample(audio.getSample(1, 0)) };
}

std::array<AudioSignature, 25> candidateAudioSignatures(const AudioSignature signature)
{
	constexpr std::array<std::int32_t, 5> offsets { 0, -1, 1, -2, 2 };
	std::array<AudioSignature, 25> result {};
	std::size_t index = 0;
	for (const auto left : offsets) {
		for (const auto right : offsets) {
			result[index++] = {
				static_cast<std::int32_t>(signature.first + left),
				static_cast<std::int32_t>(signature.second + right)
			};
		}
	}
	return result;
}

TEST(MixerCadenceBoundaryAudioOracleTest, SignaturePrefilterCoversEveryFullComparisonMatch)
{
	AudioBuffer<float> expected(2, 1);
	AudioBuffer<float> observed(2, 1);
	expected.clear();
	observed.clear();
	expected.setSample(0, 0, -0.5f * boundaryAudioEpsilon);
	observed.setSample(0, 0, 0.5f * boundaryAudioEpsilon);

	ASSERT_TRUE(audioBuffersMatch(expected, observed));
	const auto expectedSignature = audioSignature(expected);
	const auto observedSignature = audioSignature(observed);
	ASSERT_EQ(expectedSignature.first, observedSignature.first - 2);
	const auto candidates = candidateAudioSignatures(observedSignature);
	EXPECT_NE(std::find(candidates.begin(), candidates.end(), expectedSignature), candidates.end());
}

AudioBuffer<float> idealBoundaryReceiverFrame(const std::size_t receiver,
	const std::size_t participantCount, const std::uint64_t frame)
{
	AudioBuffer<float> result(2, SAMPLE_BUFFER_SIZE);
	result.clear();
	for (std::size_t participant = 0; participant < participantCount; ++participant) {
		if (participant == receiver) {
			continue;
		}
		const int outputChannel = participant % 2U == 0U ? 0 : 1;
		for (int sample = 0; sample < SAMPLE_BUFFER_SIZE; ++sample) {
			result.addSample(outputChannel, sample, SyntheticAudioSource::valueAt(
				static_cast<std::uint32_t>(participant + 1U), 0,
				frame * SAMPLE_BUFFER_SIZE + static_cast<std::uint64_t>(sample)));
		}
	}
	return result;
}

AudioBuffer<float> expectedBoundaryMixerFrame(const std::string& receiver,
	const ServerInputPackets& incoming)
{
	AudioBuffer<float> result(2, SAMPLE_BUFFER_SIZE);
	result.clear();
	for (const auto& [name, packet] : incoming) {
		if (name == receiver || !packet || !packet->audioBuffer()) {
			continue;
		}
		const auto setup = packet->channelSetup();
		if (setup.channels.empty()) {
			continue;
		}
		const auto channel = setup.channels.front();
		if (channel.target == JammerNetzChannelTarget::Left) {
			result.addFrom(0, 0, *packet->audioBuffer(), 0, 0, SAMPLE_BUFFER_SIZE, channel.volume);
		}
		else if (channel.target == JammerNetzChannelTarget::Right) {
			result.addFrom(1, 0, *packet->audioBuffer(), 0, 0, SAMPLE_BUFFER_SIZE, channel.volume);
		}
	}
	return result;
}

struct BoundaryServerFrameMetadata {
	bool healthyRemoteSourcesCoherent { false };
};

struct BoundaryReceiverAudioResult {
	std::string receiver;
	std::size_t serverFrames { 0 };
	std::size_t serverSignalMismatches { 0 };
	std::size_t serverIdealFrames { 0 };
	std::size_t callbackFrames { 0 };
	bool playbackStarted { false };
	std::size_t audibleCallbackFrames { 0 };
	std::size_t transportMatchedFrames { 0 };
	std::size_t transportUnmatchedFrames { 0 };
	std::size_t serverSequenceDiscontinuities { 0 };
	std::size_t skippedServerFrames { 0 };
	std::size_t repeatedServerFrames { 0 };
	std::size_t musicallyCoherentFrames { 0 };
	std::size_t musicallyIncoherentFrames { 0 };
	std::size_t longestIncoherentRunFrames { 0 };
	std::size_t musicalDiscontinuities { 0 };
	std::uint64_t maximumMusicalFrameLag { 0 };
	std::uint64_t maximumMusicalFrameLead { 0 };
	std::size_t silentFramesAfterStart { 0 };
	std::size_t longestSilentRunFrames { 0 };
	std::size_t healthyAudioEligibleFrames { 0 };
	std::size_t healthyAudioPreservedFrames { 0 };
	std::uint64_t playoutUnderruns { 0 };
	std::uint64_t discardedFrames { 0 };
	std::uint64_t receiveQueueOverruns { 0 };
	std::uint64_t maximumPreparedQueueFrames { 0 };

	bool operator==(const BoundaryReceiverAudioResult&) const = default;
};

class BoundaryReceiverProbe {
public:
	BoundaryReceiverProbe(const std::size_t receiver, const std::size_t participantCount,
		const bool hasRemoteHealthySources, const std::uint64_t maximumIdealFrame)
		: receiver_(receiver)
		, participantCount_(participantCount)
		, hasRemoteHealthySources_(hasRemoteHealthySources)
		, engine_(session_, juce::File())
	{
		result_.receiver = boundaryClientName(receiver_);
		engine_.setLocalMonitoring(false);
		engine_.setMasterVolume(1.0);
		engine_.setMonitorBalance(1.0);
		engine_.setPlayoutBufferRange(CLIENT_PLAYOUT_JITTER_BUFFER, CLIENT_PLAYOUT_MAX_BUFFER);
		engine_.prepare(SAMPLE_RATE, SAMPLE_BUFFER_SIZE);
		idealFrames_.reserve(static_cast<std::size_t>(maximumIdealFrame + 1U));
		for (std::uint64_t frame = 0; frame <= maximumIdealFrame; ++frame) {
			auto ideal = std::make_shared<AudioBuffer<float>>(
				idealBoundaryReceiverFrame(receiver_, participantCount_, frame));
			idealFramesBySignature_[audioSignature(*ideal)].push_back(frame);
			idealFrames_.push_back(std::move(ideal));
		}
	}

	void deliver(const OutgoingPackage& package, const ServerInputPackets& incoming,
		const BoundaryServerFrameMetadata metadata)
	{
		++result_.serverFrames;
		const auto expected = expectedBoundaryMixerFrame(result_.receiver, incoming);
		if (!audioBuffersMatch(expected, *package.audioBlock.audioBuffer)) {
			++result_.serverSignalMismatches;
		}
		if (findIdealFrame(*package.audioBlock.audioBuffer)) {
			++result_.serverIdealFrames;
		}
		const auto sequence = static_cast<std::uint64_t>(package.audioBlock.messageCounter);
		serverFrames_.emplace(sequence, package.audioBlock.audioBuffer);
		serverMetadata_.emplace(sequence, metadata);
		serverFramesBySignature_[audioSignature(*package.audioBlock.audioBuffer)].push_back(sequence);
		engine_.enqueueRemoteAudio(std::make_shared<JammerNetzAudioData>(package.audioBlock, nullptr));
		while (engine_.processNextIncomingPacket()) {}
	}

	void processCallback(const std::uint64_t tick)
	{
		AudioBuffer<float> observed(2, SAMPLE_BUFFER_SIZE);
		observed.clear();
		float* outputs[] { observed.getWritePointer(0), observed.getWritePointer(1) };
		engine_.process(nullptr, 0, outputs, 2, SAMPLE_BUFFER_SIZE);
		++result_.callbackFrames;
		const auto playout = engine_.getPlayoutQualityInfo();
		result_.maximumPreparedQueueFrames = std::max(result_.maximumPreparedQueueFrames,
			static_cast<std::uint64_t>(playout.currentPlayQueueLength_));

		const bool silent = audioIsSilent(observed);
		if (!result_.playbackStarted && !silent) {
			result_.playbackStarted = true;
		}
		if (!result_.playbackStarted) {
			return;
		}
		++result_.audibleCallbackFrames;
		if (silent) {
			++result_.silentFramesAfterStart;
			++currentSilentRunFrames_;
			result_.longestSilentRunFrames = std::max(result_.longestSilentRunFrames,
				currentSilentRunFrames_);
		}
		else {
			currentSilentRunFrames_ = 0;
		}

		const auto serverSequence = findServerSequence(observed);
		if (serverSequence) {
			++result_.transportMatchedFrames;
			if (lastServerSequence_) {
				if (*serverSequence == *lastServerSequence_) {
					++result_.repeatedServerFrames;
				}
				else if (*serverSequence != *lastServerSequence_ + 1U) {
					++result_.serverSequenceDiscontinuities;
					if (*serverSequence > *lastServerSequence_ + 1U) {
						result_.skippedServerFrames += static_cast<std::size_t>(
							*serverSequence - *lastServerSequence_ - 1U);
					}
				}
			}
			lastServerSequence_ = serverSequence;
		}
		else {
			++result_.transportUnmatchedFrames;
		}

		if (hasRemoteHealthySources_) {
			++result_.healthyAudioEligibleFrames;
			if (serverSequence) {
				const auto metadata = serverMetadata_.find(*serverSequence);
				if (metadata != serverMetadata_.end()
					&& metadata->second.healthyRemoteSourcesCoherent) {
					++result_.healthyAudioPreservedFrames;
				}
			}
		}

		const auto idealFrame = findIdealFrame(observed);
		if (idealFrame) {
			++result_.musicallyCoherentFrames;
			currentIncoherentRunFrames_ = 0;
			if (lastIdealFrame_ && *idealFrame != *lastIdealFrame_ + 1U) {
				++result_.musicalDiscontinuities;
			}
			lastIdealFrame_ = idealFrame;
			if (tick >= *idealFrame) {
				result_.maximumMusicalFrameLag = std::max(result_.maximumMusicalFrameLag,
					tick - *idealFrame);
			}
			else {
				result_.maximumMusicalFrameLead = std::max(result_.maximumMusicalFrameLead,
					*idealFrame - tick);
			}
		}
		else {
			++result_.musicallyIncoherentFrames;
			++currentIncoherentRunFrames_;
			result_.longestIncoherentRunFrames = std::max(result_.longestIncoherentRunFrames,
				currentIncoherentRunFrames_);
			lastIdealFrame_.reset();
		}
	}

	BoundaryReceiverAudioResult finish()
	{
		const auto playout = engine_.getPlayoutQualityInfo();
		const auto workers = engine_.getRealtimeWorkerStats();
		result_.playoutUnderruns = playout.playUnderruns_;
		result_.discardedFrames = playout.discardedPackageCounter_;
		result_.receiveQueueOverruns = workers.receiveQueueOverruns;
		return result_;
	}

private:
	std::optional<std::uint64_t> findServerSequence(const AudioBuffer<float>& observed) const
	{
		std::vector<std::uint64_t> matches;
		for (const auto& signature : candidateAudioSignatures(audioSignature(observed))) {
			const auto candidates = serverFramesBySignature_.find(signature);
			if (candidates == serverFramesBySignature_.end()) {
				continue;
			}
			for (const auto sequence : candidates->second) {
				const auto frame = serverFrames_.find(sequence);
				if (frame != serverFrames_.end() && audioBuffersMatch(*frame->second, observed)) {
					matches.push_back(sequence);
				}
			}
		}
		if (matches.empty()) {
			return std::nullopt;
		}
		std::sort(matches.begin(), matches.end());
		if (!lastServerSequence_) {
			return matches.front();
		}
		const auto consecutive = std::find(matches.begin(), matches.end(), *lastServerSequence_ + 1U);
		if (consecutive != matches.end()) {
			return *consecutive;
		}
		const auto later = std::upper_bound(matches.begin(), matches.end(), *lastServerSequence_);
		return later != matches.end() ? std::optional<std::uint64_t>(*later)
			: std::optional<std::uint64_t>(matches.back());
	}

	std::optional<std::uint64_t> findIdealFrame(const AudioBuffer<float>& observed) const
	{
		for (const auto& signature : candidateAudioSignatures(audioSignature(observed))) {
			const auto candidates = idealFramesBySignature_.find(signature);
			if (candidates == idealFramesBySignature_.end()) {
				continue;
			}
			for (const auto frame : candidates->second) {
				const auto& ideal = idealFrames_.at(static_cast<std::size_t>(frame));
				if (audioBuffersMatch(*ideal, observed)) {
					return frame;
				}
			}
		}
		return std::nullopt;
	}

	std::size_t receiver_ { 0 };
	std::size_t participantCount_ { 0 };
	bool hasRemoteHealthySources_ { false };
	JammerNetzSession session_;
	JammerNetzAudioEngine engine_;
	BoundaryReceiverAudioResult result_;
	std::map<std::uint64_t, std::shared_ptr<AudioBuffer<float>>> serverFrames_;
	std::map<std::uint64_t, BoundaryServerFrameMetadata> serverMetadata_;
	std::map<AudioSignature, std::vector<std::uint64_t>> serverFramesBySignature_;
	std::map<AudioSignature, std::vector<std::uint64_t>> idealFramesBySignature_;
	std::vector<std::shared_ptr<AudioBuffer<float>>> idealFrames_;
	std::optional<std::uint64_t> lastServerSequence_;
	std::optional<std::uint64_t> lastIdealFrame_;
	std::size_t currentIncoherentRunFrames_ { 0 };
	std::size_t currentSilentRunFrames_ { 0 };
};

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
	std::vector<BoundaryReceiverAudioResult> receiverAudio;
	std::string audioVerdict;

	bool operator==(const BoundaryModelResult&) const = default;
};

double boundaryPercentage(const std::uint64_t numerator, const std::uint64_t denominator)
{
	return denominator == 0U ? 0.0
		: 100.0 * static_cast<double>(numerator) / static_cast<double>(denominator);
}

nlohmann::json receiverAudioJson(const BoundaryReceiverAudioResult& result)
{
	return {
		{ "receiver", result.receiver },
		{ "server_frames", result.serverFrames },
		{ "server_signal_mismatches", result.serverSignalMismatches },
		{ "server_ideal_frames", result.serverIdealFrames },
		{ "server_ideal_frame_percent", boundaryPercentage(result.serverIdealFrames,
			result.serverFrames) },
		{ "callback_frames", result.callbackFrames },
		{ "playback_started", result.playbackStarted },
		{ "audible_callback_frames", result.audibleCallbackFrames },
		{ "transport_matched_frames", result.transportMatchedFrames },
		{ "transport_unmatched_frames", result.transportUnmatchedFrames },
		{ "server_sequence_discontinuities", result.serverSequenceDiscontinuities },
		{ "skipped_server_frames", result.skippedServerFrames },
		{ "repeated_server_frames", result.repeatedServerFrames },
		{ "musically_coherent_frames", result.musicallyCoherentFrames },
		{ "musically_incoherent_frames", result.musicallyIncoherentFrames },
		{ "musically_coherent_percent", boundaryPercentage(result.musicallyCoherentFrames,
			result.audibleCallbackFrames) },
		{ "longest_incoherent_run_frames", result.longestIncoherentRunFrames },
		{ "musical_discontinuities", result.musicalDiscontinuities },
		{ "maximum_musical_frame_lag", result.maximumMusicalFrameLag },
		{ "maximum_musical_frame_lead", result.maximumMusicalFrameLead },
		{ "silent_frames_after_start", result.silentFramesAfterStart },
		{ "longest_silent_run_frames", result.longestSilentRunFrames },
		{ "healthy_audio_eligible_frames", result.healthyAudioEligibleFrames },
		{ "healthy_audio_preserved_frames", result.healthyAudioPreservedFrames },
		{ "healthy_audio_preserved_percent", boundaryPercentage(
			result.healthyAudioPreservedFrames, result.healthyAudioEligibleFrames) },
		{ "playout_underruns", result.playoutUnderruns },
		{ "discarded_frames", result.discardedFrames },
		{ "receive_queue_overruns", result.receiveQueueOverruns },
		{ "maximum_prepared_queue_frames", result.maximumPreparedQueueFrames }
	};
}

std::string qualifyAudio(const std::vector<BoundaryReceiverAudioResult>& receivers,
	const BoundaryTopology topology)
{
	bool allPlaybackStarted = true;
	std::uint64_t renderMismatches = 0;
	std::uint64_t transportFailures = 0;
	std::uint64_t playoutUnderruns = 0;
	std::uint64_t discardedFrames = 0;
	std::uint64_t receiveQueueOverruns = 0;
	std::uint64_t incoherentFrames = 0;
	std::uint64_t healthyEligibleFrames = 0;
	std::uint64_t healthyPreservedFrames = 0;
	for (const auto& receiver : receivers) {
		allPlaybackStarted = allPlaybackStarted && receiver.playbackStarted;
		renderMismatches += receiver.serverSignalMismatches;
		transportFailures += receiver.transportUnmatchedFrames;
		playoutUnderruns += receiver.playoutUnderruns;
		discardedFrames += receiver.discardedFrames;
		receiveQueueOverruns += receiver.receiveQueueOverruns;
		incoherentFrames += receiver.musicallyIncoherentFrames;
		healthyEligibleFrames += receiver.healthyAudioEligibleFrames;
		healthyPreservedFrames += receiver.healthyAudioPreservedFrames;
	}
	if (renderMismatches > 0U) {
		return "mixer_render_error";
	}
	if (!allPlaybackStarted) {
		return "no_playback";
	}
	if (playoutUnderruns > 0U && (discardedFrames > 0U || receiveQueueOverruns > 0U)) {
		return "playout_rate_instability";
	}
	if (playoutUnderruns > 0U) {
		return "playout_starvation";
	}
	if (discardedFrames > 0U || receiveQueueOverruns > 0U) {
		return "playout_overflow";
	}
	if (transportFailures > 0U) {
		return "transport_corruption";
	}
	if (incoherentFrames == 0U) {
		return "sample_coherent";
	}
	if (topology == BoundaryTopology::OneOutlier && healthyEligibleFrames > 0U
		&& healthyEligibleFrames == healthyPreservedFrames) {
		return "isolated_source_damage";
	}
	return "mixed_audio_degradation";
}

int audioVerdictScore(const std::string& verdict)
{
	if (verdict == "sample_coherent") {
		return 4;
	}
	if (verdict == "isolated_source_damage") {
		return 3;
	}
	if (verdict == "mixed_audio_degradation") {
		return 2;
	}
	if (verdict == "playout_starvation" || verdict == "playout_overflow"
		|| verdict == "playout_rate_instability"
		|| verdict == "transport_corruption") {
		return 1;
	}
	return 0;
}

nlohmann::json modelJson(const BoundaryModelResult& result, const std::size_t generatedFrames)
{
	const auto mixDelta = static_cast<std::int64_t>(result.mixes)
		- static_cast<std::int64_t>(generatedFrames);
	std::uint64_t audibleFrames = 0;
	std::uint64_t coherentFrames = 0;
	std::uint64_t healthyEligibleFrames = 0;
	std::uint64_t healthyPreservedFrames = 0;
	std::uint64_t silentFrames = 0;
	std::uint64_t playoutUnderruns = 0;
	std::uint64_t discardedFrames = 0;
	std::uint64_t renderMismatches = 0;
	for (const auto& receiver : result.receiverAudio) {
		audibleFrames += receiver.audibleCallbackFrames;
		coherentFrames += receiver.musicallyCoherentFrames;
		healthyEligibleFrames += receiver.healthyAudioEligibleFrames;
		healthyPreservedFrames += receiver.healthyAudioPreservedFrames;
		silentFrames += receiver.silentFramesAfterStart;
		playoutUnderruns += receiver.playoutUnderruns;
		discardedFrames += receiver.discardedFrames;
		renderMismatches += receiver.serverSignalMismatches;
	}
	return {
		{ "audio_verdict", result.audioVerdict },
		{ "audio_summary", {
			{ "musically_coherent_percent", boundaryPercentage(coherentFrames, audibleFrames) },
			{ "healthy_audio_preserved_percent", boundaryPercentage(healthyPreservedFrames,
				healthyEligibleFrames) },
			{ "silent_frames_after_start", silentFrames },
			{ "playout_underruns", playoutUnderruns },
			{ "discarded_frames", discardedFrames },
			{ "server_signal_mismatches", renderMismatches }
		} },
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
		{ "sequence_errors", result.sequenceErrors },
		{ "receiver_audio", [&result] {
			nlohmann::json receivers = nlohmann::json::array();
			for (const auto& receiver : result.receiverAudio) {
				receivers.push_back(receiverAudioJson(receiver));
			}
			return receivers;
		}() }
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
	std::optional<std::string> legacyAudioDegradation;
	std::optional<std::string> cadenceAudioDegradation;
	std::optional<std::string> cadencePlayoutInstability;
	std::optional<std::string> cadenceHealthyAudioDamage;
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

nlohmann::json frontierJson(const std::size_t participantCount,
	const BoundaryTopology topology, const BoundaryFrontier& frontier)
{
	return {
		{ "participants", participantCount },
		{ "topology", topologyName(topology) },
		{ "legacy_first_rate_deviation", optionalStringJson(frontier.legacyRateDeviation) },
		{ "cadence_first_rate_deviation", optionalStringJson(frontier.cadenceRateDeviation) },
		{ "cadence_first_overspeed", optionalStringJson(frontier.cadenceOverspeed) },
		{ "legacy_first_gap_above_server_jitter", optionalStringJson(frontier.legacyLongGap) },
		{ "cadence_first_gap_above_server_jitter", optionalStringJson(frontier.cadenceLongGap) },
		{ "cadence_first_donor_switch", optionalStringJson(frontier.cadenceChurn) },
		{ "cadence_first_epoch_skew", optionalStringJson(frontier.cadenceEpochSkew) },
		{ "legacy_first_audio_degradation", optionalStringJson(frontier.legacyAudioDegradation) },
		{ "cadence_first_audio_degradation", optionalStringJson(frontier.cadenceAudioDegradation) },
		{ "cadence_first_playout_instability", optionalStringJson(
			frontier.cadencePlayoutInstability) },
		{ "cadence_first_healthy_audio_damage", optionalStringJson(
			frontier.cadenceHealthyAudioDamage) }
	};
}

class BoundaryModelRunner {
public:
	BoundaryModelRunner(BoundaryScenario scenario, const bool legacy,
		const std::uint64_t maximumIdealFrame)
		: scenario_(std::move(scenario)), participantCount_(scenario_.participantCount), legacy_(legacy)
		, receiverCallbackFrames_(participantCount_, 0U)
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
			const auto name = boundaryClientName(participant);
			clients_.insert(std::make_pair(name, std::make_shared<ClientState>(name)));
			setups_.push_back(boundaryMonoSetup(participant));
			const bool hasRemoteHealthySources = scenario_.topology == BoundaryTopology::OneOutlier
				&& (participant + 1U == participantCount_ || participantCount_ > 2U);
			receiverProbes_.push_back(std::make_unique<BoundaryReceiverProbe>(participant,
				participantCount_, hasRemoteHealthySources,
				maximumIdealFrame));
		}
	}

	BoundaryModelResult run(const std::vector<PacketArrival>& arrivals,
		const std::optional<std::uint64_t> observationLastTick)
	{
		std::size_t cursor = 0;
		const auto lastTick = observationLastTick.value_or(arrivals.empty() ? 0U : arrivals.back().tick);
		for (std::uint64_t tick = 0; tick <= lastTick; ++tick) {
			std::size_t acceptedAtTick = 0;
			while (cursor < arrivals.size() && arrivals[cursor].tick == tick) {
				const auto& arrival = arrivals[cursor++];
				auto client = clients_.find(boundaryClientName(arrival.participant));
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
			for (std::size_t participant = 0; participant < receiverProbes_.size(); ++participant) {
				const auto callbacks = clockEventsAtTick(tick, sampleRateFor(scenario_, participant));
				for (std::size_t callback = 0; callback < callbacks; ++callback) {
					receiverProbes_[participant]->processCallback(receiverCallbackFrames_[participant]++);
				}
			}
		}
		finishCadenceResidency();
		for (auto& receiver : receiverProbes_) {
			result_.receiverAudio.push_back(receiver->finish());
		}
		result_.audioVerdict = qualifyAudio(result_.receiverAudio, scenario_.topology);
		return result_;
	}

private:
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
		for (const auto& outgoing : step.mix.outgoing) {
			for (std::size_t receiver = 0; receiver < participantCount_; ++receiver) {
				if (outgoing.targetAddress == boundaryClientName(receiver)) {
					receiverProbes_.at(receiver)->deliver(outgoing, step.incoming,
						serverFrameMetadata(receiver, step));
					break;
				}
			}
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

	BoundaryServerFrameMetadata serverFrameMetadata(const std::size_t receiver,
		const ServerScheduledMixResult& step) const
	{
		BoundaryServerFrameMetadata metadata;
		std::optional<std::uint64_t> commonCounter;
		metadata.healthyRemoteSourcesCoherent = true;
		for (std::size_t source = 0; source < participantCount_; ++source) {
			const bool healthySource = scenario_.topology == BoundaryTopology::OneOutlier
				&& source + 1U != participantCount_;
			if (!healthySource || source == receiver) {
				continue;
			}
			const auto name = boundaryClientName(source);
			const auto contribution = step.contributions.find(name);
			const auto incoming = step.incoming.find(name);
			if (contribution == step.contributions.end()
				|| contribution->second != ServerSourceContribution::Packet
				|| incoming == step.incoming.end()) {
				metadata.healthyRemoteSourcesCoherent = false;
				continue;
			}
			const auto counter = static_cast<std::uint64_t>(incoming->second->messageCounter());
			if (commonCounter && counter != *commonCounter) {
				metadata.healthyRemoteSourcesCoherent = false;
			}
			else if (!commonCounter) {
				commonCounter = counter;
			}
		}
		return metadata;
	}

	void finishCadenceResidency()
	{
		result_.longestCadenceResidencyMixes = std::max(result_.longestCadenceResidencyMixes,
			currentCadenceResidencyMixes_);
		currentCadenceResidencyMixes_ = 0;
	}

	BoundaryScenario scenario_;
	std::size_t participantCount_ { 0 };
	bool legacy_ { false };
	TPacketStreamBundle clients_;
	std::vector<JammerNetzChannelSetup> setups_;
	std::unique_ptr<LegacyAllReadyScheduler> legacyScheduler_;
	std::unique_ptr<ServerMixScheduler> cadenceScheduler_;
	std::vector<std::unique_ptr<BoundaryReceiverProbe>> receiverProbes_;
	std::vector<std::uint64_t> receiverCallbackFrames_;
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
	const std::vector<PacketArrival>& arrivals, const bool legacy,
	const std::optional<std::uint64_t> observationLastTick = std::nullopt)
{
	std::uint64_t maximumIdealFrame = 0;
	for (const auto& arrival : arrivals) {
		maximumIdealFrame = std::max(maximumIdealFrame, arrival.frame);
	}
	BoundaryModelRunner runner(scenario, legacy, maximumIdealFrame);
	return runner.run(arrivals, observationLastTick);
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
		static_cast<std::size_t>(SERVER_INCOMING_MAXIMUM_BUFFER)
			+ BUFFER_PREFILL_ON_CONNECT + 1U);
}

TEST(MixerCadenceClockDriftCharacterizationTest,
	HardwareClockSweepMeasuresRenderedAudio)
{
	const BoundarySeverity clean { "clean", 0, 0, 0 };
	const BoundarySeverity heldBursts { "jitter-4_hold-8_duplicate-1", 4, 8, 1 };
	const std::vector<ClockDriftCase> cases {
		{ "both_minus_50ppm", { 47997600ULL, 47997600ULL }, 24000, clean },
		{ "both_minus_100ppm", { 47995200ULL, 47995200ULL }, 12000, clean },
		{ "both_minus_500ppm", { 47976000ULL, 47976000ULL }, 4096, clean },
		{ "both_47850hz", { 47850000ULL, 47850000ULL }, 4096, clean },
		{ "unequal_slow_47850_47950hz", { 47850000ULL, 47950000ULL }, 4096, clean },
		{ "opposing_47850_48150hz", { 47850000ULL, 48150000ULL }, 4096, clean },
		{ "both_47850hz_with_held_bursts", { 47850000ULL, 47850000ULL }, 4096,
			heldBursts },
		{ "47850hz_reference_departs", { 47850000ULL, 47850000ULL, 47850000ULL },
			departingReferenceObservationFrames, clean, 0U, departingReferenceTick }
	};
	nlohmann::json summary {
		{ "scenario", "hardware_clock_drift" },
		{ "description", "Upload production and download callbacks follow each participant's physical audio clock" },
		{ "nominal_sample_rate_hz", SAMPLE_RATE },
		{ "frame_samples", SAMPLE_BUFFER_SIZE },
		{ "target", {
			{ "matched_clock_behavior", "A shared slow clock changes room cadence without causing playout starvation or overflow" },
			{ "mismatched_clock_behavior", "Keep queues and latency bounded; isolate unavoidable correction to the affected source" },
			{ "burst_behavior", "Never catch up with multiple room mixes in one nominal frame" },
			{ "holdover_behavior", "A departed reference must not stop or permanently overspeed the room" },
			{ "musical_lag_reference", "Lag and lead are expressed in receiver callback frames, not nominal source-production frames" }
		} },
		{ "results", nlohmann::json::array() }
	};

	for (const auto& testCase : cases) {
		const auto participantCount = testCase.participantSampleRatesMilliHz.size();
		const BoundaryScenario scenario {
			participantCount, BoundaryTopology::AllUnhealthy, testCase.network,
			0x4a616d6d65724e65ULL, testCase.participantSampleRatesMilliHz
		};
		const auto arrivals = buildClockDriftArrivals(scenario, testCase);
		const auto observationLastTick = testCase.observationFrames - 1U;
		const auto legacy = runModel(scenario, arrivals, true, observationLastTick);
		const auto cadence = runModel(scenario, arrivals, false, observationLastTick);
		const auto replay = runModel(scenario, arrivals, false, observationLastTick);

		EXPECT_EQ(cadence, replay) << testCase.name;
		EXPECT_EQ(legacy.sequenceErrors, 0U) << testCase.name;
		EXPECT_EQ(cadence.sequenceErrors, 0U) << testCase.name;
		ASSERT_EQ(legacy.receiverAudio.size(), participantCount);
		ASSERT_EQ(cadence.receiverAudio.size(), participantCount);
		if (testCase.name == "both_47850hz") {
			EXPECT_EQ(scheduledClockEvents(testCase.observationFrames, 47850000ULL), 4084U);
		}
		for (std::size_t participant = 0; participant < participantCount; ++participant) {
			const auto scheduledCallbacks = scheduledClockEvents(testCase.observationFrames,
				testCase.participantSampleRatesMilliHz[participant]);
			EXPECT_EQ(legacy.receiverAudio[participant].callbackFrames, scheduledCallbacks)
				<< testCase.name;
			EXPECT_EQ(cadence.receiverAudio[participant].callbackFrames, scheduledCallbacks)
				<< testCase.name;
			EXPECT_EQ(legacy.receiverAudio[participant].serverSignalMismatches, 0U)
				<< testCase.name;
			EXPECT_EQ(cadence.receiverAudio[participant].serverSignalMismatches, 0U)
				<< testCase.name;
		}

		const bool matchedCleanClocks = testCase.network.name == "clean"
			&& !testCase.departingParticipant
			&& std::adjacent_find(testCase.participantSampleRatesMilliHz.begin(),
				testCase.participantSampleRatesMilliHz.end(), std::not_equal_to<>())
				== testCase.participantSampleRatesMilliHz.end();
		if (matchedCleanClocks) {
			EXPECT_EQ(legacy.audioVerdict, "sample_coherent") << testCase.name;
			EXPECT_EQ(cadence.audioVerdict, "sample_coherent") << testCase.name;
			for (const auto& receiver : cadence.receiverAudio) {
				EXPECT_EQ(receiver.playoutUnderruns, 0U) << testCase.name;
				EXPECT_EQ(receiver.discardedFrames, 0U) << testCase.name;
			}
			EXPECT_LE(cadence.maximumMixBurst, 1U) << testCase.name;
		}
		const auto continuingReceiversStable = [&cadence, &testCase, participantCount] {
			for (std::size_t participant = 0; participant < participantCount; ++participant) {
				if (testCase.departingParticipant == participant) {
					continue;
				}
				const auto& receiver = cadence.receiverAudio[participant];
				if (receiver.playoutUnderruns > 0U || receiver.discardedFrames > 0U
					|| receiver.receiveQueueOverruns > 0U) {
					return false;
				}
			}
			return true;
		}();
		if (testCase.departingParticipant) {
			EXPECT_GT(cadence.disconnects, 0U) << testCase.name;
			EXPECT_TRUE(continuingReceiversStable) << testCase.name;
		}

		nlohmann::json rates = nlohmann::json::array();
		for (const auto rate : testCase.participantSampleRatesMilliHz) {
			rates.push_back({
				{ "sample_rate_hz", static_cast<double>(rate) / 1000.0 },
				{ "offset_ppm", 1000000.0
					* (static_cast<double>(rate) - static_cast<double>(nominalSampleRateMilliHz))
					/ static_cast<double>(nominalSampleRateMilliHz) },
				{ "scheduled_callback_frames", scheduledClockEvents(
					testCase.observationFrames, rate) }
			});
		}
		auto row = nlohmann::json {
			{ "name", testCase.name },
			{ "participant_clocks", std::move(rates) },
			{ "observation_frames", testCase.observationFrames },
			{ "duration_seconds", static_cast<double>(testCase.observationFrames
				* SAMPLE_BUFFER_SIZE) / static_cast<double>(SAMPLE_RATE) },
			{ "network", {
				{ "jitter_frames", testCase.network.jitterFrames },
				{ "slot_hold_frames", testCase.network.slotHoldFrames },
				{ "duplicate_copies", testCase.network.duplicateCopies }
			} },
			{ "departing_participant", testCase.departingParticipant
				? nlohmann::json(*testCase.departingParticipant + 1U) : nlohmann::json(nullptr) },
			{ "departure_tick", testCase.departingParticipant
				? nlohmann::json(testCase.departureTick) : nlohmann::json(nullptr) },
			{ "legacy_all_ready", modelJson(legacy, testCase.observationFrames) },
			{ "cadence_donor", modelJson(cadence, testCase.observationFrames) },
			{ "matched_clean_clock_target_met", matchedCleanClocks
				? nlohmann::json(cadence.audioVerdict == "sample_coherent"
					&& cadence.maximumMixBurst <= 1U) : nlohmann::json(nullptr) },
			{ "continuing_receivers_stable_after_departure", testCase.departingParticipant
				? nlohmann::json(continuingReceiversStable) : nlohmann::json(nullptr) }
		};
		summary["results"].push_back(std::move(row));
	}

	const juce::File artifactDirectory(JAMMERNETZ_TEST_ARTIFACT_DIR);
	const auto scenarioDirectory = artifactDirectory.getChildFile("mixer-cadence-boundary");
	jammernetz::test::writeJsonArtifact(scenarioDirectory.getChildFile("clock-drift-summary.json"),
		summary, "mixer clock drift");
	RecordProperty("clock_drift_summary", summary.dump());
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
		{ "audio_verdict_order", { "mixer_render_error", "no_playback", "transport_corruption",
			"playout_starvation", "playout_overflow", "playout_rate_instability", "mixed_audio_degradation",
			"isolated_source_damage", "sample_coherent" } },
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
				for (const auto& receiver : legacy.receiverAudio) {
					EXPECT_EQ(receiver.serverSignalMismatches, 0U);
				}
				for (const auto& receiver : cadence.receiverAudio) {
					EXPECT_EQ(receiver.serverSignalMismatches, 0U);
				}
				if (severity.name == "clean") {
					EXPECT_EQ(cadence.mixes, legacy.mixes);
					EXPECT_EQ(cadence.longestMixGapFrames, legacy.longestMixGapFrames);
					EXPECT_EQ(cadence.skewedMixes, legacy.skewedMixes);
					EXPECT_EQ(cadence.fastForwardedPackets, legacy.fastForwardedPackets);
					EXPECT_EQ(legacy.audioVerdict, "sample_coherent");
					EXPECT_EQ(cadence.audioVerdict, "sample_coherent");
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
				if (legacy.audioVerdict != "sample_coherent") {
					setFirst(frontier.legacyAudioDegradation, severity.name);
				}
				if (cadence.audioVerdict != "sample_coherent") {
					setFirst(frontier.cadenceAudioDegradation, severity.name);
				}
				if (audioVerdictScore(cadence.audioVerdict) <= 1) {
					setFirst(frontier.cadencePlayoutInstability, severity.name);
				}
				bool healthyAudioDamaged = false;
				for (const auto& receiver : cadence.receiverAudio) {
					healthyAudioDamaged = healthyAudioDamaged
						|| receiver.healthyAudioPreservedFrames
							< receiver.healthyAudioEligibleFrames;
				}
				if (healthyAudioDamaged) {
					setFirst(frontier.cadenceHealthyAudioDamage, severity.name);
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
					{ "legacy_has_lower_burst_peak", legacy.maximumMixBurst < cadence.maximumMixBurst },
					{ "legacy_audio_verdict", legacy.audioVerdict },
					{ "cadence_audio_verdict", cadence.audioVerdict },
					{ "audio_verdict_score_delta", audioVerdictScore(cadence.audioVerdict)
						- audioVerdictScore(legacy.audioVerdict) }
				};
				summary["results"].push_back(std::move(row));
			}
			summary["frontiers"].push_back(frontierJson(participants, topology, frontier));
		}
	}

	const juce::File artifactDirectory(JAMMERNETZ_TEST_ARTIFACT_DIR);
	const auto scenarioDirectory = artifactDirectory.getChildFile("mixer-cadence-boundary");
	jammernetz::test::writeJsonArtifact(scenarioDirectory.getChildFile("summary.json"),
		summary, "mixer cadence boundary");
	RecordProperty("characterization_summary", summary.dump());
}

} // namespace
