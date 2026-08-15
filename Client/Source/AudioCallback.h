/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"
#include "JammerNetzAudioEngine.h"

#include <functional>

// Standalone-only adapter. The reusable engine deliberately has no dependency on
// an AudioIODevice or the application's ValueTree state.
class AudioCallback final : public juce::AudioIODeviceCallback {
public:
	AudioCallback(JammerNetzAudioEngine& engine, std::function<void(float)> serverBpmChanged);

	void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels, float* const* outputChannelData,
		int numOutputChannels, int numSamples, const juce::AudioIODeviceCallbackContext& context) override;
	void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
	void audioDeviceStopped() override;

private:
	JammerNetzAudioEngine& engine_;
	std::function<void(float)> serverBpmChanged_;
};
