/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "Client.h"
#include "DataReceiveThread.h"

class JammerService {
public:
	JammerService(std::function<void(std::shared_ptr<JammerNetzAudioData>)> newDataHandler);
	virtual ~JammerService();

	Client* sender();
	DataReceiveThread* receiver();

	bool isReceivingData() const
	{
		return receiver_->isReceivingData();
	}

	double currentRTT() const
	{
		return receiver_->currentRTT();
	}

	std::shared_ptr<JammerNetzClientInfoMessage> getClientInfo() const
	{
		return receiver_->getClientInfo();
	}

	JammerNetzChannelSetup getCurrentSessionSetup() const
	{
		return receiver_->sessionSetup();
	}

private:
	juce::DatagramSocket socket_;
	std::unique_ptr<Client> sender_;
	std::unique_ptr<DataReceiveThread> receiver_;
};
