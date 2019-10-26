/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "MainComponent.h"

#include "StreamLogger.h"
#include "Settings.h"
#include "AudioDeviceDiscovery.h"
#include "AudioCallback.h"
#include "ServerInfo.h"
#include "Data.h"
#include "MidiNote.h"

#include "LayoutConstants.h"

MainComponent::MainComponent(String clientID) : audioDevice_(nullptr),
inputSelector_("Inputs", false, "InputSetup", deviceManager_, true, [this](std::shared_ptr<ChannelSetup> setup) { setupChanged(setup); }),
outputSelector_("Outputs", false, "OutputSetup", deviceManager_, false, [this](std::shared_ptr<ChannelSetup> setup) { outputSetupChanged(setup);  }),
outputController_("Master", "OutputController", [](double, JammerNetzChannelTarget) {}, false, false),
clientConfigurator_([this](int clientBuffer, int maxBuffer, int flares) { callback_.changeClientConfig(clientBuffer, maxBuffer, flares);  }),
serverStatus_([this]() { newServerSelected();  }),
callback_(deviceManager_)
{
	bpmDisplay_ = std::make_unique<BPMDisplay>(callback_.getClocker());

	outputController_.setMeterSource(callback_.getOutputMeterSource(), -1);

	inputGroup_.setText("Input");
	outputGroup_.setText("Output");
	serverGroup_.setText("Settings");

	addAndMakeVisible(outputController_);
	addAndMakeVisible(inputGroup_);
	addAndMakeVisible(inputSelector_);
	addAndMakeVisible(statusInfo_);
	addAndMakeVisible(downstreamInfo_);
	addAndMakeVisible(outputGroup_);
	addAndMakeVisible(outputSelector_);
	addAndMakeVisible(clientConfigurator_);
	addAndMakeVisible(serverStatus_);
	addAndMakeVisible(serverGroup_);
	addAndMakeVisible(connectionInfo_);
	addAndMakeVisible(*bpmDisplay_);
	std::stringstream list;
	AudioDeviceDiscovery::listAudioDevices(deviceManager_, list);
	StreamLogger::instance() << list.str(); // For performance, send it to the output only once

	if (clientID.isNotEmpty()) {
		Settings::setSettingsID(clientID);
	}

	Data::instance().initializeFromSettings();
	inputSelector_.fromData();
	outputSelector_.fromData();
	serverStatus_.fromData();
	outputController_.fromData();
	clientConfigurator_.fromData();

	startTimer(100);

	// Make sure you set the size of the component after
	// you add any child components.
	setSize(1024, 800);
}

MainComponent::~MainComponent()
{
	stopAudioIfRunning();
	clientConfigurator_.toData();
	serverStatus_.toData();
	inputSelector_.toData();
	outputSelector_.toData();
	outputController_.toData();
	for (auto c : channelControllers_) {
		c->toData();
	}
	Data::instance().saveToSettings();
	Settings::instance().saveAndClose();
}

void MainComponent::refreshChannelSetup(std::shared_ptr<ChannelSetup> setup) {
	JammerNetzChannelSetup channelSetup;
	memset(&channelSetup, 0, sizeof(channelSetup));
	if (setup) {
		//TODO - warning this only allows 4 channels for now
		for (int i = 0; i < setup->activeChannelIndices.size(); i++) {
			jassert(i < MAXCHANNELSPERCLIENT);
			if (i < MAXCHANNELSPERCLIENT) {
				channelSetup.channels[i].target = channelControllers_[i]->getCurrentTarget();
				channelSetup.channels[i].volume = channelControllers_[i]->getCurrentVolume();
			}
		}
	}
	callback_.setChannelSetup(channelSetup);
}

void MainComponent::restartAudio(std::shared_ptr<ChannelSetup> inputSetup, std::shared_ptr<ChannelSetup> outputSetup)
{
	stopAudioIfRunning();

	// Sample rate and buffer size are hard coded for now
	if (!inputSetup->device.expired()) {
		audioDevice_ = inputSetup->device.lock();
	}
	jassert(audioDevice_);
	if (audioDevice_) {
		BigInteger inputChannelMask = 0;
		if (inputSetup) {
			for (int activeChannelIndex : inputSetup->activeChannelIndices) {
				inputChannelMask.setBit(activeChannelIndex);
			}
		}
		BigInteger outputChannelMask = 0;
		if (outputSetup) {
			for (int activeChannelIndex : outputSetup->activeChannelIndices) {
				outputChannelMask.setBit(activeChannelIndex);
			}
		}
		String error = audioDevice_->open(inputChannelMask, outputChannelMask, ServerInfo::sampleRate, ServerInfo::bufferSize);
		if (error.isNotEmpty()) {
			jassert(false);
			StreamLogger::instance() << "Error opening Audio Device: " << error << std::endl;
			refreshChannelSetup(std::shared_ptr < ChannelSetup>());
		}
		else {
			inputLatencyInMS_ = audioDevice_->getInputLatencyInSamples() / (float)ServerInfo::sampleRate * 1000.0f;
			StreamLogger::instance() << "Input latency is at " << inputLatencyInMS_ << "ms" << std::endl;
			outputLatencyInMS_ = audioDevice_->getOutputLatencyInSamples() / (float)ServerInfo::sampleRate* 1000.0f;
			StreamLogger::instance() << "Output latency is at " << outputLatencyInMS_ << "ms" << std::endl;

			refreshChannelSetup(inputSetup);
			// We can actually start playing
			audioDevice_->start(&callback_);
		}
	}
}

void MainComponent::stopAudioIfRunning()
{
	if (audioDevice_) {
		if (audioDevice_->isOpen()) {
			if (audioDevice_->isPlaying()) {
				audioDevice_->stop();
				audioDevice_->close();
			}
		}
	}
}

void MainComponent::resized()
{
	auto area = getLocalBounds().reduced(kSmallInset);

	int settingsHeight = 400;
	int inputSelectorWidth = std::min(area.getWidth() / 4, 250);
	int masterMixerWidth = 100;
	int inputMixerWidth = area.getWidth() - masterMixerWidth - 2 * inputSelectorWidth;

	// To the bottom, the server info and status area
	auto settingsArea = area.removeFromBottom(settingsHeight);
	serverGroup_.setBounds(settingsArea);
	settingsArea.reduce(kNormalInset, kNormalInset);

	int settingsSectionWidth = settingsArea.getWidth() / 3;
	auto clientConfigArea = settingsArea.removeFromLeft(settingsSectionWidth);
	clientConfigurator_.setBounds(clientConfigArea);
	auto serverArea = settingsArea.removeFromLeft(settingsSectionWidth);
	connectionInfo_.setBounds(serverArea.removeFromBottom(kLineSpacing));
	serverStatus_.setBounds(serverArea);
	auto midiRecordingInfo = settingsArea.removeFromBottom(30);
	bpmDisplay_->setBounds(midiRecordingInfo);
	statusInfo_.setBounds(settingsArea.removeFromTop(settingsArea.getHeight()/2));
	for (auto clientInfo : clientInfo_) {
		clientInfo->setBounds(settingsArea.removeFromTop(kLineHeight));
	}
	downstreamInfo_.setBounds(settingsArea);

	// To the left, the input selector
	auto inputArea = area.removeFromLeft(inputMixerWidth + inputSelectorWidth);
	inputGroup_.setBounds(inputArea);
	inputArea.reduce(kNormalInset, kNormalInset);
	inputSelector_.setBounds(inputArea.removeFromLeft(inputSelectorWidth));

	int sizePerController = channelControllers_.isEmpty() ? 0 : std::min(inputArea.getWidth() / channelControllers_.size(), 100);
	for (auto controller : channelControllers_) {
		controller->setBounds(inputArea.removeFromLeft(sizePerController));
	}

	// To the right, the output selector
	auto outputArea = area.removeFromRight(masterMixerWidth + inputSelectorWidth);
	outputGroup_.setBounds(outputArea);
	outputArea.reduce(kNormalInset, kNormalInset);
	outputSelector_.setBounds(outputArea.removeFromRight(inputSelectorWidth));
	outputController_.setBounds(outputArea);
}

void MainComponent::numConnectedClientsChanged() {
	clientInfo_.clear();

	auto info = callback_.getClientInfo();
	for (uint8 i = 0; i < info->getNumClients(); i++) {
		auto label = new Label();
		label->setText(info->getIPAddress(i), dontSendNotification);
		addAndMakeVisible(label);
		clientInfo_.add(label);
	}
	resized();
}

void MainComponent::timerCallback()
{
	// Refresh the UI with info from the Audio callback
	std::stringstream status;
	status << "Quality information" << std::endl;
	status << "Underruns: " << std::fixed << std::setprecision(2) << callback_.numberOfUnderruns() << std::endl;
	status << "Buffers: " << callback_.currentBufferSize() << std::endl;
	status << "Input latency: " << inputLatencyInMS_ << "ms" << std::endl;
	status << "Output latency: " << outputLatencyInMS_ << "ms" << std::endl;
	status << "Roundtrip: " << callback_.currentRTT()  << "ms" << std::endl;
	status << "PlayQ: " << callback_.currentPlayQueueSize() << std::endl;
	status << "Discarded: " << callback_.currentDiscardedPackageCounter() << std::endl;
	status << "Total: " << callback_.currentToPlayLatency() + inputLatencyInMS_ + outputLatencyInMS_ << " ms" << std::endl;
	statusInfo_.setText(status.str(), dontSendNotification);
	downstreamInfo_.setText(callback_.currentReceptionQuality(), dontSendNotification);
	std::stringstream connectionInfo;
	connectionInfo << std::fixed << std::setprecision(2) 
		<< "Network MTU: " << callback_.currentPacketSize() << " bytes. Bandwidth: "
		<< callback_.currentPacketSize() * 8 * (ServerInfo::sampleRate / (float)ServerInfo::bufferSize) / (1024 * 1024.0f) << "MBit/s. ";
	connectionInfo_.setText(connectionInfo.str(), dontSendNotification);

	if (callback_.getClientInfo()->getNumClients() != clientInfo_.size()) {
		// Need to re-setup the UI
		numConnectedClientsChanged();
	}

	serverStatus_.setConnected(callback_.isReceivingData());

	// Refresh tuning info
	for (int i = 0; i < channelControllers_.size(); i++) {
		channelControllers_[i]->setPitchDisplayed(MidiNote(callback_.channelPitch(i)));
	}
}

void MainComponent::setupChanged(std::shared_ptr<ChannelSetup> setup)
{
	stopAudioIfRunning();

	currentInputSetup_ = setup;

	// Rebuild UI for the channel controllers
	channelControllers_.clear(true);
	int i = 0;
	for (const auto& channelName : setup->activeChannelNames) {
		auto controller = new ChannelController(channelName, "Input" + String(i), [this, setup](double newVolume, JammerNetzChannelTarget newTarget) {
			refreshChannelSetup(setup);
		}, true, true, true);
		addAndMakeVisible(controller);
		channelControllers_.add(controller);
		controller->fromData();
		controller->setMeterSource(callback_.getMeterSource(), i);
		i++;
	}
	resized();

	// But now also start the Audio with this setup (hopefully it works...)
	restartAudio(currentInputSetup_, currentOutputSetup_);
}

void MainComponent::outputSetupChanged(std::shared_ptr<ChannelSetup> setup)
{
	currentOutputSetup_ = setup;
	restartAudio(currentInputSetup_, currentOutputSetup_);
}	

void MainComponent::newServerSelected()
{
	callback_.newServer();
}
