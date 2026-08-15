/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "IncludeFFMeters.h"

#include "Pool.h"
#include "RingBuffer.h"
#include "JammerNetzSession.h"

#include "PacketStreamQueue.h"
#include "Recorder.h"
#include "Tuner.h"
#include "MidiRecorder.h"
#include "MidiPlayAlong.h"
#include "MidiSendThread.h"

#include "AtomicSharedPtr.h"

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

template <typename T>
class ReadOnceLatch
{
public:
	ReadOnceLatch(T default_value) : value(default_value), is_value_set(false)
	{
	}

	void setValue(T newValue)
	{
		// Store the new value and mark it as set
		value.store(newValue, std::memory_order_release);
		is_value_set.store(true, std::memory_order_release);
	}

	std::optional<T> readOnce()
	{
		// Check if the value has been set
		if (is_value_set.load(std::memory_order_acquire)) {
			// Read the value
			T result = value.load(std::memory_order_relaxed);
			// Reset the latch
			is_value_set.store(false, std::memory_order_release);
			return result;
		}
		return {};
	}

private:
	std::atomic<T> value;
	std::atomic<bool> is_value_set;
};


class JammerNetzAudioEngine {
public:
	JammerNetzAudioEngine(JammerNetzSession& session, const juce::File& recordingDirectory);
	~JammerNetzAudioEngine();
	void start();
	void shutdown();

	void restartClock(std::vector<MidiDeviceInfo> outputs);
	void setMidiSignalToSend(MidiSignal signal);

	void process(const float* const* inputChannelData, int numInputChannels, float* const* outputChannelData, int numOutputChannels, int numSamples);
	void prepare(double sampleRate, int maximumBlockSize);
	void release();
	void enqueueRemoteAudio(std::shared_ptr<JammerNetzAudioData> buffer);

	void setPlayoutBufferRange(uint64 minimumLength, uint64 maximumLength);
	void setMasterVolume(double volume);
	void setMonitorBalance(double balance);
	void setLocalMonitoring(bool enabled);
	void setClientBpm(float bpm);
	std::optional<float> takeServerBpmUpdate();

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
	float channelPitch(size_t channel) const;
	float sessionPitch(size_t channel);

	std::shared_ptr<Recorder> getMasterRecorder() const;
	std::shared_ptr<Recorder> getLocalRecorder() const;
	std::shared_ptr<JammerNetzClientInfoMessage> getClientInfo();
	JammerNetzChannelSetup getSessionSetup();

private:
	struct InputState {
		JammerNetzChannelSetup setup;
		std::shared_ptr<RingBuffer> ingestBuffer;
	};

	void measureSamplesPerTime(PlayoutQualityInfo &qualityInfo, int numSamples) const;

	void calcLocalMonitoring(const AudioBuffer<float>& inputBuffer, AudioBuffer<float>& outputBuffer, const JammerNetzChannelSetup& channelSetup);

	JammerNetzSession& session_;
	juce::File recordingDirectory_;

	AtomicSharedPtr<const InputState> inputState_;
	std::unique_ptr<RingBuffer> playoutBuffer_;

	PacketStreamQueue playBuffer_;
	std::atomic_bool isPlaying_;
	std::atomic_bool resetQualityInfo_;
	std::atomic_uint64_t minPlayoutBufferLength_;
	std::atomic_uint64_t maxPlayoutBufferLength_;
	std::atomic<double> masterVolume_;
	std::atomic<double> monitorBalance_;
	std::atomic<bool> monitorIsLocal_;
	ReadOnceLatch<float> clientBpm_;
	std::atomic<double> serverBpm_;
	std::atomic<bool> ignoreNextServerBpmChange_;
	std::atomic<float> pendingServerBpm_;
	ReadOnceLatch<float> serverBpmUpdate_;
	std::atomic<int64_t> bpmSliderLastMovedTicks_;
	std::string currentText_;

	FFAU::LevelMeterSource meterSource_; // This is for peak metering
	FFAU::LevelMeterSource sessionMeterSource_; // This is to display the complete session peak meters
	FFAU::LevelMeterSource outMeterSource_; // This is for peak metering the output

	std::shared_ptr<Recorder> uploadRecorder_;
	std::shared_ptr<Recorder> masterRecorder_;
	std::unique_ptr<MidiRecorder> midiRecorder_;
	std::unique_ptr<MidiPlayAlong> midiPlayalong_;
	AtomicSharedPtr<MidiSendThread> midiSendThread_;
	CriticalSection retiredMidiSendThreadsLock_;
	std::vector<std::shared_ptr<MidiSendThread>> retiredMidiSendThreads_;
	std::atomic<bool> inputChannelMismatchReported_ { false };

	ReadOnceLatch<MidiSignal> midiSignalToSend_;
	ReadOnceLatch<MidiSignal> midiSignalToGenerate_;

	std::unique_ptr<Tuner> tuner_;

	// Use this to hand out statistics from the audio/real time callback to other interested threads
	tbb::concurrent_queue<PlayoutQualityInfo> playoutQualityInfo_;
	PlayoutQualityInfo lastPlayoutQualityInfo_;

};
