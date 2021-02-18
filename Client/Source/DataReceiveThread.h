/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "JammerNetzPackage.h"
#include "JammerNetzClientInfoMessage.h"

class DataReceiveThread : public Thread {
public:
	DataReceiveThread(DatagramSocket & socket, std::function<void(std::shared_ptr<JammerNetzAudioData>)> newDataHandler);
	virtual ~DataReceiveThread();

	virtual void run() override;

	bool isReceivingData() const;
	double currentRTT() const; 
	std::shared_ptr<JammerNetzClientInfoMessage> getClientInfo() const;

private:
	DatagramSocket &socket_;
	uint8 readbuffer_[MAXFRAMESIZE];
	std::function<void(std::shared_ptr<JammerNetzAudioData>)> newDataHandler_;
	std::unique_ptr<BlowFish> blowFish_;

	// Thread safe storage of info for the UI thread
	std::atomic<double> currentRTT_;	
	std::atomic<bool> isReceiving_;
	std::shared_ptr<JammerNetzClientInfoMessage> lastClientInfoMessage_;
};
