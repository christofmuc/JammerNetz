/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "SharedServerTypes.h"
#include "JammerNetzPackage.h"
#include "RingOfAudioBuffers.h"
#include "Encryption.h"

class SendThread : public Thread {
public:
	SendThread(DatagramSocket& socket, CriticalSection& socketWriteLock,
		TOutgoingQueue &sendQueue, TPacketStreamBundle &incomingData,
		TPeerEndpointMap &peerEndpoints,
		std::shared_ptr<JammerNetzSecure::SecureDatagramSealer> serverSealer,
		ValueTree serverConfiguration);

	virtual void run() override;

private:
	bool determineTargetIP(std::string const &peerId, String &ipAddress, int &portNumber) const;
	void sendWriteBuffer(std::string const &peerId, size_t size);
    void sendSessionInfoPackage(std::string const &targetAddress, JammerNetzChannelSetup &sessionSetup);
    void sendClientInfoPackage(std::string const &targetAddress);
	void sendAudioBlock(OutgoingPackage const &package);

	TOutgoingQueue& sendQueue_;
	TPacketStreamBundle &incomingData_;
	TPeerEndpointMap &peerEndpoints_;
	DatagramSocket& sendSocket_;
	CriticalSection& socketWriteLock_;
    ValueTree serverConfiguration_;
	uint8 writebuffer_[MAXFRAMESIZE];
	uint8 wireBuffer_[MAXFRAMESIZE + JammerNetzSecure::SecureDatagramSealer::WireOverhead];
	std::map<std::string, RingOfAudioBuffers<AudioBlock>> fecData_;
	std::map<std::string, uint64_t> packageCounters_;
	std::shared_ptr<JammerNetzSecure::SecureDatagramSealer> serverSealer_;
};
