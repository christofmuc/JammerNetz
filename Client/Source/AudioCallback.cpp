/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "AudioCallback.h"

#include "ServerInfo.h"

#include "StreamLogger.h"
#include "BuffersConfig.h"
#include "Settings.h"
#include "Data.h"
#include "Encryption.h"

AudioCallback::AudioCallback() : jammerService_([this](std::shared_ptr < JammerNetzAudioData> buffer) { playBuffer_.push(buffer); }),
	playBuffer_("server"), bufferPool_(10)
{
	isPlaying_ = false;
	minPlayoutBufferLength_ = CLIENT_PLAYOUT_JITTER_BUFFER;
	maxPlayoutBufferLength_ = CLIENT_PLAYOUT_MAX_BUFFER;

	// Where to record to?
	uploadRecorder_ = std::make_shared<Recorder>(Settings::instance().getSessionStorageDir(), "LocalRecording", RecordingType::WAV);
	masterRecorder_ = std::make_shared<Recorder>(Settings::instance().getSessionStorageDir(), "MasterRecording", RecordingType::FLAC);
	masterRecorder_->setChannelInfo(SAMPLE_RATE, JammerNetzChannelSetup({ JammerNetzChannelTarget::Left, JammerNetzChannelTarget::Right }));
	//midiRecorder_ = std::make_unique<MidiRecorder>(deviceManager);

	// We might want to share a score sheet or similar
	//midiPlayalong_ = std::make_unique<MidiPlayAlong>("D:\\Development\\JammerNetz-OS\\Led Zeppelin - Stairway to heaven (1).kar");

	// We want to be able to tune our instruments
	tuner_ = std::make_unique<Tuner>();

	// Setup listeners 
	listeners_.push_back(std::make_unique<ValueListener>(Data::instance().get().getPropertyAsValue(VALUE_MIN_PLAYOUT_BUFFER, nullptr), [this](Value& newValue) {
		minPlayoutBufferLength_ = (int) newValue.getValue();
	}));
	listeners_.push_back(std::make_unique<ValueListener>(Data::instance().get().getPropertyAsValue(VALUE_MAX_PLAYOUT_BUFFER, nullptr), [this](Value& newValue) {
		maxPlayoutBufferLength_ = (int)newValue.getValue();
	}));
	// Execute the listeners so we read the current value from the setting file
	for_each(listeners_.begin(), listeners_.end(), [](std::unique_ptr<ValueListener>& ptr) { ptr->triggerOnChanged();  });
}

AudioCallback::~AudioCallback()
{	
}

void AudioCallback::shutdown()
{
	jammerService_.shutdown();
}

void AudioCallback::clearOutput(float** outputChannelData, int numOutputChannels, int numSamples) {
	// Clear out the buffer so we do not play noise
	for (int i = 0; i < numOutputChannels; i++) {
		memset(outputChannelData[i], 0, sizeof(float) * numSamples);
	}
}

void AudioCallback::newServer()
{
	/*if (!globalServerInfo.cryptoKeyfilePath.empty()) {
		// Reload crypto key
		std::shared_ptr<MemoryBlock> cryptoKey;
		if (UDPEncryption::loadKeyfile(globalServerInfo.cryptoKeyfilePath.c_str(), &cryptoKey)) {
			setCryptoKey(cryptoKey->getData(), (int)cryptoKey->getSize());
		}
		else {
			juce::AlertWindow::showMessageBox(AlertWindow::WarningIcon, "No crypto key loaded", "Could not load crypto key file " + globalServerInfo.cryptoKeyfilePath);
		}
	}
	else {
		// Turn off encryption
		setCryptoKey(nullptr, 0);
	}*/

	// Reset counters etc
	minPlayoutBufferLength_ = CLIENT_PLAYOUT_JITTER_BUFFER;
	PlayoutQualityInfo pqi;
	lastPlayoutQualityInfo_ = pqi;
	while (playoutQualityInfo_.try_pop(pqi));
	isPlaying_ = false;
	std::shared_ptr<JammerNetzAudioData> elem;
	bool isFillIn;
	while (playBuffer_.try_pop(elem, isFillIn));
}

void AudioCallback::measureSamplesPerTime(PlayoutQualityInfo &qualityInfo, int numSamples) const {
	if (qualityInfo.numSamplesSinceStart_ == -1) {
		// Take start time
		qualityInfo.startTime_ = std::chrono::steady_clock::now();
		qualityInfo.numSamplesSinceStart_ = 0;
		qualityInfo.measuredSampleRate = 0.0;
	}
	else {
		qualityInfo.numSamplesSinceStart_ += numSamples;
		qualityInfo.lastTime_ = std::chrono::steady_clock::now();
		auto timeElapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(qualityInfo.lastTime_ - qualityInfo.startTime_);
		qualityInfo.measuredSampleRate = (qualityInfo.numSamplesSinceStart_) / (double)(timeElapsed.count() / (double)1e9);
	}
}

void AudioCallback::audioDeviceIOCallback(const float** inputChannelData, int numInputChannels, float** outputChannelData, int numOutputChannels, int numSamples)
{
	PlayoutQualityInfo qualityInfo = lastPlayoutQualityInfo_;

	float *const *constnessCorrection = const_cast<float *const*>(inputChannelData);
	auto audioBufferNotOwned = std::make_shared<AudioBuffer<float>>(constnessCorrection, numInputChannels, numSamples);

	// Allocate an audio buffer from the reusable pool and do a force copy of the samples, as we will need to send them down to the recorder and the network thread
	auto audioBuffer = bufferPool_.alloc();
	*audioBuffer = *audioBufferNotOwned; // Forces deep copy of data

	// Send it to pitch detection
	tuner_->detectPitch(audioBuffer);

	// Measure the peak values for each channel
	meterSource_.measureBlock(*audioBuffer);

	// Send the MAG, RMS values and the pitch to the server, which will forward it to the other clients so they can show my levels even if they have only the mixed audio
	for (int c = 0; c < numInputChannels; c++) {
		if (c < channelSetup_.channels.size()) {
			channelSetup_.channels[c].mag = meterSource_.getMaxLevel(c);
			channelSetup_.channels[c].rms = meterSource_.getRMSLevel(c);
			channelSetup_.channels[c].pitch = tuner_->getPitch(c);
		}
		else {
			jassertfalse;
		}
	}

	// Measure time passed
	measureSamplesPerTime(qualityInfo, numSamples);

	// Get play-along data. The MIDI Buffer should be ready to be played out now, but we will only look at the text events for now
	/*if (false) {
		std::vector<MidiMessage> buffer;
		midiPlayalong_->fillNextMidiBuffer(buffer, numSamples);
		if (!buffer.empty()) {
			// The whole buffer is just a few milliseconds - take only the last text event
			MidiMessage &message = buffer.back();
			if (message.isTextMetaEvent()) {
				currentText_ = message.getTextFromTextMetaEvent().toStdString();
			}
		}
	}*/

	jammerService_.sender()->sendData(channelSetup_, audioBuffer); //TODO offload the real sending to a different thread
	if (uploadRecorder_ && uploadRecorder_->isRecording()) {
		uploadRecorder_->saveBlock(audioBuffer->getArrayOfReadPointers(), audioBuffer->getNumSamples());
	}

	// Don't start playing before the desired play-out buffer size is reached
	if (!isPlaying_ && playBuffer_.size() < minPlayoutBufferLength_) {
		clearOutput(outputChannelData, numOutputChannels, numSamples);
		return;
	}
	else if (playBuffer_.size() > maxPlayoutBufferLength_) {
		// That's too many packages in our buffer, where did those come from? Did the server deliver too many packets/did our playback stop?
		// Reduce the length of the queue until it is the right size, through away audio that is too old to be played out
		
		std::shared_ptr<JammerNetzAudioData> data;
		while (playBuffer_.size() > CLIENT_PLAYOUT_JITTER_BUFFER) {
			qualityInfo.discardedPackageCounter_++;
			bool isFillIn;
			playBuffer_.try_pop(data, isFillIn);
		}
	}
	isPlaying_ = true;

	// Now, play the next audio block from the play buffer!
	std::shared_ptr<JammerNetzAudioData> toPlay;
	bool isFillIn;
	if (playBuffer_.try_pop(toPlay, isFillIn)) {
		qualityInfo.currentPlayQueueLength_ = playBuffer_.size();
		// Ok, we have an Audio buffer to play. Hand over the data to the playback!
		if (toPlay && toPlay->audioBuffer()) {
			// Calculate the to-play latency
			qualityInfo.toPlayLatency_ = Time::getMillisecondCounterHiRes() - toPlay->timestamp();

			// We have Audio data to play! Make sure it is the correct size
			if (toPlay->audioBuffer()->getNumSamples() == numSamples) {
				clearOutput(outputChannelData, numOutputChannels, numSamples);
				for (int i = 0; i < std::min(numOutputChannels, toPlay->audioBuffer()->getNumChannels()); i++) {
					//TODO - this would mean we always play the left channel, if we have only one output channel. There is no way to select if the output channel is left or right
					// Need to add another channel selector.
					memcpy(outputChannelData[i], toPlay->audioBuffer()->getReadPointer(i), sizeof(float) * numSamples);
				}
			}
			else {
				// Programming error, we should all agree on the buffer format!
				clearOutput(outputChannelData, numOutputChannels, numSamples);
				jassert(false);
			}

			if (masterRecorder_ && masterRecorder_->isRecording()) {
				masterRecorder_->saveBlock(toPlay->audioBuffer()->getArrayOfReadPointers(), toPlay->audioBuffer()->getNumSamples());
			}

			// Calculate the RMS and mag displays for the other session participants
			auto session = jammerService_.receiver()->sessionSetup();
			std::vector<float> magnitudes;
			std::vector<float> rmss;
			for (const auto& channel : session.channels) {
				magnitudes.push_back(channel.mag);
				rmss.push_back(channel.rms);
			}
			sessionMeterSource_.setBlockMeasurement(session.channels.size(), magnitudes, rmss);
		}
		else {
			// That would be considered a programming error, I shall not enqueue nullptr
			clearOutput(outputChannelData, numOutputChannels, numSamples);
			jassert(false);
		}
		outMeterSource_.measureBlock(*toPlay->audioBuffer());
	}
	else {
		// This is a serious problem - either the server never started to send data, or we have a buffer underflow.
		qualityInfo.playUnderruns_++;
		isPlaying_ = false;
		clearOutput(outputChannelData, numOutputChannels, numSamples);
	}

	// Make the calculated quality info available for an interested consumer
	lastPlayoutQualityInfo_ = qualityInfo;
	playoutQualityInfo_.push(qualityInfo);
}

void AudioCallback::audioDeviceAboutToStart(AudioIODevice* device)
{
	// This will normally no be seen unless you start from a console
	std::cout << "Audio device " << device->getName() << " starting" << std::endl;
	lastPlayoutQualityInfo_ = PlayoutQualityInfo();
}

void AudioCallback::audioDeviceStopped()
{
	// This will normally no be seen unless you start from a console
	std::cout << "Audio device stopped" << std::endl;
}

void AudioCallback::setChannelSetup(JammerNetzChannelSetup const &channelSetup)
{
	if (!(channelSetup_.isEqualEnough(channelSetup))) {
		channelSetup_ = channelSetup;
		if (uploadRecorder_) {
			uploadRecorder_->setChannelInfo(SAMPLE_RATE, channelSetup_);
		}
		if (midiRecorder_) {
			midiRecorder_->startRecording();
		}
	}
}

FFAU::LevelMeterSource* AudioCallback::getMeterSource()
{
	return &meterSource_;
}

FFAU::LevelMeterSource* AudioCallback::getSessionMeterSource()
{
	return &sessionMeterSource_;
}

FFAU::LevelMeterSource* AudioCallback::getOutputMeterSource()
{
	return &outMeterSource_;
}

std::weak_ptr<MidiClocker> AudioCallback::getClocker()
{
	return midiRecorder_->getClocker();
}

MidiPlayAlong *AudioCallback::getPlayalong()
{
	return midiPlayalong_.get();
}

PlayoutQualityInfo AudioCallback::getPlayoutQualityInfo() 
{
	// Return the latest QualityInfo
	PlayoutQualityInfo latest;
	while (playoutQualityInfo_.try_pop(latest));
	return latest;
}

uint64 AudioCallback::currentBufferSize() const
{
	return minPlayoutBufferLength_;
}

int AudioCallback::currentPacketSize()
{
	return jammerService_.sender()->getCurrentBlockSize();
}

std::string AudioCallback::currentReceptionQuality() const
{
	return playBuffer_.qualityStatement();
}

bool AudioCallback::isReceivingData() 
{
	return jammerService_.receiver()->isReceivingData();
}

double AudioCallback::currentRTT() 
{
	return jammerService_.receiver()->currentRTT();
}

float AudioCallback::channelPitch(int channel) const
{
	return tuner_->getPitch(channel);
}

float AudioCallback::sessionPitch(int channel) {
	auto setup = getSessionSetup();
	if (channel < setup.channels.size())
		return setup.channels[channel].pitch;
	return 0.0f;
}

std::shared_ptr<Recorder> AudioCallback::getMasterRecorder() const
{
	return masterRecorder_;
}

std::shared_ptr<Recorder> AudioCallback::getLocalRecorder() const
{
	return uploadRecorder_;
}

std::shared_ptr<JammerNetzClientInfoMessage> AudioCallback::getClientInfo() 
{
	return jammerService_.receiver()->getClientInfo();
}

JammerNetzChannelSetup AudioCallback::getSessionSetup() 
{
	return jammerService_.receiver()->sessionSetup();
}

