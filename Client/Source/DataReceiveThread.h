/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "JammerNetzPackage.h"
#include "JammerNetzClientInfoMessage.h"

#include "AtomicSharedPtr.h"

#include <limits>

bool shouldUpdateRoundTripTime(double echoedTimestamp, double lastEchoedTimestamp) noexcept;

class DataReceiveThread : public Thread {
public:
	DataReceiveThread(DatagramSocket & socket,
		std::function<void(std::shared_ptr<JammerNetzAudioData>)> newDataHandler,
		std::function<void(bool)> mtuCapabilityHandler,
		std::function<void(uint64, int)> mtuAcknowledgementHandler,
		std::function<void(bool)> controlCapabilityHandler,
		std::function<void(const JammerNetzControlEnvelopeData&)> controlEnvelopeHandler);
	virtual ~DataReceiveThread() override;

	virtual void run() override;

	bool isReceivingData() const;
	double currentRTT() const;
	uint64_t receiveErrorCount() const;
	JammerNetzChannelSetup sessionSetup() const;
	std::shared_ptr<JammerNetzClientInfoMessage> getClientInfo() const;
	void setCryptoKey(const void* keyData, int keyBytes);

private:
	void recordReceiveError(const char* message);

	DatagramSocket &socket_;
	uint8 readbuffer_[MAXFRAMESIZE];
	std::function<void(std::shared_ptr<JammerNetzAudioData>)> newDataHandler_;
	std::function<void(bool)> mtuCapabilityHandler_;
	std::function<void(uint64, int)> mtuAcknowledgementHandler_;
	std::function<void(bool)> controlCapabilityHandler_;
	std::function<void(const JammerNetzControlEnvelopeData&)> controlEnvelopeHandler_;
	std::unique_ptr<BlowFish> blowFish_;
	juce::CriticalSection blowFishLock_;

	// Thread safe storage of info for the UI thread
	std::atomic<double> currentRTT_;
	double lastRttTimestamp_ { -std::numeric_limits<double>::infinity() };
	std::atomic<bool> isReceiving_;
	std::atomic<uint64_t> receiveErrorCount_;

	// For the session UI
	JammerNetzChannelSetup currentSession_;
	CriticalSection sessionDataLock_;
	AtomicSharedPtr<JammerNetzClientInfoMessage> lastClientInfoMessage_;
};
