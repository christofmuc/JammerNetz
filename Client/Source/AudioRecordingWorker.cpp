/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "AudioRecordingWorker.h"

AudioRecordingWorker::AudioRecordingWorker(std::shared_ptr<Recorder> localRecorder, std::shared_ptr<Recorder> masterRecorder)
	: juce::Thread("JammerNetz recording handoff")
	, localRecorder_(std::move(localRecorder))
	, masterRecorder_(std::move(masterRecorder))
{
}

AudioRecordingWorker::~AudioRecordingWorker() { shutdown(); }

void AudioRecordingWorker::start()
{
	if (!isThreadRunning()) {
		startThread();
	}
}

void AudioRecordingWorker::shutdown()
{
	signalThreadShouldExit();
	stopThread(2000);
}

bool AudioRecordingWorker::enqueue(RecordingTarget target, const float* const* channels, int numChannels, int numSamples) noexcept
{
	const auto& recorder = target == RecordingTarget::local ? localRecorder_ : masterRecorder_;
	const auto recordingGeneration = recorder ? recorder->recordingGeneration() : 0;
	if (recordingGeneration == 0) {
		return true;
	}
	if (!channels || numChannels <= 0 || numChannels > JAMMERNETZ_MAX_AUDIO_CHANNELS
		|| numSamples <= 0 || numSamples > JAMMERNETZ_MAX_CALLBACK_SAMPLES) {
		dropped_.fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	const bool queued = queue_.tryWrite([&](RecordingAudioFrame& frame) {
		frame.target = target;
		frame.recordingGeneration = recordingGeneration;
		frame.channels = numChannels;
		frame.samplesPerChannel = numSamples;
		for (int channel = 0; channel < numChannels; ++channel) {
			if (channels[channel]) {
				juce::FloatVectorOperations::copy(frame.samples[static_cast<size_t>(channel)].data(), channels[channel], numSamples);
			} else {
				juce::FloatVectorOperations::clear(frame.samples[static_cast<size_t>(channel)].data(), numSamples);
			}
		}
	});
	if (!queued) {
		dropped_.fetch_add(1, std::memory_order_relaxed);
	}
	return queued;
}

uint64_t AudioRecordingWorker::writtenFrames() const noexcept { return written_.load(std::memory_order_relaxed); }
uint64_t AudioRecordingWorker::droppedFrames() const noexcept { return dropped_.load(std::memory_order_relaxed); }

void AudioRecordingWorker::run()
{
	while (!threadShouldExit() || queue_.size() > 0) {
		const bool hadFrame = queue_.tryRead([this](RecordingAudioFrame& frame) { writeFrame(frame); });
		if (!hadFrame) {
			juce::Thread::sleep(2);
		}
	}
}

void AudioRecordingWorker::writeFrame(RecordingAudioFrame& frame)
{
	const auto& recorder = frame.target == RecordingTarget::local ? localRecorder_ : masterRecorder_;
	if (!recorder || frame.recordingGeneration != recorder->recordingGeneration()) {
		return;
	}
	std::array<const float*, JAMMERNETZ_MAX_AUDIO_CHANNELS> pointers {};
	for (int channel = 0; channel < frame.channels; ++channel) {
		pointers[static_cast<size_t>(channel)] = frame.samples[static_cast<size_t>(channel)].data();
	}
	recorder->saveBlock(pointers.data(), frame.samplesPerChannel);
	written_.fetch_add(1, std::memory_order_relaxed);
}
