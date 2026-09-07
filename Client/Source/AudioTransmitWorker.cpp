/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "AudioTransmitWorker.h"

#include <utility>
#include <stdexcept>

#if JUCE_WINDOWS
#include <windows.h>
#include <avrt.h>
#endif

// Constructed off the audio thread; only SetEvent is used by the producer.
// Auto-reset events retain a signal sent between checking the queue and waiting.
struct AudioTransmitWorker::WindowsScheduling {
#if JUCE_WINDOWS
	WindowsScheduling()
	{
		if (!wakeEvent) {
			throw std::runtime_error("Cannot create audio transmit wake event");
		}
	}
	~WindowsScheduling() { CloseHandle(wakeEvent); }
	void signal() noexcept { SetEvent(wakeEvent); }
	void wait() { WaitForSingleObject(wakeEvent, INFINITE); }

	HANDLE wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
#else
	void signal() noexcept {}
	void wait() { juce::Thread::sleep(1); }
#endif
};

namespace {

#if JUCE_WINDOWS
// Registration and reversion must both happen on the worker itself. Loading
// dynamically lets transmission continue if MMCSS is unavailable/disabled.
class ScopedAudioScheduling {
public:
	ScopedAudioScheduling()
	{
		const auto set = reinterpret_cast<decltype(&AvSetMmThreadCharacteristicsW)>(
			library.getFunction("AvSetMmThreadCharacteristicsW"));
		revert = reinterpret_cast<decltype(&AvRevertMmThreadCharacteristics)>(
			library.getFunction("AvRevertMmThreadCharacteristics"));
		if (set && revert) {
			DWORD taskIndex = 0;
			handle = set(L"Pro Audio", &taskIndex);
		}
	}
	~ScopedAudioScheduling() { if (handle) revert(handle); }
	bool active() const noexcept { return handle != nullptr; }
private:
	juce::DynamicLibrary library { "avrt.dll" };
	decltype(&AvRevertMmThreadCharacteristics) revert = nullptr;
	HANDLE handle = nullptr;
};
#endif

void recordMaximum(std::atomic<uint64_t>& maximum, std::chrono::steady_clock::time_point since)
{
	const auto elapsed = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now() - since).count());
	// Each counter has a single writer: the transmit worker.
	if (elapsed > maximum.load(std::memory_order_relaxed)) {
		maximum.store(elapsed, std::memory_order_relaxed);
	}
}

} // namespace

AudioTransmitWorker::AudioTransmitWorker(JammerNetzSession& session,
	std::shared_ptr<AudioPacketSink> packetSink)
	: juce::Thread("JammerNetz transmit"), windowsScheduling_(std::make_unique<WindowsScheduling>()),
	  session_(session), packetSink_(std::move(packetSink))
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
	windowsScheduling_->signal();
	stopThread(2000);
	// The owning engine stops its audio callback producer before shutdown.
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
		frame.enqueuedAt = std::chrono::steady_clock::now();
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
		windowsScheduling_->signal();
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
uint64_t AudioTransmitWorker::maximumQueueWaitNanoseconds() const noexcept { return maximumQueueWaitNanoseconds_.load(std::memory_order_relaxed); }
uint64_t AudioTransmitWorker::maximumQueueToSendNanoseconds() const noexcept { return maximumQueueToSendNanoseconds_.load(std::memory_order_relaxed); }
bool AudioTransmitWorker::multimediaSchedulingActive() const noexcept { return multimediaSchedulingActive_.load(std::memory_order_relaxed); }
float AudioTransmitWorker::channelPitch(size_t channel) const { return tuner_.getPitch(channel); }
FFAU::LevelMeterSource* AudioTransmitWorker::meterSource() noexcept { return &meterSource_; }

void AudioTransmitWorker::run()
{
#if JUCE_WINDOWS
	ScopedAudioScheduling scheduling;
	multimediaSchedulingActive_.store(scheduling.active(), std::memory_order_relaxed);
#endif
	while (!threadShouldExit()) {
		if (!processNextFrame()) {
			windowsScheduling_->wait();
		}
	}
	multimediaSchedulingActive_.store(false, std::memory_order_relaxed);
}

bool AudioTransmitWorker::processNextPendingFrame()
{
	if (isThreadRunning()) {
		return false;
	}
	return processNextFrame();
}

bool AudioTransmitWorker::processNextFrame()
{
	return queue_.tryRead([this](TransmitAudioFrame& frame) { processFrame(frame); });
}

void AudioTransmitWorker::processFrame(TransmitAudioFrame& frame)
{
	recordMaximum(maximumQueueWaitNanoseconds_, frame.enqueuedAt);
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

	auto* packetSink = packetSink_ ? packetSink_.get() : session_.sender();
	if (packetSink) {
		ControlData controls;
		controls.bpm = frame.bpm;
		controls.midiSignal = frame.midiSignal;
		// Includes pitch/meter preparation, but excludes the socket send itself.
		recordMaximum(maximumQueueToSendNanoseconds_, frame.enqueuedAt);
		if (packetSink->sendData(outgoing, audio, controls)) {
			sent_.fetch_add(1, std::memory_order_relaxed);
		}
	}
}
