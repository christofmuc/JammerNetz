/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "BuffersConfig.h"
#include "ClientState.h"

#include <gtest/gtest.h>

#include <barrier>
#include <cstdint>
#include <memory>
#include <thread>

namespace {

std::shared_ptr<JammerNetzAudioData> makeStressPacket(const std::uint64_t counter)
{
	auto audio = std::make_shared<AudioBuffer<float>>(1, SAMPLE_BUFFER_SIZE);
	audio->clear();
	JammerNetzChannelSetup setup(true);
	setup.channels.push_back(JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Mono));
	return std::make_shared<JammerNetzAudioData>(counter, static_cast<double>(counter), setup,
		SAMPLE_RATE, 0.0f, MidiSignal_None, std::move(audio), nullptr);
}

TEST(ReconnectStressTest, ConcurrentPacketAndStaleUnderrunDecisionAlwaysLeaveAConnectedClient)
{
	constexpr std::size_t iterations = 2000;
	std::barrier phase(3);
	std::shared_ptr<ClientState> client;
	std::uint64_t observedGeneration = 0;

	std::thread packetThread([&] {
		for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
			phase.arrive_and_wait();
			client->push(makeStressPacket(101), 0);
			phase.arrive_and_wait();
		}
	});
	std::thread mixerThread([&] {
		for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
			phase.arrive_and_wait();
			client->markUnderrun(observedGeneration);
			phase.arrive_and_wait();
		}
	});

	for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
		client = std::make_shared<ClientState>("same-endpoint");
		client->push(makeStressPacket(100), 0);
		std::shared_ptr<JammerNetzAudioData> primingPacket;
		bool isFillIn = false;
		const bool primed = client->tryPop(primingPacket, isFillIn, observedGeneration);
		EXPECT_TRUE(primed);
		if (!primed) {
			observedGeneration = client->snapshot().activityGeneration;
		}
		phase.arrive_and_wait();
		phase.arrive_and_wait();

		const auto snapshot = client->snapshot();
		EXPECT_EQ(snapshot.state, ClientConnectionState::Connected) << "iteration=" << iteration;
		EXPECT_EQ(snapshot.size, 1U) << "iteration=" << iteration;
	}

	packetThread.join();
	mixerThread.join();
}

} // namespace
