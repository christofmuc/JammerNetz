/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "AudioCallback.h"

#include "DeviceSelector.h"
#include "ChannelController.h"
#include "ServerStatus.h"
#include "ClientConfig.h"
#include "BPMDisplay.h"
#include "RecordingInfo.h"

class MainComponent   : public Component, private Timer
{
public:
    MainComponent(String clientID);
    ~MainComponent();

    void resized() override;

private:
	virtual void timerCallback() override;
	void refreshChannelSetup(std::shared_ptr<ChannelSetup> setup);
	void restartAudio(std::shared_ptr<ChannelSetup> inputSetup, std::shared_ptr<ChannelSetup> outputSetup);
	void stopAudioIfRunning();
	void setupChanged(std::shared_ptr<ChannelSetup> setup);
	void outputSetupChanged(std::shared_ptr<ChannelSetup> setup);
	void newServerSelected();

	AudioDeviceManager deviceManager_;
	std::shared_ptr<AudioIODevice> audioDevice_;

	AudioCallback callback_;

	DeviceSelector inputSelector_;
	GroupComponent inputGroup_;
	DeviceSelector outputSelector_;
	OwnedArray<ChannelController> channelControllers_;
	ChannelController outputController_;
	GroupComponent outputGroup_;
	ClientConfigurator clientConfigurator_;
	ServerStatus serverStatus_;
	GroupComponent serverGroup_;
	Label connectionInfo_;
	Label statusInfo_;
	Label downstreamInfo_;
	std::unique_ptr<BPMDisplay> bpmDisplay_;
	GroupComponent qualityGroup_;
	GroupComponent recordingGroup_;
	std::unique_ptr<RecordingInfo> recordingInfo_;

	std::shared_ptr<ChannelSetup> currentInputSetup_;
	std::shared_ptr<ChannelSetup> currentOutputSetup_;

	float inputLatencyInMS_;
	float outputLatencyInMS_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
