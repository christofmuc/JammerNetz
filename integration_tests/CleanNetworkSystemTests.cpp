/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "BuffersConfig.h"
#include "DeterministicAudioTestSupport.h"
#include "JammerNetzAudioEngine.h"
#include "ServerMixerCore.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using jammernetz::test::CapturedAudio;
using jammernetz::test::SampleIndex;
using jammernetz::test::ScenarioScheduler;
using jammernetz::test::SignalOracle;
using jammernetz::test::SyntheticAudioSource;

constexpr float comparisonEpsilon = 1.0e-5f;
constexpr double balancedGain = 0.7071067811865475244;

struct CapturedPacket {
	std::shared_ptr<JammerNetzAudioData> packet;
	std::uint64_t frameIndex { 0 };
};

class HeadlessPacketSink final : public AudioPacketSink {
public:
	bool sendData(const JammerNetzChannelSetup& channelSetup,
		std::shared_ptr<AudioBuffer<float>> audioBuffer,
		ControlData controls) override
	{
		auto retainedAudio = std::make_shared<AudioBuffer<float>>();
		*retainedAudio = *audioBuffer;
		const auto frameIndex = nextFrameIndex_++;
		const double timestamp = 1000.0 * static_cast<double>(frameIndex * SAMPLE_BUFFER_SIZE)
			/ static_cast<double>(SAMPLE_RATE);
		packets_.push_back({ std::make_shared<JammerNetzAudioData>(
			nextMessageCounter_++, timestamp, channelSetup, SAMPLE_RATE, controls.bpm,
			controls.midiSignal.value_or(MidiSignal_None), std::move(retainedAudio), nullptr), frameIndex });
		return true;
	}

	bool hasPacket() const noexcept { return !packets_.empty(); }

	CapturedPacket pop()
	{
		auto result = std::move(packets_.front());
		packets_.pop_front();
		return result;
	}

private:
	std::deque<CapturedPacket> packets_;
	std::uint64_t nextFrameIndex_ { 0 };
	uint64 nextMessageCounter_ { 10 };
};

JammerNetzChannelSetup makeSetup(const bool suppressServerEcho,
	const std::vector<JammerNetzChannelTarget>& targets,
	const std::string& name)
{
	JammerNetzChannelSetup setup(suppressServerEcho);
	for (size_t channelIndex = 0; channelIndex < targets.size(); ++channelIndex) {
		JammerNetzSingleChannelSetup channel(static_cast<uint8>(targets[channelIndex]));
		channel.name = name + "-" + std::to_string(channelIndex);
		channel.volume = 1.0f;
		setup.channels.push_back(std::move(channel));
	}
	return setup;
}

void routeInto(AudioBuffer<float>& destination,
	const AudioBuffer<float>& source,
	const JammerNetzChannelSetup& setup,
	const bool isForSender,
	const float gain)
{
	const bool wantsEcho = !setup.isLocalMonitoringDontSendEcho;
	for (int channelIndex = 0; channelIndex < source.getNumChannels(); ++channelIndex) {
		const auto& channel = setup.channels[static_cast<size_t>(channelIndex)];
		const float volume = gain * channel.volume;
		switch (channel.target) {
		case Mute:
			break;
		case SendLeft:
			if (isForSender) {
				break;
			}
			[[fallthrough]];
		case Left:
			if (!isForSender || wantsEcho) {
				destination.addFrom(0, 0, source, channelIndex, 0, source.getNumSamples(), volume);
			}
			break;
		case SendRight:
			if (isForSender) {
				break;
			}
			[[fallthrough]];
		case Right:
			if (!isForSender || wantsEcho) {
				destination.addFrom(1, 0, source, channelIndex, 0, source.getNumSamples(), volume);
			}
			break;
		case SendMono:
			if (isForSender) {
				break;
			}
			[[fallthrough]];
		case Mono:
			if (!isForSender || wantsEcho) {
				destination.addFrom(0, 0, source, channelIndex, 0, source.getNumSamples(), volume);
				destination.addFrom(1, 0, source, channelIndex, 0, source.getNumSamples(), volume);
			}
			break;
		}
	}
}

void routeLocalMonitoringInto(AudioBuffer<float>& destination,
	const AudioBuffer<float>& source,
	const JammerNetzChannelSetup& setup,
	const float gain)
{
	for (int channelIndex = 0; channelIndex < source.getNumChannels(); ++channelIndex) {
		const auto& channel = setup.channels[static_cast<size_t>(channelIndex)];
		const float volume = gain * channel.volume;
		switch (channel.target) {
		case Mute:
		case SendLeft:
		case SendRight:
		case SendMono:
			break;
		case Left:
			destination.addFrom(0, 0, source, channelIndex, 0, source.getNumSamples(), volume);
			break;
		case Right:
			destination.addFrom(1, 0, source, channelIndex, 0, source.getNumSamples(), volume);
			break;
		case Mono:
			destination.addFrom(0, 0, source, channelIndex, 0, source.getNumSamples(), volume);
			destination.addFrom(1, 0, source, channelIndex, 0, source.getNumSamples(), volume);
			break;
		}
	}
}

class HeadlessClient {
public:
	HeadlessClient(std::string identifier,
		const std::uint32_t sourceId,
		JammerNetzChannelSetup setup,
		std::vector<int> callbackSchedule,
		const bool localMonitoring,
		const uint64_t minimumPlayoutFrames)
		: identifier_(std::move(identifier))
		, setup_(std::move(setup))
		, callbackSchedule_(std::move(callbackSchedule))
		, sink_(std::make_shared<HeadlessPacketSink>())
		, engine_(session_, juce::File(), sink_)
		, source_(sourceId, static_cast<int>(setup_.channels.size()))
		, minimumPlayoutFrames_(minimumPlayoutFrames)
		, localMonitoring_(localMonitoring)
	{
		if (callbackSchedule_.empty()) {
			throw std::invalid_argument("A headless client needs a callback schedule");
		}
		engine_.setChannelSetup(setup_);
		engine_.setLocalMonitoring(localMonitoring_);
		engine_.setMasterVolume(1.0);
		engine_.setMonitorBalance(0.0);
		engine_.setPlayoutBufferRange(minimumPlayoutFrames_, 128);
		engine_.prepare(SAMPLE_RATE, 1024);
	}

	const std::string& identifier() const noexcept { return identifier_; }
	const CapturedAudio& observedOutput() const noexcept { return observedOutput_; }
	std::deque<CapturedPacket>& pendingPackets() noexcept { return pendingPackets_; }
	std::size_t maximumPendingPackets() const noexcept { return maximumPendingPackets_; }
	std::size_t maximumRemoteBacklogFrames() const noexcept { return maximumRemoteBacklogFrames_; }
	int nextScheduledBlockSize() noexcept
	{
		const int result = callbackSchedule_[callbackScheduleIndex_ % callbackSchedule_.size()];
		++callbackScheduleIndex_;
		return result;
	}

	void processCallback(const int sampleCount)
	{
		auto input = source_.render(sampleCount);
		AudioBuffer<float> output(2, sampleCount);
		output.clear();
		const float* inputs[JAMMERNETZ_MAX_AUDIO_CHANNELS] {};
		for (int channel = 0; channel < input.getNumChannels(); ++channel) {
			inputs[channel] = input.getReadPointer(channel);
		}
		float* outputs[] { output.getWritePointer(0), output.getWritePointer(1) };

		AudioBuffer<float> expected(2, sampleCount);
		expected.clear();
		if (localMonitoring_) {
			routeLocalMonitoringInto(expected, input, setup_, static_cast<float>(balancedGain));
		}
		if (playbackExpected_) {
			const auto deliveredSamples = expectedRemote_[0].size();
			const auto availableSamples = deliveredSamples - expectedRemoteRead_;
			if (availableSamples >= static_cast<size_t>(sampleCount)) {
				for (int channel = 0; channel < 2; ++channel) {
					for (int sample = 0; sample < sampleCount; ++sample) {
						expected.addSample(channel, sample,
							expectedRemote_[static_cast<size_t>(channel)][expectedRemoteRead_ + static_cast<size_t>(sample)]
								* static_cast<float>(balancedGain));
					}
				}
				expectedRemoteRead_ += static_cast<size_t>(sampleCount);
			} else {
				++expectedUnderruns_;
			}
		}

		engine_.process(inputs, input.getNumChannels(), outputs, 2, sampleCount);
		expectedOutput_.append(expected);
		observedOutput_.append(output);
		while (engine_.processNextOutgoingPacket()) {}
		while (sink_->hasPacket()) {
			pendingPackets_.push_back(sink_->pop());
		}
		maximumPendingPackets_ = std::max(maximumPendingPackets_, pendingPackets_.size());
	}

	void deliver(const OutgoingPackage& actual, const AudioBuffer<float>& expectedRemote)
	{
		for (int channel = 0; channel < 2; ++channel) {
			const float* begin = expectedRemote.getReadPointer(channel);
			expectedRemote_[static_cast<size_t>(channel)].insert(
				expectedRemote_[static_cast<size_t>(channel)].end(), begin, begin + expectedRemote.getNumSamples());
		}
		++deliveredFrames_;
		playbackExpected_ = playbackExpected_ || deliveredFrames_ >= minimumPlayoutFrames_;
		engine_.enqueueRemoteAudio(std::make_shared<JammerNetzAudioData>(actual.audioBlock, nullptr));
		engine_.processNextIncomingPacket();
		while (engine_.processNextIncomingPacket()) {}
		const auto backlogSamples = expectedRemote_[0].size() - expectedRemoteRead_;
		maximumRemoteBacklogFrames_ = std::max(maximumRemoteBacklogFrames_,
			(backlogSamples + SAMPLE_BUFFER_SIZE - 1U) / SAMPLE_BUFFER_SIZE);
	}

	std::vector<jammernetz::test::DiscrepancySpan> discrepancies() const
	{
		return SignalOracle::compare(expectedOutput_, observedOutput_, comparisonEpsilon);
	}

	RealtimeWorkerStats workerStats() const { return engine_.getRealtimeWorkerStats(); }
	PlayoutQualityInfo playoutStats() { return engine_.getPlayoutQualityInfo(); }
	std::uint64_t expectedUnderruns() const noexcept { return expectedUnderruns_; }

private:
	std::string identifier_;
	JammerNetzChannelSetup setup_;
	std::vector<int> callbackSchedule_;
	std::size_t callbackScheduleIndex_ { 0 };
	JammerNetzSession session_;
	std::shared_ptr<HeadlessPacketSink> sink_;
	JammerNetzAudioEngine engine_;
	SyntheticAudioSource source_;
	uint64_t minimumPlayoutFrames_;
	bool localMonitoring_;
	std::deque<CapturedPacket> pendingPackets_;
	std::array<std::vector<float>, 2> expectedRemote_;
	std::size_t expectedRemoteRead_ { 0 };
	std::uint64_t deliveredFrames_ { 0 };
	bool playbackExpected_ { false };
	std::uint64_t expectedUnderruns_ { 0 };
	std::size_t maximumPendingPackets_ { 0 };
	std::size_t maximumRemoteBacklogFrames_ { 0 };
	CapturedAudio expectedOutput_;
	CapturedAudio observedOutput_;
};

AudioBuffer<float> expectedServerMix(const ServerInputPackets& inputs, const std::string& receiver)
{
	AudioBuffer<float> expected(2, SAMPLE_BUFFER_SIZE);
	expected.clear();
	for (const auto& input : inputs) {
		routeInto(expected, *input.second->audioBuffer(), input.second->channelSetup(),
			input.first == receiver, 1.0f);
	}
	return expected;
}

float maximumError(const AudioBuffer<float>& expected, const AudioBuffer<float>& observed)
{
	float result = 0.0f;
	for (int channel = 0; channel < expected.getNumChannels(); ++channel) {
		for (int sample = 0; sample < expected.getNumSamples(); ++sample) {
			result = std::max(result,
				std::fabs(expected.getSample(channel, sample) - observed.getSample(channel, sample)));
		}
	}
	return result;
}

std::uint64_t audioHash(const CapturedAudio& audio)
{
	std::uint64_t hash = 1469598103934665603ULL;
	for (const auto& channel : audio.channels()) {
		for (const float sample : channel) {
			hash ^= std::bit_cast<std::uint32_t>(sample);
			hash *= 1099511628211ULL;
		}
	}
	return hash;
}

struct ScenarioSummary {
	std::uint64_t mixCount { 0 };
	std::uint64_t allClientMixCount { 0 };
	std::size_t traceEvents { 0 };
	std::vector<std::uint64_t> outputHashes;
	std::vector<std::size_t> outputSamples;
	std::vector<std::size_t> maximumPendingPackets;
	std::vector<std::size_t> maximumRemoteBacklogFrames;
};

class CleanNetworkScenario {
public:
	explicit CleanNetworkScenario(const std::uint64_t seed)
		: scheduler_(seed)
		, mixer_(JammerNetzChannelSetup(false, {
			JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Left),
			JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Right)
		}))
	{
	}

	void addClient(std::unique_ptr<HeadlessClient> client, const SampleIndex activationSample)
	{
		clients_.push_back(std::move(client));
		activationSamples_.push_back(activationSample);
		active_.push_back(false);
		lastFrameIndex_.push_back(std::numeric_limits<std::uint64_t>::max());
		allActiveBaseFrame_.push_back(0);
	}

	ScenarioSummary run(const SampleIndex endSample)
	{
		endSample_ = endSample;
		for (size_t clientIndex = 0; clientIndex < clients_.size(); ++clientIndex) {
			scheduler_.scheduleAt(activationSamples_[clientIndex], "activate", [this, clientIndex]() {
				active_[clientIndex] = true;
				scheduleCallback(clientIndex, scheduler_.now());
			});
		}
		scheduler_.runUntilIdle(100000);

		ScenarioSummary summary;
		summary.mixCount = mixCount_;
		summary.allClientMixCount = allClientMixCount_;
		summary.traceEvents = scheduler_.trace().events().size();
		for (auto& client : clients_) {
			const auto discrepancies = client->discrepancies();
			if (!discrepancies.empty()) {
				const auto& first = discrepancies.front();
				ADD_FAILURE() << client->identifier()
					<< " first discrepancy: channel=" << first.channel
					<< ", samples=" << first.firstSample << ".." << first.lastSample
					<< ", expected=" << first.expectedAtFirst
					<< ", observed=" << first.observedAtFirst
					<< ", maximum error=" << first.maximumAbsoluteError
					<< ", spans=" << discrepancies.size();
			}
			const auto workerStats = client->workerStats();
			EXPECT_EQ(workerStats.transmitFramesDropped, 0U) << client->identifier();
			EXPECT_EQ(workerStats.receiveFramesDiscarded, 0U) << client->identifier();
			EXPECT_EQ(workerStats.receiveQueueOverruns, 0U) << client->identifier();
			EXPECT_EQ(client->playoutStats().playUnderruns_, 0U) << client->identifier();
			EXPECT_EQ(client->playoutStats().playUnderruns_, client->expectedUnderruns()) << client->identifier();
			summary.outputHashes.push_back(audioHash(client->observedOutput()));
			summary.outputSamples.push_back(client->observedOutput().sampleCount());
			summary.maximumPendingPackets.push_back(client->maximumPendingPackets());
			summary.maximumRemoteBacklogFrames.push_back(client->maximumRemoteBacklogFrames());
		}
		return summary;
	}

private:
	void scheduleCallback(const size_t clientIndex, const SampleIndex at)
	{
		if (at >= endSample_) {
			return;
		}
		scheduler_.scheduleAt(at, "audio_callback", [this, clientIndex]() {
			const auto now = scheduler_.now();
			const int requested = clients_[clientIndex]->nextScheduledBlockSize();
			const auto remaining = endSample_ - now;
			const int sampleCount = static_cast<int>(std::min<SampleIndex>(static_cast<SampleIndex>(requested), remaining));
			clients_[clientIndex]->processCallback(sampleCount);
			mixWhileReady();
			scheduleCallback(clientIndex, now + static_cast<SampleIndex>(sampleCount));
		});
	}

	void mixWhileReady()
	{
		while (true) {
			ServerInputPackets inputs;
			std::vector<std::uint64_t> frameIndices(clients_.size(), 0);
			for (size_t clientIndex = 0; clientIndex < clients_.size(); ++clientIndex) {
				if (!active_[clientIndex]) {
					continue;
				}
				if (clients_[clientIndex]->pendingPackets().empty()) {
					return;
				}
				const auto& next = clients_[clientIndex]->pendingPackets().front();
				inputs.emplace(clients_[clientIndex]->identifier(), next.packet);
				frameIndices[clientIndex] = next.frameIndex;
			}
			if (inputs.empty()) {
				return;
			}

			const bool allClientsActive = std::all_of(active_.begin(), active_.end(), [](const bool active) { return active; });
			if (allClientsActive && !allActiveBaselineSet_) {
				allActiveBaseFrame_ = frameIndices;
				allActiveBaselineSet_ = true;
			}
			for (size_t clientIndex = 0; clientIndex < clients_.size(); ++clientIndex) {
				if (!active_[clientIndex]) {
					continue;
				}
				const auto previous = lastFrameIndex_[clientIndex];
				if (previous != std::numeric_limits<std::uint64_t>::max()
					&& frameIndices[clientIndex] != previous + 1U) {
					throw std::runtime_error("Outgoing source provenance is not continuous");
				}
				if (allClientsActive
					&& frameIndices[clientIndex] - allActiveBaseFrame_[clientIndex] != allClientMixCount_) {
					throw std::runtime_error("Clean mix inputs have relative frame skew");
				}
				lastFrameIndex_[clientIndex] = frameIndices[clientIndex];
				clients_[clientIndex]->pendingPackets().pop_front();
			}

			const auto result = mixer_.mix(inputs);
			if (!result.diagnostics.empty() || result.outgoing.size() != inputs.size()
				|| result.serverTime != (mixCount_ + 1U) * SAMPLE_BUFFER_SIZE) {
				throw std::runtime_error("Server mixer violated a clean-network invariant");
			}
			for (const auto& outgoing : result.outgoing) {
				const auto foundClient = std::find_if(clients_.begin(), clients_.end(), [&outgoing](const auto& client) {
					return client->identifier() == outgoing.targetAddress;
				});
				if (foundClient == clients_.end()) {
					throw std::runtime_error("Server output has an unknown target address");
				}
				auto expected = expectedServerMix(inputs, outgoing.targetAddress);
				if (maximumError(expected, *outgoing.audioBlock.audioBuffer) > comparisonEpsilon) {
					throw std::runtime_error("Server output differs from the independent routing oracle");
				}
				(*foundClient)->deliver(outgoing, expected);
			}
			++mixCount_;
			if (allClientsActive) {
				++allClientMixCount_;
			}
		}
	}

	ScenarioScheduler scheduler_;
	ServerMixerCore mixer_;
	std::vector<std::unique_ptr<HeadlessClient>> clients_;
	std::vector<SampleIndex> activationSamples_;
	std::vector<bool> active_;
	std::vector<std::uint64_t> lastFrameIndex_;
	std::vector<std::uint64_t> allActiveBaseFrame_;
	bool allActiveBaselineSet_ { false };
	SampleIndex endSample_ { 0 };
	std::uint64_t mixCount_ { 0 };
	std::uint64_t allClientMixCount_ { 0 };
};

ScenarioSummary runTwoClientScenario()
{
	CleanNetworkScenario scenario(0x2001U);
	scenario.addClient(std::make_unique<HeadlessClient>("client-a", 1,
		makeSetup(true, { Left }, "left"), std::vector<int> { SAMPLE_BUFFER_SIZE }, true, 1), 0);
	scenario.addClient(std::make_unique<HeadlessClient>("client-b", 2,
		makeSetup(true, { Right }, "right"), std::vector<int> { SAMPLE_BUFFER_SIZE }, true, 1), 0);
	return scenario.run(1001U * SAMPLE_BUFFER_SIZE);
}

ScenarioSummary runFourClientScenario(const std::uint64_t seed)
{
	constexpr SampleIndex thirdActivation = 4096;
	constexpr SampleIndex fourthActivation = 8192;
	constexpr SampleIndex tenSeconds = 10U * SAMPLE_RATE;
	CleanNetworkScenario scenario(seed);
	scenario.addClient(std::make_unique<HeadlessClient>("client-a", 11,
		makeSetup(true, { Left, Right }, "stereo"), std::vector<int> { 32, 64, 128, 256 }, true, 32), 0);
	scenario.addClient(std::make_unique<HeadlessClient>("client-b", 12,
		makeSetup(true, { Mono }, "mono"), std::vector<int> { 64, 128, 512 }, true, 32), 0);
	scenario.addClient(std::make_unique<HeadlessClient>("client-c", 13,
		makeSetup(true, { SendLeft }, "send-left"), std::vector<int> { 128, 256, 1024, 32 }, true, 32), thirdActivation);
	scenario.addClient(std::make_unique<HeadlessClient>("client-d", 14,
		makeSetup(true, { SendMono }, "send-mono"), std::vector<int> { 256, 512, 64, 1024 }, true, 32), fourthActivation);
	return scenario.run(fourthActivation + tenSeconds);
}

TEST(CleanNetworkSystemTest, TwoClientReferenceMixesAtLeastOneThousandFrames)
{
	const auto result = runTwoClientScenario();
	EXPECT_GE(result.mixCount, 1000U);
	EXPECT_GE(result.allClientMixCount, 1000U);
	ASSERT_EQ(result.outputSamples.size(), 2U);
	EXPECT_EQ(result.outputSamples[0], 1001U * SAMPLE_BUFFER_SIZE);
	EXPECT_EQ(result.outputSamples[1], 1001U * SAMPLE_BUFFER_SIZE);
}

TEST(CleanNetworkSystemTest, FourClientVariableCallbacksAreSampleExactAndRepeatable)
{
	const auto first = runFourClientScenario(0x4004U);
	const auto replay = runFourClientScenario(0x4004U);
	EXPECT_GE(first.allClientMixCount, 10U * SAMPLE_RATE / SAMPLE_BUFFER_SIZE);
	EXPECT_EQ(first.mixCount, replay.mixCount);
	EXPECT_EQ(first.allClientMixCount, replay.allClientMixCount);
	EXPECT_EQ(first.traceEvents, replay.traceEvents);
	EXPECT_EQ(first.outputHashes, replay.outputHashes);
	EXPECT_EQ(first.outputSamples, replay.outputSamples);
	EXPECT_EQ(first.maximumPendingPackets, replay.maximumPendingPackets);
	EXPECT_EQ(first.maximumRemoteBacklogFrames, replay.maximumRemoteBacklogFrames);
	for (const auto maximumPending : first.maximumPendingPackets) {
		EXPECT_LE(maximumPending, 16U);
	}
	for (const auto maximumBacklog : first.maximumRemoteBacklogFrames) {
		EXPECT_LE(maximumBacklog, 48U);
	}
}

} // namespace
