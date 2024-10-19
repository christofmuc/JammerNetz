/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "SharedServerTypes.h"
#include "JammerNetzPackage.h"
#include "RingOfAudioBuffers.h"

class SendThread : public Thread {
public:
	SendThread(DatagramSocket& socket, TOutgoingQueue &sendQueue, TPacketStreamBundle &incomingData, void *keydata, int keysize, ValueTree serverConfiguration);

	virtual void run() override;

private:
	void determineTargetIP(std::string const &targetAddress, String &ipAddress, int &portNumber);
	void sendWriteBuffer(String ipAddress, int port, size_t size);
    void sendSessionInfoPackage(std::string const &targetAddress, JammerNetzChannelSetup &sessionSetup);
    void sendClientInfoPackage(std::string const &targetAddress);
	void sendAudioBlock(std::string const &targetAddress, AudioBlock &audioBlock);

	TOutgoingQueue& sendQueue_;
	TPacketStreamBundle &incomingData_;
	DatagramSocket& sendSocket_;
    ValueTree serverConfiguration_;
	uint8 writebuffer_[MAXFRAMESIZE];
	std::map<std::string, RingOfAudioBuffers<AudioBlock>> fecData_;
	std::map<std::string, uint64_t> packageCounters_;
	std::unique_ptr<BlowFish> blowFish_;
};
