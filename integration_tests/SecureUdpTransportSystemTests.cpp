/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "AcceptThread.h"
#include "BuffersConfig.h"
#include "Client.h"
#include "DataReceiveThread.h"
#include "SendThread.h"

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

constexpr auto networkTimeout = 5s;

template <typename Predicate>
bool waitUntil(Predicate&& predicate, const std::chrono::milliseconds timeout = networkTimeout)
{
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	while (std::chrono::steady_clock::now() < deadline) {
		if (predicate()) return true;
		std::this_thread::sleep_for(10ms);
	}
	return predicate();
}

JammerNetzChannelSetup monoSetup()
{
	JammerNetzChannelSetup setup(false);
	setup.channels.push_back(JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Mono));
	return setup;
}

std::shared_ptr<AudioBuffer<float>> audioWithValue(const float value)
{
	auto audio = std::make_shared<AudioBuffer<float>>(1, SAMPLE_BUFFER_SIZE);
	for (int sample = 0; sample < SAMPLE_BUFFER_SIZE; ++sample) {
		audio->setSample(0, sample, value);
	}
	return audio;
}

std::shared_ptr<JammerNetzSecure::SessionKey> sessionKey(const std::uint8_t idByte,
	const std::uint8_t keyByte)
{
	JammerNetzSecure::SessionId id{};
	JammerNetzSecure::MasterKey key{};
	id.fill(idByte);
	key.fill(keyByte);
	return std::make_shared<JammerNetzSecure::SessionKey>(id, key);
}

class SecureUdpTransportSystemTest : public testing::Test {
protected:
	void SetUp() override
	{
		key_ = sessionKey(0x21, 0x43);
		serverSealer_ = std::make_shared<JammerNetzSecure::SecureDatagramSealer>(
			key_, JammerNetzSecure::Direction::ServerToClient);
		serverConfiguration_.setProperty("FEC", false, nullptr);
		outgoing_.set_capacity(32);

		acceptThread_ = std::make_unique<AcceptThread>(0, serverSocket_, serverSocketLock_,
			incoming_, wakeups_, endpoints_, ServerBufferConfig { 0, 128, 0 }, key_, serverSealer_,
			serverConfiguration_);
		ASSERT_GT(serverSocket_.getBoundPort(), 0);
		sendThread_ = std::make_unique<SendThread>(serverSocket_, serverSocketLock_, outgoing_,
			incoming_, endpoints_, serverSealer_, serverConfiguration_);

		ASSERT_TRUE(clientSocket_.bindToPort(0, "127.0.0.1"));
		client_ = std::make_unique<Client>(clientSocket_);
		client_->setServer("127.0.0.1", serverSocket_.getBoundPort(), false);
		client_->setSessionKey(key_);
		receiveThread_ = std::make_unique<DataReceiveThread>(clientSocket_,
			[this](std::shared_ptr<JammerNetzAudioData> packet) {
				{
					std::lock_guard<std::mutex> lock(receivedMutex_);
					received_.push_back(std::move(packet));
				}
				receivedCondition_.notify_all();
			}, nullptr, nullptr);
		receiveThread_->setSessionKey(key_);

		acceptThread_->startThread();
		sendThread_->startThread();
		receiveThread_->startThread();
	}

	void TearDown() override
	{
		if (receiveThread_) receiveThread_->signalThreadShouldExit();
		clientSocket_.shutdown();
		if (receiveThread_) EXPECT_TRUE(receiveThread_->waitForThreadToExit(2000));

		if (sendThread_) sendThread_->signalThreadShouldExit();
		outgoing_.push(OutgoingPackage {});
		if (sendThread_) EXPECT_TRUE(sendThread_->waitForThreadToExit(2000));

		if (acceptThread_) acceptThread_->signalThreadShouldExit();
		if (acceptThread_) EXPECT_TRUE(acceptThread_->waitForThreadToExit(2000));
		serverSocket_.shutdown();
	}

	std::size_t receivedCount() const
	{
		std::lock_guard<std::mutex> lock(receivedMutex_);
		return received_.size();
	}

	bool waitForReceived(const std::size_t count)
	{
		std::unique_lock<std::mutex> lock(receivedMutex_);
		return receivedCondition_.wait_for(lock, networkTimeout,
			[this, count] { return received_.size() >= count; });
	}

	std::vector<std::shared_ptr<JammerNetzAudioData>> receivedPackets() const
	{
		std::lock_guard<std::mutex> lock(receivedMutex_);
		return received_;
	}

	DatagramSocket serverSocket_;
	CriticalSection serverSocketLock_;
	TPacketStreamBundle incoming_;
	TPeerEndpointMap endpoints_;
	TOutgoingQueue outgoing_;
	TMessageQueue wakeups_;
	ValueTree serverConfiguration_ { "SERVER_CONFIG" };
	std::shared_ptr<JammerNetzSecure::SessionKey> key_;
	std::shared_ptr<JammerNetzSecure::SecureDatagramSealer> serverSealer_;
	std::unique_ptr<AcceptThread> acceptThread_;
	std::unique_ptr<SendThread> sendThread_;

	DatagramSocket clientSocket_;
	std::unique_ptr<Client> client_;
	std::unique_ptr<DataReceiveThread> receiveThread_;
	mutable std::mutex receivedMutex_;
	std::condition_variable receivedCondition_;
	std::vector<std::shared_ptr<JammerNetzAudioData>> received_;
};

TEST_F(SecureUdpTransportSystemTest, RealSocketsCarryRepeatedAudioBothWaysAndRejectWrongKeys)
{
	const auto setup = monoSetup();
	for (int packet = 0; packet < 3; ++packet) {
		ASSERT_TRUE(client_->sendData(setup, audioWithValue(0.1f * static_cast<float>(packet + 1)), {}));
	}

	ASSERT_TRUE(waitUntil([this] {
		if (incoming_.size() != 1) return false;
		const auto state = incoming_.begin()->second;
		return state && state->snapshot().size >= 3;
	})) << "server did not accept three consecutive equal-sized client datagrams";
	ASSERT_EQ(endpoints_.size(), 1U);
	const auto peerId = endpoints_.begin()->first;
	const auto serverState = incoming_.begin()->second;
	ASSERT_NE(serverState, nullptr);
	EXPECT_EQ(serverState->snapshot().size, 3U);

	Client wrongKeyClient(clientSocket_);
	wrongKeyClient.setServer("127.0.0.1", serverSocket_.getBoundPort(), false);
	wrongKeyClient.setSessionKey(sessionKey(0x65, 0x87));
	ASSERT_TRUE(wrongKeyClient.sendData(setup, audioWithValue(0.9f), {}));
	ASSERT_TRUE(client_->sendData(setup, audioWithValue(0.4f), {}));
	ASSERT_TRUE(waitUntil([serverState] { return serverState->snapshot().size >= 4; }));
	EXPECT_EQ(incoming_.size(), 1U);
	EXPECT_EQ(serverState->snapshot().size, 4U);

	for (std::uint64_t counter = 100; counter < 103; ++counter) {
		AudioBlock block(Time::getMillisecondCounterHiRes(), counter, counter * SAMPLE_BUFFER_SIZE,
			120.0f, MidiSignal_None, SAMPLE_RATE, setup,
			audioWithValue(static_cast<float>(counter) / 100.0f));
		outgoing_.push(OutgoingPackage(peerId, block, setup, JammerNetzProtocol::Current));
	}
	ASSERT_TRUE(waitForReceived(3)) << "client did not authenticate three real server responses";

	auto packets = receivedPackets();
	ASSERT_GE(packets.size(), 3U);
	for (std::size_t index = 0; index < 3; ++index) {
		ASSERT_NE(packets[index], nullptr);
		EXPECT_EQ(packets[index]->messageCounter(), 100U + index);
	}

	const auto wrongReceiveKey = sessionKey(0x89, 0xab);
	receiveThread_->setSessionKey(wrongReceiveKey);
	const auto errorsBeforeMismatch = receiveThread_->receiveErrorCount();
	AudioBlock rejectedBlock(Time::getMillisecondCounterHiRes(), 103, 103 * SAMPLE_BUFFER_SIZE,
		120.0f, MidiSignal_None, SAMPLE_RATE, setup, audioWithValue(1.03f));
	outgoing_.push(OutgoingPackage(peerId, rejectedBlock, setup, JammerNetzProtocol::Current));
	ASSERT_TRUE(waitUntil([this, errorsBeforeMismatch] {
		return receiveThread_->receiveErrorCount() > errorsBeforeMismatch;
	})) << "client did not reject a server datagram under the wrong key";
	EXPECT_EQ(receivedCount(), 3U);

	receiveThread_->setSessionKey(key_);
	AudioBlock recoveredBlock(Time::getMillisecondCounterHiRes(), 104, 104 * SAMPLE_BUFFER_SIZE,
		120.0f, MidiSignal_None, SAMPLE_RATE, setup, audioWithValue(1.04f));
	outgoing_.push(OutgoingPackage(peerId, recoveredBlock, setup, JammerNetzProtocol::Current));
	ASSERT_TRUE(waitForReceived(4));
	packets = receivedPackets();
	ASSERT_GE(packets.size(), 4U);
	EXPECT_EQ(packets[3]->messageCounter(), 104U);
}

} // namespace
