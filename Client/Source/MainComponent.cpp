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

MainComponent::MainComponent(String clientID) : audioDevice_(nullptr),
inputSelector_("Inputs", "InputSetup", deviceManager_, true, [this](std::shared_ptr<ChannelSetup> setup) { setupChanged(setup); }),
outputSelector_("Outputs", "OutputSetup", deviceManager_, false, [this](std::shared_ptr<ChannelSetup> setup) { outputSetupChanged(setup);  }),
outputController_("Master", "OutputController", [](double, JammerNetzChannelTarget) {}, false, false),
clientConfigurator_([this](int clientBuffer, int maxBuffer, int flares) { callback_.changeClientConfig(clientBuffer, maxBuffer, flares);  }),
serverSelector_([this]() { newServerSelected();  }),
callback_(deviceManager_)
{
	bpmDisplay_ = std::make_unique<BPMDisplay>(callback_.getClocker());

	outputController_.setMeterSource(callback_.getOutputMeterSource(), -1);

	addAndMakeVisible(outputController_);
	addAndMakeVisible(inputSelector_);
	addAndMakeVisible(outputSelector_);
	addAndMakeVisible(statusInfo_);
	addAndMakeVisible(downstreamInfo_);
	addAndMakeVisible(clientConfigurator_);
	addAndMakeVisible(serverSelector_);
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
	serverSelector_.fromData();
	outputController_.fromData();
	clientConfigurator_.fromData();

	startTimer(100);

	// Make sure you set the size of the component after
	// you add any child components.
	setSize(1024, 600);
}

MainComponent::~MainComponent()
{
	stopAudioIfRunning();
	clientConfigurator_.toData();
	serverSelector_.toData();
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
			for (int i = 0; i < inputSetup->activeChannelIndices.size(); i++) {
				inputChannelMask.setBit(inputSetup->activeChannelIndices[i]);
			}
		}
		BigInteger outputChannelMask = 0;
		if (outputSetup) {
			for (int i = 0; i < outputSetup->activeChannelIndices.size(); i++) {
				outputChannelMask.setBit(outputSetup->activeChannelIndices[i]);
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
	// This is called when the MainContentComponent is resized.
	// If you add any child components, this is where you should
	// update their positions.
	auto area = getLocalBounds();
	auto midiRecordingInfo = area.removeFromBottom(30);
	bpmDisplay_->setBounds(midiRecordingInfo);
	downstreamInfo_.setBounds(area.removeFromBottom(30));
	statusInfo_.setBounds(area.removeFromBottom(30));
	auto configRow = area.removeFromBottom(30);
	clientConfigurator_.setBounds(configRow.removeFromLeft(configRow.getWidth() / 3 * 2));
	serverSelector_.setBounds(configRow);
	int sidebarWidth = area.getWidth() / 4;
	inputSelector_.setBounds(area.removeFromLeft(sidebarWidth));
	outputController_.setBounds(area.removeFromRight(100));
	outputSelector_.setBounds(area.removeFromRight(sidebarWidth));
	int sizePerController = channelControllers_.isEmpty() ? 0 : std::min(area.getWidth() / channelControllers_.size(), 100);
	for (auto controller : channelControllers_) {
		controller->setBounds(area.removeFromLeft(sizePerController));
	}

}

void MainComponent::timerCallback()
{
	// Refresh the UI with info from the Audio callback
	std::stringstream status;
	status << "Underruns: " << std::fixed << std::setprecision(2) << callback_.numberOfUnderruns() << ". Buffers: " << callback_.currentBufferSize() << ". ";
	status << "Network MTU: " << callback_.currentPacketSize() << " bytes. Bandwidth: " << callback_.currentPacketSize() * 8 * (ServerInfo::sampleRate / (float)ServerInfo::bufferSize) / (1024 * 1024.0f) << "MBit/s. ";
	status << "Input latency: " << inputLatencyInMS_ << "ms. Output latency:" << outputLatencyInMS_ << "ms. Roundtrip: " << callback_.currentRTT()  << "ms ";
	status << " PlayQ: " << callback_.currentPlayQueueSize() << " discarded: " << callback_.currentDiscardedPackageCounter();
	status << " Total: " << callback_.currentToPlayLatency() + inputLatencyInMS_ + outputLatencyInMS_ << " ms";
	statusInfo_.setText(status.str(), dontSendNotification);
	downstreamInfo_.setText(callback_.currentReceptionQuality(), dontSendNotification);
}

void MainComponent::setupChanged(std::shared_ptr<ChannelSetup> setup)
{
	stopAudioIfRunning();

	currentInputSetup_ = setup;

	// Rebuild UI for the channel controllers
	channelControllers_.clear(true);
	int i = 0;
	for (auto channelName : setup->activeChannelNames) {
		auto controller = new ChannelController(channelName, "Input" + String(i), [this, setup](double newVolume, JammerNetzChannelTarget newTarget) {
			refreshChannelSetup(setup);
		});
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
