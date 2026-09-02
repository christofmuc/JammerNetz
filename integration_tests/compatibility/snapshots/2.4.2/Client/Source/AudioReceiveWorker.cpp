/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "AudioReceiveWorker.h"

#include <utility>

AudioReceiveWorker::AudioReceiveWorker(JammerNetzSession& session)
	: juce::Thread("JammerNetz receive preparation"), session_(session)
{
}

AudioReceiveWorker::~AudioReceiveWorker() { shutdown(); }

void AudioReceiveWorker::start()
{
	if (!isThreadRunning()) {
		startThread(juce::Thread::Priority::high);
	}
}

void AudioReceiveWorker::shutdown()
{
	signalThreadShouldExit();
	stopThread(2000);
	// The owner stops the audio callback and network receive callback before
	// shutdown, so neither queue has a producer or consumer at this point.
	inboundQueue_.reset();
	outputQueue_.reset();
	packetQueue_.reset();
}

void AudioReceiveWorker::enqueue(std::shared_ptr<JammerNetzAudioData> packet)
{
	if (packet && !inboundQueue_.tryWrite([&packet](std::shared_ptr<JammerNetzAudioData>& slot) { slot = std::move(packet); })) {
		inboundOverruns_.fetch_add(1, std::memory_order_relaxed);
	}
}

bool AudioReceiveWorker::tryPop(RemoteAudioFrame& frame)
{
	return outputQueue_.tryRead([&frame](RemoteAudioFrame& queued) { frame = queued; });
}

int AudioReceiveWorker::readyFrames() const noexcept { return outputQueue_.size(); }

void AudioReceiveWorker::setPlayoutRange(uint64_t minimumFrames, uint64_t maximumFrames) noexcept
{
	minimumFrames_.store(minimumFrames, std::memory_order_relaxed);
	maximumFrames_.store(std::max(minimumFrames, maximumFrames), std::memory_order_relaxed);
}

void AudioReceiveWorker::requestRebuffer() noexcept { rebufferRequested_.store(true, std::memory_order_release); }

uint64_t AudioReceiveWorker::requestReset() noexcept
{
	return requestedGeneration_.fetch_add(1, std::memory_order_acq_rel) + 1;
}

uint64_t AudioReceiveWorker::currentGeneration() const noexcept { return requestedGeneration_.load(std::memory_order_acquire); }

std::optional<float> AudioReceiveWorker::takeServerBpmUpdate() noexcept
{
	if (serverBpmPending_.exchange(false, std::memory_order_acq_rel)) {
		return latestServerBpm_.load(std::memory_order_relaxed);
	}
	return {};
}

uint64_t AudioReceiveWorker::discardedFrames() const noexcept
{
	return discarded_.load(std::memory_order_relaxed) + inboundOverruns_.load(std::memory_order_relaxed);
}
uint64_t AudioReceiveWorker::outputQueueOverruns() const noexcept
{
	return inboundOverruns_.load(std::memory_order_relaxed) + outputOverruns_.load(std::memory_order_relaxed);
}
std::string AudioReceiveWorker::qualityStatement() const { return packetQueue_.qualityStatement(); }
FFAU::LevelMeterSource* AudioReceiveWorker::meterSource() noexcept { return &sessionMeterSource_; }

void AudioReceiveWorker::run()
{
	while (!threadShouldExit()) {
		if (!processNextFrame()) {
			juce::Thread::sleep(1);
		}
	}
}

bool AudioReceiveWorker::processNextPendingFrame()
{
	if (isThreadRunning()) {
		return false;
	}
	return processNextFrame();
}

bool AudioReceiveWorker::processNextFrame()
{
	applyResetIfRequested();
	drainInbound();
	if (rebufferRequested_.exchange(false, std::memory_order_acq_rel)) {
		streamStarted_.store(false, std::memory_order_release);
		recoveringFromOverrun_ = false;
	}

	const auto minimum = minimumFrames_.load(std::memory_order_relaxed);
	const auto maximum = maximumFrames_.load(std::memory_order_relaxed);
	const auto combinedReadyFrames = [this]() {
		return static_cast<uint64_t>(outputQueue_.size()) + static_cast<uint64_t>(packetQueue_.size());
	};

	// The prepared queue is the queue the audio callback actually drains. If
	// the callback stalls, stop feeding it and discard old ordered packets
	// until the prepared queue has played back to the configured minimum.
	if (combinedReadyFrames() > maximum) {
		recoveringFromOverrun_ = true;
	}
	if (recoveringFromOverrun_) {
		std::shared_ptr<JammerNetzAudioData> discardedPacket;
		bool fillIn = false;
		while (combinedReadyFrames() > minimum && packetQueue_.try_pop(discardedPacket, fillIn)) {
			discarded_.fetch_add(1, std::memory_order_relaxed);
		}
		if (combinedReadyFrames() <= minimum) {
			recoveringFromOverrun_ = false;
		}
	}

	if (!streamStarted_.load(std::memory_order_acquire) && packetQueue_.size() >= minimum) {
		streamStarted_.store(true, std::memory_order_release);
	}

	return streamStarted_.load(std::memory_order_acquire)
		&& !recoveringFromOverrun_
		&& static_cast<uint64_t>(outputQueue_.size()) < maximum
		&& prepareOneFrame();
}

void AudioReceiveWorker::applyResetIfRequested()
{
	const auto requested = requestedGeneration_.load(std::memory_order_acquire);
	if (requested == activeGeneration_.load(std::memory_order_relaxed)) {
		return;
	}
	std::shared_ptr<JammerNetzAudioData> packet;
	while (inboundQueue_.tryRead([&packet](std::shared_ptr<JammerNetzAudioData>& queued) { packet = std::move(queued); })) {}
	packetQueue_.reset();
	streamStarted_.store(false, std::memory_order_release);
	recoveringFromOverrun_ = false;
	activeGeneration_.store(requested, std::memory_order_release);
}

void AudioReceiveWorker::drainInbound()
{
	std::shared_ptr<JammerNetzAudioData> packet;
	while (inboundQueue_.tryRead([&packet](std::shared_ptr<JammerNetzAudioData>& queued) { packet = std::move(queued); })) {
		packetQueue_.push(std::move(packet));
	}
}

bool AudioReceiveWorker::prepareOneFrame()
{
	if (outputQueue_.freeSpace() == 0) {
		return false;
	}
	std::shared_ptr<JammerNetzAudioData> packet;
	bool fillIn = false;
	if (!packetQueue_.try_pop(packet, fillIn) || !packet || !packet->audioBuffer()) {
		return false;
	}

	const auto audio = packet->audioBuffer();
	const auto generation = activeGeneration_.load(std::memory_order_acquire);
	const bool written = outputQueue_.tryWrite([&](RemoteAudioFrame& frame) {
		frame.generation = generation;
		frame.sourceTimestamp = packet->timestamp();
		frame.serverSampleEnd = packet->serverTime();
		frame.bpm = packet->bpm();
		frame.midiSignal = packet->midiSignal();
		for (int channel = 0; channel < 2; ++channel) {
			auto& destination = frame.samples[static_cast<size_t>(channel)];
			if (channel < audio->getNumChannels()) {
				const auto samplesToCopy = std::min(SAMPLE_BUFFER_SIZE, audio->getNumSamples());
				juce::FloatVectorOperations::copy(destination.data(), audio->getReadPointer(channel), samplesToCopy);
				juce::FloatVectorOperations::clear(destination.data() + samplesToCopy, SAMPLE_BUFFER_SIZE - samplesToCopy);
			} else {
				destination.fill(0.0f);
			}
		}
	});
	if (!written) {
		outputOverruns_.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	latestServerBpm_.store(packet->bpm(), std::memory_order_relaxed);
	serverBpmPending_.store(true, std::memory_order_release);
	updateSessionMeter();
	return true;
}

void AudioReceiveWorker::updateSessionMeter()
{
	const auto setup = session_.getCurrentSessionSetup();
	std::vector<float> magnitudes;
	std::vector<float> rms;
	magnitudes.reserve(setup.channels.size());
	rms.reserve(setup.channels.size());
	for (const auto& channel : setup.channels) {
		magnitudes.push_back(channel.mag);
		rms.push_back(channel.rms);
	}
	sessionMeterSource_.setBlockMeasurement(setup.channels.size(), magnitudes, rms);
}
