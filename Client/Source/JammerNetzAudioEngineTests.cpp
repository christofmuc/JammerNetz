/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "JammerNetzAudioEngine.h"
#include "AudioReceiveWorker.h"
#include "BuffersConfig.h"
#include "DeterministicAudioTestSupport.h"
#include "BoundedSpscQueue.h"
#include "RingBuffer.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <memory>
#include <type_traits>
#include <vector>

namespace {

class CapturingOutputTap final : public AudioOutputTap {
public:
	void prepare(double sampleRate, int maximumBlockSize) override
	{
		preparedSampleRate = sampleRate;
		preparedBlockSize = maximumBlockSize;
	}

	void release() override { ++releaseCalls; }

	bool enqueue(const float* const* channels, int numChannels, int numSamples) noexcept override
	{
		++enqueueCalls;
		if (channels != nullptr && numChannels >= 2 && numSamples > 0) {
			left = channels[0][0];
			right = channels[1][0];
		}
		return true;
	}

	double preparedSampleRate { 0.0 };
	int preparedBlockSize { 0 };
	int releaseCalls { 0 };
	int enqueueCalls { 0 };
	float left { 0.0f };
	float right { 0.0f };
};

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
		auto capturedAudio = std::make_shared<AudioBuffer<float>>();
		*capturedAudio = *audioBuffer;
		packets.push_back(std::make_shared<JammerNetzAudioData>(
			nextMessageCounter++, nextTimestamp++, channelSetup, SAMPLE_RATE,
			controllers.bpm, midiSignal, std::move(capturedAudio), nullptr));
		return true;
	}

	std::vector<std::shared_ptr<JammerNetzAudioData>> packets;

private:
	uint64 nextMessageCounter { 10 };
	double nextTimestamp { 0.0 };
};

static_assert(std::is_base_of_v<AudioPacketSink, Client>);

TEST(RingBufferTest, ReadsFromTheFifoStartAfterWrapping)
{
	RingBuffer ringBuffer(1, 8);
	const std::array<float, 6> firstWrite { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f };
	const float* firstWriteChannels[] { firstWrite.data() };
	ringBuffer.write(firstWriteChannels, 1, static_cast<int>(firstWrite.size()));

	std::array<float, 5> discarded {};
	float* discardedChannels[] { discarded.data() };
	ringBuffer.read(discardedChannels, 1, static_cast<int>(discarded.size()));

	const std::array<float, 5> secondWrite { 6.0f, 7.0f, 8.0f, 9.0f, 10.0f };
	const float* secondWriteChannels[] { secondWrite.data() };
	ringBuffer.write(secondWriteChannels, 1, static_cast<int>(secondWrite.size()));

	std::array<float, 6> observed {};
	float* observedChannels[] { observed.data() };
	ringBuffer.read(observedChannels, 1, static_cast<int>(observed.size()));
	EXPECT_EQ(observed, (std::array<float, 6> { 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f }));
}

std::shared_ptr<JammerNetzAudioData> remotePacket(uint64 counter, float sampleValue = 0.0f)
{
	auto audio = std::make_shared<juce::AudioBuffer<float>>(2, SAMPLE_BUFFER_SIZE);
	for (int channel = 0; channel < audio->getNumChannels(); ++channel) {
		juce::FloatVectorOperations::fill(audio->getWritePointer(channel), sampleValue, SAMPLE_BUFFER_SIZE);
	}
	JammerNetzChannelSetup setup(false, {
		JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Left),
		JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Right)
	});
	return std::make_shared<JammerNetzAudioData>(
		counter, juce::Time::getMillisecondCounterHiRes(), setup, SAMPLE_RATE, 120.0f, MidiSignal_None, audio, nullptr);
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

TEST(JammerNetzAudioEngineTest, OutputTapReceivesTheFinalStereoMix)
{
	JammerNetzSession session;
	CapturingOutputTap tap;
	JammerNetzAudioEngine engine(session, juce::File());
	engine.setOutputTap(&tap);
	engine.setChannelSetup(monoLocalSetup());
	engine.setLocalMonitoring(true);
	engine.setMasterVolume(0.5);
	engine.setMonitorBalance(0.0);
	engine.prepare(48000.0, 64);

	std::array<float, 64> input;
	std::array<float, 64> left {};
	std::array<float, 64> right {};
	input.fill(1.0f);
	const float* inputs[] { input.data() };
	float* outputs[] { left.data(), right.data() };
	engine.process(inputs, 1, outputs, 2, static_cast<int>(input.size()));

	const float expectedGain = static_cast<float>(0.5 * std::sqrt(0.5));
	EXPECT_DOUBLE_EQ(tap.preparedSampleRate, 48000.0);
	EXPECT_EQ(tap.preparedBlockSize, 64);
	EXPECT_EQ(tap.enqueueCalls, 1);
	EXPECT_NEAR(tap.left, expectedGain, 1.0e-5f);
	EXPECT_NEAR(tap.right, expectedGain, 1.0e-5f);

	engine.release();
	EXPECT_EQ(tap.releaseCalls, 1);
	engine.setOutputTap(nullptr);
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
	ASSERT_TRUE(engine.processNextOutgoingPacket());
	EXPECT_FALSE(engine.processNextOutgoingPacket());

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

TEST(JammerNetzAudioEngineTest, ShutdownSilencesLateAudioCallbacks)
{
	JammerNetzSession session;
	JammerNetzAudioEngine engine(session, juce::File());
	engine.shutdown();

	std::array<float, 32> left;
	std::array<float, 32> right;
	left.fill(1.0f);
	right.fill(1.0f);
	float* outputs[] { left.data(), right.data() };
	engine.process(nullptr, 0, outputs, 2, static_cast<int>(left.size()));

	EXPECT_FLOAT_EQ(left.front(), 0.0f);
	EXPECT_FLOAT_EQ(left.back(), 0.0f);
	EXPECT_FLOAT_EQ(right.front(), 0.0f);
	EXPECT_FLOAT_EQ(right.back(), 0.0f);
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
	ASSERT_TRUE(engine.processNextIncomingPacket());

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

TEST(RemoteGenerationTest, DelayedOlderPublicationCannotReplaceNewerGeneration)
{
	std::atomic<uint64_t> publishedGeneration { 0 };
	const uint64_t delayedGeneration = 1;
	const uint64_t newerGeneration = 2;

	jammernetz::detail::publishGenerationAtLeast(publishedGeneration, newerGeneration);
	jammernetz::detail::publishGenerationAtLeast(publishedGeneration, delayedGeneration);

	EXPECT_EQ(publishedGeneration.load(std::memory_order_acquire), newerGeneration);
}

TEST(JammerNetzAudioEngineTest, BypassTransitionsDiscardStaleRemoteAudioAndRebuffer)
{
	const std::array<uint64_t, 2> maximums {
		CLIENT_PLAYOUT_MAX_BUFFER,
		256
	};
	constexpr uint64_t minimum = CLIENT_PLAYOUT_JITTER_BUFFER;
	constexpr float remoteGain = 0.70710678f;

	for (const auto maximum : maximums) {
		JammerNetzSession session;
		JammerNetzAudioEngine engine(session, juce::File());
		engine.setPlayoutBufferRange(minimum, maximum);
		engine.setLocalMonitoring(false);
		uint64 counter = 1;

		const auto enqueue = [&](float value) {
			engine.enqueueRemoteAudio(remotePacket(counter++, value));
		};
		const auto render = [&](int samples) {
			juce::AudioBuffer<float> output(2, samples);
			std::array<float*, 2> channels { output.getWritePointer(0), output.getWritePointer(1) };
			engine.process(nullptr, 0, channels.data(), 2, samples);
			return output;
		};

		float staleValue = 0.2f;
		for (uint64_t frame = 0; frame < minimum; ++frame) {
			enqueue(staleValue);
		}
		while (engine.processNextIncomingPacket()) {}

		for (int cycle = 0; cycle < 2; ++cycle) {
			// Leave half a stale frame in the engine's local playout ring so the
			// bypass transition has to invalidate both receive queues and playout.
			const auto beforeBypass = render(SAMPLE_BUFFER_SIZE / 2);
			EXPECT_NEAR(beforeBypass.getSample(0, 0), staleValue * remoteGain, 1.0e-5f);

			engine.setBypassed(true);
			// Apply the entry reset, then simulate a receive stream continuing for
			// longer than the configured maximum while normal processing is paused.
			EXPECT_FALSE(engine.processNextIncomingPacket());
			for (uint64_t frame = 0; frame < maximum + minimum; ++frame) {
				enqueue(staleValue + 0.25f);
				engine.processNextIncomingPacket();
			}

			engine.setBypassed(false);
			const auto immediatelyAfterResume = render(SAMPLE_BUFFER_SIZE);
			EXPECT_EQ(immediatelyAfterResume.findMinMax(0, 0, SAMPLE_BUFFER_SIZE), juce::Range<float>());
			EXPECT_EQ(immediatelyAfterResume.findMinMax(1, 0, SAMPLE_BUFFER_SIZE), juce::Range<float>());

			// The resume reset discards all during-bypass packets and requires the
			// configured minimum number of new frames before remote audio restarts.
			EXPECT_FALSE(engine.processNextIncomingPacket());
			const float currentValue = 0.7f + static_cast<float>(cycle) * 0.1f;
			for (uint64_t frame = 0; frame + 1 < minimum; ++frame) {
				enqueue(currentValue);
				EXPECT_FALSE(engine.processNextIncomingPacket());
			}
			const auto beforeMinimum = render(SAMPLE_BUFFER_SIZE);
			EXPECT_EQ(beforeMinimum.findMinMax(0, 0, SAMPLE_BUFFER_SIZE), juce::Range<float>());

			enqueue(currentValue);
			ASSERT_TRUE(engine.processNextIncomingPacket());
			const auto afterMinimum = render(SAMPLE_BUFFER_SIZE);
			EXPECT_NEAR(afterMinimum.getSample(0, 0), currentValue * remoteGain, 1.0e-5f);
			EXPECT_NEAR(afterMinimum.getSample(1, SAMPLE_BUFFER_SIZE - 1), currentValue * remoteGain, 1.0e-5f);
			while (engine.processNextIncomingPacket()) {}
			staleValue = currentValue;
		}
	}
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

TEST(BoundedSpscQueueTest, ResetReleasesRetainedItemsAndMakesQueueReusable)
{
	BoundedSpscQueue<std::shared_ptr<int>> queue(1);
	auto retained = std::make_shared<int>(42);
	const std::weak_ptr<int> observer = retained;
	EXPECT_TRUE(queue.tryWrite([&](std::shared_ptr<int>& slot) { slot = retained; }));
	retained.reset();
	EXPECT_FALSE(observer.expired());

	queue.reset();

	EXPECT_TRUE(observer.expired());
	EXPECT_EQ(queue.size(), 0);
	EXPECT_TRUE(queue.tryWrite([](std::shared_ptr<int>& slot) { slot = std::make_shared<int>(7); }));
	int value = 0;
	EXPECT_TRUE(queue.tryRead([&](std::shared_ptr<int>& slot) { value = *slot; }));
	EXPECT_EQ(value, 7);
}

TEST(LatestBpmMailboxTest, CoalescesToLatestValueWithoutClearingANewerWrite)
{
	LatestBpmMailbox mailbox;
	mailbox.setValue(120.0f);
	mailbox.setValue(127.5f);
	const auto latest = mailbox.takeLatest();
	ASSERT_TRUE(latest.has_value());
	EXPECT_FLOAT_EQ(*latest, 127.5f);
	EXPECT_FALSE(mailbox.takeLatest().has_value());

	// A value published after the atomic take remains pending for the next take;
	// there is no separate presence flag for the reader to clear.
	mailbox.setValue(98.0f);
	const auto next = mailbox.takeLatest();
	ASSERT_TRUE(next.has_value());
	EXPECT_FLOAT_EQ(*next, 98.0f);
}

TEST(JammerNetzAudioEngineTest, CountsOverflowOfOrderedMidiTransportCommands)
{
	JammerNetzSession session;
	JammerNetzAudioEngine engine(session, juce::File());
	for (int command = 0; command < 32; ++command) {
		engine.setMidiSignalToSend(command % 2 == 0 ? MidiSignal_Start : MidiSignal_Stop);
	}
	EXPECT_EQ(engine.getRealtimeWorkerStats().midiTransportCommandsDropped, 0u);
	engine.setMidiSignalToSend(MidiSignal_Start);
	EXPECT_EQ(engine.getRealtimeWorkerStats().midiTransportCommandsDropped, 1u);
}

TEST(JammerNetzAudioDataTest, FillInPreservesRecoveredTimingAndDoesNotDuplicateCommands)
{
	auto audio = std::make_shared<juce::AudioBuffer<float>>(2, SAMPLE_BUFFER_SIZE);
	JammerNetzChannelSetup setup(false, {
		JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Left),
		JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Right)
	});
	AudioBlock current(0.0, 2, 256, 120.0f, MidiSignal_Start, SAMPLE_RATE, setup, audio);
	JammerNetzAudioData withoutFec(current, nullptr);
	bool hadFec = true;
	const auto repeated = withoutFec.createFillInPackage(1, hadFec);
	EXPECT_FALSE(hadFec);
	EXPECT_EQ(repeated->serverTime(), 128u);
	EXPECT_EQ(repeated->midiSignal(), MidiSignal_None);

	AudioBlock afterLongGap(0.0, 5, 640, 120.0f, MidiSignal_Stop, SAMPLE_RATE, setup, audio);
	auto unrelatedFec = std::make_shared<AudioBlock>(
		0.0, 4, 512, 120.0f, MidiSignal_Stop, SAMPLE_RATE, setup, audio);
	JammerNetzAudioData longGap(afterLongGap, unrelatedFec);
	const auto inferred = longGap.createFillInPackage(2, hadFec);
	EXPECT_FALSE(hadFec);
	EXPECT_EQ(inferred->serverTime(), 256u);
	EXPECT_EQ(inferred->midiSignal(), MidiSignal_None);

	auto recoveredBlock = std::make_shared<AudioBlock>(
		0.0, 1, 128, 120.0f, MidiSignal_Start, SAMPLE_RATE, setup, audio);
	JammerNetzAudioData withFec(current, recoveredBlock);
	const auto recovered = withFec.createFillInPackage(1, hadFec);
	EXPECT_TRUE(hadFec);
	EXPECT_EQ(recovered->serverTime(), 128u);
	EXPECT_EQ(recovered->midiSignal(), MidiSignal_Start);
}

TEST(PathMtuProbeIntegrationTest, DoesNotProbeALegacyServerWithoutACapabilityAdvertisement)
{
	juce::DatagramSocket serverSocket;
	ASSERT_TRUE(serverSocket.bindToPort(0, "127.0.0.1"));
	juce::DatagramSocket clientSocket;
	ASSERT_TRUE(clientSocket.bindToPort(0, "127.0.0.1"));
	Client client(clientSocket);
	client.setServer("127.0.0.1", serverSocket.getBoundPort(), false);

	auto audio = std::make_shared<juce::AudioBuffer<float>>(1, SAMPLE_BUFFER_SIZE);
	JammerNetzChannelSetup setup(false, {
		JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Mono)
	});
	ASSERT_TRUE(client.sendData(setup, audio, {}));
	ASSERT_EQ(serverSocket.waitUntilReady(true, 1000), 1);
	std::array<uint8, MAXFRAMESIZE> bytes {};
	EXPECT_GT(serverSocket.read(bytes.data(), static_cast<int>(bytes.size()), false), 0);
	EXPECT_EQ(serverSocket.waitUntilReady(true, 20), 0);
}

TEST(PathMtuProbeIntegrationTest, SendsAnExactSizeProbeAfterCapabilityAdvertisement)
{
	juce::DatagramSocket serverSocket;
	ASSERT_TRUE(serverSocket.bindToPort(0, "127.0.0.1"));
	juce::DatagramSocket clientSocket;
	ASSERT_TRUE(clientSocket.bindToPort(0, "127.0.0.1"));
	Client client(clientSocket);
	client.setServer("127.0.0.1", serverSocket.getBoundPort(), false);
	const std::array<uint8, 16> key { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
	client.setCryptoKey(key.data(), static_cast<int>(key.size()));
	client.setMtuDiscoverySupported(true);
	ASSERT_EQ(client.getMtuDiscoveryStatus(), PathMtuDiscoveryStatus::Searching);

	auto audio = std::make_shared<juce::AudioBuffer<float>>(1, SAMPLE_BUFFER_SIZE);
	JammerNetzChannelSetup setup(false, {
		JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Mono)
	});
	ASSERT_TRUE(client.sendData(setup, audio, {}));
	std::array<uint8, MAXFRAMESIZE> bytes {};
	ASSERT_EQ(serverSocket.waitUntilReady(true, 1000), 1);
	EXPECT_GT(serverSocket.read(bytes.data(), static_cast<int>(bytes.size()), false), 0);
	ASSERT_EQ(serverSocket.waitUntilReady(true, 1000), 1);
	const auto probeBytes = serverSocket.read(bytes.data(), static_cast<int>(bytes.size()), false);
	ASSERT_EQ(probeBytes, PathMtuDiscovery::fallbackPayloadBytes);
	BlowFish decryptor(key.data(), static_cast<int>(key.size()));
	const auto plaintextBytes = decryptor.decrypt(bytes.data(), static_cast<size_t>(probeBytes));
	ASSERT_GT(plaintextBytes, 0);

	auto message = std::dynamic_pointer_cast<JammerNetzControlMessage>(
		JammerNetzMessage::deserialize(bytes.data(), static_cast<size_t>(plaintextBytes)));
	ASSERT_NE(message, nullptr);
	ASSERT_TRUE(message->json_.contains("mtu_probe_v1"));
	EXPECT_EQ(message->json_["mtu_probe_v1"]["size"].get<int>(), probeBytes);
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

TEST(AudioReceiveWorkerTest, CapsPreparedPlayoutAfterAConsumerHiccup)
{
	JammerNetzSession session;
	AudioReceiveWorker worker(session);
	worker.setPlayoutRange(1, 4);
	worker.start();

	// Feed at approximately the normal network cadence while deliberately not
	// consuming. The old worker filled its separate prepared queue indefinitely
	// because the configured maximum was enforced only on the ordering queue.
	for (uint64 counter = 1; counter <= 12; ++counter) {
		worker.enqueue(remotePacket(counter));
		juce::Thread::sleep(4);
	}
	for (int attempt = 0; attempt < 100 && worker.discardedFrames() == 0; ++attempt) {
		juce::Thread::sleep(2);
	}

	EXPECT_LE(worker.readyFrames(), 4);
	EXPECT_GT(worker.discardedFrames(), 0u);

	RemoteAudioFrame frame;
	while (worker.readyFrames() > 1) {
		EXPECT_TRUE(worker.tryPop(frame));
	}
	juce::Thread::sleep(10);
	EXPECT_LE(worker.readyFrames(), 1);
	worker.shutdown();
}

TEST(MidiSendThreadTest, ShutdownInterruptsAFutureScheduledMessage)
{
	MidiSendThread sender(std::vector<juce::MidiDeviceInfo> {});
	ASSERT_TRUE(sender.enqueueAt(std::chrono::steady_clock::now() + std::chrono::seconds(5),
		120.0f, MidiSignal_None, true));
	juce::Thread::sleep(20);
	const auto started = std::chrono::steady_clock::now();
	sender.shutdown();
	EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::milliseconds(500));
}

TEST(MidiSendThreadTest, FutureMessageDoesNotReserveAQueueSlotWhileWaiting)
{
	MidiSendThread sender(std::vector<juce::MidiDeviceInfo> {});
	const auto future = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	ASSERT_TRUE(sender.enqueueAt(future, 120.0f, MidiSignal_None, true));
	juce::Thread::sleep(20);

	for (int message = 0; message < 256; ++message) {
		ASSERT_TRUE(sender.enqueueAt(future, 120.0f, MidiSignal_None, true));
	}
	EXPECT_FALSE(sender.enqueueAt(future, 120.0f, MidiSignal_None, true));
	sender.shutdown();
}

} // namespace
