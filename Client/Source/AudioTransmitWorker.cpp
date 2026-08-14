/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "AudioTransmitWorker.h"

AudioTransmitWorker::AudioTransmitWorker(JammerNetzSession& session)
	: juce::Thread("JammerNetz transmit"), session_(session)
{
	channelSetup_.store(std::make_shared<const JammerNetzChannelSetup>(false), std::memory_order_release);
}

AudioTransmitWorker::~AudioTransmitWorker()
{
	shutdown();
}

void AudioTransmitWorker::start()
{
	if (!isThreadRunning()) {
		startThread(juce::Thread::Priority::high);
	}
}

void AudioTransmitWorker::shutdown()
{
	signalThreadShouldExit();
	stopThread(2000);
	queue_.reset();
}

void AudioTransmitWorker::setChannelSetup(const JammerNetzChannelSetup& setup)
{
	channelSetup_.store(std::make_shared<const JammerNetzChannelSetup>(setup), std::memory_order_release);
}

bool AudioTransmitWorker::hasCapacity() const noexcept
{
	return queue_.freeSpace() > 0;
}

bool AudioTransmitWorker::enqueueFrom(RingBuffer& source, int channels, std::optional<float> bpm, std::optional<MidiSignal> midiSignal)
{
	if (channels <= 0 || channels > JAMMERNETZ_MAX_AUDIO_CHANNELS) {
		recordDroppedFrame();
		return false;
	}

	const bool written = queue_.tryWrite([&](TransmitAudioFrame& frame) {
		frame.channels = channels;
		frame.bpm = bpm;
		frame.midiSignal = midiSignal;
		std::array<float*, JAMMERNETZ_MAX_AUDIO_CHANNELS> pointers {};
		for (int channel = 0; channel < channels; ++channel) {
			pointers[static_cast<size_t>(channel)] = frame.samples[static_cast<size_t>(channel)].data();
		}
		source.read(pointers.data(), channels, SAMPLE_BUFFER_SIZE);
	});

	if (written) {
		enqueued_.fetch_add(1, std::memory_order_relaxed);
	} else {
		recordDroppedFrame();
	}
	return written;
}

void AudioTransmitWorker::recordDroppedFrame() noexcept
{
	dropped_.fetch_add(1, std::memory_order_relaxed);
}

uint64_t AudioTransmitWorker::enqueuedFrames() const noexcept { return enqueued_.load(std::memory_order_relaxed); }
uint64_t AudioTransmitWorker::sentFrames() const noexcept { return sent_.load(std::memory_order_relaxed); }
uint64_t AudioTransmitWorker::droppedFrames() const noexcept { return dropped_.load(std::memory_order_relaxed); }
float AudioTransmitWorker::channelPitch(size_t channel) const { return tuner_.getPitch(channel); }
FFAU::LevelMeterSource* AudioTransmitWorker::meterSource() noexcept { return &meterSource_; }

void AudioTransmitWorker::run()
{
	while (!threadShouldExit()) {
		const bool hadFrame = queue_.tryRead([this](TransmitAudioFrame& frame) { processFrame(frame); });
		if (!hadFrame) {
			juce::Thread::sleep(1);
		}
	}
}

void AudioTransmitWorker::processFrame(TransmitAudioFrame& frame)
{
	std::array<float*, JAMMERNETZ_MAX_AUDIO_CHANNELS> pointers {};
	for (int channel = 0; channel < frame.channels; ++channel) {
		pointers[static_cast<size_t>(channel)] = frame.samples[static_cast<size_t>(channel)].data();
	}
	auto audio = std::make_shared<juce::AudioBuffer<float>>(pointers.data(), frame.channels, SAMPLE_BUFFER_SIZE);
	tuner_.detectPitch(audio);
	meterSource_.measureBlock(*audio);

	const auto setup = channelSetup_.load(std::memory_order_acquire);
	if (!setup || setup->channels.size() != static_cast<size_t>(frame.channels)) {
		recordDroppedFrame();
		return;
	}
	JammerNetzChannelSetup outgoing = *setup;
	for (int channel = 0; channel < frame.channels; ++channel) {
		auto& details = outgoing.channels[static_cast<size_t>(channel)];
		details.mag = meterSource_.getMaxLevel(channel);
		details.rms = meterSource_.getRMSLevel(channel);
		details.pitch = tuner_.getPitch(static_cast<size_t>(channel));
	}

	if (auto* sender = session_.sender()) {
		ControlData controls;
		controls.bpm = frame.bpm;
		controls.midiSignal = frame.midiSignal;
		if (sender->sendData(outgoing, audio, controls)) {
			sent_.fetch_add(1, std::memory_order_relaxed);
		}
	}
}
