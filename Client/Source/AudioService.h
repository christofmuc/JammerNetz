/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include <miniaudio.h>

#include "AudioCallback.h"
#include "ApplicationState.h"
#include "DebounceTimer.h"


struct ChannelSetup {
	std::string typeName;
	std::string device;
	bool isInputAndOutput; // Flag that this device is input and output and the same time -> if this is the case, the output can be bound to the input
	std::vector<std::string> activeChannelNames;
	std::vector<int> activeChannelIndices;
};


class AudioService : private ValueTree::Listener {
public:
	AudioService();
	~AudioService();

	void shutdown(); // Controlled stop 

	bool isConnected();

	void stopAudioIfRunning();

	std::shared_ptr<Recorder> getMasterRecorder() const;
	std::shared_ptr<Recorder> getLocalRecorder() const;
	
	std::shared_ptr<ChannelSetup> getInputSetup() const;
	std::shared_ptr<ChannelSetup> getOutputSetup() const;

	JammerNetzChannelSetup getSessionSetup();
	std::shared_ptr<JammerNetzClientInfoMessage> getClientInfo();

	PlayoutQualityInfo getPlayoutQualityInfo();
	double currentRTT();
	std::string currentReceptionQuality() const;
	int currentPacketSize();

	float channelPitch(int channel) const;
	float sessionPitch(int channel);

	FFAU::LevelMeterSource* getInputMeterSource();
	FFAU::LevelMeterSource* getOutputMeterSource();
	FFAU::LevelMeterSource* getSessionMeterSource();

private:
	virtual void valueTreePropertyChanged(ValueTree& treeWhosePropertyHasChanged, const Identifier& property) override;

	std::shared_ptr<ChannelSetup> getSetup(ValueTree data) const;

	void refreshChannelSetup(std::shared_ptr<ChannelSetup> setup);
	void restartAudio();
	void restartAudio(std::shared_ptr<ChannelSetup> inputSetup, std::shared_ptr<ChannelSetup> outputSetup);

	std::shared_ptr<juce::AudioIODevice> audioDevice_;

	AudioCallback callback_;
	DebounceTimer debouncer_;
};
