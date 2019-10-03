/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "SharedServerTypes.h"

class PrintQualityTimer;

class AcceptThread : public Thread {
public:
	AcceptThread(DatagramSocket &socket, TPacketStreamBundle &incomingData, TMessageQueue &wakeUpQueue);
	~AcceptThread();

	virtual void run() override;

private:
	DatagramSocket &receiveSocket_;
	TPacketStreamBundle &incomingData_;
	TMessageQueue &wakeUpQueue_;
	uint8 readbuffer[MAXFRAMESIZE];
	std::unique_ptr<PrintQualityTimer> qualityTimer_;
	BlowFish blowFish_;
};
