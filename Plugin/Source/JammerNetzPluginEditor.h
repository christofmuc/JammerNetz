/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JammerNetzPluginProcessor.h"

class JammerNetzPluginEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
	explicit JammerNetzPluginEditor(JammerNetzPluginProcessor& pluginProcessor);
	~JammerNetzPluginEditor() override;

	void paint(juce::Graphics& graphics) override;
	void resized() override;

private:
	void timerCallback() override;
	void loadConfiguration();
	void storeConfiguration();
	void setConfigurationEnabled(bool enabled);
	void chooseKeyFile();
	static void configureGainSlider(juce::Slider& slider);
	static void configureFrameSlider(juce::Slider& slider);

	JammerNetzPluginProcessor& processor_;
	juce::Label title_;
	juce::Label serverLabel_;
	juce::TextEditor server_;
	juce::Label portLabel_;
	juce::Slider port_;
	juce::Label usernameLabel_;
	juce::TextEditor username_;
	juce::Label keyLabel_;
	juce::TextEditor keyPath_;
	juce::TextButton chooseKey_ { "Choose..." };
	juce::ToggleButton localhost_ { "Use localhost" };
	juce::ToggleButton fec_ { "Forward error correction" };
	juce::ToggleButton passthrough_ { "Dry passthrough / suppress own return" };
	juce::Label sendGainLabel_;
	juce::Slider sendGain_;
	juce::Label dryGainLabel_;
	juce::Slider dryGain_;
	juce::Label remoteGainLabel_;
	juce::Slider remoteGain_;
	juce::Label minimumJitterLabel_;
	juce::Slider minimumJitter_;
	juce::Label maximumJitterLabel_;
	juce::Slider maximumJitter_;
	juce::TextButton connect_ { "Connect" };
	juce::Label status_;
	double inputMeterValue_ { 0.0 };
	double remoteMeterValue_ { 0.0 };
	juce::ProgressBar inputMeter_ { inputMeterValue_ };
	juce::ProgressBar remoteMeter_ { remoteMeterValue_ };
	juce::Label inputMeterLabel_;
	juce::Label remoteMeterLabel_;
	std::unique_ptr<juce::FileChooser> keyChooser_;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JammerNetzPluginEditor)
};
