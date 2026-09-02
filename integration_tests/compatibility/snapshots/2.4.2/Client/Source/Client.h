/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "AudioPacketSink.h"
#include "DataReceiveThread.h"

#include "RingOfAudioBuffers.h"
#include "PathMtuDiscovery.h"
#include "nlohmann/json.hpp"

class Client : public AudioPacketSink {
public:
	Client(DatagramSocket& socket);
	~Client() override;

	bool sendData(JammerNetzChannelSetup const& channelSetup,
		std::shared_ptr<AudioBuffer<float>> audioBuffer,
		ControlData controllers) override;
	bool sendControl(nlohmann::json &json);
	void setServer(const juce::String& serverName, int serverPort, bool useLocalhost);
	void setUseFEC(bool enabled);
	void setCryptoKey(const void* keyData, int keyBytes);
	void setMtuDiscoverySupported(bool supported);
	void acknowledgeMtuProbe(uint64 probeId, int payloadBytes);

	// Statistics info
	int getCurrentBlockSize() const;
	int getSafeUdpPayloadSize() const;
	PathMtuDiscoveryStatus getMtuDiscoveryStatus() const;

private:
	bool sendData(String const &remoteHostname, int remotePort, void *data, int numbytes);
    bool sendBufferToServer(size_t totalBytes);
	void maybeSendMtuProbe();
	bool sendMtuProbe(const PathMtuProbe& probe);
	bool enableDoNotFragment();

	DatagramSocket &socket_;
	uint64 messageCounter_;
	uint8 sendBuffer_[65536];
	std::atomic_int currentBlockSize_;
	std::atomic<bool> useFEC_;
	juce::CriticalSection socketLock_;
	juce::CriticalSection serverLock_;
	String serverName_;
	std::atomic<int> serverPort_;
	std::atomic<bool> useLocalhost_;

	RingOfAudioBuffers<AudioBlock> fecBuffer_; // Forward error correction buffer, keep the last n sent packages
	juce::CriticalSection blowFishLock_;
	std::unique_ptr<BlowFish> blowFish_;
	mutable juce::CriticalSection mtuDiscoveryLock_;
	PathMtuDiscovery mtuDiscovery_;
};
