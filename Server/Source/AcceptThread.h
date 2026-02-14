/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "SharedServerTypes.h"
#include "BuffersConfig.h"

#include "JammerNetzPackage.h"
#include <atomic>
#include <map>
#include <tuple>

class PrintQualityTimer;

class AcceptThread : public Thread {
public:
	AcceptThread(int serverPort, DatagramSocket &socket,
	                 TPacketStreamBundle &incomingData, TMessageQueue &wakeUpQueue,
	                 ServerBufferConfig bufferConfig,
	                 void *keydata,
	                 int keysize,
	                 ValueTree serverConfiguration,
	                 ClientIdentityRegistry &clientIdentityRegistry,
	                 std::atomic<uint64_t> &sessionControlRevision);
	virtual ~AcceptThread() override;

	virtual void run() override;

private:
	void sendControlMessageToClient(const std::string& targetAddress, nlohmann::json payload);
    void processControlMessage(std::shared_ptr<JammerNetzControlMessage> message, std::string const& clientName);
    void processAudioMessage(std::shared_ptr<JammerNetzAudioData> message, std::string const& clientName);

    DatagramSocket &receiveSocket_;
	TPacketStreamBundle &incomingData_;
	TMessageQueue &wakeUpQueue_;
    ValueTree serverConfiguration_;
	ClientIdentityRegistry &clientIdentityRegistry_;
	std::atomic<uint64_t> &sessionControlRevision_;
	uint8 readbuffer[MAXFRAMESIZE];
	std::unique_ptr<PrintQualityTimer> qualityTimer_;
	ServerBufferConfig bufferConfig_;
	std::unique_ptr<BlowFish> blowFish_;
	std::map<std::tuple<uint32, uint32, uint16>, uint64_t> lastForwardedControlSequence_;
};
