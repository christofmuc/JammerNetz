/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "SpectrumAnalysisWorker.h"

SpectrumAnalysisWorker::SpectrumAnalysisWorker(std::shared_ptr<Spectrogram> analyzer)
	: juce::Thread("JammerNetz spectrum analysis")
	, analyzer_(std::move(analyzer))
{
}

SpectrumAnalysisWorker::~SpectrumAnalysisWorker()
{
	shutdown();
}

void SpectrumAnalysisWorker::prepare(double sampleRate, int maximumBlockSize)
{
	shutdown();
	if (analyzer_ == nullptr || maximumBlockSize <= 0 || maximumBlockSize > JAMMERNETZ_MAX_CALLBACK_SAMPLES)
		return;

	analyzer_->prepare(sampleRate);
	processed_.store(0, std::memory_order_relaxed);
	dropped_.store(0, std::memory_order_relaxed);
	startThread();
}

void SpectrumAnalysisWorker::release()
{
	shutdown();
	if (analyzer_ != nullptr)
		analyzer_->reset();
}

bool SpectrumAnalysisWorker::enqueue(
	const float* const* channels, int numChannels, int numSamples) noexcept
{
	if (!isThreadRunning() || channels == nullptr || numChannels <= 0
		|| numSamples <= 0 || numSamples > JAMMERNETZ_MAX_CALLBACK_SAMPLES) {
		dropped_.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	const auto queued = queue_.tryWrite([&](SpectrumAudioFrame& frame) {
		frame.samplesPerChannel = numSamples;
		for (int destinationChannel = 0; destinationChannel < 2; ++destinationChannel) {
			const auto sourceChannel = juce::jmin(destinationChannel, numChannels - 1);
			if (channels[sourceChannel] != nullptr) {
				juce::FloatVectorOperations::copy(
					frame.samples[static_cast<size_t>(destinationChannel)].data(),
					channels[sourceChannel], numSamples);
			} else {
				juce::FloatVectorOperations::clear(
					frame.samples[static_cast<size_t>(destinationChannel)].data(), numSamples);
			}
		}
	});

	if (!queued)
		dropped_.fetch_add(1, std::memory_order_relaxed);
	return queued;
}

std::uint64_t SpectrumAnalysisWorker::processedBlocks() const noexcept
{
	return processed_.load(std::memory_order_relaxed);
}

std::uint64_t SpectrumAnalysisWorker::droppedBlocks() const noexcept
{
	return dropped_.load(std::memory_order_relaxed);
}

void SpectrumAnalysisWorker::run()
{
	while (!threadShouldExit() || queue_.size() > 0) {
		const auto hadFrame = queue_.tryRead([this](SpectrumAudioFrame& frame) { processFrame(frame); });
		if (!hadFrame)
			juce::Thread::sleep(2);
	}
}

void SpectrumAnalysisWorker::processFrame(SpectrumAudioFrame& frame)
{
	if (analyzer_ == nullptr)
		return;

	std::array<float*, 2> channelPointers {
		frame.samples[0].data(), frame.samples[1].data()
	};
	juce::AudioBuffer<float> buffer(channelPointers.data(), 2, frame.samplesPerChannel);
	analyzer_->process({ &buffer, 0, frame.samplesPerChannel });
	processed_.fetch_add(1, std::memory_order_relaxed);
}

void SpectrumAnalysisWorker::shutdown()
{
	signalThreadShouldExit();
	stopThread(2000);
}
