/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ServerMixScheduler.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

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
	EXPECT_EQ(pressured.trigger, ServerMixTrigger::MaximumBufferPressure);
	EXPECT_FALSE(pressured.shouldWakeAgain);
	EXPECT_EQ(pressured.incoming.size(), 2U);
	EXPECT_EQ(pressured.mix.outgoing.size(), 2U);
	EXPECT_TRUE(pressured.underrunClients.empty());
	ASSERT_EQ(pressured.fastForwardedClients.size(), 1U);
	EXPECT_EQ(pressured.fastForwardedClients.at("a").discardedPackets, 4U);
	ASSERT_TRUE(pressured.fastForwardedClients.at("a").oldestRetainedCounter.has_value());
	EXPECT_EQ(*pressured.fastForwardedClients.at("a").oldestRetainedCounter, 15U);
	EXPECT_EQ(pressured.queuesAfter.at("a").size, 0U);
	EXPECT_EQ(pressured.queuesAfter.at("b").size, 0U);
	EXPECT_EQ(pressured.queuesAfter.at("b").state, ClientConnectionState::Connected);
}

const OutgoingPackage* findOutput(const ServerScheduledMixResult& result, const std::string& target)
{
	const auto found = std::find_if(result.mix.outgoing.begin(), result.mix.outgoing.end(),
		[&target](const OutgoingPackage& outgoing) { return outgoing.targetAddress == target; });
	return found == result.mix.outgoing.end() ? nullptr : &*found;
}

TEST(ServerMixSchedulerTest, SustainedFasterStreamKeepsRoomCadenceWithoutGrowingLatency)
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
	std::size_t mixes = 0;
	std::size_t slowerSilenceTicks = 0;
	for (int cycle = 0; cycle < 40; ++cycle) {
		for (int packet = 0; packet < 2; ++packet) {
			faster->push(makeSchedulerPacket(fasterCounter++), 0);
			const auto step = scheduler.process(clients);
			EXPECT_TRUE(step.fillInClients.empty());
			ASSERT_EQ(step.mix.outgoing.size(), 2U);
			EXPECT_EQ(step.contributions.at("faster"), ServerSourceContribution::Packet);
			if (step.contributions.at("slower") == ServerSourceContribution::Silence) {
				++slowerSilenceTicks;
			}
			EXPECT_LE(step.queuesAfter.at("faster").size, 4U);
			EXPECT_LE(step.queuesAfter.at("slower").size, 2U);
			++mixes;
		}

		slower->push(makeSchedulerPacket(slowerCounter++), 0);
		const auto step = scheduler.process(clients);
		EXPECT_TRUE(step.mix.outgoing.empty());
		EXPECT_LE(step.queuesAfter.at("faster").size, 4U);
		EXPECT_LE(step.queuesAfter.at("slower").size, 2U);
	}

	EXPECT_EQ(mixes, 80U);
	EXPECT_GT(slowerSilenceTicks, 0U);
	EXPECT_EQ(slower->snapshot().state, ClientConnectionState::Connected);
}

TEST(ServerMixSchedulerTest, MissingUploadStillReceivesTheUnaffectedRoomMix)
{
	TPacketStreamBundle clients;
	auto clientA = std::make_shared<ClientState>("client-a");
	auto clientB = std::make_shared<ClientState>("client-b");
	auto clientC = std::make_shared<ClientState>("client-c");
	clients.emplace("client-a", clientA);
	clients.emplace("client-b", clientB);
	clients.emplace("client-c", clientC);
	ServerMixScheduler scheduler(stereoMixdown(), { 1, 3, 0 });

	for (std::uint64_t counter = 10; counter <= 11; ++counter) {
		clientA->push(makeSchedulerPacket(counter), 0);
		clientB->push(makeSchedulerPacket(counter), 0);
		clientC->push(makeSchedulerPacket(counter), 0);
	}
	const auto clean = scheduler.process(clients);
	ASSERT_EQ(clean.mix.outgoing.size(), 3U);

	std::shared_ptr<JammerNetzAudioData> discarded;
	bool isFillIn = false;
	std::uint64_t generation = 0;
	ASSERT_TRUE(clientB->tryPop(discarded, isFillIn, generation));
	ASSERT_EQ(clientB->snapshot().size, 0U);

	clientA->push(makeSchedulerPacket(12), 0);
	clientC->push(makeSchedulerPacket(12), 0);
	const auto impaired = scheduler.process(clients);

	EXPECT_EQ(impaired.cadenceClient, "client-a");
	EXPECT_EQ(impaired.incoming.size(), 2U);
	EXPECT_EQ(impaired.incoming.count("client-b"), 0U);
	EXPECT_EQ(impaired.contributions.at("client-b"), ServerSourceContribution::Silence);
	ASSERT_EQ(impaired.mix.outgoing.size(), 3U);
	EXPECT_NE(findOutput(impaired, "client-a"), nullptr);
	EXPECT_NE(findOutput(impaired, "client-b"), nullptr);
	EXPECT_NE(findOutput(impaired, "client-c"), nullptr);
}

TEST(ServerMixSchedulerTest, MissingCadenceClientFailsOverWithoutStoppingItsDownload)
{
	TPacketStreamBundle clients;
	auto clientA = std::make_shared<ClientState>("client-a");
	auto clientB = std::make_shared<ClientState>("client-b");
	auto clientC = std::make_shared<ClientState>("client-c");
	clients.emplace("client-a", clientA);
	clients.emplace("client-b", clientB);
	clients.emplace("client-c", clientC);
	ServerMixScheduler scheduler(stereoMixdown(), { 1, 3, 0 });

	for (std::uint64_t counter = 10; counter <= 11; ++counter) {
		clientA->push(makeSchedulerPacket(counter), 0);
		clientB->push(makeSchedulerPacket(counter), 0);
		clientC->push(makeSchedulerPacket(counter), 0);
	}
	const auto clean = scheduler.process(clients);
	ASSERT_EQ(clean.cadenceClient, "client-a");

	std::shared_ptr<JammerNetzAudioData> discarded;
	bool isFillIn = false;
	std::uint64_t generation = 0;
	ASSERT_TRUE(clientA->tryPop(discarded, isFillIn, generation));
	clientB->push(makeSchedulerPacket(12), 0);
	clientC->push(makeSchedulerPacket(12), 0);

	const auto failedOver = scheduler.process(clients);

	EXPECT_EQ(failedOver.trigger, ServerMixTrigger::CadenceClientFailover);
	EXPECT_TRUE(failedOver.cadenceClientChanged);
	EXPECT_EQ(failedOver.cadenceClient, "client-b");
	EXPECT_EQ(failedOver.contributions.at("client-a"), ServerSourceContribution::Silence);
	ASSERT_EQ(failedOver.mix.outgoing.size(), 3U);
	EXPECT_NE(findOutput(failedOver, "client-a"), nullptr);
}

TEST(ServerMixSchedulerTest, LowerHealthCandidateStillTakesOverAStalledCadence)
{
	TPacketStreamBundle clients;
	auto clientA = std::make_shared<ClientState>("client-a");
	auto clientB = std::make_shared<ClientState>("client-b");
	clients.emplace("client-a", clientA);
	clients.emplace("client-b", clientB);
	ServerMixScheduler scheduler(stereoMixdown(), { 1, 3, 0 });

	for (std::uint64_t counter = 10; counter <= 15; ++counter) {
		clientA->push(makeSchedulerPacket(counter), 0);
		clientB->push(makeSchedulerPacket(counter), 0);
		scheduler.process(clients);
	}

	std::shared_ptr<JammerNetzAudioData> discarded;
	bool isFillIn = false;
	std::uint64_t generation = 0;
	ASSERT_TRUE(clientB->tryPop(discarded, isFillIn, generation));
	clientA->push(makeSchedulerPacket(16), 0);
	const auto degradedB = scheduler.process(clients);
	ASSERT_EQ(degradedB.contributions.at("client-b"), ServerSourceContribution::Silence);

	clientB->push(makeSchedulerPacket(16), 0);
	clientA->push(makeSchedulerPacket(17), 0);
	clientB->push(makeSchedulerPacket(17), 0);
	ASSERT_EQ(scheduler.process(clients).contributions.at("client-b"),
		ServerSourceContribution::Packet);

	clientA->push(makeSchedulerPacket(18), 0);
	clientB->push(makeSchedulerPacket(18), 0);
	ASSERT_EQ(scheduler.process(clients).contributions.at("client-b"),
		ServerSourceContribution::Packet);

	ASSERT_TRUE(clientA->tryPop(discarded, isFillIn, generation));
	clientB->push(makeSchedulerPacket(19), 0);
	const auto failedOver = scheduler.process(clients);

	EXPECT_EQ(failedOver.trigger, ServerMixTrigger::CadenceClientFailover);
	EXPECT_EQ(failedOver.cadenceClient, "client-b");
	EXPECT_EQ(failedOver.mix.outgoing.size(), 2U);
	EXPECT_NE(findOutput(failedOver, "client-a"), nullptr);
}

TEST(ServerMixSchedulerTest, NewCadenceCandidateTakesOverAfterOneFrameGrace)
{
	TPacketStreamBundle clients;
	auto clientA = std::make_shared<ClientState>("client-a");
	clients.emplace("client-a", clientA);
	ServerMixScheduler scheduler(stereoMixdown(), { 1, 3, 0 });
	const auto start = ClientState::TimePoint {};

	clientA->push(makeSchedulerPacket(10), 0);
	ASSERT_EQ(scheduler.process(clients, start).cadenceClient, "client-a");

	auto clientB = std::make_shared<ClientState>("client-b");
	clients.emplace("client-b", clientB);
	clientB->push(makeSchedulerPacket(10), 0);
	clientB->push(makeSchedulerPacket(11), 0);

	const auto waiting = scheduler.process(clients, start);
	EXPECT_EQ(waiting.trigger, ServerMixTrigger::None);
	EXPECT_TRUE(waiting.mix.outgoing.empty());

	const auto failedOver = scheduler.process(clients,
		start + ServerMixScheduler::CadenceFailoverGracePeriod);
	EXPECT_EQ(failedOver.trigger, ServerMixTrigger::CadenceClientFailover);
	EXPECT_EQ(failedOver.cadenceClient, "client-b");
	EXPECT_EQ(failedOver.contributions.at("client-a"), ServerSourceContribution::Silence);
	EXPECT_EQ(failedOver.mix.outgoing.size(), 2U);
}

TEST(ServerMixSchedulerTest, HealthyCadenceIsNotStolenByANewSlottedClient)
{
	TPacketStreamBundle clients;
	auto clientA = std::make_shared<ClientState>("client-a");
	auto clientB = std::make_shared<ClientState>("client-b");
	auto wifiClient = std::make_shared<ClientState>("client-wifi");
	clients.emplace("client-a", clientA);
	clients.emplace("client-b", clientB);
	clients.emplace("client-wifi", wifiClient);
	ServerMixScheduler scheduler(stereoMixdown(), { 1, 3, 0 });

	for (std::uint64_t counter = 10; counter <= 16; ++counter) {
		clientA->push(makeSchedulerPacket(counter), 0);
		clientB->push(makeSchedulerPacket(counter), 0);
		const auto step = scheduler.process(clients);
		if (counter > 10) {
			ASSERT_EQ(step.mix.outgoing.size(), 2U);
		}
	}

	for (std::uint64_t counter = 10; counter <= 17; ++counter) {
		wifiClient->push(makeSchedulerPacket(counter), counter == 10 ? 1 : 0);
	}
	const auto burst = scheduler.process(clients);
	EXPECT_TRUE(burst.mix.outgoing.empty());
	EXPECT_EQ(burst.cadenceClient, "client-a");
	EXPECT_FALSE(burst.cadenceClientChanged);
	EXPECT_EQ(burst.fastForwardedClients.count("client-wifi"), 1U);

	clientA->push(makeSchedulerPacket(17), 0);
	clientB->push(makeSchedulerPacket(17), 0);
	const auto nextStableTick = scheduler.process(clients);
	EXPECT_EQ(nextStableTick.cadenceClient, "client-a");
	EXPECT_EQ(nextStableTick.mix.outgoing.size(), 3U);
}

TEST(ServerMixSchedulerTest, SlottedOutlierDoesNotControlStableRoomCadence)
{
	TPacketStreamBundle clients;
	std::vector<std::shared_ptr<ClientState>> stableClients;
	for (const auto* name : { "client-a", "client-b", "client-c", "client-d" }) {
		auto client = std::make_shared<ClientState>(name);
		clients.emplace(name, client);
		stableClients.push_back(std::move(client));
	}
	auto wifiClient = std::make_shared<ClientState>("client-wifi");
	clients.emplace("client-wifi", wifiClient);
	ServerMixScheduler scheduler(stereoMixdown(), { 1, 3, 0 });

	for (std::uint64_t counter = 100; counter <= 101; ++counter) {
		for (auto& client : stableClients) {
			client->push(makeSchedulerPacket(counter), 0);
		}
		wifiClient->push(makeSchedulerPacket(counter), 0);
	}
	ASSERT_EQ(scheduler.process(clients).mix.outgoing.size(), 5U);

	std::size_t mixes = 0;
	std::size_t wifiSilenceTicks = 0;
	for (std::uint64_t counter = 102; counter < 118; ++counter) {
		for (auto& client : stableClients) {
			client->push(makeSchedulerPacket(counter), 0);
		}
		if ((counter - 102) % 8 == 7) {
			for (std::uint64_t released = counter - 7; released <= counter; ++released) {
				wifiClient->push(makeSchedulerPacket(released), 0);
			}
		}

		const auto step = scheduler.process(clients);
		ASSERT_EQ(step.mix.outgoing.size(), 5U) << "counter=" << counter;
		EXPECT_EQ(step.cadenceClient, "client-a");
		EXPECT_NE(findOutput(step, "client-wifi"), nullptr);
		for (const auto* stable : { "client-a", "client-b", "client-c", "client-d" }) {
			ASSERT_EQ(step.incoming.count(stable), 1U) << "counter=" << counter;
			EXPECT_EQ(step.incoming.at(stable)->messageCounter(), counter - 1U);
		}
		if (step.contributions.at("client-wifi") == ServerSourceContribution::Silence) {
			++wifiSilenceTicks;
		}
		++mixes;
	}
	EXPECT_EQ(mixes, 16U);
	EXPECT_GT(wifiSilenceTicks, 0U);
}

} // namespace
