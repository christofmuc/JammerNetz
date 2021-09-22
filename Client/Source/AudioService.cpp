/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "AudioService.h"

#include "Data.h"
#include "ApplicationState.h"
#include "BuffersConfig.h"
#include "AudioDeviceDiscovery.h"

AudioService::AudioService() 
{
	// Put the list into the ephemeral app data (not stored across runs of the software)
	auto& data = Data::instance().getEphemeral();
	data.setProperty(EPHEMERAL_VALUE_DEVICE_TYPES_AVAILABLE, AudioDeviceDiscovery::allDeviceTypeNames(), nullptr);

	Data::instance().get().addListener(this);
}

AudioService::~AudioService()
{
	Data::instance().get().removeListener(this);
	stopAudioIfRunning();
	AudioDeviceDiscovery::shutdown();
}

void AudioService::refreshChannelSetup(std::shared_ptr<ChannelSetup> setup)
{
	JammerNetzChannelSetup channelSetup;
	if (setup) {
		for (int i = 0; i < setup->activeChannelIndices.size(); i++) {
			JammerNetzSingleChannelSetup channel(JammerNetzChannelTarget::Mono /*(uint8)ownChannels_.getCurrentTarget(i)*/); //TODO
			channel.volume = 1.0f; // ownChannels_.getCurrentVolume(i);
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
	if (ValueTreeUtils::isChildOf(VALUE_INPUT_SETUP, treeWhosePropertyHasChanged) || 
		ValueTreeUtils::isChildOf(VALUE_OUTPUT_SETUP, treeWhosePropertyHasChanged)) {
		debouncer_.callDebounced([this]() {
			restartAudio();
		}, 250);
	}
}

std::shared_ptr<ChannelSetup> AudioService::getSetup(ValueTree data) const
{
	// Build the current setup as data record 
	std::shared_ptr<ChannelSetup> channelSetup = std::make_shared<ChannelSetup>();
	String deviceName = data.getProperty(VALUE_DEVICE_NAME, "unknown");
	channelSetup->device = deviceName.toStdString();
	auto selectedType = AudioDeviceDiscovery::deviceTypeByName(data.getProperty(VALUE_DEVICE_TYPE, "unknown"));
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

BigInteger makeChannelMask(std::vector<int> const& indices) {
	BigInteger inputChannelMask;
	for (int activeChannelIndex : indices) {
		inputChannelMask.setBit(activeChannelIndex);
	}
	return inputChannelMask;
}

void AudioService::restartAudio(std::shared_ptr<ChannelSetup> inputSetup, std::shared_ptr<ChannelSetup> outputSetup)
{
	juce::AudioIODeviceType* selectedType = AudioDeviceDiscovery::deviceTypeByName(inputSetup ? inputSetup->typeName : "");
	// Sample rate and buffer size are hard coded for now
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
			String error = audioDevice_->open(inputChannelMask, outputChannelMask, SAMPLE_RATE, SAMPLE_BUFFER_SIZE);
			if (error.isNotEmpty()) {
				jassert(false);
				AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Error opening audio device", "Error text: " + error);
				refreshChannelSetup(std::shared_ptr < ChannelSetup>());
			}
			else {
				float inputLatencyInMS = audioDevice_->getInputLatencyInSamples() / (float)SAMPLE_RATE * 1000.0f;
				Data::instance().get().setProperty(VALUE_INPUT_LATENCY, inputLatencyInMS, nullptr);
				float outputLatencyInMS = audioDevice_->getOutputLatencyInSamples() / (float)SAMPLE_RATE * 1000.0f;
				Data::instance().get().setProperty(VALUE_OUTPUT_LATENCY, outputLatencyInMS, nullptr);

				refreshChannelSetup(inputSetup);
				// We can actually start recording and playing
				audioDevice_->start(&callback_);
			}
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
