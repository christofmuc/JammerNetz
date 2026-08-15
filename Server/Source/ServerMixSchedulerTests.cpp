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

TEST(ServerMixSchedulerTest, MaximumBufferPressureMixesAvailableClientsAndMarksTheMissingClient)
{
	TPacketStreamBundle clients;
	auto clientA = std::make_shared<ClientState>("a");
	auto clientB = std::make_shared<ClientState>("b");
	clients.emplace("a", clientA);
	clients.emplace("b", clientB);
	ServerMixScheduler scheduler(stereoMixdown(), { 1, 3, 0 });
	const auto now = ClientState::TimePoint{};

	clientA->push(makeSchedulerPacket(10), 0, now);
	clientB->push(makeSchedulerPacket(10), 0, now);
	clientA->push(makeSchedulerPacket(11), 0, now);
	clientB->push(makeSchedulerPacket(11), 0, now);
	ASSERT_EQ(scheduler.process(clients, now).incoming.size(), 2U);
	std::shared_ptr<JammerNetzAudioData> drained;
	bool isFillIn = false;
	std::uint64_t observedGeneration = 0;
	ASSERT_TRUE(clientB->tryPop(drained, isFillIn, observedGeneration));

	for (std::uint64_t counter = 12; counter <= 15; ++counter) {
		clientA->push(makeSchedulerPacket(counter), 0, now);
	}
	auto pressured = scheduler.process(clients, now);
	EXPECT_EQ(pressured.trigger, ServerMixTrigger::MaximumBufferPressure);
	EXPECT_TRUE(pressured.shouldWakeAgain);
	ASSERT_EQ(pressured.incoming.size(), 1U);
	EXPECT_EQ(pressured.incoming.begin()->first, "a");
	EXPECT_EQ(pressured.underrunClients, std::vector<std::string>({ "b" }));
	EXPECT_EQ(pressured.queuesAfter.at("b").state, ClientConnectionState::Disconnecting);
}

} // namespace
