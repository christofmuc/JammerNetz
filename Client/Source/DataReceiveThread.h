/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "JammerNetzPackage.h"

class DataReceiveThread : public Thread {
public:
	DataReceiveThread(DatagramSocket & socket, std::function<void(std::shared_ptr<JammerNetzAudioData>)> newDataHandler);
	virtual ~DataReceiveThread();

	virtual void run() override;

	bool isReceivingData() const;
	double currentRTT() const;

private:
	DatagramSocket &socket_;
	uint8 readbuffer_[MAXFRAMESIZE];
	std::function<void(std::shared_ptr<JammerNetzAudioData>)> newDataHandler_;
	double currentRTT_;
	BlowFish blowFish_;
	std::atomic<bool> isReceiving_;
};
