/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "AudioCallback.h"

#include "Logger.h"

AudioCallback::AudioCallback(JammerNetzAudioEngine& engine, std::function<void(float)> serverBpmChanged)
	: engine_(engine), serverBpmChanged_(std::move(serverBpmChanged))
{
	startTimerHz(20);
}

AudioCallback::~AudioCallback()
{
	stopTimer();
}

void AudioCallback::audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
	float* const* outputChannelData, int numOutputChannels, int numSamples, const juce::AudioIODeviceCallbackContext& context)
{
	juce::ignoreUnused(context);
	engine_.process(inputChannelData, numInputChannels, outputChannelData, numOutputChannels, numSamples);
}

void AudioCallback::timerCallback()
{
	// Formatting/logging stays on the message thread, never the audio callback.
	if (++diagnosticTimerTicks_ >= 100) {
		diagnosticTimerTicks_ = 0;
		const auto stats = engine_.getRealtimeWorkerStats();
		if (stats.callbackCount != lastDiagnosticCallbackCount_) {
			lastDiagnosticCallbackCount_ = stats.callbackCount;
			const auto milliseconds = [](uint64_t ns) { return juce::String(static_cast<double>(ns) / 1.0e6, 3); };
			SimpleLogger::instance()->postMessage("Audio timing (lifetime maxima, ms): callback gap="
				+ milliseconds(stats.maximumCallbackGapNanoseconds)
				+ ", gap excess=" + milliseconds(stats.maximumCallbackGapExcessNanoseconds)
				+ ", processing=" + milliseconds(stats.maximumCallbackNanoseconds)
				+ ", transmit queue=" + milliseconds(stats.maximumTransmitQueueWaitNanoseconds)
				+ ", queue-to-send=" + milliseconds(stats.maximumTransmitQueueToSendNanoseconds)
				+ ", transmit MMCSS=" + juce::String(stats.transmitMultimediaSchedulingActive ? "active" : "inactive")
				+ ", transmit drops=" + juce::String(static_cast<juce::int64>(stats.transmitFramesDropped)));
		}
	}
	if (const auto bpm = engine_.takeServerBpmUpdate(); bpm && serverBpmChanged_) {
		serverBpmChanged_(*bpm);
	}
}

void AudioCallback::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
	const juce::String deviceDescription = "Audio device " + device->getName() + " starting with "
		+ juce::String(device->getCurrentSampleRate()) + "Hz, buffer size " + juce::String(device->getCurrentBufferSizeSamples());
	juce::MessageManager::callAsync([deviceDescription]() {
		SimpleLogger::instance()->postMessage(deviceDescription);
	});
	engine_.prepare(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
}

void AudioCallback::audioDeviceStopped()
{
	engine_.release();
	juce::MessageManager::callAsync([]() {
		SimpleLogger::instance()->postMessage("Audio device stopped");
	});
}
