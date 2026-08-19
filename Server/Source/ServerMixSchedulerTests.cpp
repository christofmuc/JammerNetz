/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ServerMixScheduler.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

std::shared_ptr<JammerNetzAudioData> makeSchedulerPacket(const std::uint64_t counter)
{
	auto buffer = std::make_shared<AudioBuffer<float>>(1, SAMPLE_BUFFER_SIZE);
	buffer->clear();
	JammerNetzChannelSetup setup(true);
	setup.channels.push_back(JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Mono));
	return std::make_shared<JammerNetzAudioData>(counter, static_cast<double>(counter), setup,
		SAMPLE_RATE, 0.0f, MidiSignal_None, std::move(buffer), nullptr);
}

JammerNetzChannelSetup stereoMixdown()
{
	return JammerNetzChannelSetup(false, {
		JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Left),
		JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Right)
	});
}

TEST(ServerMixSchedulerTest, WaitsUntilEveryClientExceedsTheJitterThreshold)
{
	TPacketStreamBundle clients;
	auto clientA = std::make_shared<ClientState>("a");
	auto clientB = std::make_shared<ClientState>("b");
	clients.emplace("a", clientA);
	clients.emplace("b", clientB);
	ServerMixScheduler scheduler(stereoMixdown(), { 1, 3, 0 });

	clientA->push(makeSchedulerPacket(10), 0);
	clientB->push(makeSchedulerPacket(10), 0);
	auto waiting = scheduler.process(clients);
	EXPECT_EQ(waiting.trigger, ServerMixTrigger::None);
	EXPECT_TRUE(waiting.incoming.empty());

	clientA->push(makeSchedulerPacket(11), 0);
	clientB->push(makeSchedulerPacket(11), 0);
	auto mixed = scheduler.process(clients);
	EXPECT_EQ(mixed.trigger, ServerMixTrigger::AllClientsReady);
	EXPECT_EQ(mixed.incoming.size(), 2U);
	EXPECT_EQ(mixed.mix.outgoing.size(), 2U);
	EXPECT_EQ(mixed.queuesAfter.at("a").size, 1U);
	EXPECT_EQ(mixed.queuesAfter.at("b").size, 1U);
}

TEST(ServerMixSchedulerTest, MaximumBufferPressureFastForwardsOnlyTheOverfullClient)
{
	TPacketStreamBundle clients;
	auto clientA = std::make_shared<ClientState>("a");
	auto clientB = std::make_shared<ClientState>("b");
	clients.emplace("a", clientA);
	clients.emplace("b", clientB);
	ServerMixScheduler scheduler(stereoMixdown(), { 1, 3, 0 });
	clientA->push(makeSchedulerPacket(10), 0);
	clientB->push(makeSchedulerPacket(10), 0);
	clientA->push(makeSchedulerPacket(11), 0);
	clientB->push(makeSchedulerPacket(11), 0);
	ASSERT_EQ(scheduler.process(clients).incoming.size(), 2U);

	for (std::uint64_t counter = 12; counter <= 15; ++counter) {
		clientA->push(makeSchedulerPacket(counter), 0);
	}
	auto pressured = scheduler.process(clients);
	EXPECT_EQ(pressured.trigger, ServerMixTrigger::None);
	EXPECT_FALSE(pressured.shouldWakeAgain);
	EXPECT_TRUE(pressured.incoming.empty());
	EXPECT_TRUE(pressured.underrunClients.empty());
	ASSERT_EQ(pressured.fastForwardedClients.size(), 1U);
	EXPECT_EQ(pressured.fastForwardedClients.at("a").discardedPackets, 4U);
	ASSERT_TRUE(pressured.fastForwardedClients.at("a").oldestRetainedCounter.has_value());
	EXPECT_EQ(*pressured.fastForwardedClients.at("a").oldestRetainedCounter, 15U);
	EXPECT_EQ(pressured.queuesAfter.at("a").size, 1U);
	EXPECT_EQ(pressured.queuesAfter.at("b").size, 1U);
	EXPECT_EQ(pressured.queuesAfter.at("b").state, ClientConnectionState::Connected);
}

TEST(ServerMixSchedulerTest, SustainedFasterStreamStaysBoundedWithoutDrainingTheSlowerStream)
{
	TPacketStreamBundle clients;
	auto faster = std::make_shared<ClientState>("faster");
	auto slower = std::make_shared<ClientState>("slower");
	clients.emplace("faster", faster);
	clients.emplace("slower", slower);
	ServerMixScheduler scheduler(stereoMixdown(), { 2, 4, 0 });

	for (std::uint64_t counter = 100; counter <= 102; ++counter) {
		faster->push(makeSchedulerPacket(counter), 0);
		slower->push(makeSchedulerPacket(counter), 0);
	}
	ASSERT_EQ(scheduler.process(clients).incoming.size(), 2U);

	std::uint64_t fasterCounter = 103;
	std::uint64_t slowerCounter = 103;
	std::size_t fastForwardEvents = 0;
	for (int cycle = 0; cycle < 40; ++cycle) {
		for (int packet = 0; packet < 2; ++packet) {
			faster->push(makeSchedulerPacket(fasterCounter++), 0);
			const auto step = scheduler.process(clients);
			fastForwardEvents += step.fastForwardedClients.size();
			EXPECT_TRUE(step.underrunClients.empty());
			EXPECT_TRUE(step.fillInClients.empty());
			EXPECT_TRUE(step.incoming.empty() || step.incoming.size() == 2U);
			EXPECT_LE(step.queuesAfter.at("faster").size, 4U);
			EXPECT_GE(step.queuesAfter.at("slower").size, 2U);
		}

		slower->push(makeSchedulerPacket(slowerCounter++), 0);
		const auto step = scheduler.process(clients);
		fastForwardEvents += step.fastForwardedClients.size();
		EXPECT_TRUE(step.underrunClients.empty());
		EXPECT_TRUE(step.fillInClients.empty());
		EXPECT_TRUE(step.incoming.empty() || step.incoming.size() == 2U);
		EXPECT_LE(step.queuesAfter.at("faster").size, 4U);
		EXPECT_GE(step.queuesAfter.at("slower").size, 2U);
	}

	EXPECT_GT(fastForwardEvents, 0U);
	EXPECT_EQ(slower->snapshot().state, ClientConnectionState::Connected);
}

} // namespace
