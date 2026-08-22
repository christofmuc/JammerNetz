/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "JammerNetzAudioEngine.h"

#include "BuffersConfig.h"
#include "Logger.h"

#include <cmath>
#include <cstring>

namespace {

class ScopedAudioCallbackActivity {
public:
	explicit ScopedAudioCallbackActivity(std::atomic<uint32_t>& activeCallbacks) noexcept
		: activeCallbacks_(activeCallbacks)
	{
		activeCallbacks_.fetch_add(1, std::memory_order_seq_cst);
	}

	~ScopedAudioCallbackActivity()
	{
		activeCallbacks_.fetch_sub(1, std::memory_order_seq_cst);
	}

private:
	std::atomic<uint32_t>& activeCallbacks_;
};

void clearOutputChannels(float* const* outputChannelData, int numOutputChannels, int numSamples) noexcept
{
	if (!outputChannelData || numOutputChannels <= 0 || numSamples <= 0) {
		return;
	}
	for (int channel = 0; channel < numOutputChannels; ++channel) {
		if (outputChannelData[channel]) {
			juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);
		}
	}
}

} // namespace

JammerNetzAudioEngine::JammerNetzAudioEngine(JammerNetzSession& session,
	const juce::File& recordingDirectory,
	std::shared_ptr<AudioPacketSink> packetSink)
	: session_(session)
	, recordingDirectory_(recordingDirectory)
	, remoteScratch_(2, JAMMERNETZ_MAX_CALLBACK_SAMPLES)
	, playoutResamplerInput_(2, resamplingScratchSamples)
	, masterVolume_(1.0)
	, monitorBalance_(0.0)
	, serverBpm_(0.0)
	, ignoreNextServerBpmChange_(false)
	, pendingServerBpm_(0.0f)
	, bpmSliderLastMovedTicks_(0)
{
	configuredInputState_ = std::make_shared<const InputState>(InputState { JammerNetzChannelSetup(false), nullptr, nullptr, nullptr });
	inputState_.store(configuredInputState_.get(), std::memory_order_release);
	minPlayoutBufferLength_ = CLIENT_PLAYOUT_JITTER_BUFFER;
	maxPlayoutBufferLength_ = CLIENT_PLAYOUT_MAX_BUFFER;
	playoutBuffer_ = std::make_unique<RingBuffer>(2, PLAYOUT_RINGBUFFER_SIZE);
	playoutResamplerReady_ = playoutResampler_.prepare(2,
		minimumResamplingFactor, maximumResamplingFactor);
	if (!playoutResamplerReady_) {
		playoutResamplerFailureReported_.store(true, std::memory_order_relaxed);
		SimpleLogger::instance()->postMessage("Audio playout resampler initialization failed");
	}
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
	shutdownRequested_.store(false, std::memory_order_seq_cst);
	started_ = true;
	if (enableRecording) {
		uploadRecorder_ = std::make_shared<Recorder>(recordingDirectory_, "LocalRecording", RecordingType::WAV);
		masterRecorder_ = std::make_shared<Recorder>(recordingDirectory_, "MasterRecording", RecordingType::FLAC);
		const auto deviceRate = static_cast<int>(std::llround(
			preparedSampleRate_.load(std::memory_order_relaxed)));
		masterRecorder_->setChannelInfo(deviceRate, JammerNetzChannelSetup(false, { JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Left), JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Right) }));
		if (configuredInputState_) {
			uploadRecorder_->setChannelInfo(deviceRate, configuredInputState_->setup);
		}
		recordingWorker_ = std::make_unique<AudioRecordingWorker>(uploadRecorder_, masterRecorder_);
		recordingWorker_->start();
	}
	transmitWorker_->start();
	receiveWorker_->start();
}

void JammerNetzAudioEngine::shutdown()
{
	shutdownRequested_.store(true, std::memory_order_seq_cst);
	while (activeAudioCallbacks_.load(std::memory_order_seq_cst) != 0) {
		juce::Thread::sleep(1);
	}
	started_ = false;
	if (auto* tap = outputTap_.exchange(nullptr, std::memory_order_acq_rel)) {
		tap->release();
	}
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
	realtimeMidiSender_.store(nullptr, std::memory_order_seq_cst);
	auto activeMidiSendThread = midiSendThread_.load(std::memory_order_acquire);
	midiSendThread_.store(nullptr, std::memory_order_release);
	retireMidiSender(std::move(activeMidiSendThread));
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
	auto sender = std::make_shared<MidiSendThread>(outputs);
	midiSendThread_.store(sender, std::memory_order_release);
	// Publish the replacement before retiring the previous sender. Retirement
	// waits for any audio chunk that observed the old raw pointer to finish.
	realtimeMidiSender_.store(sender.get(), std::memory_order_seq_cst);
	retireMidiSender(std::move(retiredMidiSendThread));
}

void JammerNetzAudioEngine::retireMidiSender(std::shared_ptr<MidiSendThread> sender)
{
	if (!sender) {
		return;
	}
	sender->disableOutput();
	// realtimeMidiSender_ has already published its replacement. The audio
	// callback validates its hazard pointer before dereferencing the sender, so
	// a matching hazard identifies the only callback that can still use it.
	while (realtimeMidiSenderHazard_.load(std::memory_order_seq_cst) == sender.get()) {
		juce::Thread::sleep(1);
	}
	sender->shutdown();
	retiredMidiOutputEventsDropped_.fetch_add(sender->droppedMessages(), std::memory_order_relaxed);
}

void JammerNetzAudioEngine::setMidiSignalToSend(MidiSignal signal)
{
	if (signal == MidiSignal_None) {
		return;
	}
	if (!midiSignalsToSend_.tryWrite([signal](MidiSignal& queued) { queued = signal; })) {
		midiTransportCommandsDropped_.fetch_add(1, std::memory_order_relaxed);
	}
}

std::optional<MidiSignal> JammerNetzAudioEngine::takeMidiSignalToSend() noexcept
{
	MidiSignal result = MidiSignal_None;
	if (midiSignalsToSend_.tryRead([&result](MidiSignal& queued) { result = queued; })) {
		return result;
	}
	return {};
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

void JammerNetzAudioEngine::resetPlayoutState() noexcept
{
	playoutBuffer_->clear();
	playoutTimingRead_ = 0;
	playoutTimingWrite_ = 0;
	playoutTimingCount_ = 0;
	playoutSamplesWritten_ = 0;
	playoutNetworkSamplePosition_ = 0.0;
	playoutResamplerInputSamples_ = 0;
	if (!playoutAdaptiveResampling_) {
		playoutQueueExcursionBlocks_ = 0;
	}
}

void JammerNetzAudioEngine::appendPlayoutTiming(const RemoteAudioFrame& frame) noexcept
{
	if (playoutTimingCount_ == playoutTimingMarkers_.size()) {
		midiTimingMarkersDropped_.fetch_add(1, std::memory_order_relaxed);
		playoutSamplesWritten_ += SAMPLE_BUFFER_SIZE;
		return;
	}
	auto& marker = playoutTimingMarkers_[playoutTimingWrite_];
	marker.playoutSample = playoutSamplesWritten_;
	marker.serverSampleEnd = frame.serverSampleEnd;
	marker.bpm = frame.bpm;
	marker.midiSignal = frame.midiSignal;
	playoutTimingWrite_ = (playoutTimingWrite_ + 1) % playoutTimingMarkers_.size();
	++playoutTimingCount_;
	playoutSamplesWritten_ += SAMPLE_BUFFER_SIZE;
}

void JammerNetzAudioEngine::scheduleMidiForPlayout(const int outputSamples,
	const double outputSamplesPerNetworkSample) noexcept
{
	const auto sourceStart = playoutNetworkSamplePosition_;
	const auto sourceEnd = sourceStart
		+ static_cast<double>(outputSamples) / outputSamplesPerNetworkSample;
	const auto playoutStart = std::chrono::steady_clock::now();
	MidiSendThread* sender = nullptr;
	do {
		sender = realtimeMidiSender_.load(std::memory_order_seq_cst);
		realtimeMidiSenderHazard_.store(sender, std::memory_order_seq_cst);
	} while (sender != realtimeMidiSender_.load(std::memory_order_seq_cst));
	while (playoutTimingCount_ > 0) {
		const auto& marker = playoutTimingMarkers_[playoutTimingRead_];
		if (static_cast<double>(marker.playoutSample) >= sourceEnd) {
			break;
		}
		if (sender) {
			const auto sourceOffset = std::max(0.0,
				static_cast<double>(marker.playoutSample) - sourceStart);
			const auto frameOffset = static_cast<uint64_t>(std::llround(
				sourceOffset * outputSamplesPerNetworkSample));
			scheduleMidiFrame(sender, marker.serverSampleEnd, marker.bpm, marker.midiSignal,
				frameOffset, outputSamplesPerNetworkSample, playoutStart);
		}
		playoutTimingRead_ = (playoutTimingRead_ + 1) % playoutTimingMarkers_.size();
		--playoutTimingCount_;
	}
	realtimeMidiSenderHazard_.store(nullptr, std::memory_order_seq_cst);
	playoutNetworkSamplePosition_ = sourceEnd;
}

void JammerNetzAudioEngine::scheduleMidiFrame(MidiSendThread* sender, uint64 serverSampleEnd, float bpm,
	MidiSignal signal, uint64_t frameOffsetSamples, const double outputSamplesPerNetworkSample,
	std::chrono::steady_clock::time_point playoutStart) noexcept
{
	const auto outputSampleRate = preparedSampleRate_.load(std::memory_order_relaxed);
	const auto atSampleOffset = [playoutStart, outputSampleRate](double samples) {
		return playoutStart + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
			std::chrono::duration<double>(samples / outputSampleRate));
	};
	if (signal != MidiSignal_None) {
		sender->enqueueAt(atSampleOffset(static_cast<double>(frameOffsetSamples)), bpm, signal, false);
	}

	if (!std::isfinite(bpm) || bpm <= 0.0f || bpm > 1000.0f || serverSampleEnd < SAMPLE_BUFFER_SIZE) {
		return;
	}
	constexpr double pulsesPerQuarterNote = 24.0;
	const double samplesPerPulse = static_cast<double>(SAMPLE_RATE) * 60.0
		/ (static_cast<double>(bpm) * pulsesPerQuarterNote);
	if (!std::isfinite(samplesPerPulse) || samplesPerPulse <= 0.0) {
		return;
	}
	const double frameStart = static_cast<double>(serverSampleEnd - SAMPLE_BUFFER_SIZE);
	const double frameEnd = static_cast<double>(serverSampleEnd);
	const auto firstPulse = static_cast<uint64_t>(std::ceil(frameStart / samplesPerPulse - 1.0e-12));
	// The UI currently caps tempo at 250 BPM (one pulse per 480 samples). Keep
	// this loop bounded even when receiving malformed or future protocol data.
	constexpr uint64_t maximumPulsesPerFrame = 8;
	for (uint64_t pulse = firstPulse; pulse < firstPulse + maximumPulsesPerFrame; ++pulse) {
		const double pulseSample = static_cast<double>(pulse) * samplesPerPulse;
		if (pulseSample >= frameEnd - 1.0e-9) {
			break;
		}
		const double offset = static_cast<double>(frameOffsetSamples)
			+ (pulseSample - frameStart) * outputSamplesPerNetworkSample;
		sender->enqueueAt(atSampleOffset(std::max(0.0, offset)), bpm, MidiSignal_None, true);
	}
}

double JammerNetzAudioEngine::inputResamplingFactor(const PlayoutQualityInfo& qualityInfo) const noexcept
{
	const auto reportedRate = preparedSampleRate_.load(std::memory_order_relaxed);
	double effectiveRate = reportedRate;
	if (qualityInfo.numSamplesSinceStart_ >= static_cast<int64>(reportedRate)
		&& std::isfinite(qualityInfo.measuredSampleRate)
		&& qualityInfo.measuredSampleRate >= reportedRate * 0.95
		&& qualityInfo.measuredSampleRate <= reportedRate * 1.05) {
		effectiveRate = qualityInfo.measuredSampleRate;
	}
	auto factor = static_cast<double>(SAMPLE_RATE) / effectiveRate;
	// A 100 ppm dead band keeps good nominal-48 kHz interfaces sample-perfect.
	if (std::abs(factor - 1.0) <= 100.0e-6) {
		factor = 1.0;
	}
	return std::clamp(factor, minimumResamplingFactor, maximumResamplingFactor);
}

int JammerNetzAudioEngine::queuedPlayoutNetworkSamples() const noexcept
{
	const auto preparedFrames = receiveWorker_ ? receiveWorker_->readyFrames() : 0;
	return playoutResamplerInputSamples_ + playoutBuffer_->getNumReady()
		+ preparedFrames * SAMPLE_BUFFER_SIZE;
}

double JammerNetzAudioEngine::playoutResamplingFactor() noexcept
{
	const auto outputRate = preparedSampleRate_.load(std::memory_order_relaxed);
	const auto baseFactor = std::clamp(outputRate / static_cast<double>(SAMPLE_RATE),
		minimumResamplingFactor, maximumResamplingFactor);
	const auto queuedFrames = static_cast<double>(queuedPlayoutNetworkSamples())
		/ static_cast<double>(SAMPLE_BUFFER_SIZE);
	const auto targetFrames = static_cast<double>(minPlayoutBufferLength_.load(
		std::memory_order_relaxed) + 1U);
	const auto queueErrorFrames = queuedFrames - targetFrames;
	if (std::abs(baseFactor - 1.0) > 100.0e-6) {
		playoutAdaptiveResampling_ = true;
	}
	else if (!playoutAdaptiveResampling_) {
		if (std::abs(queueErrorFrames) > 4.0) {
			playoutQueueExcursionBlocks_ = std::min(playoutQueueExcursionBlocks_ + 1U,
				adaptiveResamplingActivationBlocks);
			playoutAdaptiveResampling_ = playoutQueueExcursionBlocks_
				>= adaptiveResamplingActivationBlocks;
		}
		else {
			playoutQueueExcursionBlocks_ = 0;
		}
	}
	if (!playoutAdaptiveResampling_) {
		return 1.0;
	}
	constexpr double correctionPerFrame = 500.0e-6;
	constexpr double maximumCorrection = 5000.0e-6;
	constexpr double correctionDeadBandFrames = 2.0;
	const auto controlledError = queueErrorFrames > correctionDeadBandFrames
		? queueErrorFrames - correctionDeadBandFrames
		: (queueErrorFrames < -correctionDeadBandFrames
			? queueErrorFrames + correctionDeadBandFrames : 0.0);
	const auto correction = std::clamp(controlledError * correctionPerFrame,
		-maximumCorrection, maximumCorrection);
	return std::clamp(baseFactor * (1.0 - correction),
		minimumResamplingFactor, maximumResamplingFactor);
}

void JammerNetzAudioEngine::fillPlayoutResamplerInput(const int minimumSamples)
{
	while (receiveWorker_
		&& playoutResamplerInputSamples_ + playoutBuffer_->getNumReady()
			< minimumSamples + SAMPLE_BUFFER_SIZE) {
		RemoteAudioFrame frame;
		if (!receiveWorker_->tryPop(frame)) {
			break;
		}
		if (frame.generation != expectedRemoteGeneration_.load(std::memory_order_acquire)) {
			continue;
		}
		std::array<const float*, 2> pointers { frame.samples[0].data(), frame.samples[1].data() };
		playoutBuffer_->write(pointers.data(), 2, SAMPLE_BUFFER_SIZE);
		appendPlayoutTiming(frame);
		lastPlayoutQualityInfo_.toPlayLatency_ = Time::getMillisecondCounterHiRes()
			- frame.sourceTimestamp;
	}
	const auto availableCapacity = playoutResamplerInput_.getNumSamples()
		- playoutResamplerInputSamples_;
	const auto samplesToStage = std::min(playoutBuffer_->getNumReady(), availableCapacity);
	if (samplesToStage <= 0) {
		return;
	}
	std::array<float*, 2> destinations {
		playoutResamplerInput_.getWritePointer(0, playoutResamplerInputSamples_),
		playoutResamplerInput_.getWritePointer(1, playoutResamplerInputSamples_)
	};
	playoutBuffer_->read(destinations.data(), 2, samplesToStage);
	playoutResamplerInputSamples_ += samplesToStage;
}

void JammerNetzAudioEngine::process(const float* const* inputChannelData, int numInputChannels, float* const* outputChannelData,
    int numOutputChannels, int numSamples)
{
	if (shutdownRequested_.load(std::memory_order_seq_cst)) {
		clearOutputChannels(outputChannelData, numOutputChannels, numSamples);
		return;
	}
	const ScopedAudioCallbackActivity callbackActivity(activeAudioCallbacks_);
	// Close the race where shutdown starts after the first check but before this
	// callback publishes that it may access the workers.
	if (shutdownRequested_.load(std::memory_order_seq_cst)) {
		clearOutputChannels(outputChannelData, numOutputChannels, numSamples);
		return;
	}
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
	PlayoutQualityInfo qualityInfo = lastPlayoutQualityInfo_;
	if (resetQualityInfo_.exchange(false, std::memory_order_acq_rel)) {
		qualityInfo = PlayoutQualityInfo();
	}
	if (resetPlayoutRequested_.exchange(false, std::memory_order_acq_rel)) {
		resetPlayoutState();
		isPlaying_.store(false, std::memory_order_release);
	}
	const auto* inputState = inputState_.load(std::memory_order_acquire);

	// Measure time passed
	measureSamplesPerTime(qualityInfo, numSamples);

	const bool inputStateMatchesDevice = inputState && inputState->ingestBuffer
		&& inputState->resampler && inputState->resampler->isPrepared()
		&& inputState->resampleScratch
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

		const auto factor = inputResamplingFactor(qualityInfo);
		auto resampled = inputState->resampler->process(inputChannelData, numSamples,
			inputState->resampleScratch->getArrayOfWritePointers(),
			inputState->resampleScratch->getNumSamples(), factor);
		if (!inputState->resampler->isPrepared()
			&& !inputResamplerFailureReported_.exchange(true, std::memory_order_acq_rel)) {
			MessageManager::callAsync([]() {
				SimpleLogger::instance()->postMessage("Audio input resampler failed");
			});
		}
		if (resampled.inputSamplesUsed != numSamples) {
			inputBlocksDropped_.fetch_add(1, std::memory_order_relaxed);
		}
		if (resampled.outputSamplesGenerated <= inputState->ingestBuffer->getFreeSpace()) {
			inputState->ingestBuffer->write(inputState->resampleScratch->getArrayOfWritePointers(),
				numInputChannels, resampled.outputSamplesGenerated);
		}
		else {
			inputBlocksDropped_.fetch_add(1, std::memory_order_relaxed);
		}

		while (inputState->ingestBuffer->getNumReady() >= SAMPLE_BUFFER_SIZE) {
			if (transmitWorker_ && transmitWorker_->hasCapacity()) {
				if (!transmitWorker_->enqueueFrom(*inputState->ingestBuffer, numInputChannels,
						clientBpm_.takeLatest(), takeMidiSignalToSend())) {
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

	const auto playoutFactor = playoutResamplerReady_ ? playoutResamplingFactor() : 1.0;
	const auto resamplerLookahead = !playoutAdaptiveResampling_
		&& std::abs(playoutFactor - 1.0) <= 1.0e-12 ? 0 : playoutResampler_.filterWidth();
	const auto requiredNetworkSamples = static_cast<int>(std::ceil(
		static_cast<double>(numSamples) / playoutFactor)) + resamplerLookahead
		+ (resamplerLookahead > 0 ? 2 : 0);
	fillPlayoutResamplerInput(requiredNetworkSamples);
	qualityInfo.toPlayLatency_ = lastPlayoutQualityInfo_.toPlayLatency_;
	qualityInfo.currentPlayQueueLength_ = static_cast<uint64>(
		(queuedPlayoutNetworkSamples() + SAMPLE_BUFFER_SIZE - 1) / SAMPLE_BUFFER_SIZE);
	qualityInfo.discardedPackageCounter_ = receiveWorker_ ? receiveWorker_->discardedFrames() : 0;
	if (playoutResamplerReady_ && !isPlaying_.load(std::memory_order_acquire)
		&& playoutResamplerInputSamples_ >= requiredNetworkSamples) {
		isPlaying_.store(true, std::memory_order_release);
	}

	if (playoutResamplerReady_ && isPlaying_.load(std::memory_order_acquire)) {
		std::array<const float*, 2> resamplerInputs {
			playoutResamplerInput_.getReadPointer(0), playoutResamplerInput_.getReadPointer(1)
		};
		std::array<float*, 2> resamplerOutputs {
			remoteScratch_.getWritePointer(0), remoteScratch_.getWritePointer(1)
		};
		const auto resampled = playoutResampler_.process(resamplerInputs.data(),
			playoutResamplerInputSamples_, resamplerOutputs.data(), numSamples, playoutFactor);
		const bool validConsumption = resampled.inputSamplesUsed >= 0
			&& resampled.inputSamplesUsed <= playoutResamplerInputSamples_;
		if (resampled.outputSamplesGenerated != numSamples || !validConsumption) {
			if (!playoutResampler_.isPrepared()) {
				playoutResamplerReady_ = false;
				if (!playoutResamplerFailureReported_.exchange(true, std::memory_order_acq_rel)) {
					MessageManager::callAsync([]() {
						SimpleLogger::instance()->postMessage("Audio playout resampler failed");
					});
				}
			}
			qualityInfo.playUnderruns_++;
			isPlaying_.store(false, std::memory_order_release);
			resetPlayoutState();
			if (receiveWorker_) {
				receiveWorker_->requestRebuffer();
			}
		}
		else {
			const auto remaining = playoutResamplerInputSamples_ - resampled.inputSamplesUsed;
			for (int channel = 0; channel < 2; ++channel) {
				std::memmove(playoutResamplerInput_.getWritePointer(channel),
					playoutResamplerInput_.getReadPointer(channel, resampled.inputSamplesUsed),
					static_cast<std::size_t>(remaining) * sizeof(float));
			}
			playoutResamplerInputSamples_ = remaining;
			scheduleMidiForPlayout(numSamples, playoutFactor);

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
	if (auto* tap = outputTap_.load(std::memory_order_acquire)) {
		std::array<const float*, 2> tapPointers { meterPointers[0], meterPointers[1] };
		tap->enqueue(tapPointers.data(), 2, numSamples);
	}
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
	preparedSampleRate_.store(sampleRate, std::memory_order_relaxed);
	playoutResamplerReady_ = playoutResampler_.prepare(2,
		minimumResamplingFactor, maximumResamplingFactor);
	if (playoutResamplerReady_) {
		playoutResamplerFailureReported_.store(false, std::memory_order_release);
	}
	else if (!playoutResamplerFailureReported_.exchange(true, std::memory_order_acq_rel)) {
		SimpleLogger::instance()->postMessage("Audio playout resampler initialization failed");
	}
	playoutAdaptiveResampling_ = std::abs(sampleRate - static_cast<double>(SAMPLE_RATE))
		> static_cast<double>(SAMPLE_RATE) * 100.0e-6;
	playoutQueueExcursionBlocks_ = 0;
	if (configuredInputState_) {
		publishInputState(configuredInputState_->setup);
	}
	const auto deviceRate = static_cast<int>(std::llround(sampleRate));
	if (masterRecorder_) {
		masterRecorder_->setChannelInfo(deviceRate, JammerNetzChannelSetup(false, {
			JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Left),
			JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Right) }));
	}
	if (uploadRecorder_ && configuredInputState_) {
		uploadRecorder_->setChannelInfo(deviceRate, configuredInputState_->setup);
	}
	lastPlayoutQualityInfo_ = PlayoutQualityInfo();
	resetPlayoutRequested_.store(true, std::memory_order_release);
	if (auto* tap = outputTap_.load(std::memory_order_acquire)) {
		tap->prepare(sampleRate, maximumBlockSize);
	}
}

void JammerNetzAudioEngine::release()
{
	resetPlayoutRequested_.store(true, std::memory_order_release);
	if (auto* tap = outputTap_.load(std::memory_order_acquire)) {
		tap->release();
	}
}

void JammerNetzAudioEngine::setOutputTap(AudioOutputTap* tap) noexcept
{
	outputTap_.store(tap, std::memory_order_release);
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

	if (uploadRecorder_) {
		uploadRecorder_->setChannelInfo(static_cast<int>(std::llround(
			preparedSampleRate_.load(std::memory_order_relaxed))), channelSetup);
	}
	publishInputState(channelSetup);
	if (!channelSetup.channels.empty() && midiRecorder_) {
		midiRecorder_->startRecording();
	}
}

void JammerNetzAudioEngine::publishInputState(const JammerNetzChannelSetup& channelSetup)
{
	const auto previousState = inputState_.load(std::memory_order_acquire);
	const bool channelCountChanged = !previousState
		|| previousState->setup.channels.size() != channelSetup.channels.size();
	std::shared_ptr<RingBuffer> ingestBuffer = previousState ? previousState->ingestBuffer : nullptr;
	if (channelCountChanged) {
		ingestBuffer = channelSetup.channels.empty() ? nullptr : std::make_shared<RingBuffer>(
			static_cast<int>(channelSetup.channels.size()), INGEST_RINGBUFFER_SIZE);
	}
	std::shared_ptr<StreamingAudioResampler> resampler;
	std::shared_ptr<AudioBuffer<float>> scratch;
	if (!channelSetup.channels.empty()) {
		resampler = std::make_shared<StreamingAudioResampler>();
		if (resampler->prepare(static_cast<int>(channelSetup.channels.size()),
			minimumResamplingFactor, maximumResamplingFactor)) {
			scratch = std::make_shared<AudioBuffer<float>>(
				static_cast<int>(channelSetup.channels.size()), resamplingScratchSamples);
			inputResamplerFailureReported_.store(false, std::memory_order_release);
		}
		else {
			resampler.reset();
			if (!inputResamplerFailureReported_.exchange(true, std::memory_order_acq_rel)) {
				SimpleLogger::instance()->postMessage("Audio input resampler initialization failed");
			}
		}
	}

	inputChannelMismatchReported_.store(false, std::memory_order_release);
	const auto completedEpoch = completedAudioEpoch_.load(std::memory_order_acquire);
	retiredInputStates_.erase(std::remove_if(retiredInputStates_.begin(), retiredInputStates_.end(), [completedEpoch](const RetiredInputState& retired) {
		return retired.retireEpoch < completedEpoch;
	}), retiredInputStates_.end());
	if (configuredInputState_) {
		retiredInputStates_.push_back({ configuredInputState_, completedEpoch });
	}
	configuredInputState_ = std::make_shared<const InputState>(InputState { channelSetup,
		std::move(ingestBuffer), std::move(resampler), std::move(scratch) });
	inputState_.store(configuredInputState_.get(), std::memory_order_release);
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
	stats.midiTransportCommandsDropped = midiTransportCommandsDropped_.load(std::memory_order_relaxed);
	stats.midiTimingMarkersDropped = midiTimingMarkersDropped_.load(std::memory_order_relaxed);
	if (const auto sender = midiSendThread_.load(std::memory_order_acquire)) {
		stats.midiOutputEventsDropped = sender->droppedMessages();
	}
	stats.midiOutputEventsDropped += retiredMidiOutputEventsDropped_.load(std::memory_order_relaxed);
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

int JammerNetzAudioEngine::safeUdpPayloadSize() const
{
	return session_.safeUdpPayloadSize();
}

PathMtuDiscoveryStatus JammerNetzAudioEngine::mtuDiscoveryStatus() const
{
	return session_.mtuDiscoveryStatus();
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
