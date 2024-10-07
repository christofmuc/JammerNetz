/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "DataReceiveThread.h"

#include "RingOfAudioBuffers.h"
#include "ApplicationState.h"

struct ControlData
{
	std::optional<float> bpm;
	std::optional<MidiSignal> midiSignal;
};

class Client {
public:
	Client(DatagramSocket& socket);
	~Client();

	bool sendData(JammerNetzChannelSetup const &channelSetup, std::shared_ptr<AudioBuffer<float>> audioBuffer, ControlData controllers);

	// Statistics info
	int getCurrentBlockSize() const;

private:
	void setCryptoKey(const void* keyData, int keyBytes);
	bool sendData(String const &remoteHostname, int remotePort, void *data, int numbytes);

	DatagramSocket &socket_;
	uint64 messageCounter_;
	uint8 sendBuffer_[65536];
	std::atomic_int currentBlockSize_;
	std::atomic<bool> useFEC_;
	juce::CriticalSection serverLock_;
	String serverName_;
	std::atomic<int> serverPort_;
	std::atomic<bool> useLocalhost_;

	RingOfAudioBuffers<AudioBlock> fecBuffer_; // Forward error correction buffer, keep the last n sent packages
	juce::CriticalSection blowFishLock_;
	std::unique_ptr<BlowFish> blowFish_;

	// Generic listeners, required to maintain the lifetime of the Values and their listeners
	std::vector<std::unique_ptr<ValueListener>> listeners_;
};
