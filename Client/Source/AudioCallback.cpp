/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "AudioCallback.h"

#include "ServerInfo.h"

#include "StreamLogger.h"
#include "BuffersConfig.h"
#include "Settings.h"

AudioCallback::AudioCallback(AudioDeviceManager &deviceManager) : client_([this](std::shared_ptr < JammerNetzAudioData> buffer) { playBuffer_.push(buffer); }),
	toPlayLatency_(0.0), currentPlayQueueLength_(0), discardedPackageCounter_(0), playBuffer_("server"),
	recordingCallback_(this), playbackCallback_(this)
{
	isPlaying_ = false;
	playUnderruns_ = 0;
	minPlayoutBufferLength_ = CLIENT_PLAYOUT_JITTER_BUFFER;
	maxPlayoutBufferLength_ = CLIENT_PLAYOUT_MAX_BUFFER;

	// Where to record to?
	uploadRecorder_ = std::make_shared<Recorder>(Settings::instance().getSessionStorageDir(), "LocalRecording", RecordingType::WAV);
	masterRecorder_ = std::make_shared<Recorder>(Settings::instance().getSessionStorageDir(), "MasterRecording", RecordingType::FLAC);
	masterRecorder_->updateChannelInfo(SAMPLE_RATE, JammerNetzChannelSetup({ JammerNetzChannelTarget::Left, JammerNetzChannelTarget::Right }));
	midiRecorder_ = std::make_unique<MidiRecorder>(deviceManager);

	// We want to be able to tune our instruments
	tuner_ = std::make_unique<Tuner>();
}

void AudioCallback::clearOutput(float** outputChannelData, int numOutputChannels, int numSamples) {
	// Clear out the buffer so we do not play noise
	for (int i = 0; i < numOutputChannels; i++) {
		memset(outputChannelData[i], 0, sizeof(float) * numSamples);
	}
}

void AudioCallback::newServer()
{
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
	if (recordingCallback_.numSamplesSinceStart_ == -1) {
		// Take start time
		startTime_ = std::chrono::steady_clock::now();
		recordingCallback_.numSamplesSinceStart_ = 0;
	}
	else {
		recordingCallback_.numSamplesSinceStart_ += numSamples;
		lastTime_ = std::chrono::steady_clock::now();
	}
}

void AudioCallback::RecordingCallback::audioDeviceIOCallback(const float** inputChannelData, int numInputChannels, float** outputChannelData, int numOutputChannels, int numSamples)
{
	ignoreUnused(numOutputChannels, outputChannelData);

	float *const *constnessCorrection = const_cast<float *const*>(inputChannelData);
	auto audioBuffer = std::make_shared<AudioBuffer<float>>(constnessCorrection, numInputChannels, numSamples);

	// Measure the peak values for each channel
	parent_->meterSource_.measureBlock(*audioBuffer);
	parent_->samplesPerTime(numSamples);

	// Send it to pitch detection
	parent_->tuner_->detectPitch(audioBuffer);

	parent_->client_.sendData(parent_->channelSetup_, audioBuffer); //TODO offload the real sending to a different thread
	if (parent_->uploadRecorder_ && parent_->uploadRecorder_->isRecording()) {
		parent_->uploadRecorder_->saveBlock(audioBuffer->getArrayOfReadPointers(), audioBuffer->getNumSamples());
	}
}

void AudioCallback::PlaybackCallback::audioDeviceIOCallback(const float** inputChannelData, int numInputChannels, float** outputChannelData, int numOutputChannels, int numSamples) 
{
	ignoreUnused(numInputChannels, inputChannelData);

	// Don't start playing before the desired play-out buffer size is reached
	if (!parent_->isPlaying_ && parent_->playBuffer_.size() < parent_->minPlayoutBufferLength_) {
		parent_->clearOutput(outputChannelData, numOutputChannels, numSamples);
		return;
	}
	else if (parent_->playBuffer_.size() > parent_->maxPlayoutBufferLength_) {
		// That's too many packages in our buffer, where did those come from? Did the server deliver too many packets/did our playback stop?
		// Reduce the length of the queue until it is the right size, through away audio that is too old to be played out
		std::shared_ptr<JammerNetzAudioData> data;
		while (parent_->playBuffer_.size() > CLIENT_PLAYOUT_JITTER_BUFFER) {
			parent_->discardedPackageCounter_++;
			bool isFillIn;
			parent_->playBuffer_.try_pop(data, isFillIn);
		}
	}
	parent_->isPlaying_ = true;

	// Now, play the next audio block from the play buffer!
	std::shared_ptr<JammerNetzAudioData> toPlay;
	bool isFillIn;
	if (parent_->playBuffer_.try_pop(toPlay, isFillIn)) {
		parent_->currentPlayQueueLength_ = parent_->playBuffer_.size();
		// Ok, we have an Audio buffer to play. Hand over the data to the playback!
		if (toPlay && toPlay->audioBuffer()) {
			// Calculate the to-play latency
			parent_->toPlayLatency_ = Time::getMillisecondCounterHiRes() - toPlay->timestamp();

			// We have Audio data to play! Make sure it is the correct size
			if (toPlay->audioBuffer()->getNumSamples() == numSamples) {
				parent_->clearOutput(outputChannelData, numOutputChannels, numSamples);
				for (int i = 0; i < std::min(numOutputChannels, toPlay->audioBuffer()->getNumChannels()); i++) {
					//TODO - this would mean we always play the left channel, if we have only one output channel. There is no way to select if the output channel is left or right
					// Need to add another channel selector.
					memcpy(outputChannelData[i], toPlay->audioBuffer()->getReadPointer(i), sizeof(float) * numSamples);
				}
			}
			else {
				// Programming error, we should all agree on the buffer format!
				parent_->clearOutput(outputChannelData, numOutputChannels, numSamples);
				jassert(false);
			}

			if (parent_->masterRecorder_ && parent_->masterRecorder_->isRecording()) {
				parent_->masterRecorder_->saveBlock(toPlay->audioBuffer()->getArrayOfReadPointers(), toPlay->audioBuffer()->getNumSamples());
			}
		}
		else {
			// That would be considered a programming error, I shall not enqueue nullptr
			parent_->clearOutput(outputChannelData, numOutputChannels, numSamples);
			jassert(false);
		}
		parent_->outMeterSource_.measureBlock(*toPlay->audioBuffer());
	}
	else {
		// This is a serious problem - either the server never started to send data, or we have a buffer underflow.
		parent_->playUnderruns_++;
		parent_->isPlaying_ = false;
		parent_->clearOutput(outputChannelData, numOutputChannels, numSamples);
	}
}

void AudioCallback::AbstractCallback::audioDeviceAboutToStart(AudioIODevice* device)
{
	StreamLogger::instance() << "Audio device " << device->getName() << " starting" << std::endl;
	numSamplesSinceStart_ = -1;
}

void AudioCallback::AbstractCallback::audioDeviceStopped()
{
	StreamLogger::instance() << "Audio device stopped" << std::endl;
}

void AudioCallback::setChannelSetup(JammerNetzChannelSetup const &channelSetup)
{
	if (!(channelSetup_ == channelSetup)) {
		channelSetup_ = channelSetup;
		if (uploadRecorder_) {
			uploadRecorder_->updateChannelInfo(SAMPLE_RATE, channelSetup_);
		}
		if (midiRecorder_) {
			midiRecorder_->startRecording();
		}
	}
}

void AudioCallback::changeClientConfig(int clientBuffers, int maxBuffers, int flares)
{
	client_.setFlareNumber(flares);
	minPlayoutBufferLength_ = clientBuffers;
	maxPlayoutBufferLength_ = maxBuffers;
}

FFAU::LevelMeterSource* AudioCallback::getMeterSource()
{
	return &meterSource_;
}

FFAU::LevelMeterSource* AudioCallback::getOutputMeterSource()
{
	return &outMeterSource_;
}

std::weak_ptr<MidiClocker> AudioCallback::getClocker()
{
	return midiRecorder_->getClocker();
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
	return 0; // (numSamplesSinceStart_) / (double)(timeElapsed.count() / (double) 1e9);
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

juce::AudioIODeviceCallback * AudioCallback::getRecordingCallback()
{
	return &recordingCallback_;
}

juce::AudioIODeviceCallback * AudioCallback::getPlaybackCallback()
{
	return &playbackCallback_;
}

