/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "DeviceSelector.h"
#include "ChannelControllerGroup.h"
#include "ServerStatus.h"
#include "ClientConfig.h"
#include "BPMDisplay.h"
#include "RecordingInfo.h"
#include "PlayalongDisplay.h"
#include "LogView.h"

#include "DSLookAndFeel.h"

#include "ApplicationState.h"

class MainComponent   : public Component, private Timer, public ValueTree::Listener
{
public:
    MainComponent(String clientID, std::shared_ptr<Recorder> masterRecorder, std::shared_ptr<Recorder> localRecorder);
    ~MainComponent();

    void resized() override;

	void valueTreePropertyChanged(ValueTree& treeWhosePropertyHasChanged, const Identifier& property) override;

private:
	virtual void timerCallback() override;
	void inputSetupChanged();
	void updateUserName();
	void numConnectedClientsChanged();
	void fillConnectedClientsStatistics();

	// DEFINE ALL OUR WIDGETS
	DeviceSelector inputSelector_;
	GroupComponent inputGroup_;
	DeviceSelector outputSelector_;
	ChannelControllerGroup ownChannels_;
	ChannelControllerGroup allChannels_;
	GroupComponent sessionGroup_;
	ChannelController outputController_;
	GroupComponent outputGroup_;
	Label nameLabel_;
	TextEditor nameEntry_;
	TextButton nameChange_;
	ClientConfigurator clientConfigurator_;
	ServerStatus serverStatus_;
	GroupComponent serverGroup_;
	Label connectionInfo_;
	OwnedArray<Label> clientInfo_;
	Label statusInfo_;
	Label downstreamInfo_;
	std::unique_ptr<BPMDisplay> bpmDisplay_;
	GroupComponent qualityGroup_;
	ImageButton logo_;
	GroupComponent recordingGroup_;
	std::unique_ptr<RecordingInfo> recordingInfo_; // For the master files
	std::unique_ptr<RecordingInfo> localRecordingInfo_; // For the local data
	std::unique_ptr<PlayalongDisplay> playalongDisplay_;
	LogView logView_;
	// END OF WIDGET LIST

	// Generic listeners, required to maintain the lifetime of the Values and their listeners
	std::vector<std::unique_ptr<ValueListener>> listeners_;

	// This is a cached variable, not state, used by the timer callback to display the quality statistics
	// Probably should go away
	std::shared_ptr<JammerNetzChannelSetup> currentSessionSetup_;

	DSLookAndFeel dsLookAndFeel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
