/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "JammerNetzPluginEditor.h"

#include <algorithm>

namespace {

constexpr int editorWidth = 560;
constexpr int editorHeight = 570;
constexpr int rowHeight = 28;
constexpr int labelWidth = 150;

} // namespace

JammerNetzPluginEditor::JammerNetzPluginEditor(JammerNetzPluginProcessor& processor)
	: AudioProcessorEditor(processor), processor_(processor)
{
	title_.setText("JammerNetz", juce::dontSendNotification);
	title_.setFont(juce::FontOptions(24.0f, juce::Font::bold));
	serverLabel_.setText("Server", juce::dontSendNotification);
	portLabel_.setText("Port", juce::dontSendNotification);
	usernameLabel_.setText("Participant name", juce::dontSendNotification);
	keyLabel_.setText("Machine key file", juce::dontSendNotification);
	sendGainLabel_.setText("Send gain", juce::dontSendNotification);
	dryGainLabel_.setText("Dry gain", juce::dontSendNotification);
	remoteGainLabel_.setText("Remote gain", juce::dontSendNotification);
	minimumJitterLabel_.setText("Minimum jitter frames", juce::dontSendNotification);
	maximumJitterLabel_.setText("Maximum jitter frames", juce::dontSendNotification);
	inputMeterLabel_.setText("Input", juce::dontSendNotification);
	remoteMeterLabel_.setText("Remote return", juce::dontSendNotification);
	status_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
	status_.setJustificationType(juce::Justification::centredLeft);
	status_.setMinimumHorizontalScale(0.7f);

	port_.setRange(1.0, 65535.0, 1.0);
	port_.setSliderStyle(juce::Slider::LinearHorizontal);
	port_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, rowHeight);
	configureGainSlider(sendGain_);
	configureGainSlider(dryGain_);
	configureGainSlider(remoteGain_);
	configureFrameSlider(minimumJitter_);
	configureFrameSlider(maximumJitter_);

	for (auto* component : std::array<juce::Component*, 27> {
		&title_, &serverLabel_, &server_, &portLabel_, &port_, &usernameLabel_, &username_,
		&keyLabel_, &keyPath_, &chooseKey_, &localhost_, &fec_, &passthrough_,
		&sendGainLabel_, &sendGain_, &dryGainLabel_, &dryGain_, &remoteGainLabel_, &remoteGain_,
		&minimumJitterLabel_, &minimumJitter_, &maximumJitterLabel_, &maximumJitter_,
		&connect_, &status_, &inputMeterLabel_, &inputMeter_
	}) {
		addAndMakeVisible(*component);
	}
	addAndMakeVisible(remoteMeterLabel_);
	addAndMakeVisible(remoteMeter_);

	chooseKey_.onClick = [this] { chooseKeyFile(); };
	connect_.onClick = [this] {
		if (processor_.isSessionActive()) {
			processor_.disconnectSession();
		} else {
			storeConfiguration();
			processor_.connectSession();
		}
		timerCallback();
	};

	loadConfiguration();
	setSize(editorWidth, editorHeight);
	startTimerHz(5);
	timerCallback();
}

JammerNetzPluginEditor::~JammerNetzPluginEditor()
{
	stopTimer();
	keyChooser_.reset();
}

void JammerNetzPluginEditor::configureGainSlider(juce::Slider& slider)
{
	slider.setRange(0.0, 2.0, 0.01);
	slider.setSliderStyle(juce::Slider::LinearHorizontal);
	slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, rowHeight);
}

void JammerNetzPluginEditor::configureFrameSlider(juce::Slider& slider)
{
	slider.setRange(0.0, 256.0, 1.0);
	slider.setSliderStyle(juce::Slider::LinearHorizontal);
	slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, rowHeight);
}

void JammerNetzPluginEditor::paint(juce::Graphics& graphics)
{
	graphics.fillAll(juce::Colour(0xff20242a));
	graphics.setColour(juce::Colour(0xff3c4652));
	graphics.drawRoundedRectangle(getLocalBounds().toFloat().reduced(8.0f), 6.0f, 1.0f);
}

void JammerNetzPluginEditor::resized()
{
	auto area = getLocalBounds().reduced(18);
	title_.setBounds(area.removeFromTop(38));
	area.removeFromTop(4);

	auto placeRow = [&area](juce::Label& label, juce::Component& field) {
		auto row = area.removeFromTop(rowHeight);
		label.setBounds(row.removeFromLeft(labelWidth));
		field.setBounds(row);
		area.removeFromTop(5);
	};
	placeRow(serverLabel_, server_);
	placeRow(portLabel_, port_);
	placeRow(usernameLabel_, username_);
	{
		auto row = area.removeFromTop(rowHeight);
		keyLabel_.setBounds(row.removeFromLeft(labelWidth));
		chooseKey_.setBounds(row.removeFromRight(90));
		row.removeFromRight(6);
		keyPath_.setBounds(row);
		area.removeFromTop(5);
	}
	{
		auto row = area.removeFromTop(rowHeight);
		localhost_.setBounds(row.removeFromLeft(150));
		fec_.setBounds(row.removeFromLeft(200));
		passthrough_.setBounds(row);
		area.removeFromTop(5);
	}
	placeRow(sendGainLabel_, sendGain_);
	placeRow(dryGainLabel_, dryGain_);
	placeRow(remoteGainLabel_, remoteGain_);
	placeRow(minimumJitterLabel_, minimumJitter_);
	placeRow(maximumJitterLabel_, maximumJitter_);
	{
		auto row = area.removeFromTop(rowHeight);
		inputMeterLabel_.setBounds(row.removeFromLeft(labelWidth));
		inputMeter_.setBounds(row);
		area.removeFromTop(5);
	}
	{
		auto row = area.removeFromTop(rowHeight);
		remoteMeterLabel_.setBounds(row.removeFromLeft(labelWidth));
		remoteMeter_.setBounds(row);
		area.removeFromTop(8);
	}
	connect_.setBounds(area.removeFromLeft(110).removeFromTop(32));
	area.removeFromLeft(10);
	status_.setBounds(area.removeFromTop(32));
}

void JammerNetzPluginEditor::loadConfiguration()
{
	const auto config = processor_.configuration();
	server_.setText(config.serverName, false);
	port_.setValue(config.serverPort, juce::dontSendNotification);
	username_.setText(config.username, false);
	keyPath_.setText(processor_.machineKeyPath(), false);
	localhost_.setToggleState(config.useLocalhost, juce::dontSendNotification);
	fec_.setToggleState(config.useFEC, juce::dontSendNotification);
	passthrough_.setToggleState(config.localPassthrough, juce::dontSendNotification);
	sendGain_.setValue(config.sendGain, juce::dontSendNotification);
	dryGain_.setValue(config.dryGain, juce::dontSendNotification);
	remoteGain_.setValue(config.remoteGain, juce::dontSendNotification);
	minimumJitter_.setValue(static_cast<double>(config.minimumJitterFrames), juce::dontSendNotification);
	maximumJitter_.setValue(static_cast<double>(config.maximumJitterFrames), juce::dontSendNotification);
}

void JammerNetzPluginEditor::storeConfiguration()
{
	auto config = processor_.configuration();
	config.serverName = server_.getText();
	config.serverPort = static_cast<int>(port_.getValue());
	config.username = username_.getText();
	config.useLocalhost = localhost_.getToggleState();
	config.useFEC = fec_.getToggleState();
	config.localPassthrough = passthrough_.getToggleState();
	config.sendGain = static_cast<float>(sendGain_.getValue());
	config.dryGain = static_cast<float>(dryGain_.getValue());
	config.remoteGain = static_cast<float>(remoteGain_.getValue());
	config.minimumJitterFrames = static_cast<uint64_t>(minimumJitter_.getValue());
	config.maximumJitterFrames = static_cast<uint64_t>(maximumJitter_.getValue());
	processor_.setConfiguration(config);
	processor_.setMachineKeyPath(keyPath_.getText());
}

void JammerNetzPluginEditor::setConfigurationEnabled(bool enabled)
{
	for (auto* component : std::array<juce::Component*, 13> {
		&server_, &port_, &username_, &keyPath_, &chooseKey_, &localhost_, &fec_, &passthrough_,
		&sendGain_, &dryGain_, &remoteGain_, &minimumJitter_, &maximumJitter_
	}) {
		component->setEnabled(enabled);
	}
}

void JammerNetzPluginEditor::chooseKeyFile()
{
	keyChooser_ = std::make_unique<juce::FileChooser>("Choose the JammerNetz key file", juce::File(keyPath_.getText()));
	juce::Component::SafePointer<JammerNetzPluginEditor> safeThis(this);
	keyChooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
		[safeThis](const juce::FileChooser& chooser) {
			if (!safeThis) {
				return;
			}
			const auto result = chooser.getResult();
			if (result.existsAsFile()) {
				safeThis->keyPath_.setText(result.getFullPathName());
			}
			safeThis->keyChooser_.reset();
		});
}

void JammerNetzPluginEditor::timerCallback()
{
	const bool active = processor_.isSessionActive();
	connect_.setButtonText(active ? "Disconnect" : "Connect");
	setConfigurationEnabled(!active);
	status_.setText(processor_.statusText(), juce::dontSendNotification);
	inputMeterValue_ = std::max(processor_.inputLevel(0), processor_.inputLevel(1));
	remoteMeterValue_ = std::max(processor_.remoteLevel(0), processor_.remoteLevel(1));
}
