/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "SharedServerTypes.h"
#include "BuffersConfig.h"

#include "JammerNetzPackage.h"

class PrintQualityTimer;

class AcceptThread : public Thread {
public:
	AcceptThread(int serverPort, DatagramSocket &socket,
                 TPacketStreamBundle &incomingData, TMessageQueue &wakeUpQueue,
                 ServerBufferConfig bufferConfig,
                 void *keydata,
                 int keysize,
                 ValueTree serverConfiguration);
	virtual ~AcceptThread() override;

	virtual void run() override;

private:
    void processControlMessage(std::shared_ptr<JammerNetzControlMessage> message);
    void processAudioMessage(std::shared_ptr<JammerNetzAudioData> message, std::string const& clientName);

    DatagramSocket &receiveSocket_;
	TPacketStreamBundle &incomingData_;
	TMessageQueue &wakeUpQueue_;
    ValueTree serverConfiguration_;
	uint8 readbuffer[MAXFRAMESIZE];
	std::unique_ptr<PrintQualityTimer> qualityTimer_;
	ServerBufferConfig bufferConfig_;
	std::unique_ptr<BlowFish> blowFish_;
};
