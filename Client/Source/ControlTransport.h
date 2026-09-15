/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"
#include "ControlProtocol.h"

#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <string>

class Client;

struct ControlTransportStats {
	uint64_t sent { 0 };
	uint64_t received { 0 };
	uint64_t retries { 0 };
	uint64_t outgoingOverflows { 0 };
	uint64_t incomingOverflows { 0 };
	uint64_t eventOverflows { 0 };
	uint64_t acknowledgementTimeouts { 0 };
};

class ControlTransport : public Thread {
public:
	explicit ControlTransport(Client& client);
	~ControlTransport() override;

	void run() override;
	void shutdown();
	void setSupported(bool supported);
	bool enqueueIncoming(const JammerNetzControlEnvelopeData& envelope);
	bool send(const std::string& topic, nlohmann::json payload,
		JammerNetzControlRoute route = JammerNetzControlRoute::Server,
		uint32_t targetId = 0,
		JammerNetzControlDelivery delivery = JammerNetzControlDelivery::Ephemeral,
		bool includeSender = false,
		uint64_t sequence = 0);
	bool pollEvent(JammerNetzControlEnvelopeData& envelope);

	[[nodiscard]] bool isReady() const noexcept;
	[[nodiscard]] uint32_t participantId() const noexcept;
	[[nodiscard]] uint64_t sessionEpoch() const noexcept;
	[[nodiscard]] ControlTransportStats stats() const noexcept;

private:
	struct PendingAcknowledgement {
		JammerNetzControlEnvelopeData envelope;
		uint32_t lastSendMillis { 0 };
		int attempts { 0 };
	};

	bool enqueueOutgoing(JammerNetzControlEnvelopeData envelope);
	void processIncoming();
	void processOutgoing();
	void retryAcknowledged();
	void publishEvent(JammerNetzControlEnvelopeData envelope);
	void sendHello();
	void acceptWelcome(const JammerNetzControlEnvelopeData& envelope);

	static constexpr std::size_t QueueCapacity = 128;
	static constexpr uint32_t RetryIntervalMillis = 250;
	static constexpr int MaximumAttempts = 3;

	Client& client_;
	WaitableEvent wakeEvent_;
	mutable std::mutex queueMutex_;
	std::deque<JammerNetzControlEnvelopeData> incoming_;
	std::deque<JammerNetzControlEnvelopeData> outgoing_;
	std::deque<JammerNetzControlEnvelopeData> events_;
	std::map<uint64_t, PendingAcknowledgement> pending_;
	std::string clientInstance_;
	std::atomic<bool> supported_ { false };
	std::atomic<bool> helloQueued_ { false };
	std::atomic<uint32_t> participantId_ { 0 };
	std::atomic<uint64_t> sessionEpoch_ { 0 };
	std::atomic<uint64_t> nextMessageId_ { 1 };
	std::atomic<uint64_t> sent_ { 0 };
	std::atomic<uint64_t> received_ { 0 };
	std::atomic<uint64_t> retries_ { 0 };
	std::atomic<uint64_t> outgoingOverflows_ { 0 };
	std::atomic<uint64_t> incomingOverflows_ { 0 };
	std::atomic<uint64_t> eventOverflows_ { 0 };
	std::atomic<uint64_t> acknowledgementTimeouts_ { 0 };
};
