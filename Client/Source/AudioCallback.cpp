/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "AudioCallback.h"

#include "Logger.h"

AudioCallback::AudioCallback(JammerNetzAudioEngine& engine, std::function<void(float)> serverBpmChanged)
	: engine_(engine), serverBpmChanged_(std::move(serverBpmChanged))
{
}

void AudioCallback::audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
	float* const* outputChannelData, int numOutputChannels, int numSamples, const juce::AudioIODeviceCallbackContext& context)
{
	juce::ignoreUnused(context);
	engine_.process(inputChannelData, numInputChannels, outputChannelData, numOutputChannels, numSamples);
	if (const auto bpm = engine_.takeServerBpmUpdate()) {
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
