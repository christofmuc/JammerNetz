/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "BoundedSpscQueue.h"
#include "RealtimeAudioFrames.h"
#include "Recorder.h"

class AudioRecordingWorker final : private juce::Thread {
public:
	AudioRecordingWorker(std::shared_ptr<Recorder> localRecorder, std::shared_ptr<Recorder> masterRecorder);
	~AudioRecordingWorker() override;

	void start();
	void shutdown();
	bool enqueue(RecordingTarget target, const float* const* channels, int numChannels, int numSamples) noexcept;

	uint64_t writtenFrames() const noexcept;
	uint64_t droppedFrames() const noexcept;

private:
	void run() override;
	void writeFrame(RecordingAudioFrame& frame);

	// Recording is best-effort: the ninth pending callback block is dropped.
	static constexpr int queueCapacity = 8;
	BoundedSpscQueue<RecordingAudioFrame> queue_ { queueCapacity };
	std::shared_ptr<Recorder> localRecorder_;
	std::shared_ptr<Recorder> masterRecorder_;
	std::atomic<uint64_t> written_ { 0 };
	std::atomic<uint64_t> dropped_ { 0 };
};
