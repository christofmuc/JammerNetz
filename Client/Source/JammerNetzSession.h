/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "Client.h"
#include "DataReceiveThread.h"
#include "ControlTransport.h"

#include <atomic>

struct JammerNetzSessionConfiguration {
	juce::String serverName;
	int serverPort { 7777 };
	bool useLocalhost { false };
	bool useFEC { false };
	std::shared_ptr<const juce::MemoryBlock> cryptoKey;
};

class JammerNetzSession {
public:
	JammerNetzSession() = default;
	virtual ~JammerNetzSession();

	// Lifecycle calls are owned by AudioService and must be serialized on its message thread.
	bool start(std::function<void(std::shared_ptr<JammerNetzAudioData>)> newDataHandler,
		const JammerNetzSessionConfiguration& configuration);
	void updateConfiguration(const JammerNetzSessionConfiguration& configuration);
	void shutdown();

	Client* sender();
	DataReceiveThread* receiver();

	bool isReceivingData() const;
	double currentRTT() const;
	int safeUdpPayloadSize() const;
	PathMtuDiscoveryStatus mtuDiscoveryStatus() const;
	std::shared_ptr<JammerNetzClientInfoMessage> getClientInfo() const;
	JammerNetzChannelSetup getCurrentSessionSetup() const;
	bool sendControl(const std::string& topic, nlohmann::json payload,
		JammerNetzControlRoute route = JammerNetzControlRoute::Server,
		uint32_t targetId = 0,
		JammerNetzControlDelivery delivery = JammerNetzControlDelivery::Ephemeral,
		bool includeSender = false,
		uint64_t sequence = 0);
	bool pollControlEvent(JammerNetzControlEnvelopeData& envelope);
	bool isControlReady() const;
	uint32_t controlParticipantId() const;
	ControlTransportStats controlStats() const;
	uint64_t receiveErrorCount() const;
	bool isAvailable() const;
	juce::String startupError() const;

private:
	std::unique_ptr<juce::DatagramSocket> socket_;
	std::unique_ptr<Client> sender_;
	std::unique_ptr<DataReceiveThread> receiver_;
	std::unique_ptr<ControlTransport> controlTransport_;
	std::atomic<bool> shutdown_ { true };
	juce::String startupError_;
};
