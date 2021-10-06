/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "IncludeFFMeters.h"

#include "Pool.h"
#include "RingBuffer.h"
#include "JammerService.h"

#include "PacketStreamQueue.h"
#include "Recorder.h"
#include "Tuner.h"
#include "MidiRecorder.h"
#include "MidiPlayAlong.h"

#include "ApplicationState.h"

#include <chrono>
#include <tbb/concurrent_queue.h>

struct PlayoutQualityInfo {
	PlayoutQualityInfo()
		: currentPlayQueueLength_(0), playUnderruns_(0), discardedPackageCounter_(0),
		toPlayLatency_(0.0), numSamplesSinceStart_(-1), measuredSampleRate(0.0) {}

	uint64 currentPlayQueueLength_;
	uint64 playUnderruns_;
	uint64 discardedPackageCounter_;
	double toPlayLatency_; // in ms

	int64 numSamplesSinceStart_;
	std::chrono::time_point<std::chrono::steady_clock> startTime_;
	std::chrono::time_point<std::chrono::steady_clock> lastTime_;
	double measuredSampleRate; // in Hz
};

class AudioCallback : public AudioIODeviceCallback {
public:
	AudioCallback();
	virtual ~AudioCallback();

	void shutdown();

	virtual void audioDeviceIOCallback(const float** inputChannelData, int numInputChannels, float** outputChannelData, int numOutputChannels, int numSamples) override;
	virtual void audioDeviceAboutToStart(AudioIODevice* device) override;
	virtual void audioDeviceStopped() override;

	void newServer();
	void setChannelSetup(JammerNetzChannelSetup const &channelSetup);

	FFAU::LevelMeterSource* getMeterSource();
	FFAU::LevelMeterSource* getSessionMeterSource();
	FFAU::LevelMeterSource* getOutputMeterSource();
	std::weak_ptr<MidiClocker> getClocker();
	MidiPlayAlong *getPlayalong();

	// Statistics
	PlayoutQualityInfo getPlayoutQualityInfo();

	uint64 currentBufferSize() const;
	int currentPacketSize();

	std::string currentReceptionQuality() const;
	bool isReceivingData();
	double currentRTT();
	float channelPitch(int channel) const;
	float sessionPitch(int channel);

	std::shared_ptr<Recorder> getMasterRecorder() const;
	std::shared_ptr<Recorder> getLocalRecorder() const;
	std::shared_ptr<JammerNetzClientInfoMessage> getClientInfo();
	JammerNetzChannelSetup getSessionSetup();

private:
	void measureSamplesPerTime(PlayoutQualityInfo &qualityInfo, int numSamples) const;

	void calcLocalMonitoring(std::shared_ptr<AudioBuffer<float>> inputBuffer, AudioBuffer<float>& outputBuffer);

	JammerService jammerService_; //TODO - this instance needs to be pulled up another level, so the audiocallback class wouldn't know anything about the network

	std::unique_ptr<RingBuffer> ingestBuffer_;
	std::unique_ptr<RingBuffer> playoutBuffer_;

	PacketStreamQueue playBuffer_;
	std::atomic_bool isPlaying_;
	std::atomic_uint64_t minPlayoutBufferLength_;
	std::atomic_uint64_t maxPlayoutBufferLength_;
	std::atomic<double> masterVolume_;
	std::atomic<double> monitorBalance_;
	std::atomic<bool> monitorIsLocal_;
	std::string currentText_;

	JammerNetzChannelSetup channelSetup_;
	FFAU::LevelMeterSource meterSource_; // This is for peak metering
	FFAU::LevelMeterSource sessionMeterSource_; // This is to display the complete session peak meters
	FFAU::LevelMeterSource outMeterSource_; // This is for peak metering the output

	std::shared_ptr<Recorder> uploadRecorder_;
	std::shared_ptr<Recorder> masterRecorder_;
	std::unique_ptr<MidiRecorder> midiRecorder_;
	std::unique_ptr<MidiPlayAlong> midiPlayalong_;

	std::unique_ptr<Tuner> tuner_;

	// Use this to hand out statistics from the audio/real time callback to other interested threads
	tbb::concurrent_queue<PlayoutQualityInfo> playoutQualityInfo_;
	PlayoutQualityInfo lastPlayoutQualityInfo_;

	// Generic listeners, required to maintain the lifetime of the Values and their listeners
	std::vector<std::unique_ptr<ValueListener>> listeners_;
};
