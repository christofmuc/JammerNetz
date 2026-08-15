/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "BuffersConfig.h"
#include "ClientState.h"

#include <gtest/gtest.h>

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
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

class ReusableBarrier {
public:
	explicit ReusableBarrier(const std::size_t participants)
		: participants_(participants)
	{
	}

	void arriveAndWait()
	{
		std::unique_lock lock(mutex_);
		const auto generation = generation_;
		if (++arrived_ == participants_) {
			arrived_ = 0;
			++generation_;
			lock.unlock();
			condition_.notify_all();
			return;
		}
		condition_.wait(lock, [this, generation] { return generation_ != generation; });
	}

private:
	const std::size_t participants_;
	std::mutex mutex_;
	std::condition_variable condition_;
	std::size_t arrived_ { 0 };
	std::size_t generation_ { 0 };
};

TEST(ReconnectStressTest, ConcurrentPacketAndStaleUnderrunDecisionAlwaysLeaveAConnectedClient)
{
	constexpr std::size_t iterations = 2000;
	ReusableBarrier phase(3);
	std::shared_ptr<ClientState> client;
	std::uint64_t observedGeneration = 0;

	std::thread packetThread([&] {
		for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
			phase.arriveAndWait();
			client->push(makeStressPacket(101), 0);
			phase.arriveAndWait();
		}
	});
	std::thread mixerThread([&] {
		for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
			phase.arriveAndWait();
			client->markUnderrun(observedGeneration);
			phase.arriveAndWait();
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
		phase.arriveAndWait();
		phase.arriveAndWait();

		const auto snapshot = client->snapshot();
		EXPECT_EQ(snapshot.state, ClientConnectionState::Connected) << "iteration=" << iteration;
		EXPECT_EQ(snapshot.size, 1U) << "iteration=" << iteration;
	}

	packetThread.join();
	mixerThread.join();
}

} // namespace
