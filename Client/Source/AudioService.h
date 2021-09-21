/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "AudioCallback.h"
#include "ApplicationState.h"

struct ChannelSetup {
	std::string typeName;
	std::string device;
	bool isInputAndOutput; // Flag that this device is input and output and the same time -> if this is the case, the output can be bound to the input
	std::vector<std::string> activeChannelNames;
	std::vector<int> activeChannelIndices;
};


class AudioService {
public:
	AudioService();
	~AudioService();

	void stopAudioIfRunning();

	std::shared_ptr<Recorder> getMasterRecorder() const;
	std::shared_ptr<Recorder> getLocalRecorder() const;

private:
	std::shared_ptr<ChannelSetup> getSetup(ValueTree data);

	void refreshChannelSetup(std::shared_ptr<ChannelSetup> setup);
	void restartAudio();
	void restartAudio(std::shared_ptr<ChannelSetup> inputSetup, std::shared_ptr<ChannelSetup> outputSetup);

	juce::AudioDeviceManager deviceManager_;
	std::shared_ptr<juce::AudioIODevice> audioDevice_;

	AudioCallback callback_;

	// Generic listeners, required to maintain the lifetime of the Values and their listeners
	std::vector<std::unique_ptr<ValueListener>> listeners_;
};
