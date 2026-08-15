/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "Client.h"
#include "DataReceiveThread.h"

#include <atomic>

class JammerService {
public:
	JammerService(std::function<void(std::shared_ptr<JammerNetzAudioData>)> newDataHandler);
	virtual ~JammerService();

	void shutdown();

	Client* sender();
	DataReceiveThread* receiver();

	bool isReceivingData() const;
	double currentRTT() const;
	std::shared_ptr<JammerNetzClientInfoMessage> getClientInfo() const;
	JammerNetzChannelSetup getCurrentSessionSetup() const;
	uint64_t receiveErrorCount() const;
	bool isAvailable() const;
	juce::String startupError() const;

private:
	juce::DatagramSocket socket_;
	std::unique_ptr<Client> sender_;
	std::unique_ptr<DataReceiveThread> receiver_;
	std::atomic<bool> shutdown_ { false };
	juce::String startupError_;
};
