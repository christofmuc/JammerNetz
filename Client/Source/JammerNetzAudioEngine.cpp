/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "JammerNetzAudioEngine.h"

#include "BuffersConfig.h"
#include "Logger.h"

#include <cmath>

JammerNetzAudioEngine::JammerNetzAudioEngine(JammerNetzSession& session,
	const juce::File& recordingDirectory,
	std::shared_ptr<AudioPacketSink> packetSink)
	: session_(session)
	, recordingDirectory_(recordingDirectory)
	, remoteScratch_(2, JAMMERNETZ_MAX_CALLBACK_SAMPLES)
	, masterVolume_(1.0)
	, monitorBalance_(0.0)
	, clientBpm_(0.0f)
	, serverBpm_(0.0)
	, ignoreNextServerBpmChange_(false)
	, pendingServerBpm_(0.0f)
	, bpmSliderLastMovedTicks_(0)
	, midiSignalToSend_(MidiSignal_None)
{
	configuredInputState_ = std::make_shared<const InputState>(InputState { JammerNetzChannelSetup(false), nullptr });
	inputState_.store(configuredInputState_.get(), std::memory_order_release);
	minPlayoutBufferLength_ = CLIENT_PLAYOUT_JITTER_BUFFER;
	maxPlayoutBufferLength_ = CLIENT_PLAYOUT_MAX_BUFFER;
	playoutBuffer_ = std::make_unique<RingBuffer>(2, PLAYOUT_RINGBUFFER_SIZE);
	transmitWorker_ = std::make_unique<AudioTransmitWorker>(session_, std::move(packetSink));
	receiveWorker_ = std::make_unique<AudioReceiveWorker>(session_);
	outMeterSource_.resize(2, 1);

	//midiRecorder_ = std::make_unique<MidiRecorder>(deviceManager);

	// We might want to share a score sheet or similar
	//midiPlayalong_ = std::make_unique<MidiPlayAlong>("D:\\Development\\JammerNetz-OS\\Led Zeppelin - Stairway to heaven (1).kar");

}

JammerNetzAudioEngine::~JammerNetzAudioEngine()
{
	shutdown();
}

void JammerNetzAudioEngine::start(bool enableRecording)
{
	if (started_) {
		return;
	}
	started_ = true;
	if (enableRecording) {
		uploadRecorder_ = std::make_shared<Recorder>(recordingDirectory_, "LocalRecording", RecordingType::WAV);
		masterRecorder_ = std::make_shared<Recorder>(recordingDirectory_, "MasterRecording", RecordingType::FLAC);
		masterRecorder_->setChannelInfo(SAMPLE_RATE, JammerNetzChannelSetup(false, { JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Left), JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Right) }));
		recordingWorker_ = std::make_unique<AudioRecordingWorker>(uploadRecorder_, masterRecorder_);
		recordingWorker_->start();
	}
	transmitWorker_->start();
	receiveWorker_->start();
}

void JammerNetzAudioEngine::shutdown()
{
	started_ = false;
	if (recordingWorker_) {
		recordingWorker_->shutdown();
		recordingWorker_.reset();
	}
	if (transmitWorker_) {
		transmitWorker_->shutdown();
	}
	if (receiveWorker_) {
		receiveWorker_->shutdown();
	}
	auto activeMidiSendThread = midiSendThread_.load(std::memory_order_acquire);
	midiSendThread_.store(nullptr, std::memory_order_release);
	{
		const ScopedLock lock(retiredMidiSendThreadsLock_);
		if (activeMidiSendThread) {
			retiredMidiSendThreads_.push_back(std::move(activeMidiSendThread));
		}
		retiredMidiSendThreads_.clear();
	}
	masterRecorder_.reset();
	uploadRecorder_.reset();
}

void JammerNetzAudioEngine::enqueueRemoteAudio(std::shared_ptr<JammerNetzAudioData> buffer)
{
	if (receiveWorker_) {
		receiveWorker_->enqueue(std::move(buffer));
	}
}

bool JammerNetzAudioEngine::processNextOutgoingPacket()
{
	return transmitWorker_ && transmitWorker_->processNextPendingFrame();
}

bool JammerNetzAudioEngine::processNextIncomingPacket()
{
	return receiveWorker_ && receiveWorker_->processNextPendingFrame();
}

void JammerNetzAudioEngine::setPlayoutBufferRange(uint64 minimumLength, uint64 maximumLength)
{
	minPlayoutBufferLength_.store(minimumLength, std::memory_order_relaxed);
	maxPlayoutBufferLength_.store(std::max(minimumLength, maximumLength), std::memory_order_relaxed);
	if (receiveWorker_) {
		receiveWorker_->setPlayoutRange(minimumLength, maximumLength);
	}
}

void JammerNetzAudioEngine::setMasterVolume(double volume)
{
	masterVolume_.store(volume, std::memory_order_relaxed);
}

void JammerNetzAudioEngine::setMonitorBalance(double balance)
{
	monitorBalance_.store(balance, std::memory_order_relaxed);
}

void JammerNetzAudioEngine::setLocalMonitoring(bool enabled)
{
	monitorIsLocal_.store(enabled, std::memory_order_relaxed);
}

void JammerNetzAudioEngine::setClientBpm(float bpm)
{
	if (ignoreNextServerBpmChange_.load(std::memory_order_acquire)) {
		const float pendingValue = pendingServerBpm_.load(std::memory_order_relaxed);
		ignoreNextServerBpmChange_.store(false, std::memory_order_release);
		if (std::fabs(bpm - pendingValue) < 1.0e-4f) {
			return;
		}
	}
	clientBpm_.setValue(bpm);
	bpmSliderLastMovedTicks_.store(std::chrono::steady_clock::now().time_since_epoch().count(), std::memory_order_release);
}

std::optional<float> JammerNetzAudioEngine::takeServerBpmUpdate()
{
	if (!receiveWorker_) {
		return {};
	}
	const auto bpm = receiveWorker_->takeServerBpmUpdate();
	if (!bpm) {
		return {};
	}
	const auto now = std::chrono::steady_clock::now();
	const auto lastMovedTicks = bpmSliderLastMovedTicks_.load(std::memory_order_acquire);
	const auto lastMoved = std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(lastMovedTicks));
	if (lastMovedTicks != 0 && now - lastMoved <= std::chrono::seconds(1)) {
		return {};
	}
	serverBpm_.store(*bpm, std::memory_order_relaxed);
	pendingServerBpm_.store(*bpm, std::memory_order_release);
	ignoreNextServerBpmChange_.store(true, std::memory_order_release);
	bpmSliderLastMovedTicks_.store(now.time_since_epoch().count(), std::memory_order_release);
	return bpm;
}

void JammerNetzAudioEngine::restartClock(std::vector<MidiDeviceInfo> outputs)
{
	// Where to send the Midi Clock signals
	auto retiredMidiSendThread = midiSendThread_.load(std::memory_order_acquire);
	midiSendThread_.store(nullptr, std::memory_order_release);
	if (retiredMidiSendThread) {
		retiredMidiSendThread->shutdown();
	}
	auto sender = std::make_shared<MidiSendThread>(outputs);
	midiSendThread_.store(sender, std::memory_order_release);
	if (receiveWorker_) {
		receiveWorker_->setMidiSender(sender);
	}
	if (retiredMidiSendThread) {
		const ScopedLock lock(retiredMidiSendThreadsLock_);
		retiredMidiSendThreads_.push_back(std::move(retiredMidiSendThread));
	}
}

void JammerNetzAudioEngine::setMidiSignalToSend(MidiSignal signal)
{
	midiSignalToSend_.setValue(signal);
}

void JammerNetzAudioEngine::newServer()
{
	resetQualityInfo_.store(true, std::memory_order_release);
	if (receiveWorker_) {
		expectedRemoteGeneration_.store(receiveWorker_->requestReset(), std::memory_order_release);
	}
	resetPlayoutRequested_.store(true, std::memory_order_release);
}

void JammerNetzAudioEngine::measureSamplesPerTime(PlayoutQualityInfo &qualityInfo, int numSamples) const {
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
		const auto elapsedSeconds = static_cast<double>(timeElapsed.count()) / 1e9;
		qualityInfo.measuredSampleRate = elapsedSeconds > 0.0 ? static_cast<double>(qualityInfo.numSamplesSinceStart_) / elapsedSeconds : 0.0;
	}
}

// https://dsp.stackexchange.com/questions/14754/equal-power-crossfade
static std::pair<double, double> calcMonitorGain(double t) {
	double left = sqrt(0.5 * (1.0 - t));
	double right = sqrt(0.5 * (1.0 + t));

	return { left, right };
}

void JammerNetzAudioEngine::calcLocalMonitoring(const float* const* inputChannels, int numInputChannels, AudioBuffer<float>& outputBuffer,
	const JammerNetzChannelSetup& channelSetup) {
	if (monitorIsLocal_ && inputChannels && numInputChannels > 0) {
		auto [monitorVolume, _] = calcMonitorGain(monitorBalance_.load(std::memory_order_relaxed));
		const auto channelsToMix = std::min(static_cast<size_t>(numInputChannels), channelSetup.channels.size());
		for (size_t channel = 0; channel < channelsToMix; channel++) {
			if (!inputChannels[channel]) {
				continue;
			}
			const JammerNetzSingleChannelSetup& setup = channelSetup.channels[channel];
			float input_volume = (float) (setup.volume * monitorVolume * masterVolume_);
			switch (setup.target) {
			case Mute:
				// Nothing to be done, ignore this channel
				break;
			case Left:
				// This is a left channel, going into the left.
				if (outputBuffer.getNumChannels() > 0) {
					outputBuffer.addFrom(0, 0, inputChannels[channel], outputBuffer.getNumSamples(), input_volume);
				}
				break;
			case Right:
				// And the same for the right channel
				if (outputBuffer.getNumChannels() > 1) {
					outputBuffer.addFrom(1, 0, inputChannels[channel], outputBuffer.getNumSamples(), input_volume);
				}
				break;
			case SendLeft:
			case SendRight:
			case SendMono:
				// Don't include the "send only" channel types into the local monitoring mix, this is what that flag is for!
				break;
			case Mono:
				if (outputBuffer.getNumChannels() > 0) {
					outputBuffer.addFrom(0, 0, inputChannels[channel], outputBuffer.getNumSamples(), input_volume);
				}
				if (outputBuffer.getNumChannels() > 1) {
					outputBuffer.addFrom(1, 0, inputChannels[channel], outputBuffer.getNumSamples(), input_volume);
				}
				break;
			}
		}
	}
}

void JammerNetzAudioEngine::process(const float* const* inputChannelData, int numInputChannels, float* const* outputChannelData,
    int numOutputChannels, int numSamples)
{
	if (!outputChannelData || numOutputChannels <= 0 || numSamples <= 0) {
		return;
	}
	const auto callbackStart = std::chrono::steady_clock::now();
	if (numInputChannels > JAMMERNETZ_MAX_AUDIO_CHANNELS || numOutputChannels > JAMMERNETZ_MAX_AUDIO_CHANNELS) {
		for (int channel = 0; channel < numOutputChannels; ++channel) {
			if (outputChannelData[channel]) {
				juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);
			}
		}
		return;
	}

	for (int offset = 0; offset < numSamples; offset += JAMMERNETZ_MAX_CALLBACK_SAMPLES) {
		const int chunkSamples = std::min(JAMMERNETZ_MAX_CALLBACK_SAMPLES, numSamples - offset);
		std::array<const float*, JAMMERNETZ_MAX_AUDIO_CHANNELS> chunkInputs {};
		std::array<float*, JAMMERNETZ_MAX_AUDIO_CHANNELS> chunkOutputs {};
		for (int channel = 0; channel < numInputChannels; ++channel) {
			chunkInputs[static_cast<size_t>(channel)] = inputChannelData && inputChannelData[channel] ? inputChannelData[channel] + offset : nullptr;
		}
		for (int channel = 0; channel < numOutputChannels; ++channel) {
			chunkOutputs[static_cast<size_t>(channel)] = outputChannelData[channel] ? outputChannelData[channel] + offset : nullptr;
		}
		processChunk(chunkInputs.data(), numInputChannels, chunkOutputs.data(), numOutputChannels, chunkSamples);
	}
	const auto elapsed = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now() - callbackStart).count());
	callbackCount_.fetch_add(1, std::memory_order_relaxed);
	auto previousMaximum = maximumCallbackNanoseconds_.load(std::memory_order_relaxed);
	if (elapsed > previousMaximum) {
		maximumCallbackNanoseconds_.store(elapsed, std::memory_order_relaxed);
	}
	const auto sampleRate = preparedSampleRate_.load(std::memory_order_relaxed);
	const auto deadline = sampleRate > 0.0 ? static_cast<uint64_t>(1.0e9 * static_cast<double>(numSamples) / sampleRate) : 0;
	if (deadline > 0 && elapsed > deadline) {
		callbackDeadlineMisses_.fetch_add(1, std::memory_order_relaxed);
	}
}

void JammerNetzAudioEngine::processChunk(const float* const* inputChannelData, int numInputChannels, float* const* outputChannelData,
	int numOutputChannels, int numSamples)
{
	float* const* constnessCorrection = const_cast<float* const*>(inputChannelData);
	PlayoutQualityInfo qualityInfo = lastPlayoutQualityInfo_;
	if (resetQualityInfo_.exchange(false, std::memory_order_acq_rel)) {
		qualityInfo = PlayoutQualityInfo();
	}
	if (resetPlayoutRequested_.exchange(false, std::memory_order_acq_rel)) {
		playoutBuffer_->clear();
		isPlaying_.store(false, std::memory_order_release);
	}
	const auto* inputState = inputState_.load(std::memory_order_acquire);

	// Measure time passed
	measureSamplesPerTime(qualityInfo, numSamples);

	const bool inputStateMatchesDevice = inputState && inputState->ingestBuffer
		&& inputState->setup.channels.size() == static_cast<size_t>(numInputChannels);
	if (numInputChannels > 0 && !inputStateMatchesDevice
		&& !inputChannelMismatchReported_.exchange(true, std::memory_order_acq_rel)) {
		const auto configuredChannels = inputState ? inputState->setup.channels.size() : 0;
		MessageManager::callAsync([numInputChannels, configuredChannels]() {
			SimpleLogger::instance()->postMessage("Audio input channel mismatch: device provides " + String(numInputChannels)
				+ " channels, but " + String(configuredChannels) + " are configured");
		});
	}

	// If we have at least one input channel, do something with the data!
	bool inputChannelsValid = inputChannelData != nullptr;
	for (int channel = 0; channel < numInputChannels && inputChannelsValid; ++channel) {
		inputChannelsValid = inputChannelData[channel] != nullptr;
	}
	if (numInputChannels > 0 && inputChannelsValid && inputStateMatchesDevice) {
		if (recordingWorker_) {
			recordingWorker_->enqueue(RecordingTarget::local, inputChannelData, numInputChannels, numSamples);
		}

		if (numSamples <= inputState->ingestBuffer->getFreeSpace()) {
			inputState->ingestBuffer->write(constnessCorrection, numInputChannels, numSamples);
		} else {
			inputBlocksDropped_.fetch_add(1, std::memory_order_relaxed);
		}

		while (inputState->ingestBuffer->getNumReady() >= SAMPLE_BUFFER_SIZE) {
			if (transmitWorker_ && transmitWorker_->hasCapacity()) {
				if (!transmitWorker_->enqueueFrom(*inputState->ingestBuffer, numInputChannels,
						clientBpm_.readOnce(), midiSignalToSend_.readOnce())) {
					inputState->ingestBuffer->discard(SAMPLE_BUFFER_SIZE);
				}
			} else {
				inputState->ingestBuffer->discard(SAMPLE_BUFFER_SIZE);
				if (transmitWorker_) {
					transmitWorker_->recordDroppedFrame();
				}
			}
		}
	}

	// Create a better access structure for the output data
	for (int channel = 0; channel < numOutputChannels; ++channel) {
		if (outputChannelData[channel]) {
			juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);
		}
	}
	int engineOutputChannels = 0;
	while (engineOutputChannels < std::min(numOutputChannels, 2) && outputChannelData[engineOutputChannels]) {
		++engineOutputChannels;
	}
	AudioBuffer<float> outputBuffer(outputChannelData, engineOutputChannels, numSamples);

	// Prepare the output buffer with the local monitoring signal
	if (inputState) {
		calcLocalMonitoring(inputChannelData, numInputChannels, outputBuffer, inputState->setup);
	}

	// For playout, we have to have enough bytes in the out ringbuffer to fill the output audio block.
	// Let's see if we have enough data from the network!

	while (receiveWorker_ && playoutBuffer_->getNumReady() < numSamples) {
		RemoteAudioFrame frame;
		if (!receiveWorker_->tryPop(frame)) {
			break;
		}
		if (frame.generation != expectedRemoteGeneration_.load(std::memory_order_acquire)) {
			continue;
		}
		std::array<const float*, 2> pointers { frame.samples[0].data(), frame.samples[1].data() };
		playoutBuffer_->write(pointers.data(), 2, SAMPLE_BUFFER_SIZE);
		qualityInfo.toPlayLatency_ = Time::getMillisecondCounterHiRes() - frame.sourceTimestamp;
	}
	qualityInfo.currentPlayQueueLength_ = receiveWorker_ ? static_cast<uint64>(receiveWorker_->readyFrames()) : 0;
	qualityInfo.discardedPackageCounter_ = receiveWorker_ ? receiveWorker_->discardedFrames() : 0;
	if (!isPlaying_.load(std::memory_order_acquire) && playoutBuffer_->getNumReady() >= numSamples) {
		isPlaying_.store(true, std::memory_order_release);
	}

	if (isPlaying_.load(std::memory_order_acquire)) {
		if (playoutBuffer_->getNumReady() < numSamples) {
			qualityInfo.playUnderruns_++;
			isPlaying_.store(false, std::memory_order_release);
			playoutBuffer_->clear();
			if (receiveWorker_) {
				receiveWorker_->requestRebuffer();
			}
		}
		else {
			std::array<float*, 2> scratchPointers { remoteScratch_.getWritePointer(0), remoteScratch_.getWritePointer(1) };
			playoutBuffer_->read(scratchPointers.data(), 2, numSamples);

			auto [_, remoteVolume] = calcMonitorGain(monitorBalance_.load(std::memory_order_relaxed));
			float volume = (float) (remoteVolume * masterVolume_);
			for (int c = 0; c < std::min(2, outputBuffer.getNumChannels()); c++) {
				outputBuffer.addFrom(c, 0, remoteScratch_.getReadPointer(c), numSamples, volume);
			}
		}
	}

	std::array<float*, 2> meterPointers {
		engineOutputChannels > 0 ? outputChannelData[0] : silentMeterChannel_.data(),
		engineOutputChannels > 1 ? outputChannelData[1] : silentMeterChannel_.data()
	};
	AudioBuffer<float> meterBuffer(meterPointers.data(), 2, numSamples);
	outMeterSource_.measureBlock(meterBuffer);
	if (recordingWorker_) {
		recordingWorker_->enqueue(RecordingTarget::master, outputBuffer.getArrayOfReadPointers(), engineOutputChannels, numSamples);
	}

	lastPlayoutQualityInfo_ = qualityInfo;
	publishedQueueLength_.store(qualityInfo.currentPlayQueueLength_, std::memory_order_relaxed);
	publishedUnderruns_.store(qualityInfo.playUnderruns_, std::memory_order_relaxed);
	publishedDiscarded_.store(qualityInfo.discardedPackageCounter_, std::memory_order_relaxed);
	publishedLatency_.store(qualityInfo.toPlayLatency_, std::memory_order_relaxed);
	publishedSampleRate_.store(qualityInfo.measuredSampleRate, std::memory_order_relaxed);
	completedAudioEpoch_.fetch_add(1, std::memory_order_release);
}

void JammerNetzAudioEngine::prepare(double sampleRate, int maximumBlockSize)
{
	ignoreUnused(maximumBlockSize);
	preparedSampleRate_.store(sampleRate, std::memory_order_relaxed);
	lastPlayoutQualityInfo_ = PlayoutQualityInfo();
	resetPlayoutRequested_.store(true, std::memory_order_release);
}

void JammerNetzAudioEngine::release()
{
	resetPlayoutRequested_.store(true, std::memory_order_release);
}

void JammerNetzAudioEngine::setChannelSetup(JammerNetzChannelSetup const &channelSetup)
{
	if (transmitWorker_) {
		transmitWorker_->setChannelSetup(channelSetup);
	}
	const auto previousState = inputState_.load(std::memory_order_acquire);
	if (previousState && previousState->setup.isEqualEnough(channelSetup)) {
		return;
	}

	const bool channelCountChanged = !previousState || previousState->setup.channels.size() != channelSetup.channels.size();
	std::shared_ptr<RingBuffer> ingestBuffer = previousState ? previousState->ingestBuffer : nullptr;
	if (channelCountChanged) {
		ingestBuffer = channelSetup.channels.empty() ? nullptr : std::make_shared<RingBuffer>(static_cast<int>(channelSetup.channels.size()), INGEST_RINGBUFFER_SIZE);
	}
	if (uploadRecorder_) {
		uploadRecorder_->setChannelInfo(SAMPLE_RATE, channelSetup);
	}

	inputChannelMismatchReported_.store(false, std::memory_order_release);
	const auto completedEpoch = completedAudioEpoch_.load(std::memory_order_acquire);
	retiredInputStates_.erase(std::remove_if(retiredInputStates_.begin(), retiredInputStates_.end(), [completedEpoch](const RetiredInputState& retired) {
		return retired.retireEpoch < completedEpoch;
	}), retiredInputStates_.end());
	if (configuredInputState_) {
		retiredInputStates_.push_back({ configuredInputState_, completedEpoch });
	}
	configuredInputState_ = std::make_shared<const InputState>(InputState { channelSetup, std::move(ingestBuffer) });
	inputState_.store(configuredInputState_.get(), std::memory_order_release);
	if (channelCountChanged && midiRecorder_) {
		midiRecorder_->startRecording();
	}
}

FFAU::LevelMeterSource* JammerNetzAudioEngine::getMeterSource()
{
	return transmitWorker_ ? transmitWorker_->meterSource() : nullptr;
}

FFAU::LevelMeterSource* JammerNetzAudioEngine::getSessionMeterSource()
{
	return receiveWorker_ ? receiveWorker_->meterSource() : nullptr;
}

FFAU::LevelMeterSource* JammerNetzAudioEngine::getOutputMeterSource()
{
	return &outMeterSource_;
}

std::weak_ptr<MidiClocker> JammerNetzAudioEngine::getClocker()
{
	return midiRecorder_ ? midiRecorder_->getClocker() : std::weak_ptr<MidiClocker>();
}

MidiPlayAlong *JammerNetzAudioEngine::getPlayalong()
{
	return midiPlayalong_.get();
}

PlayoutQualityInfo JammerNetzAudioEngine::getPlayoutQualityInfo()
{
	PlayoutQualityInfo latest;
	latest.currentPlayQueueLength_ = publishedQueueLength_.load(std::memory_order_relaxed);
	latest.playUnderruns_ = publishedUnderruns_.load(std::memory_order_relaxed);
	latest.discardedPackageCounter_ = publishedDiscarded_.load(std::memory_order_relaxed);
	latest.toPlayLatency_ = publishedLatency_.load(std::memory_order_relaxed);
	latest.measuredSampleRate = publishedSampleRate_.load(std::memory_order_relaxed);
	return latest;
}

RealtimeWorkerStats JammerNetzAudioEngine::getRealtimeWorkerStats() const
{
	RealtimeWorkerStats stats;
	stats.callbackCount = callbackCount_.load(std::memory_order_relaxed);
	stats.maximumCallbackNanoseconds = maximumCallbackNanoseconds_.load(std::memory_order_relaxed);
	stats.callbackDeadlineMisses = callbackDeadlineMisses_.load(std::memory_order_relaxed);
	stats.inputBlocksDropped = inputBlocksDropped_.load(std::memory_order_relaxed);
	if (transmitWorker_) {
		stats.transmitFramesQueued = transmitWorker_->enqueuedFrames();
		stats.transmitFramesSent = transmitWorker_->sentFrames();
		stats.transmitFramesDropped = transmitWorker_->droppedFrames();
	}
	if (receiveWorker_) {
		stats.receiveFramesDiscarded = receiveWorker_->discardedFrames();
		stats.receiveQueueOverruns = receiveWorker_->outputQueueOverruns();
	}
	if (recordingWorker_) {
		stats.recordingFramesWritten = recordingWorker_->writtenFrames();
		stats.recordingFramesDropped = recordingWorker_->droppedFrames();
	}
	return stats;
}

uint64 JammerNetzAudioEngine::currentBufferSize() const
{
	return minPlayoutBufferLength_;
}

int JammerNetzAudioEngine::currentPacketSize()
{
	auto* sender = session_.sender();
	return sender ? sender->getCurrentBlockSize() : 0;
}

std::string JammerNetzAudioEngine::currentReceptionQuality() const
{
	return receiveWorker_ ? receiveWorker_->qualityStatement() : std::string();
}

bool JammerNetzAudioEngine::isReceivingData()
{
	return session_.isReceivingData();
}

double JammerNetzAudioEngine::currentRTT()
{
	return session_.currentRTT();
}

float JammerNetzAudioEngine::channelPitch(size_t channel) const
{
	return transmitWorker_ ? transmitWorker_->channelPitch(channel) : 0.0f;
}

float JammerNetzAudioEngine::sessionPitch(size_t channel) {
	auto setup = getSessionSetup();
	if (channel < setup.channels.size())
		return setup.channels[channel].pitch;
	return 0.0f;
}

std::shared_ptr<Recorder> JammerNetzAudioEngine::getMasterRecorder() const
{
	return masterRecorder_;
}

std::shared_ptr<Recorder> JammerNetzAudioEngine::getLocalRecorder() const
{
	return uploadRecorder_;
}

std::shared_ptr<JammerNetzClientInfoMessage> JammerNetzAudioEngine::getClientInfo()
{
	return session_.getClientInfo();
}

JammerNetzChannelSetup JammerNetzAudioEngine::getSessionSetup()
{
	return session_.getCurrentSessionSetup();
}
