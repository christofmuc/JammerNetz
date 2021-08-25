/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "AudioCallback.h"

#include "ServerInfo.h"

#include "StreamLogger.h"
#include "BuffersConfig.h"
#include "Settings.h"

#include "Encryption.h"

AudioCallback::AudioCallback(AudioDeviceManager &deviceManager) : client_([this](std::shared_ptr < JammerNetzAudioData> buffer) { playBuffer_.push(buffer); }),
	toPlayLatency_(0.0), currentPlayQueueLength_(0), discardedPackageCounter_(0), playBuffer_("server"), bufferPool_(10)
{
	isPlaying_ = false;
	playUnderruns_ = 0;
	minPlayoutBufferLength_ = CLIENT_PLAYOUT_JITTER_BUFFER;
	maxPlayoutBufferLength_ = CLIENT_PLAYOUT_MAX_BUFFER;

	// Where to record to?
	uploadRecorder_ = std::make_shared<Recorder>(Settings::instance().getSessionStorageDir(), "LocalRecording", RecordingType::WAV);
	masterRecorder_ = std::make_shared<Recorder>(Settings::instance().getSessionStorageDir(), "MasterRecording", RecordingType::FLAC);
	masterRecorder_->setChannelInfo(SAMPLE_RATE, JammerNetzChannelSetup({ JammerNetzChannelTarget::Left, JammerNetzChannelTarget::Right }));
	ignoreUnused(deviceManager);
	//midiRecorder_ = std::make_unique<MidiRecorder>(deviceManager);

	// We might want to share a score sheet or similar
	//midiPlayalong_ = std::make_unique<MidiPlayAlong>("D:\\Development\\JammerNetz-OS\\Led Zeppelin - Stairway to heaven (1).kar");

	// We want to be able to tune our instruments
	tuner_ = std::make_unique<Tuner>();
}

void AudioCallback::newServer()
{
	if (!ServerInfo::cryptoKeyfilePath.empty()) {
		// Reload crypto key
		std::shared_ptr<MemoryBlock> cryptoKey;
		if (UDPEncryption::loadKeyfile(ServerInfo::cryptoKeyfilePath.c_str(), &cryptoKey)) {
			setCryptoKey(cryptoKey->getData(), (int)cryptoKey->getSize());
		}
		else {
			juce::AlertWindow::showMessageBox(AlertWindow::WarningIcon, "No crypto key loaded", "Could not load crypto key file " + ServerInfo::cryptoKeyfilePath);
		}
	}
	else {
		// Turn off encryption
		setCryptoKey(nullptr, 0);
	}

	// Reset counters etc
	minPlayoutBufferLength_ = CLIENT_PLAYOUT_JITTER_BUFFER;
	currentPlayQueueLength_ = 0;
	discardedPackageCounter_ = 0;
	playUnderruns_ = 0;
	toPlayLatency_ = 0.0;
	isPlaying_ = false;
	std::shared_ptr<JammerNetzAudioData> elem;
	bool isFillIn;
	while (playBuffer_.try_pop(elem, isFillIn));
}

void AudioCallback::samplesPerTime(int numSamples) {
	if (numSamplesSinceStart_ == -1) {
		// Take start time
		startTime_ = std::chrono::steady_clock::now();
		numSamplesSinceStart_ = 0;
	}
	else {
		numSamplesSinceStart_ += numSamples;
		lastTime_ = std::chrono::steady_clock::now();
	}
}

void AudioCallback::calcLocalMonitoring(std::shared_ptr<AudioBuffer<float>> inputBuffer, AudioBuffer<float>& outputBuffer) {
	
	outputBuffer.clear();
	if (localMonitoring_) {
		// Apply gain to our channels and do a stereo mixdown
		jassert(inputBuffer->getNumSamples() == outputBuffer.getNumSamples());
		for (int channel = 0; channel < inputBuffer->getNumChannels(); channel++) {
			const JammerNetzSingleChannelSetup& setup = channelSetup_.channels[channel];
			switch (setup.target) {
			case Unused:
				// Nothing to be done, ignore this channel; This is the same as Mute
				break;
			case Left:
				// This is a left channel, going into the left. 
				if (outputBuffer.getNumChannels() > 0) {
					outputBuffer.addFrom(0, 0, *inputBuffer, channel, 0, inputBuffer->getNumSamples(), setup.volume);
				}
				break;
			case Right:
				// And the same for the right channel
				if (outputBuffer.getNumChannels() > 1) {
					outputBuffer.addFrom(1, 0, *inputBuffer, channel, 0, inputBuffer->getNumSamples(), setup.volume);
				}
				break;
			case SendOnly:
				// Fall-through on purpose, treat it as Mono
			case Mono:
				if (outputBuffer.getNumChannels() > 0) {
					outputBuffer.addFrom(0, 0, *inputBuffer, channel, 0, inputBuffer->getNumSamples(), setup.volume);
				}
				if (outputBuffer.getNumChannels() > 1) {
					outputBuffer.addFrom(1, 0, *inputBuffer, channel, 0, inputBuffer->getNumSamples(), setup.volume);
				}
				break;
			}
		}
	}
}

void AudioCallback::audioDeviceIOCallback(const float** inputChannelData, int numInputChannels, float** outputChannelData, int numOutputChannels, int numSamples)
{
	float *const *constnessCorrection = const_cast<float *const*>(inputChannelData);
	auto audioBufferNotOwned = std::make_shared<AudioBuffer<float>>(constnessCorrection, numInputChannels, numSamples);

	// Create a better access structure for the output data
	AudioBuffer<float> outputBuffer(outputChannelData, numOutputChannels, numSamples);

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
	samplesPerTime(numSamples);

	// Get play-along data. The MIDI Buffer should be ready to be played out now, but we will only look at the text events for now
	if (false) {
		std::vector<MidiMessage> buffer;
		midiPlayalong_->fillNextMidiBuffer(buffer, numSamples);
		if (!buffer.empty()) {
			// The whole buffer is just a few milliseconds - take only the last text event
			MidiMessage &message = buffer.back();
			if (message.isTextMetaEvent()) {
				currentText_ = message.getTextFromTextMetaEvent().toStdString();
			}
		}
	}

	client_.sendData(channelSetup_, audioBuffer); //TODO offload the real sending to a different thread
	if (uploadRecorder_ && uploadRecorder_->isRecording()) {
		uploadRecorder_->saveBlock(audioBuffer->getArrayOfReadPointers(), audioBuffer->getNumSamples());
	}

	// Don't start playing before the desired play-out buffer size is reached
	if (!isPlaying_ && playBuffer_.size() < minPlayoutBufferLength_) {
		calcLocalMonitoring(audioBuffer, outputBuffer);
		return;
	}
	else if (playBuffer_.size() > maxPlayoutBufferLength_) {
		// That's too many packages in our buffer, where did those come from? Did the server deliver too many packets/did our playback stop?
		// Reduce the length of the queue until it is the right size, through away audio that is too old to be played out
		std::shared_ptr<JammerNetzAudioData> data;
		while (playBuffer_.size() > CLIENT_PLAYOUT_JITTER_BUFFER) {
			discardedPackageCounter_++;
			bool isFillIn;
			playBuffer_.try_pop(data, isFillIn);
		}
	}
	isPlaying_ = true;

	// Prepare the output buffer with the local monitoring signal
	calcLocalMonitoring(audioBuffer, outputBuffer);

	// Now, play the next audio block from the play buffer!
	std::shared_ptr<JammerNetzAudioData> toPlay;
	bool isFillIn;
	if (playBuffer_.try_pop(toPlay, isFillIn)) {
		currentPlayQueueLength_ = playBuffer_.size();
		// Ok, we have an Audio buffer to play. Hand over the data to the playback!
		if (toPlay && toPlay->audioBuffer()) {
			// Calculate the to-play latency
			toPlayLatency_ = Time::getMillisecondCounterHiRes() - toPlay->timestamp();

			// We have Audio data to play! Make sure it is the correct size
			if (toPlay->audioBuffer()->getNumSamples() == numSamples) {
				for (int i = 0; i < std::min(numOutputChannels, toPlay->audioBuffer()->getNumChannels()); i++) {
					//TODO - this would mean we always play the left channel, if we have only one output channel. There is no way to select if the output channel is left or right
					// Need to add another channel selector.
					memcpy(outputChannelData[i], toPlay->audioBuffer()->getReadPointer(i), sizeof(float) * numSamples);
				}
			}
			else {
				// Programming error, we should all agree on the buffer format!
				jassert(false);
			}

			if (masterRecorder_ && masterRecorder_->isRecording()) {
				masterRecorder_->saveBlock(toPlay->audioBuffer()->getArrayOfReadPointers(), toPlay->audioBuffer()->getNumSamples());
			}

			// Calculate the RMS and mag displays for the other session participants
			auto session = client_.getCurrentSessionSetup();
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
			jassert(false);
		}
		outMeterSource_.measureBlock(*toPlay->audioBuffer());
	}
	else {
		// This is a serious problem - either the server never started to send data, or we have a buffer underflow.
		playUnderruns_++;
		isPlaying_ = false;
	}
}

void AudioCallback::audioDeviceAboutToStart(AudioIODevice* device)
{
	// This will normally no be seen unless you start from a console
	std::cout << "Audio device " << device->getName() << " starting" << std::endl;
	numSamplesSinceStart_ = -1;
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

void AudioCallback::changeClientConfig(int clientBuffers, int maxBuffers)
{
	minPlayoutBufferLength_ = clientBuffers;
	maxPlayoutBufferLength_ = maxBuffers;
}

void AudioCallback::setFEC(bool fec)
{
	client_.setSendFECData(fec);
}

void AudioCallback::setCryptoKey(const void* keyData, int keyBytes)
{
	client_.setCryptoKey(keyData, keyBytes);
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

int64 AudioCallback::numberOfUnderruns() const
{
	return playUnderruns_;
}

uint64 AudioCallback::currentBufferSize() const
{
	return minPlayoutBufferLength_;
}

int AudioCallback::currentPacketSize() const
{
	return client_.getCurrentBlockSize();
}

uint64 AudioCallback::currentPlayQueueSize() const
{
	return currentPlayQueueLength_;
}

int AudioCallback::currentDiscardedPackageCounter() const
{
	return discardedPackageCounter_;
}

double AudioCallback::currentToPlayLatency() const
{
	return toPlayLatency_;
}

std::string AudioCallback::currentReceptionQuality() const
{
	return playBuffer_.qualityStatement();
}

double AudioCallback::currentSampleRate() const
{
	auto timeElapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(lastTime_ - startTime_);
	return (numSamplesSinceStart_) / (double)(timeElapsed.count() / (double) 1e9);
}

bool AudioCallback::isReceivingData() const
{
	return client_.isReceivingData();
}

double AudioCallback::currentRTT() const
{
	return client_.currentRTT();
}

float AudioCallback::channelPitch(int channel) const
{
	return tuner_->getPitch(channel);
}

float AudioCallback::sessionPitch(int channel) const {
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

std::shared_ptr<JammerNetzClientInfoMessage> AudioCallback::getClientInfo() const
{
	return client_.getClientInfo();
}

JammerNetzChannelSetup AudioCallback::getSessionSetup() const
{
	return client_.getCurrentSessionSetup();
}

