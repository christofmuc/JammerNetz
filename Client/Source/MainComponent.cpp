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

#include "BuffersConfig.h"
#include <cmath>
#include <set>

MainComponent::MainComponent(std::shared_ptr<AudioService> audioService, std::shared_ptr<Recorder> masterRecorder, std::shared_ptr<Recorder> localRecorder) :
	audioService_(audioService),
	inputSelector_(VALUE_INPUT_SETUP, false, true),
	outputSelector_(VALUE_OUTPUT_SETUP, false, false),
	outputController_("Master", VALUE_MASTER_OUTPUT, true, false),
	monitorBalance_("Local", "Remote", 50),
	bpmSlider_(juce::Slider::SliderStyle::LinearHorizontal, juce::Slider::TextBoxRight),
    logView_(false), // Turn off line numbers
    stageLeftWhenInMillis_(Time::currentTimeMillis())
{
	// Create an a Logger for the JammerNetz client
	logViewLogger_ = std::make_unique<LogViewLogger>(logView_);
	SimpleLogger::instance()->postMessage("Welcome!");

	// We want data updates for our log window
	Data::instance().get().addListener(this);

	recordingInfo_ = std::make_unique<RecordingInfo>(masterRecorder, "Press to record master mix");
	//playalongDisplay_ = std::make_unique<PlayalongDisplay>(callback_.getPlayalong());
	localRecordingInfo_ = std::make_unique<RecordingInfo>(localRecorder, "Press to record yourself only");

	// MidiClock
	addAndMakeVisible(bpmSlider_);
	bpmSlider_.setRange(Range(10.0, 250.0), 0.1);
	bpmSlider_.setTitle("bpm");
	Data::ensurePropertyExists(VALUE_SERVER_BPM, 0.0);
	bpmSlider_.getValueObject().referTo(Data::getPropertyAsValue(VALUE_SERVER_BPM));
	midiStart_.setButtonText("Start");
	midiStart_.onClick = [this]() { audioService_->setMidiSignal(MidiSignal_Start); };
	midiStop_.setButtonText("Stop");
	midiStop_.onClick = [this]() { audioService_->setMidiSignal(MidiSignal_Stop); };
	addAndMakeVisible(midiStart_);
	addAndMakeVisible(midiStop_);

	// Setup output and monitoring
	monitorLocal_.setClickingTogglesState(true);
	auto mixer = Data::instance().get().getOrCreateChildWithName(VALUE_MIXER, nullptr);
	listeners_.push_back(std::make_unique<ValueListener>(mixer.getPropertyAsValue(VALUE_USE_LOCAL_MONITOR, nullptr), [this](Value& value) {
		ignoreUnused(value);
		monitorLocal_.setButtonText(monitorLocal_.getToggleState() ? "Local" : "Remote");
	}));
	monitorLocal_.getToggleStateValue().referTo(mixer.getPropertyAsValue(VALUE_USE_LOCAL_MONITOR, nullptr));
	monitorBalance_.slider().setSliderStyle(Slider::SliderStyle::LinearHorizontal);
	monitorBalance_.slider().setTextBoxStyle(Slider::TextEntryBoxPosition::NoTextBox, true, 0, 0);
	monitorBalance_.slider().setRange(-1.0, 1.0, 0.1);
	monitorBalance_.slider().getValueObject().referTo(mixer.getPropertyAsValue(VALUE_MONITOR_BALANCE, nullptr));
	outputController_.setMeterSource(audioService_->getOutputMeterSource(), -1);

	nameLabel_.setText("My name", dontSendNotification);
	listeners_.push_back(std::make_unique<ValueListener>(Data::instance().get().getPropertyAsValue(VALUE_USER_NAME, nullptr), [this](Value &value) {
		nameEntry_.setText(value.toString(), dontSendNotification);
	}));
	for_each(listeners_.begin(), listeners_.end(), [](std::unique_ptr<ValueListener>& ptr) { ptr->triggerOnChanged();  });

	nameChange_.setButtonText("Change");
	nameChange_.onClick = [this]() { updateUserName();  };
	nameEntry_.onEscapeKey = [this]() { nameEntry_.setText(Data::instance().get().getProperty(VALUE_USER_NAME), dontSendNotification);  };
	nameEntry_.onReturnKey = [this]() { updateUserName(); };

	inputGroup_.setText("Input");
	sessionGroup_.setText("Session participants");
	outputGroup_.setText("Output");
	clockGroup_.setText("MIDI Clock");
	serverGroup_.setText("Settings");
	qualityGroup_.setText("Quality Info");
	recordingGroup_.setText("Recording");
	monitorGroup_.setText("Monitor Balance");
	logGroup_.setText("Log");

	addAndMakeVisible(outputController_);
	addAndMakeVisible(inputGroup_);
	addAndMakeVisible(inputSelector_);
	addAndMakeVisible(ownChannels_);
	addAndMakeVisible(sessionGroup_);
	addAndMakeVisible(allChannels_);
	allChannels_.setSessionVolumeChangedHandler([this](uint32 targetClientId, uint16 targetChannelIndex, float volumePercent) {
		if (audioService_) {
			constexpr juce::int64 kDragSendIntervalMs = 50;
			constexpr float kDragMinDeltaPercent = 1.0f;
			constexpr juce::int64 kSettleWindowMs = 300;

			auto key = std::make_pair(targetClientId, targetChannelIndex);
			auto now = Time::currentTimeMillis();
			auto &state = remoteVolumeCommandState_[key];
			bool isDragging = allChannels_.isSessionVolumeSliderBeingDragged(targetClientId, targetChannelIndex);
			if (isDragging) {
				bool enoughTimePassed = state.lastSentMillis == 0 || (now - state.lastSentMillis) >= kDragSendIntervalMs;
				bool enoughVolumeDelta = state.lastSentMillis == 0 || std::fabs(volumePercent - state.lastSentPercent) >= kDragMinDeltaPercent;
				if (!enoughTimePassed || !enoughVolumeDelta) {
					return;
				}
			}

			audioService_->setRemoteParticipantVolume(targetClientId, targetChannelIndex, volumePercent);
			state.lastSentMillis = now;
			state.lastSentPercent = volumePercent;
			state.holdUntilMillis = now + kSettleWindowMs;
		}
	});
	addAndMakeVisible(statusInfo_);
	addAndMakeVisible(downstreamInfo_);
	addAndMakeVisible(outputGroup_);
	addAndMakeVisible(clockGroup_);
	addAndMakeVisible(outputSelector_);
	addAndMakeVisible(monitorBalance_);
	addAndMakeVisible(monitorLocal_);
	addAndMakeVisible(monitorGroup_);
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
	//addAndMakeVisible(*playalongDisplay_);
	addAndMakeVisible(*localRecordingInfo_);
	addAndMakeVisible(logGroup_);
	addAndMakeVisible(logView_);
	addAndMakeVisible(clockSelector_);

	startTimer(100);

	// Make sure you set the size of the component after
	// you add any child components.
	setSize(1536, 800);

	clockSelector_.onSelectionChanged = [this](std::vector<juce::MidiDeviceInfo> activeOutputs) {
		if (audioService_) {
			audioService_->setClockOutputs(activeOutputs);
		}
	};
	MessageManager::callAsync([this]() { clockSelector_.refreshList(); });
}

MainComponent::~MainComponent()
{
	Data::instance().saveToSettings();
	Settings::instance().saveAndClose();
	audioService_->shutdown();
	setLookAndFeel(nullptr);
}

void MainComponent::resized()
{
	auto area = getLocalBounds();
	area = area.reduced(kSmallInset);

	int settingsHeight = 400;
	int deviceSelectorWidth = std::min(area.getWidth() / 5, 250);
	int masterMixerWidth = 180; // Stereo mixer
	int singleMixerWidth = 100;

	auto numInputMixers = ownChannels_.numChannels();

	int inputMixerWidth = singleMixerWidth * numInputMixers + deviceSelectorWidth + 2 * kNormalInset;

	// To the bottom, the server info and status area
	auto settingsArea = area.removeFromBottom(settingsHeight);
	int settingsSectionWidth = settingsArea.getWidth() / 4;

	// Setup lower left - the server and client config
	auto clientConfigArea = settingsArea.removeFromLeft(settingsSectionWidth);
	serverGroup_.setBounds(clientConfigArea);
	clientConfigArea.reduce(kNormalInset, kNormalInset);
	auto nameRow = clientConfigArea.removeFromTop(kLineSpacing).withTrimmedTop(kNormalInset).withTrimmedLeft(kNormalInset).withTrimmedRight(kNormalInset);
	nameLabel_.setBounds(nameRow.removeFromLeft(kLabelWidth));
	nameEntry_.setBounds(nameRow.removeFromLeft(kEntryBoxWidth));
	nameChange_.setBounds(nameRow.removeFromLeft(kLabelWidth / 2).withTrimmedLeft(kNormalInset));

	clientConfigurator_.setBounds(clientConfigArea.removeFromBottom(kLineSpacing * 2));
	connectionInfo_.setBounds(clientConfigArea.removeFromBottom(kLineSpacing));
	serverStatus_.setBounds(clientConfigArea);

	// Setup lower middle - quality information
	auto qualityArea = settingsArea.removeFromLeft(settingsSectionWidth);
	qualityGroup_.setBounds(qualityArea);
	qualityArea.reduce(kNormalInset, kNormalInset);
	statusInfo_.setBounds(qualityArea.removeFromTop(qualityArea.getHeight() / 2));
	for (auto clientInfo : clientInfo_) {
		clientInfo->setBounds(qualityArea.removeFromTop(kLineHeight * 2));
	}
	downstreamInfo_.setBounds(qualityArea);

	// Lower right - everything with recording!
	auto recordingArea = settingsArea.removeFromLeft(settingsSectionWidth);
	recordingGroup_.setBounds(recordingArea);
	recordingArea.reduce(kNormalInset, kNormalInset);
	//auto midiRecordingInfo = recordingArea.removeFromBottom(30);
	//bpmDisplay_->setBounds(midiRecordingInfo);
	recordingInfo_->setBounds(recordingArea.removeFromTop(recordingArea.getHeight() / 2));
	localRecordingInfo_->setBounds(recordingArea);

	// Far lower right  - the new Log View
	auto logArea = settingsArea;
	logGroup_.setBounds(logArea);
	logArea.reduce(kNormalInset, kNormalInset);
	logView_.setBounds(logArea.reduced(kNormalInset));

	// To the left, the input selector
	auto inputArea = area.removeFromLeft(inputMixerWidth);
	inputGroup_.setBounds(inputArea);
	inputArea.reduce(kNormalInset, kNormalInset);
	inputSelector_.setBounds(inputArea.removeFromLeft(deviceSelectorWidth));
	ownChannels_.setBounds(inputArea);

	// To the right, the output selector
	auto clockArea = area.removeFromRight(deviceSelectorWidth /* + playalongArea.getWidth()*/);
	auto outputArea = area.removeFromRight(masterMixerWidth + deviceSelectorWidth /* + playalongArea.getWidth()*/);

	// Upper middle, other session participants
	sessionGroup_.setBounds(area);
	allChannels_.setBounds(area.reduced(kNormalInset, kNormalInset));

	// Upper middle, play-along display (prominently)
//	auto playalongArea = area.removeFromLeft(100);
//	playalongDisplay_->setBounds(playalongArea);
	clockGroup_.setBounds(clockArea);
	clockArea.reduce(kNormalInset, kNormalInset);
	bpmSlider_.setBounds(clockArea.removeFromTop(kLineSpacing));
	auto transportRow = clockArea.removeFromTop(kLineSpacing);
	midiStart_.setBounds(transportRow.removeFromLeft(kButtonWidth).reduced(kSmallInset));
	midiStop_.setBounds(transportRow.removeFromLeft(kButtonWidth).reduced(kSmallInset));
	clockSelector_.setBounds(clockArea.withTrimmedTop(kNormalInset));

	outputGroup_.setBounds(outputArea);
	outputArea.reduce(kNormalInset, kNormalInset);
	auto outputRightColumn = outputArea.removeFromRight(deviceSelectorWidth).withTrimmedLeft(kSmallInset);
	auto monitorArea = outputRightColumn.removeFromBottom(2 * kLineSpacing + 2 * kNormalInset);
	monitorGroup_.setBounds(monitorArea);
	auto monitorContent = monitorArea.reduced(kNormalInset);
	monitorBalance_.setBounds(monitorContent.removeFromBottom(kLineHeight));
	monitorLocal_.setBounds(monitorContent.removeFromBottom(kLineSpacing).reduced(kSmallInset));

	outputSelector_.setBounds(outputRightColumn);
	outputController_.setBounds(outputArea);
}

void MainComponent::valueTreePropertyChanged(ValueTree& treeWhosePropertyHasChanged, const Identifier& property)
{
	String propertyName = property.toString();
	ValueTree parent = treeWhosePropertyHasChanged;
	while (parent.isValid()) {
		propertyName = parent.getType().toString() + ">" + propertyName;
		parent = parent.getParent();
	}
	logView_.addMessageToList(propertyName + " updated: " + treeWhosePropertyHasChanged.getProperty(property).toString());

	// Check if this was an input channel definition?
	if (ValueTreeUtils::isChildOf(VALUE_INPUT_SETUP, treeWhosePropertyHasChanged)) {
		inputSetupChanged();
	}
}

void MainComponent::numConnectedClientsChanged() {
	clientInfo_.clear();

	auto info = audioService_->getClientInfo();
	for (uint8 i = 0; i < info->getNumClients(); i++) {
		auto label = new Label();
		addAndMakeVisible(label);
		clientInfo_.add(label);
	}
	fillConnectedClientsStatistics();
	resized();
}

void MainComponent::fillConnectedClientsStatistics() {
	auto info = audioService_->getClientInfo();
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
	float inputLatency = Data::instance().get().getProperty(VALUE_INPUT_LATENCY);
	float outputLatency = Data::instance().get().getProperty(VALUE_INPUT_LATENCY);
	PlayoutQualityInfo qualityInfo = audioService_->getPlayoutQualityInfo();
	status << "Quality information" << std::endl << std::fixed << std::setprecision(2);
	status << "Sample rate measured " << qualityInfo.measuredSampleRate << std::endl;
	status << "Underruns: " << qualityInfo.playUnderruns_ << std::endl;
	status << "Input latency: " << inputLatency << "ms" << std::endl;
	status << "Output latency: " << outputLatency << "ms" << std::endl;
	status << "Roundtrip: " << audioService_->currentRTT() << "ms" << std::endl;
	status << "PlayQ: " << qualityInfo.currentPlayQueueLength_ << std::endl;
	status << "Discarded: " << qualityInfo.discardedPackageCounter_ << std::endl;
	status << "Latency without I/O: " << qualityInfo.toPlayLatency_ << " ms" << std::endl;
	status << "Total: " <<  qualityInfo.toPlayLatency_ + inputLatency + outputLatency << " ms" << std::endl;
	statusInfo_.setText(status.str(), dontSendNotification);
	downstreamInfo_.setText(audioService_->currentReceptionQuality(), dontSendNotification);
	std::stringstream connectionInfo;
	connectionInfo << std::fixed << std::setprecision(2)
		<< "Network MTU: " << audioService_->currentPacketSize() << " bytes. Bandwidth: "
		<< audioService_->currentPacketSize() * 8 * (SAMPLE_RATE / (float)SAMPLE_BUFFER_SIZE) / (1024 * 1024.0f) << "MBit/s. ";
	connectionInfo_.setText(connectionInfo.str(), dontSendNotification);

	if (audioService_->getClientInfo() && audioService_->getClientInfo()->getNumClients() != clientInfo_.size()) {
		// Need to re-setup the UI
		numConnectedClientsChanged();
	}
	else {
		fillConnectedClientsStatistics();
	}

	serverStatus_.setConnected(audioService_->isConnected());

	// Refresh tuning info for my own channels
	for (int i = 0; i < ownChannels_.numChannels(); i++) {
		ownChannels_.setPitchDisplayed(i, MidiNote(audioService_->channelPitch((size_t) i)));
	}
	// and for the session channels
	if (currentSessionSetup_) {
		for (int i = 0; i < (int) currentSessionSetup_->channels.size(); i++) {
			allChannels_.setPitchDisplayed(i, MidiNote(audioService_->sessionPitch((size_t) i)));
		}
	}

	// Refresh session participants in case this changed!
	auto thisSetup = audioService_->getSessionSetup();
	auto now = Time::currentTimeMillis();
	// Hold freshly commanded remote volumes briefly to avoid visible snap/wiggle from stale session snapshots.
	for (auto &channel : thisSetup.channels) {
		auto key = std::make_pair(channel.sourceClientId, channel.sourceChannelIndex);
		auto state = remoteVolumeCommandState_.find(key);
		if (state != remoteVolumeCommandState_.end()) {
			auto sliderBeingDragged = allChannels_.isSessionVolumeSliderBeingDragged(channel.sourceClientId, channel.sourceChannelIndex);
			if (sliderBeingDragged || now <= state->second.holdUntilMillis) {
				channel.volume = state->second.lastSentPercent / 100.0f;
			}
		}
	}
	bool sessionSliderDragged = allChannels_.isAnyVolumeSliderBeingDragged();
	if (!currentSessionSetup_ || !isSessionLayoutEqual(*currentSessionSetup_, thisSetup)) {
		if (sessionSliderDragged) {
			return;
		}
		currentSessionSetup_ = std::make_shared<JammerNetzChannelSetup>(thisSetup);
		// Setup changed, need to re-init UI
		allChannels_.setup(currentSessionSetup_, audioService_->getSessionMeterSource());
	}
	else {
		*currentSessionSetup_ = thisSetup;
		updateSessionChannelsIncremental(thisSetup);
	}
	pruneRemoteVolumeControlState(thisSetup);
}

void MainComponent::inputSetupChanged() {
	// Rebuild UI for the channel controllers, and provide a callback to change the data in the Audio callback sent to the server
	ownChannels_.setup(audioService_->getInputSetup(), audioService_->getInputMeterSource());
	resized();
}

void MainComponent::updateUserName() {
	Data::instance().get().setProperty(VALUE_USER_NAME, nameEntry_.getText(), nullptr);
}

bool MainComponent::isSessionLayoutEqual(const JammerNetzChannelSetup& lhs, const JammerNetzChannelSetup& rhs) const
{
	if (lhs.channels.size() != rhs.channels.size()) {
		return false;
	}
	for (size_t i = 0; i < lhs.channels.size(); i++) {
		const auto &left = lhs.channels[i];
		const auto &right = rhs.channels[i];
		if (left.name != right.name
			|| left.target != right.target
			|| left.sourceClientId != right.sourceClientId
			|| left.sourceChannelIndex != right.sourceChannelIndex) {
			return false;
		}
	}
	return true;
}

void MainComponent::updateSessionChannelsIncremental(const JammerNetzChannelSetup& setup)
{
	for (int i = 0; i < static_cast<int>(setup.channels.size()); i++) {
		const auto &channel = setup.channels[static_cast<size_t>(i)];
		if (allChannels_.isSessionVolumeSliderBeingDragged(channel.sourceClientId, channel.sourceChannelIndex)) {
			continue;
		}
		allChannels_.setSessionChannelVolume(i, channel.volume * 100.0f);
	}
}

void MainComponent::pruneRemoteVolumeControlState(const JammerNetzChannelSetup& setup)
{
	std::set<std::pair<uint32, uint16>> validKeys;
	for (const auto &channel : setup.channels) {
		validKeys.insert(std::make_pair(channel.sourceClientId, channel.sourceChannelIndex));
	}

	for (auto it = remoteVolumeCommandState_.begin(); it != remoteVolumeCommandState_.end(); ) {
		if (validKeys.find(it->first) == validKeys.end()) {
			it = remoteVolumeCommandState_.erase(it);
		}
		else {
			++it;
		}
	}
}
