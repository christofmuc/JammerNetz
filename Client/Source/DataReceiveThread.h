/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "JammerNetzPackage.h"

#include <chrono>
#include "JammerNetzClientInfoMessage.h"

#include "AtomicSharedPtr.h"
#include "Encryption.h"

class DataReceiveThread : public Thread {
public:
	DataReceiveThread(DatagramSocket & socket,
		std::function<void(std::shared_ptr<JammerNetzAudioData>)> newDataHandler,
		std::function<void(bool)> mtuCapabilityHandler,
		std::function<void(uint64, int)> mtuAcknowledgementHandler);
	virtual ~DataReceiveThread() override;

	virtual void run() override;

	bool isReceivingData() const;
	double currentRTT() const;
	uint64_t receiveErrorCount() const;
	JammerNetzChannelSetup sessionSetup() const;
	std::shared_ptr<JammerNetzClientInfoMessage> getClientInfo() const;
	void setSessionKey(std::shared_ptr<const JammerNetzSecure::SessionKey> sessionKey);

private:
	void recordReceiveError(const char* message);

	DatagramSocket &socket_;
	uint8 readbuffer_[MAXFRAMESIZE + JammerNetzSecure::SecureDatagramSealer::WireOverhead];
	uint8 plaintextBuffer_[MAXFRAMESIZE];
	std::function<void(std::shared_ptr<JammerNetzAudioData>)> newDataHandler_;
	std::function<void(bool)> mtuCapabilityHandler_;
	std::function<void(uint64, int)> mtuAcknowledgementHandler_;
	std::unique_ptr<JammerNetzSecure::SecureDatagramOpener> opener_;
	juce::CriticalSection cryptoLock_;

	// Thread safe storage of info for the UI thread
	std::atomic<double> currentRTT_;
	std::atomic<bool> isReceiving_;
	std::atomic<uint64_t> receiveErrorCount_;
	std::uint64_t errorsSinceLastLog_{0};
	std::chrono::steady_clock::time_point lastReceiveErrorLog_{};

	// For the session UI
	JammerNetzChannelSetup currentSession_;
	CriticalSection sessionDataLock_;
	AtomicSharedPtr<JammerNetzClientInfoMessage> lastClientInfoMessage_;
};
