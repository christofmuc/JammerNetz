/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "IncludeFFMeters.h"

#include "RingBuffer.h"
#include "AudioPacketSink.h"
#include "JammerNetzSession.h"
#include "BoundedSpscQueue.h"
#include "BuffersConfig.h"

#include "AudioReceiveWorker.h"
#include "AudioRecordingWorker.h"
#include "AudioTransmitWorker.h"
#include "Recorder.h"
#include "MidiRecorder.h"
#include "MidiPlayAlong.h"
#include "MidiSendThread.h"

#include "AtomicSharedPtr.h"

#include <array>
#include <chrono>
#include <cmath>
#include <limits>

struct PlayoutQualityInfo {
	PlayoutQualityInfo()
		: currentPlayQueueLength_(0), playUnderruns_(0), discardedPackageCounter_(0),
		toPlayLatency_(0.0), numSamplesSinceStart_(-1), measuredSampleRate(0.0) {}

	uint64 currentPlayQueueLength_; // Prepared PCM frames waiting in AudioReceiveWorker.
	uint64 playUnderruns_;
	uint64 discardedPackageCounter_;
	double toPlayLatency_; // in ms

	int64 numSamplesSinceStart_;
	std::chrono::time_point<std::chrono::steady_clock> startTime_;
	std::chrono::time_point<std::chrono::steady_clock> lastTime_;
	double measuredSampleRate; // in Hz
};

struct RealtimeWorkerStats {
	uint64_t callbackCount { 0 };
	uint64_t maximumCallbackNanoseconds { 0 };
	uint64_t callbackDeadlineMisses { 0 };
	uint64_t inputBlocksDropped { 0 };
	uint64_t transmitFramesQueued { 0 };
	uint64_t transmitFramesSent { 0 };
	uint64_t transmitFramesDropped { 0 };
	uint64_t receiveFramesDiscarded { 0 };
	uint64_t receiveQueueOverruns { 0 };
	uint64_t recordingFramesWritten { 0 };
	uint64_t recordingFramesDropped { 0 };
	uint64_t midiTransportCommandsDropped { 0 };
	uint64_t midiOutputEventsDropped { 0 };
	uint64_t midiTimingMarkersDropped { 0 };
};

// A single atomic exchange makes taking the latest BPM linearizable: a write
// racing the exchange is observed either now or by the next read, never erased
// by a separate "present" flag.
class LatestBpmMailbox
{
public:
	void setValue(float newValue) noexcept
	{
		value_.store(newValue, std::memory_order_release);
	}

	std::optional<float> takeLatest() noexcept
	{
		const float result = value_.exchange(noValue(), std::memory_order_acq_rel);
		if (!std::isnan(result)) {
			return result;
		}
		return {};
	}

private:
	static constexpr float noValue() noexcept { return std::numeric_limits<float>::quiet_NaN(); }
	static_assert(std::atomic<float>::is_always_lock_free,
		"The real-time BPM handoff requires lock-free float atomics");
	std::atomic<float> value_ { noValue() };
};


class JammerNetzAudioEngine {
public:
	JammerNetzAudioEngine(JammerNetzSession& session,
		const juce::File& recordingDirectory,
		std::shared_ptr<AudioPacketSink> packetSink = {});
	~JammerNetzAudioEngine();
	void start(bool enableRecording = true);
	void shutdown();

	void restartClock(std::vector<MidiDeviceInfo> outputs);
	void setMidiSignalToSend(MidiSignal signal);

	void process(const float* const* inputChannelData, int numInputChannels, float* const* outputChannelData, int numOutputChannels, int numSamples);
	void prepare(double sampleRate, int maximumBlockSize);
	void release();
	void enqueueRemoteAudio(std::shared_ptr<JammerNetzAudioData> buffer);
	// Headless callers use this instead of starting the background transmit thread.
	bool processNextOutgoingPacket();
	// Headless callers use this instead of starting the background receive thread.
	bool processNextIncomingPacket();

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
	RealtimeWorkerStats getRealtimeWorkerStats() const;

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
	struct RetiredInputState {
		std::shared_ptr<const InputState> state;
		uint64_t retireEpoch { 0 };
	};

	void measureSamplesPerTime(PlayoutQualityInfo &qualityInfo, int numSamples) const;
	void processChunk(const float* const* inputChannelData, int numInputChannels, float* const* outputChannelData,
		int numOutputChannels, int numSamples);

	void calcLocalMonitoring(const float* const* inputChannels, int numInputChannels, AudioBuffer<float>& outputBuffer,
		const JammerNetzChannelSetup& channelSetup);
	void resetPlayoutState() noexcept;
	void appendPlayoutTiming(const RemoteAudioFrame& frame) noexcept;
	void scheduleMidiForPlayout(int numSamples) noexcept;
	void scheduleMidiFrame(MidiSendThread* sender, uint64 serverSampleEnd, float bpm,
		MidiSignal signal, uint64_t frameOffsetSamples,
		std::chrono::steady_clock::time_point playoutStart) noexcept;
	std::optional<MidiSignal> takeMidiSignalToSend() noexcept;
	void retireMidiSender(std::shared_ptr<MidiSendThread> sender);

	JammerNetzSession& session_;
	juce::File recordingDirectory_;

	std::atomic<const InputState*> inputState_ { nullptr };
	std::shared_ptr<const InputState> configuredInputState_;
	std::vector<RetiredInputState> retiredInputStates_;
	std::atomic<uint64_t> completedAudioEpoch_ { 0 };
	std::atomic<uint32_t> activeAudioCallbacks_ { 0 };
	std::atomic<bool> shutdownRequested_ { false };
	std::unique_ptr<RingBuffer> playoutBuffer_;
	juce::AudioBuffer<float> remoteScratch_;
	std::array<float, JAMMERNETZ_MAX_CALLBACK_SAMPLES> silentMeterChannel_ {};

	std::atomic_bool isPlaying_ { false };
	std::atomic_bool resetQualityInfo_ { false };
	std::atomic_bool resetPlayoutRequested_ { false };
	std::atomic<uint64_t> expectedRemoteGeneration_ { 0 };
	std::atomic_uint64_t minPlayoutBufferLength_;
	std::atomic_uint64_t maxPlayoutBufferLength_;
	std::atomic<double> masterVolume_;
	std::atomic<double> monitorBalance_;
	std::atomic<bool> monitorIsLocal_ { false };
	LatestBpmMailbox clientBpm_;
	std::atomic<double> serverBpm_;
	std::atomic<bool> ignoreNextServerBpmChange_;
	std::atomic<float> pendingServerBpm_;
	std::atomic<int64_t> bpmSliderLastMovedTicks_;
	FFAU::LevelMeterSource outMeterSource_; // This is for peak metering the output

	std::shared_ptr<Recorder> uploadRecorder_;
	std::shared_ptr<Recorder> masterRecorder_;
	std::unique_ptr<MidiRecorder> midiRecorder_;
	std::unique_ptr<MidiPlayAlong> midiPlayalong_;
	AtomicSharedPtr<MidiSendThread> midiSendThread_;
	std::atomic<MidiSendThread*> realtimeMidiSender_ { nullptr };
	std::atomic<MidiSendThread*> realtimeMidiSenderHazard_ { nullptr };
	std::unique_ptr<AudioTransmitWorker> transmitWorker_;
	std::unique_ptr<AudioReceiveWorker> receiveWorker_;
	std::unique_ptr<AudioRecordingWorker> recordingWorker_;
	bool started_ { false };
	std::atomic<uint64_t> retiredMidiOutputEventsDropped_ { 0 };
	std::atomic<bool> inputChannelMismatchReported_ { false };

	// Message-thread producer, audio-thread consumer. Start/Stop are edge events,
	// so preserve their order instead of coalescing them like BPM.
	BoundedSpscQueue<MidiSignal> midiSignalsToSend_ { 32 };
	std::atomic<uint64_t> midiTransportCommandsDropped_ { 0 };
	struct PlayoutTimingMarker {
		uint64_t playoutSample { 0 };
		uint64 serverSampleEnd { 0 };
		float bpm { 0.0f };
		MidiSignal midiSignal { MidiSignal_None };
	};
	static constexpr size_t maxPlayoutTimingMarkers = PLAYOUT_RINGBUFFER_SIZE / SAMPLE_BUFFER_SIZE + 1;
	std::array<PlayoutTimingMarker, maxPlayoutTimingMarkers> playoutTimingMarkers_ {};
	size_t playoutTimingRead_ { 0 };
	size_t playoutTimingWrite_ { 0 };
	size_t playoutTimingCount_ { 0 };
	uint64_t playoutSamplesWritten_ { 0 };
	uint64_t playoutSamplesRead_ { 0 };
	std::atomic<uint64_t> midiTimingMarkersDropped_ { 0 };
	PlayoutQualityInfo lastPlayoutQualityInfo_;
	std::atomic<uint64_t> publishedQueueLength_ { 0 };
	std::atomic<uint64_t> publishedUnderruns_ { 0 };
	std::atomic<uint64_t> publishedDiscarded_ { 0 };
	std::atomic<double> publishedLatency_ { 0.0 };
	std::atomic<double> publishedSampleRate_ { 0.0 };
	std::atomic<double> preparedSampleRate_ { SAMPLE_RATE };
	std::atomic<uint64_t> callbackCount_ { 0 };
	std::atomic<uint64_t> maximumCallbackNanoseconds_ { 0 };
	std::atomic<uint64_t> callbackDeadlineMisses_ { 0 };
	std::atomic<uint64_t> inputBlocksDropped_ { 0 };

};
