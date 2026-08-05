/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "AudioService.h"

#include "Data.h"
#include "ApplicationState.h"
#include "BuffersConfig.h"
#include "AudioDeviceDiscovery.h"
#include "AudioCorrectness.h"

#include "Logger.h"

AudioService::AudioService()
{
	// Put the list into the ephemeral app data (not stored across runs of the software)
	auto& data = Data::instance().getEphemeral();
	data.setProperty(EPHEMERAL_VALUE_DEVICE_TYPES_AVAILABLE, AudioDeviceDiscovery::allDeviceTypeNames(), nullptr);
	data.setProperty(EPHEMERAL_VALUE_AUDIO_RUNNING, false, nullptr);

	Data::instance().get().addListener(this);
	Data::instance().getEphemeral().addListener(this);
}

AudioService::~AudioService()
{
	Data::instance().get().removeListener(this);
	Data::instance().getEphemeral().removeListener(this);
	shutdown();
	AudioDeviceDiscovery::shutdown();
}

void AudioService::shutdown()
{
	if (shutdown_.exchange(true, std::memory_order_acq_rel)) {
		return;
	}
	stopAudioIfRunning();
	callback_.shutdown();
}

bool AudioService::isConnected()
{
	return callback_.isReceivingData();
}

void AudioService::refreshChannelSetup(std::shared_ptr<ChannelSetup> setup)
{
	auto mixer = Data::instance().get().getChildWithName(VALUE_MIXER);
	bool isLocalMonitoring = mixer.getProperty(VALUE_USE_LOCAL_MONITOR);
	JammerNetzChannelSetup channelSetup(isLocalMonitoring);

	if (setup) {
		for (size_t i = 0; i < setup->activeChannelIndices.size(); i++) {
			String inputController = "Input" + String(i);
			auto controllerData = mixer.getChildWithName(inputController);
			jassert(controllerData.isValid());
			JammerNetzChannelTarget target = static_cast<JammerNetzChannelTarget>(((int)  controllerData.getProperty(VALUE_TARGET, JammerNetzChannelTarget::Mono)) - 1);
			JammerNetzSingleChannelSetup channel((uint8) target);
			double volume = controllerData.getProperty(VALUE_VOLUME, 100.0);
			channel.volume = (float)volume/100.0f;
			auto username = Data::instance().get().getProperty(VALUE_USER_NAME).toString().toStdString();
			channel.name = setup->activeChannelIndices.size() > 1 ? username + " " + setup->activeChannelNames[i] : username;
			// Not more than 20 characters please
			if (channel.name.length() > 20)
				channel.name.erase(20, std::string::npos);
			channelSetup.channels.push_back(channel);
		}
	}
	callback_.setChannelSetup(channelSetup);
}

void AudioService::stopAudioIfRunning()
{
	jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

	Data::instance().getEphemeral().setProperty(EPHEMERAL_VALUE_AUDIO_RUNNING, false, nullptr);
	if (audioDevice_) {
		if (audioDevice_->isPlaying()) {
			audioDevice_->stop();
		}
		if (audioDevice_->isOpen()) {
			audioDevice_->close();
			if (audioDevice_->isOpen()) {
				SimpleLogger::instance()->postMessage("Audio device did not close cleanly");
			}
		}
		audioDevice_.reset();
	}
}

void AudioService::setClockOutputs(std::vector<juce::MidiDeviceInfo> outputs)
{
	callback_.restartClock(outputs);
}

void AudioService::setMidiSignal(MidiSignal signal)
{
	callback_.setMidiSignalToSend(signal);
}

std::shared_ptr<Recorder> AudioService::getMasterRecorder() const
{
	return callback_.getMasterRecorder();
}

std::shared_ptr<Recorder> AudioService::getLocalRecorder() const
{
	return callback_.getLocalRecorder();
}

std::shared_ptr<ChannelSetup> AudioService::getInputSetup() const
{
	return getSetup(Data::instance().get().getChildWithName(VALUE_INPUT_SETUP));
}

std::shared_ptr<ChannelSetup> AudioService::getOutputSetup() const
{
	return getSetup(Data::instance().get().getChildWithName(VALUE_OUTPUT_SETUP));
}

JammerNetzChannelSetup AudioService::getSessionSetup()
{
	return callback_.getSessionSetup();
}

std::shared_ptr<JammerNetzClientInfoMessage> AudioService::getClientInfo()
{
	return callback_.getClientInfo();
}

PlayoutQualityInfo AudioService::getPlayoutQualityInfo()
{
	return callback_.getPlayoutQualityInfo();
}

double AudioService::currentRTT()
{
	return callback_.currentRTT();
}

std::string AudioService::currentReceptionQuality() const
{
	return callback_.currentReceptionQuality();
}

int AudioService::currentPacketSize()
{
	return callback_.currentPacketSize();
}

float AudioService::channelPitch(size_t channel) const
{
	return callback_.channelPitch(channel);
}

float AudioService::sessionPitch(size_t channel)
{
	return callback_.sessionPitch(channel);
}

FFAU::LevelMeterSource* AudioService::getInputMeterSource()
{
	return callback_.getMeterSource();
}

FFAU::LevelMeterSource* AudioService::getOutputMeterSource()
{
	return callback_.getOutputMeterSource();
}

FFAU::LevelMeterSource* AudioService::getSessionMeterSource()
{
	return callback_.getSessionMeterSource();
}

void AudioService::valueTreePropertyChanged(ValueTree& treeWhosePropertyHasChanged, const Identifier& property)
{
	if (//ValueTreeUtils::isChildOf(VALUE_INPUT_SETUP, treeWhosePropertyHasChanged) ||
		//ValueTreeUtils::isChildOf(VALUE_OUTPUT_SETUP, treeWhosePropertyHasChanged) ||
		property == Identifier(EPHEMERAL_VALUE_AUDIO_SHOULD_RUN)) {
		debouncer_.callDebounced(
		    [this]() {
			    bool shouldRun = Data::getEphemeralProperty(EPHEMERAL_VALUE_AUDIO_SHOULD_RUN);
			    if (shouldRun) {
				    restartAudio();
				    if (!Data::getEphemeralProperty(EPHEMERAL_VALUE_AUDIO_RUNNING)) {
						// That failed, turn it off again without notifying us.
					    Data::instance().getEphemeral().setPropertyExcludingListener(this, EPHEMERAL_VALUE_AUDIO_SHOULD_RUN, false, nullptr);
				    }
			    } else {
				    stopAudioIfRunning();
				}

		}, 250);
	}
	else if (ValueTreeUtils::isChildOf(VALUE_MIXER, treeWhosePropertyHasChanged) || property.toString() == VALUE_USER_NAME) {
		refreshChannelSetup(getSetup(Data::instance().get().getChildWithName(VALUE_INPUT_SETUP)));
	}
}

std::shared_ptr<ChannelSetup> AudioService::getSetup(ValueTree data) const
{
	// Build the current setup as data record
	std::shared_ptr<ChannelSetup> channelSetup = std::make_shared<ChannelSetup>();
	String deviceName = data.getProperty(VALUE_DEVICE_NAME, "unknown");
	channelSetup->device = deviceName.toStdString();
	auto selectedType = AudioDeviceDiscovery::deviceTypeByName(Data::instance().get().getProperty(VALUE_DEVICE_TYPE, "unknown"));
	if (selectedType && selectedType->getTypeName().isNotEmpty()) {
		channelSetup->typeName = selectedType->getTypeName().toStdString();
		channelSetup->isInputAndOutput = !selectedType->hasSeparateInputsAndOutputs();
		auto channels = data.getChildWithName(VALUE_CHANNELS);
		if (channels.isValid()) {
			int numChannels = channels.getProperty(VALUE_CHANNEL_COUNT);
			for (int i = 0; i < numChannels; i++) {
				String channelPropName = "Channel" + String(i);
				auto channel = channels.getChildWithName(channelPropName);
				if (channel.isValid()) {
					bool isActive = channel.getProperty(VALUE_CHANNEL_ACTIVE);
					if (isActive) {
						channelSetup->activeChannelIndices.push_back(i);
						channelSetup->activeChannelNames.push_back(channel.getProperty(VALUE_CHANNEL_NAME).toString().toStdString());
					}
				}
			}
		}
	}
	return channelSetup;
}

static BigInteger makeChannelMask(std::vector<int> const& indices) {
	BigInteger inputChannelMask;
	for (int activeChannelIndex : indices) {
		inputChannelMask.setBit(activeChannelIndex);
	}
	return inputChannelMask;
}

void AudioService::restartAudio(std::shared_ptr<ChannelSetup> inputSetup, std::shared_ptr<ChannelSetup> outputSetup)
{
	auto failStartup = [this](const String& message) {
		SimpleLogger::instance()->postMessage("Audio startup failed: " + message);
		Data::instance().getEphemeral().setProperty(EPHEMERAL_VALUE_AUDIO_RUNNING, false, nullptr);
		refreshChannelSetup({});
		if (audioDevice_) {
			if (audioDevice_->isPlaying()) {
				audioDevice_->stop();
			}
			if (audioDevice_->isOpen()) {
				audioDevice_->close();
			}
			audioDevice_.reset();
		}
	};

	if (!inputSetup || !outputSetup || !AudioCorrectness::hasUsableChannelSelection(inputSetup->activeChannelIndices, outputSetup->activeChannelIndices)) {
		failStartup("at least one input and one output channel must be selected");
		return;
	}

	juce::AudioIODeviceType* selectedType = AudioDeviceDiscovery::deviceTypeByName(inputSetup ? inputSetup->typeName : "");
	// Sample rate and buffer size are hard coded for now
	if (!selectedType) {
		failStartup("the selected audio device type is unavailable");
		return;
	}

	selectedType->scanForDevices();
	{
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

		if (!audioDevice_) {
			failStartup("the selected audio device could not be created");
			return;
		}

		{
			BigInteger inputChannelMask = inputSetup ? makeChannelMask(inputSetup->activeChannelIndices) : 0;
			BigInteger outputChannelMask = outputSetup ? makeChannelMask(outputSetup->activeChannelIndices) : 0;

			// Prefer the network block size, or the smallest supported size above it.
			auto buffers = audioDevice_->getAvailableBufferSizes();
			const std::vector<int> availableBufferSizes(buffers.begin(), buffers.end());
			const auto selectedBufferSize = AudioCorrectness::selectBufferSize(availableBufferSizes, SAMPLE_BUFFER_SIZE);
			if (!selectedBufferSize) {
				failStartup("the device reports no usable buffer sizes");
				return;
			}

			String error = audioDevice_->open(inputChannelMask, outputChannelMask, SAMPLE_RATE, *selectedBufferSize);
			if (error.isNotEmpty()) {
				failStartup(error);
				return;
			}

			const double actualSampleRate = audioDevice_->getCurrentSampleRate();
			if (std::abs(actualSampleRate - static_cast<double>(SAMPLE_RATE)) > 0.5) {
				failStartup("the device opened at " + String(actualSampleRate) + " Hz instead of " + String(SAMPLE_RATE) + " Hz");
				return;
			}

			const auto inputLatencyInMS = static_cast<double>(audioDevice_->getInputLatencyInSamples()) / actualSampleRate * 1000.0;
			Data::instance().get().setProperty(VALUE_INPUT_LATENCY, inputLatencyInMS, nullptr);
			const auto outputLatencyInMS = static_cast<double>(audioDevice_->getOutputLatencyInSamples()) / actualSampleRate * 1000.0;
			Data::instance().get().setProperty(VALUE_OUTPUT_LATENCY, outputLatencyInMS, nullptr);

			refreshChannelSetup(inputSetup);
			audioDevice_->start(&callback_);
			if (!audioDevice_->isPlaying()) {
				failStartup("the device opened but did not start");
				return;
			}
			Data::instance().getEphemeral().setProperty(EPHEMERAL_VALUE_AUDIO_RUNNING, true, nullptr);
		}
	}
}

void AudioService::restartAudio()
{
	// Build the data structures required to properly restart the audio objects
	jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

	stopAudioIfRunning();

	auto& data = Data::instance().get();
	auto inputSetup = getSetup(data.getChildWithName(VALUE_INPUT_SETUP));
	auto outputSetup = getSetup(data.getChildWithName(VALUE_OUTPUT_SETUP));
	restartAudio(inputSetup, outputSetup);
}
