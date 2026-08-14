/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "JammerNetzAudioEngine.h"
#include "BuffersConfig.h"
#include "BoundedSpscQueue.h"
#include "PacketStreamQueue.h"

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
	engine.prepare(48000.0, 1024);

	for (const int blockSize : std::array<int, 6> { 32, 64, 128, 256, 512, 1024 }) {
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
	const auto realtimeStats = engine.getRealtimeWorkerStats();
	EXPECT_EQ(realtimeStats.callbackCount, 6u);
	EXPECT_GT(realtimeStats.maximumCallbackNanoseconds, 0u);

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
	engine.start();
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
	for (int attempt = 0; attempt < 100 && left.front() == 0.0f; ++attempt) {
		engine.process(inputs, 0, outputs, 2, SAMPLE_BUFFER_SIZE);
		juce::Thread::sleep(2);
	}

	const float expectedGain = static_cast<float>(0.25 * std::sqrt(0.5));
	EXPECT_NEAR(left.front(), expectedGain, 1.0e-5f);
	EXPECT_NEAR(right.front(), expectedGain, 1.0e-5f);
	engine.shutdown();
}

TEST(BoundedSpscQueueTest, RejectsWritesWhenFullAndPreservesOrder)
{
	BoundedSpscQueue<int> queue(2);
	EXPECT_TRUE(queue.tryWrite([](int& value) { value = 10; }));
	EXPECT_TRUE(queue.tryWrite([](int& value) { value = 20; }));
	EXPECT_FALSE(queue.tryWrite([](int& value) { value = 30; }));
	int value = 0;
	EXPECT_TRUE(queue.tryRead([&](int& queued) { value = queued; }));
	EXPECT_EQ(value, 10);
	EXPECT_TRUE(queue.tryRead([&](int& queued) { value = queued; }));
	EXPECT_EQ(value, 20);
	EXPECT_FALSE(queue.tryRead([&](int& queued) { value = queued; }));
}

TEST(PacketStreamQueueTest, ResetAcceptsANewPacketSequence)
{
	PacketStreamQueue queue("test");
	auto audio = std::make_shared<juce::AudioBuffer<float>>(2, SAMPLE_BUFFER_SIZE);
	JammerNetzChannelSetup setup(false, {
		JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Left),
		JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Right)
	});
	const auto makePacket = [&](uint64 counter) {
		return std::make_shared<JammerNetzAudioData>(
			counter, 0.0, setup, SAMPLE_RATE, 120.0f, MidiSignal_None, audio, nullptr);
	};
	std::shared_ptr<JammerNetzAudioData> popped;
	bool fillIn = false;

	ASSERT_TRUE(queue.push(makePacket(42)));
	ASSERT_TRUE(queue.try_pop(popped, fillIn));
	EXPECT_EQ(popped->messageCounter(), 42u);
	queue.reset();
	ASSERT_TRUE(queue.push(makePacket(1)));
	ASSERT_TRUE(queue.try_pop(popped, fillIn));
	EXPECT_EQ(popped->messageCounter(), 1u);
	EXPECT_FALSE(fillIn);
}

TEST(JammerNetzAudioEngineTest, DropsFramesInsteadOfBlockingWhenTransmitWorkerIsStalled)
{
	JammerNetzSession session;
	JammerNetzAudioEngine engine(session, juce::File());
	engine.setChannelSetup(monoLocalSetup());
	std::array<float, SAMPLE_BUFFER_SIZE> input {};
	std::array<float, SAMPLE_BUFFER_SIZE> left {};
	std::array<float, SAMPLE_BUFFER_SIZE> right {};
	const float* inputs[] { input.data() };
	float* outputs[] { left.data(), right.data() };
	for (int block = 0; block < 100; ++block) {
		engine.process(inputs, 1, outputs, 2, SAMPLE_BUFFER_SIZE);
	}
	const auto stats = engine.getRealtimeWorkerStats();
	EXPECT_EQ(stats.transmitFramesQueued, 64u);
	EXPECT_GT(stats.transmitFramesDropped, 0u);
}

TEST(JammerNetzAudioEngineTest, BoundsReceiveBurstsBeforeTheWorkerStarts)
{
	JammerNetzSession session;
	JammerNetzAudioEngine engine(session, juce::File());
	auto audio = std::make_shared<juce::AudioBuffer<float>>(2, SAMPLE_BUFFER_SIZE);
	JammerNetzChannelSetup setup(false, {
		JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Left),
		JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Right)
	});
	auto packet = std::make_shared<JammerNetzAudioData>(
		1, 0.0, setup, SAMPLE_RATE, 120.0f, MidiSignal_None, audio, nullptr);

	for (int frame = 0; frame < 600; ++frame) {
		engine.enqueueRemoteAudio(packet);
	}

	const auto stats = engine.getRealtimeWorkerStats();
	EXPECT_EQ(stats.receiveQueueOverruns, 88u);
	EXPECT_EQ(stats.receiveFramesDiscarded, 88u);
}

} // namespace
