#include "ClientState.h"
#include "SharedServerTypes.h"

#include "BuffersConfig.h"

#include "gtest/gtest.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace {

std::shared_ptr<JammerNetzAudioData> makePacket(std::uint64_t counter) {
	auto buffer = std::make_shared<AudioBuffer<float>>(2, SAMPLE_BUFFER_SIZE);
	JammerNetzChannelSetup setup(false);
	setup.channels.push_back(JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Left));
	setup.channels.push_back(JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Right));
	return std::make_shared<JammerNetzAudioData>(counter, 1234.0, setup, SAMPLE_RATE, 0.0f,
		MidiSignal_None, buffer, nullptr);
}

TEST(ClientStateTest, CoversInitialConnectionGraceRecoveryDisconnectAndReconnect) {
	ClientState client("127.0.0.1:1234");
	const auto start = ClientState::TimePoint{};

	auto initial = client.push(makePacket(100), 2, start);
	EXPECT_TRUE(initial.queued);
	EXPECT_EQ(initial.transition, ClientConnectionTransition::InitialConnection);
	auto initialSnapshot = client.snapshot();
	EXPECT_EQ(initialSnapshot.state, ClientConnectionState::Connected);
	EXPECT_EQ(initialSnapshot.size, 3u);

	EXPECT_TRUE(client.markUnderrun(initialSnapshot.activityGeneration, start));
	EXPECT_EQ(client.snapshot().state, ClientConnectionState::Disconnecting);

	auto recovery = client.push(makePacket(101), 2, start + std::chrono::milliseconds(500));
	EXPECT_TRUE(recovery.queued);
	EXPECT_EQ(recovery.transition, ClientConnectionTransition::GraceRecovery);
	EXPECT_EQ(client.snapshot().state, ClientConnectionState::Connected);

	auto recoverySnapshot = client.snapshot();
	EXPECT_TRUE(client.markUnderrun(recoverySnapshot.activityGeneration, start + std::chrono::seconds(1)));
	EXPECT_FALSE(client.disconnectIfGraceExpired(start + std::chrono::seconds(2)));
	EXPECT_TRUE(client.disconnectIfGraceExpired(start + std::chrono::seconds(3)));
	EXPECT_EQ(client.snapshot().state, ClientConnectionState::Disconnected);
	EXPECT_EQ(client.snapshot().size, 0u);

	auto reconnect = client.push(makePacket(1), 2, start + std::chrono::seconds(4));
	EXPECT_TRUE(reconnect.queued);
	EXPECT_EQ(reconnect.transition, ClientConnectionTransition::Reconnection);
	EXPECT_EQ(client.snapshot().state, ClientConnectionState::Connected);
	EXPECT_EQ(client.snapshot().size, 1u); // Reconnects intentionally do not prefill.
}

TEST(ClientStateTest, RejectsAnUnderrunDecisionMadeBeforeConcurrentPacketActivity) {
	ClientState client("127.0.0.1:1234");
	client.push(makePacket(100), 0);
	const auto staleGeneration = client.snapshot().activityGeneration;
	client.push(makePacket(101), 0);

	EXPECT_FALSE(client.markUnderrun(staleGeneration));
	EXPECT_EQ(client.snapshot().state, ClientConnectionState::Connected);
}

TEST(ClientStateTest, SupportsConcurrentPublicationMixSendAndStatisticsAccess) {
	TPacketStreamBundle clients;
	std::atomic<bool> accepting{true};
	std::atomic<int> readersReady{0};
	constexpr int clientCount = 16;
	constexpr int packetCount = 2000;

	std::thread acceptThread([&] {
		while (readersReady.load() != 3) {
			std::this_thread::yield();
		}
		for (int i = 0; i < packetCount; ++i) {
			const auto name = "127.0.0.1:" + std::to_string(1000 + (i % clientCount));
			auto insertion = clients.insert(std::make_pair(name, std::make_shared<ClientState>(name)));
			insertion.first->second->push(
				makePacket(static_cast<std::uint64_t>(100 + i / clientCount)), 0);
		}
		accepting = false;
	});

	std::thread mixThread([&] {
		++readersReady;
		while (accepting.load()) {
			for (auto &entry : clients) {
				std::shared_ptr<JammerNetzAudioData> packet;
				bool isFillIn = false;
				std::uint64_t generation = 0;
				if (!entry.second->tryPop(packet, isFillIn, generation)) {
					entry.second->markUnderrun(generation);
				}
				entry.second->disconnectIfGraceExpired();
			}
		}
	});

	auto inspectClients = [&] {
		++readersReady;
		while (accepting.load()) {
			for (auto &entry : clients) {
				const auto snapshot = entry.second->snapshot();
				JammerNetzStreamQualityInfo qualityInfo;
				if (snapshot.size > 0) {
					entry.second->qualityInfo(qualityInfo);
				}
			}
		}
	};
	std::thread sendThread(inspectClients);
	std::thread statisticsThread(inspectClients);

	acceptThread.join();
	mixThread.join();
	sendThread.join();
	statisticsThread.join();

	EXPECT_EQ(clients.size(), static_cast<std::size_t>(clientCount));
	for (const auto &entry : clients) {
		EXPECT_NE(entry.second, nullptr);
	}
}

} // namespace
