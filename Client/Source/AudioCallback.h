/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "Client.h"
#include "PacketStreamQueue.h"
#include "Recorder.h"
#include "Tuner.h"
#include "MidiRecorder.h"

class AudioCallback : public AudioIODeviceCallback {
public:
	AudioCallback(AudioDeviceManager &deviceManager);

	virtual void audioDeviceIOCallback(const float** inputChannelData, int numInputChannels, float** outputChannelData, int numOutputChannels, int numSamples) override;
	virtual void audioDeviceAboutToStart(AudioIODevice* device) override;
	virtual void audioDeviceStopped() override;

	void newServer();
	void setChannelSetup(JammerNetzChannelSetup const &channelSetup);
	void changeClientConfig(int clientBuffers, int maxBuffers, int flares);

	FFAU::LevelMeterSource* getMeterSource();
	FFAU::LevelMeterSource* getOutputMeterSource();
	std::weak_ptr<MidiClocker> getClocker();

	// Statistics
	int numberOfUnderruns() const;
	int currentBufferSize() const;
	int currentPacketSize() const;
	uint64 currentPlayQueueSize() const;
	int currentDiscardedPackageCounter() const;
	double currentToPlayLatency() const;

	std::string currentReceptionQuality() const;
	bool isReceivingData() const;
	double currentRTT() const;
	float channelPitch(int channel) const;

private:
	void clearOutput(float** outputChannelData, int numOutputChannels, int numSamples);

	PacketStreamQueue playBuffer_;
	std::atomic_bool isPlaying_;
	std::atomic_int64_t playUnderruns_;
	std::atomic_uint64_t minPlayoutBufferLength_;
	std::atomic_uint64_t maxPlayoutBufferLength_;
	std::atomic_int64_t currentPlayQueueLength_;
	int discardedPackageCounter_;
	double toPlayLatency_;
	Client client_;
	JammerNetzChannelSetup channelSetup_;
	FFAU::LevelMeterSource meterSource_; // This is for peak metering
	FFAU::LevelMeterSource outMeterSource_;

	std::unique_ptr<Recorder> uploadRecorder_;
	std::unique_ptr<Recorder> masterRecorder_;
	std::unique_ptr<MidiRecorder> midiRecorder_;

	std::unique_ptr<Tuner> tuner_;
};
