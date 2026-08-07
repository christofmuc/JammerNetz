/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "JammerNetzAudioEngine.h"
#include "BuffersConfig.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <vector>

namespace {

JammerNetzChannelSetup monoLocalSetup()
{
	JammerNetzChannelSetup setup(true);
	setup.channels.emplace_back(JammerNetzChannelTarget::Mono);
	return setup;
}

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
}

} // namespace
