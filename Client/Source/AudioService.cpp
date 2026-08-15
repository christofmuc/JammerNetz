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
#include "Encryption.h"
#include "Settings.h"

#include "Logger.h"

#include <cmath>

AudioService::AudioService()
	: engine_(session_, Settings::instance().getSessionStorageDir())
	, callback_(engine_, [](float bpm) {
		juce::MessageManager::callAsync([bpm]() {
			Data::getPropertyAsValue(VALUE_SERVER_BPM).setValue(bpm);
		});
	})
{
	// Put the list into the ephemeral app data (not stored across runs of the software)
	auto& data = Data::instance().getEphemeral();
	data.setProperty(EPHEMERAL_VALUE_DEVICE_TYPES_AVAILABLE, AudioDeviceDiscovery::allDeviceTypeNames(), nullptr);
	data.setProperty(EPHEMERAL_VALUE_AUDIO_RUNNING, false, nullptr);
	Data::instance().ensurePropertyExists(VALUE_SERVER_BPM, 0.0);

	Data::instance().get().addListener(this);
	Data::instance().getEphemeral().addListener(this);
	engine_.start();
	refreshEngineConfiguration();
	refreshSessionConfiguration();
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
	engine_.shutdown();
	session_.shutdown();
}

bool AudioService::isConnected()
{
	return engine_.isReceivingData();
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
	engine_.setChannelSetup(channelSetup);
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
	engine_.restartClock(outputs);
}

void AudioService::setMidiSignal(MidiSignal signal)
{
	engine_.setMidiSignalToSend(signal);
}

std::shared_ptr<Recorder> AudioService::getMasterRecorder() const
{
	return engine_.getMasterRecorder();
}

std::shared_ptr<Recorder> AudioService::getLocalRecorder() const
{
	return engine_.getLocalRecorder();
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
	return engine_.getSessionSetup();
}

std::shared_ptr<JammerNetzClientInfoMessage> AudioService::getClientInfo()
{
	return engine_.getClientInfo();
}

PlayoutQualityInfo AudioService::getPlayoutQualityInfo()
{
	return engine_.getPlayoutQualityInfo();
}

double AudioService::currentRTT()
{
	return engine_.currentRTT();
}

std::string AudioService::currentReceptionQuality() const
{
	return engine_.currentReceptionQuality();
}

int AudioService::currentPacketSize()
{
	return engine_.currentPacketSize();
}

float AudioService::channelPitch(size_t channel) const
{
	return engine_.channelPitch(channel);
}

float AudioService::sessionPitch(size_t channel)
{
	return engine_.sessionPitch(channel);
}

FFAU::LevelMeterSource* AudioService::getInputMeterSource()
{
	return engine_.getMeterSource();
}

FFAU::LevelMeterSource* AudioService::getOutputMeterSource()
{
	return engine_.getOutputMeterSource();
}

FFAU::LevelMeterSource* AudioService::getSessionMeterSource()
{
	return engine_.getSessionMeterSource();
}

void AudioService::refreshEngineConfiguration()
{
	auto& data = Data::instance().get();
	auto mixer = data.getOrCreateChildWithName(VALUE_MIXER, nullptr);
	const auto outputController = mixer.getOrCreateChildWithName(VALUE_MASTER_OUTPUT, nullptr);
	const auto configuredMinimum = static_cast<int64>(data.getProperty(VALUE_MIN_PLAYOUT_BUFFER, CLIENT_PLAYOUT_JITTER_BUFFER));
	const auto configuredMaximum = static_cast<int64>(data.getProperty(VALUE_MAX_PLAYOUT_BUFFER, CLIENT_PLAYOUT_MAX_BUFFER));
	const auto minimum = static_cast<uint64>(std::max<int64>(1, configuredMinimum));
	const auto maximum = static_cast<uint64>(std::max<int64>(1, configuredMaximum));
	engine_.setPlayoutBufferRange(minimum, maximum);
	engine_.setMasterVolume(static_cast<double>(outputController.getProperty(VALUE_VOLUME, 100.0)) / 100.0);
	engine_.setMonitorBalance(outputController.getProperty(VALUE_MONITOR_BALANCE, 0.0));
	engine_.setLocalMonitoring(mixer.getProperty(VALUE_USE_LOCAL_MONITOR, false));
	engine_.setClientBpm(data.getProperty(VALUE_SERVER_BPM, 0.0));
}

std::optional<JammerNetzSessionConfiguration> AudioService::getSessionConfiguration() const
{
	const auto& data = Data::instance().get();
	JammerNetzSessionConfiguration configuration;
	configuration.serverName = data.getProperty(VALUE_SERVER_NAME).toString();
	configuration.serverPort = data.getProperty(VALUE_SERVER_PORT, 7777).toString().getIntValue();
	configuration.useLocalhost = data.getProperty(VALUE_USE_LOCALHOST, false);
	configuration.useFEC = data.getProperty(VALUE_USE_FEC, false);

	const auto cryptoPath = data.getProperty(VALUE_CRYPTOPATH).toString();
	if (cryptoPath.isNotEmpty()) {
		std::shared_ptr<juce::MemoryBlock> key;
		if (!UDPEncryption::loadKeyfile(cryptoPath.toRawUTF8(), &key)) {
			SimpleLogger::instance()->postMessage("Session configuration rejected: could not load the configured encryption key");
			return std::nullopt;
		}
		configuration.cryptoKey = std::move(key);
	}
	return configuration;
}

void AudioService::refreshSessionConfiguration()
{
	const auto configuration = getSessionConfiguration();
	if (!configuration) {
		return;
	}
	if (session_.isAvailable()) {
		session_.updateConfiguration(*configuration);
	} else {
		session_.start([this](std::shared_ptr<JammerNetzAudioData> audio) { engine_.enqueueRemoteAudio(std::move(audio)); }, *configuration);
	}
	engine_.newServer();
}

void AudioService::valueTreePropertyChanged(ValueTree& treeWhosePropertyHasChanged, const Identifier& property)
{
	if (shutdown_.load(std::memory_order_acquire)) {
		return;
	}

	if (//ValueTreeUtils::isChildOf(VALUE_INPUT_SETUP, treeWhosePropertyHasChanged) ||
		//ValueTreeUtils::isChildOf(VALUE_OUTPUT_SETUP, treeWhosePropertyHasChanged) ||
		property == Identifier(EPHEMERAL_VALUE_AUDIO_SHOULD_RUN)) {
		debouncer_.callDebounced(
		    [this]() {
			    if (shutdown_.load(std::memory_order_acquire)) {
				    return;
			    }
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
		refreshEngineConfiguration();
		refreshChannelSetup(getSetup(Data::instance().get().getChildWithName(VALUE_INPUT_SETUP)));
	}
	else if (property == Identifier(VALUE_MIN_PLAYOUT_BUFFER) || property == Identifier(VALUE_MAX_PLAYOUT_BUFFER)
		|| property == Identifier(VALUE_SERVER_BPM)) {
		refreshEngineConfiguration();
	}
	else if (property == Identifier(VALUE_SERVER_NAME) || property == Identifier(VALUE_SERVER_PORT)
		|| property == Identifier(VALUE_USE_LOCALHOST) || property == Identifier(VALUE_USE_FEC)
		|| property == Identifier(VALUE_CRYPTOPATH)) {
		refreshSessionConfiguration();
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
	if (shutdown_.load(std::memory_order_acquire)) {
		return;
	}

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

	juce::AudioIODeviceType* selectedType = AudioDeviceDiscovery::deviceTypeByName(inputSetup->typeName);
	// Sample rate and buffer size are hard coded for now
	if (!selectedType) {
		failStartup("the selected audio device type is unavailable");
		return;
	}

	selectedType->scanForDevices();
	{
		if (selectedType->hasSeparateInputsAndOutputs()) {
			// This is for other Audio types like DirectSound
			audioDevice_.reset(selectedType->createDevice(outputSetup->device, inputSetup->device));
		}
		else {
			// Try to create the device purely from the input name, this would be the path for ASIO)
			audioDevice_.reset(selectedType->createDevice("", inputSetup->device));
		}

		if (!audioDevice_) {
			failStartup("the selected audio device could not be created");
			return;
		}

		{
			BigInteger inputChannelMask = makeChannelMask(inputSetup->activeChannelIndices);
			BigInteger outputChannelMask = makeChannelMask(outputSetup->activeChannelIndices);

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
			if (!std::isfinite(actualSampleRate) || actualSampleRate <= 0.0) {
				failStartup("the device reported an invalid sample rate");
				return;
			}
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
			// Some JUCE backends only report isPlaying() after their callback thread
			// has started, so a synchronous check here can reject a valid startup.
			Data::instance().getEphemeral().setProperty(EPHEMERAL_VALUE_AUDIO_RUNNING, true, nullptr);
		}
	}
}

void AudioService::restartAudio()
{
	// Build the data structures required to properly restart the audio objects
	jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
	if (shutdown_.load(std::memory_order_acquire)) {
		return;
	}

	stopAudioIfRunning();

	auto& data = Data::instance().get();
	auto inputSetup = getSetup(data.getChildWithName(VALUE_INPUT_SETUP));
	auto outputSetup = getSetup(data.getChildWithName(VALUE_OUTPUT_SETUP));
	restartAudio(inputSetup, outputSetup);
}
