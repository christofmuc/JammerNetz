/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "MainComponent.h"

#include "ApplicationState.h"

#include "StreamLogger.h"
#include "Settings.h"
#include "AudioDeviceDiscovery.h"
#include "AudioCallback.h"
#include "ServerInfo.h"
#include "Data.h"
#include "MidiNote.h"

#include "LayoutConstants.h"

#ifdef DIGITAL_STAGE
#include "Login.h"
#include "DataStore.h"
#include "JoinStageDialog.h"

std::shared_ptr<DataStore> globalDataStore_;
#endif

MainComponent::MainComponent(String clientID) : audioDevice_(nullptr),
inputSelector_("Inputs", false, "InputSetup", deviceManager_, true, [this](std::shared_ptr<ChannelSetup> setup) { setupChanged(setup); }),
outputSelector_("Outputs", false, "OutputSetup", deviceManager_, false, [this](std::shared_ptr<ChannelSetup> setup) { outputSetupChanged(setup);  }),
outputController_("Master", "OutputController", [](double, JammerNetzChannelTarget) {}, false, false),
clientConfigurator_([this](int clientBuffer, int maxBuffer, bool fec) { callback_.changeClientConfig(clientBuffer, maxBuffer);  callback_.setFEC(fec);  }),
serverStatus_([this]() { newServerSelected();  }),
callback_(deviceManager_)
{
	setLookAndFeel(&dsLookAndFeel_);
	addAndMakeVisible(dsLookAndFeel_.backgroundGradient());

	// Load application state from settings file
	ApplicationState::fromSettings();

	//bpmDisplay_ = std::make_unique<BPMDisplay>(callback_.getClocker());
	recordingInfo_ = std::make_unique<RecordingInfo>(callback_.getMasterRecorder(), "Press to record master mix");
	playalongDisplay_ = std::make_unique<PlayalongDisplay>(callback_.getPlayalong());
	localRecordingInfo_ = std::make_unique<RecordingInfo>(callback_.getLocalRecorder(), "Press to record yourself only");

	outputController_.setMeterSource(callback_.getOutputMeterSource(), -1);

	
	nameLabel_.setText("My name", dontSendNotification);
	listeners_.push_back(std::make_unique<ValueListener>(ApplicationState::sApplicationState.getPropertyAsValue(VALUE_USER_NAME, nullptr), [this](Value &value) {
		nameEntry_.setText(value.toString(), dontSendNotification);
	}));
	nameChange_.setButtonText("Change");
	nameChange_.onClick = [this]() { updateUserName();  };
	nameEntry_.onEscapeKey = [this]() { nameEntry_.setText(ApplicationState::sApplicationState.getProperty(VALUE_USER_NAME), dontSendNotification);  };
	nameEntry_.onReturnKey = [this]() { updateUserName(); };

	inputGroup_.setText("Input");
	sessionGroup_.setText("Session participants");
	outputGroup_.setText("Output");
	serverGroup_.setText("Settings");
	qualityGroup_.setText("Quality Info");
	recordingGroup_.setText("Recording");

	addAndMakeVisible(outputController_);
	addAndMakeVisible(inputGroup_);
	addAndMakeVisible(inputSelector_);
	addAndMakeVisible(ownChannels_);
	addAndMakeVisible(sessionGroup_);
	addAndMakeVisible(allChannels_);
	addAndMakeVisible(statusInfo_);
	addAndMakeVisible(downstreamInfo_);
	addAndMakeVisible(outputGroup_);
	addAndMakeVisible(outputSelector_);
	addAndMakeVisible(nameLabel_);
	addAndMakeVisible(nameEntry_);
	addAndMakeVisible(nameChange_);
	addAndMakeVisible(clientConfigurator_);
	addAndMakeVisible(serverStatus_);
	addAndMakeVisible(serverGroup_);
	addAndMakeVisible(connectionInfo_);
	//addAndMakeVisible(*bpmDisplay_);
	addAndMakeVisible(qualityGroup_);
	addAndMakeVisible(recordingGroup_);
	addAndMakeVisible(*recordingInfo_);
	addAndMakeVisible(*playalongDisplay_);
	addAndMakeVisible(*localRecordingInfo_);

	if (clientID.isNotEmpty()) {
		Settings::setSettingsID(clientID);
	}

	// Add logo
	logo_.setClickingTogglesState(false);
	logo_.setEnabled(false);
	logo_.setImages(false, true, true, dsLookAndFeel_.logo(), 1.0f, Colours::white, dsLookAndFeel_.logo(), 1.0f, Colours::white, dsLookAndFeel_.logo(), 0.8f, Colours::white);
	addAndMakeVisible(logo_);

	Data::instance().initializeFromSettings();
	inputSelector_.fromData();
	outputSelector_.fromData();
#ifndef DIGITAL_STAGE
	serverStatus_.fromData();
#endif
	outputController_.fromData();
	clientConfigurator_.fromData();

	startTimer(100);

	// Make sure you set the size of the component after
	// you add any child components.
	setSize(1536, 800);

#ifdef DIGITAL_STAGE
	MessageManager::callAsync([this]() {
		LoginDialog::showDialog([this](LoginData loginData) {
			globalDataStore_ = std::make_shared<DataStore>(loginData.apiToken);

			// Register callbacks
			globalDataStore_->onJoin_ = [this](ServerInfo serverInfo) {
				serverStatus_.fromServerInfo(serverInfo);
			};
			globalDataStore_->onLeave_ = [this]() {
				serverStatus_.clear();
			};

			// Busy wait until ready
			while (!globalDataStore_->isReady());
			JoinStageDialog::showDialog(globalDataStore_);
		});
	});
#endif	

	ApplicationState::sApplicationState.sendPropertyChangeMessage(VALUE_USER_NAME);
}

MainComponent::~MainComponent()
{
#ifdef DIGITAL_STAGE
	LoginDialog::release();
	JoinStageDialog::release();
#endif
	stopAudioIfRunning();
	clientConfigurator_.toData();
	serverStatus_.toData();
	inputSelector_.toData();
	outputSelector_.toData();
	outputController_.toData();
	ownChannels_.toData();
	Data::instance().saveToSettings();
	Settings::instance().saveAndClose();
	setLookAndFeel(nullptr);
}

void MainComponent::refreshChannelSetup(std::shared_ptr<ChannelSetup> setup) {
	JammerNetzChannelSetup channelSetup;
	if (setup) {
		for (int i = 0; i < setup->activeChannelIndices.size(); i++) {
			JammerNetzSingleChannelSetup channel((uint8)ownChannels_.getCurrentTarget(i));
			channel.volume = ownChannels_.getCurrentVolume(i);
			auto username = ApplicationState::sApplicationState.getProperty(VALUE_USER_NAME).toString().toStdString();
			channel.name = setup->activeChannelIndices.size() > 1 ?  username + " " + setup->activeChannelNames[i] : username;
			// Not more than 20 characters please
			if (channel.name.length() > 20)
				channel.name.erase(20, std::string::npos); 
			channelSetup.channels.push_back(channel);
		}
	}
	callback_.setChannelSetup(channelSetup);
	resized();
}

BigInteger makeChannelMask(std::vector<int> const &indices) {
	BigInteger inputChannelMask;
	for (int activeChannelIndex : indices) {
		inputChannelMask.setBit(activeChannelIndex);
	}
	return inputChannelMask;
}

void MainComponent::restartAudio(std::shared_ptr<ChannelSetup> inputSetup, std::shared_ptr<ChannelSetup> outputSetup)
{
	stopAudioIfRunning();

	// Sample rate and buffer size are hard coded for now
	auto selectedType = inputSelector_.deviceType();
	if (selectedType) {
		if (selectedType->hasSeparateInputsAndOutputs()) {
			// This is for other Audio types like DirectSound
			audioDevice_.reset(selectedType->createDevice(outputSetup ? outputSetup->device : "", inputSetup ? inputSetup->device : ""));
		}
		else {
			// Try to create the device purely from the input name, this would be the path for ASIO)
			if (inputSetup) {
				audioDevice_.reset(selectedType->createDevice("", inputSetup->device));
			}
		}

		if (audioDevice_) {
			BigInteger inputChannelMask = inputSetup ? makeChannelMask(inputSetup->activeChannelIndices) : 0;
			BigInteger outputChannelMask = outputSetup ? makeChannelMask(outputSetup->activeChannelIndices) : 0;
			String error = audioDevice_->open(inputChannelMask, outputChannelMask, globalServerInfo.sampleRate, globalServerInfo.bufferSize);
			if (error.isNotEmpty()) {
				jassert(false);
				AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Error opening audio device", "Error text: " + error);
				refreshChannelSetup(std::shared_ptr < ChannelSetup>());
			}
			else {
				float inputLatencyInMS = audioDevice_->getInputLatencyInSamples() / (float)globalServerInfo.sampleRate * 1000.0f;
				ApplicationState::sApplicationState.setProperty(VALUE_INPUT_LATENCY, inputLatencyInMS, nullptr);
				float outputLatencyInMS = audioDevice_->getOutputLatencyInSamples() / (float)globalServerInfo.sampleRate* 1000.0f;
				ApplicationState::sApplicationState.setProperty(VALUE_OUTPUT_LATENCY, outputLatencyInMS, nullptr);

				refreshChannelSetup(inputSetup);
				// We can actually start recording and playing
				audioDevice_->start(&callback_);
			}
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
				while (audioDevice_->isOpen()) {
					Thread::sleep(10);
				}
			}
		}
	}
}

void MainComponent::resized()
{
	auto area = getLocalBounds();
	dsLookAndFeel_.backgroundGradient()->setBounds(area);
	dsLookAndFeel_.backgroundGradient()->setTransformToFit(area.toFloat(), RectanglePlacement::stretchToFit);
	area = area.reduced(kSmallInset);

	int settingsHeight = 400;
	int deviceSelectorWidth = std::min(area.getWidth() / 4, 250);
	int masterMixerWidth = 100; // Stereo mixer

	int inputMixerWidth = masterMixerWidth * (int)currentInputSetup_->activeChannelNames.size() + deviceSelectorWidth + 2 * kNormalInset;

	// To the bottom, the server info and status area
	auto settingsArea = area.removeFromBottom(settingsHeight);
	int settingsSectionWidth = settingsArea.getWidth() / 3;

	// Setup lower left - the server and client config
	auto clientConfigArea = settingsArea.removeFromLeft(settingsSectionWidth);
	serverGroup_.setBounds(clientConfigArea);
	clientConfigArea.reduce(kNormalInset, kNormalInset);
	auto nameRow = clientConfigArea.removeFromTop(kLineSpacing).withTrimmedTop(kNormalInset).withTrimmedLeft(kNormalInset).withTrimmedRight(kNormalInset);
	nameLabel_.setBounds(nameRow.removeFromLeft(kLabelWidth));
	nameEntry_.setBounds(nameRow.removeFromLeft(kEntryBoxWidth));
	nameChange_.setBounds(nameRow.removeFromLeft(kLabelWidth).withTrimmedLeft(kNormalInset));
	clientConfigurator_.setBounds(clientConfigArea.removeFromBottom(kLineSpacing * 2));
	connectionInfo_.setBounds(clientConfigArea.removeFromBottom(kLineSpacing));
	serverStatus_.setBounds(clientConfigArea);

	// Setup lower middle - quality information
	auto qualityArea = settingsArea.removeFromLeft(settingsSectionWidth);
	qualityGroup_.setBounds(qualityArea);
	qualityArea.reduce(kNormalInset, kNormalInset);
	statusInfo_.setBounds(qualityArea.removeFromTop(qualityArea.getHeight() / 2));
	/*for (auto clientInfo : clientInfo_) {
		clientInfo->setBounds(qualityArea.removeFromTop(kLineHeight * 2));
	}
	downstreamInfo_.setBounds(qualityArea);*/
	logo_.setBounds(qualityArea.reduced(kNormalInset).removeFromRight(151).removeFromBottom(81));


	// Lower right - everything with recording!
	auto recordingArea = settingsArea.removeFromLeft(settingsSectionWidth);
	recordingGroup_.setBounds(recordingArea);
	recordingArea.reduce(kNormalInset, kNormalInset);
	//auto midiRecordingInfo = recordingArea.removeFromBottom(30);
	//bpmDisplay_->setBounds(midiRecordingInfo);	
	recordingInfo_->setBounds(recordingArea.removeFromTop(recordingArea.getHeight() / 2));
	localRecordingInfo_->setBounds(recordingArea);

	// To the left, the input selector
	auto inputArea = area.removeFromLeft(inputMixerWidth);
	inputGroup_.setBounds(inputArea);
	inputArea.reduce(kNormalInset, kNormalInset);
	inputSelector_.setBounds(inputArea.removeFromLeft(deviceSelectorWidth));
	ownChannels_.setBounds(inputArea);

	// To the right, the output selector
	auto outputArea = area.removeFromRight(masterMixerWidth + deviceSelectorWidth /* + playalongArea.getWidth()*/);

	// Upper middle, other session participants
	auto sessionArea = area;
	sessionGroup_.setBounds(area);
	allChannels_.setBounds(area.reduced(kNormalInset, kNormalInset));

	// Upper middle, play-along display (prominently)
//	auto playalongArea = area.removeFromLeft(100);
//	playalongDisplay_->setBounds(playalongArea);

	outputGroup_.setBounds(outputArea);
	outputArea.reduce(kNormalInset, kNormalInset);
	outputSelector_.setBounds(outputArea.removeFromRight(deviceSelectorWidth));
	outputController_.setBounds(outputArea);
}

void MainComponent::numConnectedClientsChanged() {
	clientInfo_.clear();

	auto info = callback_.getClientInfo();
	for (uint8 i = 0; i < info->getNumClients(); i++) {
		auto label = new Label();
		addAndMakeVisible(label);
		clientInfo_.add(label);
	}
	fillConnectedClientsStatistics();
	resized();
}

void MainComponent::fillConnectedClientsStatistics() {
	auto info = callback_.getClientInfo();
	if (info) {
		for (uint8 i = 0; i < info->getNumClients(); i++) {
			auto label = clientInfo_[i];
			std::stringstream status;
			status << info->getIPAddress(i) << ":";
			auto quality = info->getStreamQuality(i);
			status
				<< quality.packagesPushed - quality.packagesPopped << " len, "
				<< quality.outOfOrderPacketCounter << " ooO, "
				<< quality.maxWrongOrderSpan << " span, "
				<< quality.duplicatePacketCounter << " dup, "
				<< quality.dropsHealed << " heal, "
				<< quality.tooLateOrDuplicate << " late, "
				<< quality.droppedPacketCounter << " drop ("
				<< std::setprecision(2) << quality.droppedPacketCounter / (float)quality.packagesPopped * 100.0f << "%), "
				<< quality.maxLengthOfGap << " gap";

			label->setText(status.str(), dontSendNotification);
		}
	}
}

void MainComponent::timerCallback()
{
	// Refresh the UI with info from the Audio callback
	std::stringstream status;
	float inputLatency = ApplicationState::sApplicationState.getProperty(VALUE_INPUT_LATENCY);
	float outputLatency = ApplicationState::sApplicationState.getProperty(VALUE_INPUT_LATENCY);
	status << "Quality information" << std::endl << std::fixed << std::setprecision(2);
	status << "Sample rate measured " << callback_.currentSampleRate() << std::endl;
	status << "Underruns: " << callback_.numberOfUnderruns() << std::endl;
	status << "Buffers: " << callback_.currentBufferSize() << std::endl;
	status << "Input latency: " << inputLatency << "ms" << std::endl;
	status << "Output latency: " << outputLatency << "ms" << std::endl;
	status << "Roundtrip: " << callback_.currentRTT() << "ms" << std::endl;
	status << "PlayQ: " << callback_.currentPlayQueueSize() << std::endl;
	status << "Discarded: " << callback_.currentDiscardedPackageCounter() << std::endl;
	status << "Total: " << callback_.currentToPlayLatency() + inputLatency + outputLatency << " ms" << std::endl;
	statusInfo_.setText(status.str(), dontSendNotification);
	downstreamInfo_.setText(callback_.currentReceptionQuality(), dontSendNotification);
	std::stringstream connectionInfo;
	connectionInfo << std::fixed << std::setprecision(2)
		<< "Network MTU: " << callback_.currentPacketSize() << " bytes. Bandwidth: "
		<< callback_.currentPacketSize() * 8 * (globalServerInfo.sampleRate / (float)globalServerInfo.bufferSize) / (1024 * 1024.0f) << "MBit/s. ";
	connectionInfo_.setText(connectionInfo.str(), dontSendNotification);

	if (callback_.getClientInfo() && callback_.getClientInfo()->getNumClients() != clientInfo_.size()) {
		// Need to re-setup the UI
		numConnectedClientsChanged();
	}
	else {
		fillConnectedClientsStatistics();
	}

	serverStatus_.setConnected(callback_.isReceivingData());

	// Refresh tuning info for my own channels
	for (int i = 0; i < currentInputSetup_->activeChannelNames.size(); i++) {
		ownChannels_.setPitchDisplayed(i, MidiNote(callback_.channelPitch(i)));
	}
	// and for the session channels
	if (currentSessionSetup_) {
		for (int i = 0; i < currentSessionSetup_->channels.size(); i++) {
			allChannels_.setPitchDisplayed(i, MidiNote(callback_.sessionPitch(i)));
		}
	}

	// Refresh session participants in case this changed!
	if (!currentSessionSetup_ || !(currentSessionSetup_->isEqualEnough(callback_.getSessionSetup()))) {
		currentSessionSetup_ = std::make_shared<JammerNetzChannelSetup>(callback_.getSessionSetup());
		// Setup changed, need to re-init UI
		allChannels_.setup(currentSessionSetup_, callback_.getSessionMeterSource());
	}
}

void MainComponent::setupChanged(std::shared_ptr<ChannelSetup> setup)
{
	stopAudioIfRunning();

	currentInputSetup_ = setup;

	// Rebuild UI for the channel controllers, and provide a callback to change the data in the Audio callback sent to the server
	ownChannels_.setup(setup, callback_.getMeterSource(), [this](std::shared_ptr<ChannelSetup> setup) {
		refreshChannelSetup(setup); 
	});

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

void MainComponent::updateUserName() {
	ApplicationState::sApplicationState.setProperty(VALUE_USER_NAME, nameEntry_.getText(), nullptr);
	ApplicationState::toSettings(); // TODO This should be automatic!
	refreshChannelSetup(currentInputSetup_);
}
