/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "AudioCallback.h"
#include "JammerNetzSession.h"
#include "ApplicationState.h"
#include "DebounceTimer.h"
#include "SpectrumAnalysisWorker.h"

#include <atomic>
#include <optional>


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
	virtual ~AudioService() override;

	void shutdown(); // Controlled stop

	bool isConnected();

	void stopAudioIfRunning();

	void setClockOutputs(std::vector<juce::MidiDeviceInfo> outputs);
	void setMidiSignal(MidiSignal signal);

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

	float channelPitch(size_t channel) const;
	float sessionPitch(size_t channel);

	FFAU::LevelMeterSource* getInputMeterSource();
	FFAU::LevelMeterSource* getOutputMeterSource();
	FFAU::LevelMeterSource* getSessionMeterSource();
	std::weak_ptr<Spectrogram> getSpectrogram() const noexcept;

private:
	virtual void valueTreePropertyChanged(ValueTree& treeWhosePropertyHasChanged, const Identifier& property) override;

	std::shared_ptr<ChannelSetup> getSetup(ValueTree data) const;

	void refreshChannelSetup(std::shared_ptr<ChannelSetup> setup);
	void refreshEngineConfiguration();
	void refreshSessionConfiguration();
	std::optional<JammerNetzSessionConfiguration> getSessionConfiguration() const;
	void restartAudio();
	void restartAudio(std::shared_ptr<ChannelSetup> inputSetup, std::shared_ptr<ChannelSetup> outputSetup);

	std::shared_ptr<juce::AudioIODevice> audioDevice_;

	std::shared_ptr<Spectrogram> spectrumAnalyzer_;
	std::unique_ptr<SpectrumAnalysisWorker> spectrumWorker_;
	JammerNetzSession session_;
	JammerNetzAudioEngine engine_;
	AudioCallback callback_;
	DebounceTimer debouncer_;
	std::atomic<bool> shutdown_ { false };
};
