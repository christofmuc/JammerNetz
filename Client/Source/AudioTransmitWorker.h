/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "AtomicSharedPtr.h"
#include "BoundedSpscQueue.h"
#include "IncludeFFMeters.h"
#include "JammerNetzSession.h"
#include "RealtimeAudioFrames.h"
#include "RingBuffer.h"
#include "Tuner.h"

class AudioTransmitWorker final : private juce::Thread {
public:
	explicit AudioTransmitWorker(JammerNetzSession& session);
	~AudioTransmitWorker() override;

	void start();
	void shutdown();
	void setChannelSetup(const JammerNetzChannelSetup& setup);

	bool hasCapacity() const noexcept;
	bool enqueueFrom(RingBuffer& source, int channels, std::optional<float> bpm, std::optional<MidiSignal> midiSignal);
	void recordDroppedFrame() noexcept;

	uint64_t enqueuedFrames() const noexcept;
	uint64_t sentFrames() const noexcept;
	uint64_t droppedFrames() const noexcept;
	float channelPitch(size_t channel) const;
	FFAU::LevelMeterSource* meterSource() noexcept;

private:
	void run() override;
	void processFrame(TransmitAudioFrame& frame);

	// About 170 ms at 48 kHz. New frames are dropped when the worker stalls.
	static constexpr int queueCapacity = 64;
	JammerNetzSession& session_;
	BoundedSpscQueue<TransmitAudioFrame> queue_ { queueCapacity };
	AtomicSharedPtr<const JammerNetzChannelSetup> channelSetup_;
	Tuner tuner_;
	FFAU::LevelMeterSource meterSource_;
	std::atomic<uint64_t> enqueued_ { 0 };
	std::atomic<uint64_t> sent_ { 0 };
	std::atomic<uint64_t> dropped_ { 0 };
};
