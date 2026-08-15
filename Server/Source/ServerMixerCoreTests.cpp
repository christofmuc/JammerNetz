/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ServerMixerCore.h"

#include "BuffersConfig.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <utility>

namespace {

JammerNetzChannelSetup stereoOutputSetup()
{
	return JammerNetzChannelSetup(false, {
		JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Left),
		JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Right)
	});
}

std::shared_ptr<JammerNetzAudioData> packet(const std::string& channelName,
	const JammerNetzChannelTarget target,
	const bool suppressEcho,
	const float value,
	const float volume,
	const uint64 counter,
	const float bpm = 0.0f,
	const MidiSignal midiSignal = MidiSignal_None)
{
	auto audio = std::make_shared<AudioBuffer<float>>(1, SAMPLE_BUFFER_SIZE);
	for (int sample = 0; sample < SAMPLE_BUFFER_SIZE; ++sample) {
		audio->setSample(0, sample, value);
	}
	JammerNetzChannelSetup setup(suppressEcho);
	JammerNetzSingleChannelSetup channel(static_cast<uint8>(target));
	channel.name = channelName;
	channel.volume = volume;
	setup.channels.push_back(channel);
	return std::make_shared<JammerNetzAudioData>(counter, static_cast<double>(counter), setup,
		SAMPLE_RATE, bpm, midiSignal, std::move(audio), nullptr);
}

const OutgoingPackage& outputFor(const ServerMixStepResult& result, const std::string& address)
{
	const auto found = std::find_if(result.outgoing.begin(), result.outgoing.end(),
		[&address](const OutgoingPackage& output) { return output.targetAddress == address; });
	EXPECT_NE(found, result.outgoing.end());
	return found != result.outgoing.end() ? *found : result.outgoing.front();
}

std::array<float, 2> routedValue(const JammerNetzChannelTarget target,
	const bool isSender,
	const bool suppressEcho,
	const float value)
{
	if (target == Mute || (isSender && (target == SendLeft || target == SendRight || target == SendMono))
		|| (isSender && suppressEcho)) {
		return { 0.0f, 0.0f };
	}
	switch (target) {
	case Left:
	case SendLeft:
		return { value, 0.0f };
	case Right:
	case SendRight:
		return { 0.0f, value };
	case Mono:
	case SendMono:
		return { value, value };
	case Mute:
		break;
	}
	return { 0.0f, 0.0f };
}

TEST(ServerMixerCoreTest, PreservesRoutingVolumeAndReceiverSpecificEcho)
{
	constexpr std::array<JammerNetzChannelTarget, 7> targets {
		Mute, Left, Right, Mono, SendLeft, SendRight, SendMono
	};
	for (const auto target : targets) {
		for (const bool suppressEcho : { false, true }) {
			SCOPED_TRACE(static_cast<int>(target));
			SCOPED_TRACE(suppressEcho);
			ServerMixerCore mixer(stereoOutputSetup());
			ServerInputPackets inputs;
			inputs.emplace("client-a", packet("a", target, suppressEcho, 0.8f, 0.25f, 11));
			inputs.emplace("client-b", packet("b", Mute, true, 0.0f, 1.0f, 22));

			const auto result = mixer.mix(inputs);
			ASSERT_EQ(result.outgoing.size(), 2U);
			EXPECT_TRUE(result.diagnostics.empty());
			const auto senderExpected = routedValue(target, true, suppressEcho, 0.2f);
			const auto remoteExpected = routedValue(target, false, suppressEcho, 0.2f);
			const auto& sender = outputFor(result, "client-a").audioBlock;
			const auto& remote = outputFor(result, "client-b").audioBlock;
			EXPECT_FLOAT_EQ(sender.audioBuffer->getSample(0, 0), senderExpected[0]);
			EXPECT_FLOAT_EQ(sender.audioBuffer->getSample(1, 0), senderExpected[1]);
			EXPECT_FLOAT_EQ(remote.audioBuffer->getSample(0, 0), remoteExpected[0]);
			EXPECT_FLOAT_EQ(remote.audioBuffer->getSample(1, 0), remoteExpected[1]);
		}
	}
}

TEST(ServerMixerCoreTest, PreservesAddressingSessionMetadataClockAndControlSelection)
{
	ServerMixerCore mixer(stereoOutputSetup());
	ServerInputPackets firstInputs;
	firstInputs.emplace("10.0.0.1:1001", packet("guitar", Left, true, 0.1f, 1.0f, 31, 100.0f, MidiSignal_Start));
	firstInputs.emplace("10.0.0.2:1002", packet("drums", Right, true, 0.2f, 1.0f, 47, 140.0f, MidiSignal_Stop));

	const auto first = mixer.mix(firstInputs);
	ASSERT_EQ(first.outgoing.size(), 2U);
	EXPECT_EQ(first.serverTime, SAMPLE_BUFFER_SIZE);
	EXPECT_EQ(first.outgoing[0].targetAddress, "10.0.0.1:1001");
	EXPECT_EQ(first.outgoing[1].targetAddress, "10.0.0.2:1002");
	for (const auto& output : first.outgoing) {
		EXPECT_EQ(output.audioBlock.serverTime, SAMPLE_BUFFER_SIZE);
		EXPECT_FLOAT_EQ(output.audioBlock.bpm, 140.0f);
		EXPECT_EQ(output.audioBlock.midiSignal, MidiSignal_Stop);
		ASSERT_EQ(output.sessionSetup.channels.size(), 1U);
	}
	EXPECT_EQ(first.outgoing[0].audioBlock.messageCounter, 31U);
	EXPECT_EQ(first.outgoing[1].audioBlock.messageCounter, 47U);
	EXPECT_EQ(first.outgoing[0].sessionSetup.channels.front().name, "drums");
	EXPECT_EQ(first.outgoing[1].sessionSetup.channels.front().name, "guitar");

	ServerInputPackets secondInputs;
	secondInputs.emplace("10.0.0.1:1001", packet("guitar", Left, true, 0.1f, 1.0f, 32));
	secondInputs.emplace("10.0.0.2:1002", packet("drums", Right, true, 0.2f, 1.0f, 48));
	const auto second = mixer.mix(secondInputs);
	EXPECT_EQ(second.serverTime, 2 * SAMPLE_BUFFER_SIZE);
	for (const auto& output : second.outgoing) {
		EXPECT_FLOAT_EQ(output.audioBlock.bpm, 140.0f);
		EXPECT_EQ(output.audioBlock.midiSignal, MidiSignal_None);
	}
}

TEST(ServerMixerCoreTest, IgnoresAudioChannelsWithoutMatchingChannelSetup)
{
	ServerMixerCore mixer(stereoOutputSetup());
	auto audio = std::make_shared<AudioBuffer<float>>(2, SAMPLE_BUFFER_SIZE);
	audio->clear();
	audio->setSample(0, 0, 0.25f);
	audio->setSample(1, 0, 0.75f);
	JammerNetzChannelSetup setup(false);
	setup.channels.emplace_back(JammerNetzChannelTarget::Left);
	ServerInputPackets inputs;
	inputs.emplace("malformed-client", std::make_shared<JammerNetzAudioData>(
		1, 0.0, setup, SAMPLE_RATE, 0.0f, MidiSignal_None, std::move(audio), nullptr));

	const auto result = mixer.mix(inputs);

	ASSERT_EQ(result.outgoing.size(), 1U);
	ASSERT_EQ(result.diagnostics.size(), 1U);
	EXPECT_NE(result.diagnostics.front().find("2 audio channels but declared 1 channel setups"),
		std::string::npos);
	EXPECT_FLOAT_EQ(result.outgoing.front().audioBlock.audioBuffer->getSample(0, 0), 0.25f);
	EXPECT_FLOAT_EQ(result.outgoing.front().audioBlock.audioBuffer->getSample(1, 0), 0.0f);
}

} // namespace
