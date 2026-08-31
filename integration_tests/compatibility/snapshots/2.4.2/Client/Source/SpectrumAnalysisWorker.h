/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "AudioOutputTap.h"
#include "BoundedSpscQueue.h"
#include "RealtimeAudioFrames.h"
#include "Spectrogram.h"

#include <array>
#include <atomic>
#include <memory>

struct SpectrumAudioFrame {
	int samplesPerChannel { 0 };
	std::array<std::array<float, JAMMERNETZ_MAX_CALLBACK_SAMPLES>, 2> samples {};
};

class SpectrumAnalysisWorker final : public AudioOutputTap, private juce::Thread {
public:
	explicit SpectrumAnalysisWorker(std::shared_ptr<Spectrogram> analyzer);
	~SpectrumAnalysisWorker() override;

	void prepare(double sampleRate, int maximumBlockSize) override;
	void release() override;
	bool enqueue(const float* const* channels, int numChannels, int numSamples) noexcept override;

	std::uint64_t processedBlocks() const noexcept;
	std::uint64_t droppedBlocks() const noexcept;

private:
	void run() override;
	void processFrame(SpectrumAudioFrame& frame);
	void shutdown();

	static constexpr int queueCapacity = 8;
	BoundedSpscQueue<SpectrumAudioFrame> queue_ { queueCapacity };
	std::shared_ptr<Spectrogram> analyzer_;
	std::atomic<std::uint64_t> processed_ { 0 };
	std::atomic<std::uint64_t> dropped_ { 0 };
};
