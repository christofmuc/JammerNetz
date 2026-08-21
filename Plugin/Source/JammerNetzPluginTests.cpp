/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "JammerNetzPluginProcessor.h"

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

TEST(JammerNetzPluginTest, ConstructionAndStateRestoreDoNotStartASession)
{
	JammerNetzPluginProcessor source;
	JammerNetzPluginConfiguration configuration;
	configuration.serverName = "example.invalid";
	configuration.serverPort = 8123;
	configuration.username = "Synth Player";
	configuration.sendGain = 0.75f;
	configuration.remoteGain = 1.25f;
	configuration.dryGain = 0.5f;
	configuration.minimumJitterFrames = 5;
	configuration.maximumJitterFrames = 17;
	configuration.useFEC = true;
	configuration.localPassthrough = false;
	source.setConfiguration(configuration);

	juce::MemoryBlock state;
	source.getStateInformation(state);
	JammerNetzPluginProcessor restored;
	restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
	const auto restoredConfiguration = restored.configuration();

	EXPECT_FALSE(source.isSessionActive());
	EXPECT_FALSE(restored.isSessionActive());
	EXPECT_EQ(restoredConfiguration.serverName, configuration.serverName);
	EXPECT_EQ(restoredConfiguration.serverPort, configuration.serverPort);
	EXPECT_EQ(restoredConfiguration.username, configuration.username);
	EXPECT_FLOAT_EQ(restoredConfiguration.sendGain, configuration.sendGain);
	EXPECT_FLOAT_EQ(restoredConfiguration.remoteGain, configuration.remoteGain);
	EXPECT_FLOAT_EQ(restoredConfiguration.dryGain, configuration.dryGain);
	EXPECT_EQ(restoredConfiguration.minimumJitterFrames, configuration.minimumJitterFrames);
	EXPECT_EQ(restoredConfiguration.maximumJitterFrames, configuration.maximumJitterFrames);
	EXPECT_EQ(restoredConfiguration.useFEC, configuration.useFEC);
	EXPECT_EQ(restoredConfiguration.localPassthrough, configuration.localPassthrough);

	const std::string serialised(static_cast<const char*>(state.getData()), state.getSize());
	EXPECT_EQ(serialised.find("pluginCryptoKeyPath"), std::string::npos);
}

TEST(JammerNetzPluginTest, DisconnectedProcessingIsTransparentForVariableAndLargeBlocks)
{
	JammerNetzPluginProcessor processor;
	processor.prepareToPlay(48000.0, 1024);
	for (const int blockSize : std::array<int, 7> { 32, 64, 128, 256, 512, 1024, 9000 }) {
		juce::AudioBuffer<float> buffer(2, blockSize);
		for (int sample = 0; sample < blockSize; ++sample) {
			buffer.setSample(0, sample, static_cast<float>(sample + 1) / static_cast<float>(blockSize));
			buffer.setSample(1, sample, -static_cast<float>(sample + 1) / static_cast<float>(blockSize));
		}
		const juce::AudioBuffer<float> original(buffer);
		juce::MidiBuffer midi;
		midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);

		processor.processBlock(buffer, midi);

		EXPECT_EQ(midi.getNumEvents(), 0);
		for (int channel = 0; channel < 2; ++channel) {
			EXPECT_EQ(buffer.findMinMax(channel, 0, blockSize), original.findMinMax(channel, 0, blockSize));
			EXPECT_FLOAT_EQ(buffer.getSample(channel, 0), original.getSample(channel, 0));
			EXPECT_FLOAT_EQ(buffer.getSample(channel, blockSize - 1), original.getSample(channel, blockSize - 1));
		}
	}
}

TEST(JammerNetzPluginTest, SupportsOnlyStereoEffectBuses)
{
	JammerNetzPluginProcessor processor;
	auto layout = processor.getBusesLayout();
	EXPECT_TRUE(processor.isBusesLayoutSupported(layout));

	layout.inputBuses.getReference(0) = juce::AudioChannelSet::mono();
	EXPECT_FALSE(processor.isBusesLayoutSupported(layout));
	layout = processor.getBusesLayout();
	layout.outputBuses.getReference(0) = juce::AudioChannelSet::mono();
	EXPECT_FALSE(processor.isBusesLayoutSupported(layout));
}

TEST(JammerNetzPluginTest, RepeatedHostPreparationDoesNotStartASession)
{
	JammerNetzPluginProcessor processor;
	processor.prepareToPlay(48000.0, 64);
	processor.prepareToPlay(48000.0, 2048);
	processor.releaseResources();

	EXPECT_FALSE(processor.isSessionActive());
	EXPECT_EQ(processor.statusText(), "Disconnected");
}

TEST(JammerNetzPluginTest, SupportsRepeatedCreationAndDestruction)
{
	for (int iteration = 0; iteration < 50; ++iteration) {
		JammerNetzPluginProcessor processor;
		processor.prepareToPlay(48000.0, 512);
		EXPECT_FALSE(processor.isSessionActive());
		processor.releaseResources();
	}
}

TEST(JammerNetzPluginTest, SupportsMultipleSimultaneousDisconnectedInstances)
{
	std::vector<std::unique_ptr<JammerNetzPluginProcessor>> processors;
	for (int instance = 0; instance < 4; ++instance) {
		auto processor = std::make_unique<JammerNetzPluginProcessor>();
		processor->prepareToPlay(48000.0, 512);
		processors.push_back(std::move(processor));
	}

	for (const auto &processor : processors) {
		EXPECT_FALSE(processor->isSessionActive());
		EXPECT_EQ(processor->statusText(), "Disconnected");
	}
}

TEST(JammerNetzPluginTest, SameRateHostPreparationPreservesUnrelatedConnectionError)
{
	JammerNetzPluginProcessor processor;
	processor.prepareToPlay(48000.0, 64);
	EXPECT_FALSE(processor.connectSession());
	ASSERT_EQ(processor.statusText(), "Enter a JammerNetz server before connecting");

	processor.prepareToPlay(48000.0, 2048);

	EXPECT_EQ(processor.statusText(), "Enter a JammerNetz server before connecting");
}

TEST(JammerNetzPluginTest, BypassAndOfflineRenderingRemainTransparent)
{
	JammerNetzPluginProcessor processor;
	processor.prepareToPlay(48000.0, 512);
	juce::AudioBuffer<float> buffer(2, 128);
	buffer.getWritePointer(0)[0] = 0.25f;
	buffer.getWritePointer(1)[0] = -0.5f;
	juce::MidiBuffer midi;

	processor.processBlockBypassed(buffer, midi);
	EXPECT_FLOAT_EQ(buffer.getSample(0, 0), 0.25f);
	EXPECT_FLOAT_EQ(buffer.getSample(1, 0), -0.5f);

	processor.setNonRealtime(true);
	processor.processBlock(buffer, midi);
	EXPECT_FALSE(processor.isSessionActive());
	EXPECT_FLOAT_EQ(buffer.getSample(0, 0), 0.25f);
	EXPECT_FLOAT_EQ(buffer.getSample(1, 0), -0.5f);
}

TEST(JammerNetzPluginTest, Accepts44100WithoutTouchingDisconnectedDryAudio)
{
	JammerNetzPluginProcessor processor;
	processor.prepareToPlay(44100.0, 512);
	EXPECT_FALSE(processor.isSessionActive());
	EXPECT_EQ(processor.statusText(), "Disconnected");

	juce::AudioBuffer<float> buffer(2, 64);
	buffer.getWritePointer(0)[0] = 0.75f;
	buffer.getWritePointer(1)[0] = -0.75f;
	juce::MidiBuffer midi;
	processor.processBlock(buffer, midi);
	EXPECT_FLOAT_EQ(buffer.getSample(0, 0), 0.75f);
	EXPECT_FLOAT_EQ(buffer.getSample(1, 0), -0.75f);
}

TEST(JammerNetzPluginTest, UnsupportedSampleRatesStillFailWithoutTouchingDryAudio)
{
	JammerNetzPluginProcessor processor;
	processor.prepareToPlay(96000.0, 512);
	EXPECT_FALSE(processor.connectSession());
	EXPECT_FALSE(processor.isSessionActive());
	EXPECT_TRUE(processor.statusText().contains("40 to 57.6 kHz"));

	juce::AudioBuffer<float> buffer(2, 64);
	buffer.getWritePointer(0)[0] = 0.75f;
	buffer.getWritePointer(1)[0] = -0.75f;
	juce::MidiBuffer midi;
	processor.processBlock(buffer, midi);
	EXPECT_FLOAT_EQ(buffer.getSample(0, 0), 0.75f);
	EXPECT_FLOAT_EQ(buffer.getSample(1, 0), -0.75f);
}

TEST(JammerNetzPluginTest, OnlyOneSessionLeaseCanBeActivePerProcess)
{
	SingleActiveSessionLease first;
	SingleActiveSessionLease second;
	int firstOwner = 1;
	int secondOwner = 2;
	EXPECT_TRUE(first.acquire(&firstOwner));
	EXPECT_FALSE(second.acquire(&secondOwner));
	first.release();
	EXPECT_TRUE(second.acquire(&secondOwner));
	second.release();
}
