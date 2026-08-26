/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "SharedServerTypes.h"
#include "BuffersConfig.h"

#include "JammerNetzPackage.h"
#include "Encryption.h"

#include <atomic>

class PrintQualityTimer;

class AcceptThread : public Thread {
public:
	AcceptThread(int serverPort, DatagramSocket &socket,
                 CriticalSection& socketWriteLock,
                 TPacketStreamBundle &incomingData, TMessageQueue &wakeUpQueue,
                 TPeerEndpointMap &peerEndpoints,
                 ServerBufferConfig bufferConfig,
                 std::shared_ptr<const JammerNetzSecure::SessionKey> sessionKey,
                 std::shared_ptr<JammerNetzSecure::SecureDatagramSealer> serverSealer,
                 ValueTree serverConfiguration);
	virtual ~AcceptThread() override;

	virtual void run() override;

private:
    void processControlMessage(std::shared_ptr<JammerNetzControlMessage> message,
		const String& senderIPAddress, int senderPort, int receivedPayloadBytes);
	void sendMtuAcknowledgement(const String& senderIPAddress, int senderPort,
		uint64 probeId, int receivedPayloadBytes);
    void processAudioMessage(std::shared_ptr<JammerNetzAudioData> message, std::string const& clientName);

    DatagramSocket &receiveSocket_;
	CriticalSection& socketWriteLock_;
	TPacketStreamBundle &incomingData_;
	TPeerEndpointMap &peerEndpoints_;
	TMessageQueue &wakeUpQueue_;
    ValueTree serverConfiguration_;
	uint8 readbuffer[MAXFRAMESIZE + JammerNetzSecure::SecureDatagramSealer::WireOverhead];
	uint8 plaintextBuffer_[MAXFRAMESIZE];
	uint8 wireBuffer_[MAXFRAMESIZE + JammerNetzSecure::SecureDatagramSealer::WireOverhead];
	std::unique_ptr<PrintQualityTimer> qualityTimer_;
	ServerBufferConfig bufferConfig_;
	JammerNetzSecure::SecureDatagramOpener opener_;
	std::shared_ptr<JammerNetzSecure::SecureDatagramSealer> serverSealer_;
	std::atomic<std::uint64_t> rejectedDatagrams_{0};
};
