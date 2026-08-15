/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "JammerNetzAudioEngine.h"
#include "BuffersConfig.h"
#include "DeterministicAudioTestSupport.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <memory>
#include <type_traits>
#include <vector>

namespace {

JammerNetzChannelSetup monoLocalSetup()
{
	JammerNetzChannelSetup setup(true);
	setup.channels.emplace_back(JammerNetzChannelTarget::Mono);
	return setup;
}

class CapturingAudioPacketSink final : public AudioPacketSink {
public:
	bool sendData(JammerNetzChannelSetup const& channelSetup,
		std::shared_ptr<AudioBuffer<float>> audioBuffer,
		ControlData controllers) override
	{
		const MidiSignal midiSignal = controllers.midiSignal.value_or(MidiSignal_None);
		packets.push_back(std::make_shared<JammerNetzAudioData>(
			nextMessageCounter++, nextTimestamp++, channelSetup, SAMPLE_RATE,
			controllers.bpm, midiSignal, std::move(audioBuffer), nullptr));
		return true;
	}

	std::vector<std::shared_ptr<JammerNetzAudioData>> packets;

private:
	uint64 nextMessageCounter { 10 };
	double nextTimestamp { 0.0 };
};

static_assert(std::is_base_of_v<AudioPacketSink, Client>);

TEST(JammerNetzSessionTest, ConstructionHasNoExternalSideEffects)
{
	JammerNetzSession session;
	EXPECT_FALSE(session.isAvailable());
	EXPECT_EQ(session.sender(), nullptr);
	EXPECT_EQ(session.receiver(), nullptr);
}

TEST(JammerNetzAudioEngineTest, ProcessesSyntheticBlocksWithoutAnAudioDevice)
{
	JammerNetzSession session;
	JammerNetzAudioEngine engine(session, juce::File());
	engine.setChannelSetup(monoLocalSetup());
	engine.setLocalMonitoring(true);
	engine.setMasterVolume(1.0);
	engine.setMonitorBalance(0.0);
	engine.prepare(48000.0, 512);

	for (const int blockSize : std::array<int, 3> { 32, 128, 256 }) {
		std::vector<float> input(static_cast<size_t>(blockSize), 1.0f);
		std::vector<float> left(static_cast<size_t>(blockSize), 0.0f);
		std::vector<float> right(static_cast<size_t>(blockSize), 0.0f);
		const float* inputs[] { input.data() };
		float* outputs[] { left.data(), right.data() };

		engine.process(inputs, 1, outputs, 2, blockSize);

		const float expectedGain = static_cast<float>(std::sqrt(0.5));
		EXPECT_NEAR(left.front(), expectedGain, 1.0e-5f);
		EXPECT_NEAR(right.front(), expectedGain, 1.0e-5f);
		EXPECT_NEAR(left.back(), expectedGain, 1.0e-5f);
		EXPECT_NEAR(right.back(), expectedGain, 1.0e-5f);
	}

	engine.release();
}

TEST(JammerNetzAudioEngineTest, ConfigurationChangesDoNotRequireReconstruction)
{
	JammerNetzSession session;
	JammerNetzAudioEngine engine(session, juce::File());
	engine.setChannelSetup(monoLocalSetup());
	engine.setLocalMonitoring(false);

	std::array<float, 32> input;
	std::array<float, 32> left;
	std::array<float, 32> right;
	input.fill(1.0f);
	left.fill(1.0f);
	right.fill(1.0f);
	const float* inputs[] { input.data() };
	float* outputs[] { left.data(), right.data() };

	engine.process(inputs, 1, outputs, 2, static_cast<int>(input.size()));
	EXPECT_FLOAT_EQ(left.front(), 0.0f);
	EXPECT_FLOAT_EQ(right.front(), 0.0f);

	engine.setLocalMonitoring(true);
	engine.setMasterVolume(0.5);
	engine.process(inputs, 1, outputs, 2, static_cast<int>(input.size()));
	const float expectedGain = static_cast<float>(0.5 * std::sqrt(0.5));
	EXPECT_NEAR(left.front(), expectedGain, 1.0e-5f);
	EXPECT_NEAR(right.front(), expectedGain, 1.0e-5f);
}

TEST(JammerNetzAudioEngineTest, SendsDeterministicPacketThroughInjectedSink)
{
	JammerNetzSession session;
	auto sink = std::make_shared<CapturingAudioPacketSink>();
	JammerNetzAudioEngine engine(session, juce::File(), sink);
	engine.setChannelSetup(monoLocalSetup());
	engine.setLocalMonitoring(false);
	engine.setClientBpm(123.0f);
	engine.setMidiSignalToSend(MidiSignal_Start);

	jammernetz::test::SyntheticAudioSource source(7, 1);
	for (const int blockSize : std::array<int, 2> { 32, SAMPLE_BUFFER_SIZE - 32 }) {
		auto input = source.render(blockSize);
		std::vector<float> left(static_cast<size_t>(blockSize));
		std::vector<float> right(static_cast<size_t>(blockSize));
		const float* inputs[] { input.getReadPointer(0) };
		float* outputs[] { left.data(), right.data() };
		engine.process(inputs, 1, outputs, 2, blockSize);
	}

	ASSERT_EQ(sink->packets.size(), 1U);
	const auto& packet = *sink->packets.front();
	EXPECT_EQ(packet.messageCounter(), 10U);
	EXPECT_FLOAT_EQ(packet.bpm(), 123.0f);
	EXPECT_EQ(packet.midiSignal(), MidiSignal_Start);
	EXPECT_TRUE(packet.channelSetup().isLocalMonitoringDontSendEcho);
	ASSERT_EQ(packet.audioBuffer()->getNumChannels(), 1);
	ASSERT_EQ(packet.audioBuffer()->getNumSamples(), SAMPLE_BUFFER_SIZE);
	for (int sample = 0; sample < SAMPLE_BUFFER_SIZE; ++sample) {
		EXPECT_FLOAT_EQ(packet.audioBuffer()->getSample(0, sample),
			jammernetz::test::SyntheticAudioSource::valueAt(7, 0, static_cast<jammernetz::test::SampleIndex>(sample)));
	}
}

TEST(JammerNetzAudioEngineTest, MixesASimulatedRemoteFrame)
{
	JammerNetzSession session;
	JammerNetzAudioEngine engine(session, juce::File());
	engine.setPlayoutBufferRange(1, 4);
	engine.setLocalMonitoring(false);

	auto remoteAudio = std::make_shared<juce::AudioBuffer<float>>(2, SAMPLE_BUFFER_SIZE);
	for (int channel = 0; channel < remoteAudio->getNumChannels(); ++channel) {
		for (int sample = 0; sample < remoteAudio->getNumSamples(); ++sample) {
			remoteAudio->setSample(channel, sample, 0.25f);
		}
	}
	JammerNetzChannelSetup remoteSetup(false, {
		JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Left),
		JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Right)
	});
	engine.enqueueRemoteAudio(std::make_shared<JammerNetzAudioData>(
		1, juce::Time::getMillisecondCounterHiRes(), remoteSetup, SAMPLE_RATE, 120.0f, MidiSignal_None, remoteAudio, nullptr));

	std::array<float, SAMPLE_BUFFER_SIZE> left {};
	std::array<float, SAMPLE_BUFFER_SIZE> right {};
	float unusedInput = 0.0f;
	const float* inputs[] { &unusedInput };
	float* outputs[] { left.data(), right.data() };
	engine.process(inputs, 0, outputs, 2, SAMPLE_BUFFER_SIZE);

	const float expectedGain = static_cast<float>(0.25 * std::sqrt(0.5));
	EXPECT_NEAR(left.front(), expectedGain, 1.0e-5f);
	EXPECT_NEAR(right.front(), expectedGain, 1.0e-5f);
	const auto serverBpm = engine.takeServerBpmUpdate();
	ASSERT_TRUE(serverBpm.has_value());
	EXPECT_FLOAT_EQ(*serverBpm, 120.0f);
}

} // namespace
