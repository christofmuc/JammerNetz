/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "JammerNetzAudioEngine.h"

#include "ServerInfo.h"

#include "BuffersConfig.h"
#include "Logger.h"

#include <cmath>

JammerNetzAudioEngine::JammerNetzAudioEngine(JammerNetzSession& session, const juce::File& recordingDirectory) :
    session_(session)
	, recordingDirectory_(recordingDirectory)
    , playBuffer_("server")
    , masterVolume_(1.0)
    , monitorBalance_(0.0)
    , clientBpm_(0.0f)
    , serverBpm_(0.0)
    , ignoreNextServerBpmChange_(false)
    , pendingServerBpm_(0.0f)
	, serverBpmUpdate_(0.0f)
	, bpmSliderLastMovedTicks_(0)
    , midiSignalToSend_(MidiSignal_None)
    , midiSignalToGenerate_(MidiSignal_None)
{
	inputState_.store(std::make_shared<const InputState>(InputState { JammerNetzChannelSetup(false), nullptr }), std::memory_order_release);
	isPlaying_ = false;
	resetQualityInfo_ = false;
	minPlayoutBufferLength_ = CLIENT_PLAYOUT_JITTER_BUFFER;
	maxPlayoutBufferLength_ = CLIENT_PLAYOUT_MAX_BUFFER;
	playoutBuffer_ = std::make_unique<RingBuffer>(2, PLAYOUT_RINGBUFFER_SIZE);

	//midiRecorder_ = std::make_unique<MidiRecorder>(deviceManager);

	// We might want to share a score sheet or similar
	//midiPlayalong_ = std::make_unique<MidiPlayAlong>("D:\\Development\\JammerNetz-OS\\Led Zeppelin - Stairway to heaven (1).kar");

	// We want to be able to tune our instruments
	tuner_ = std::make_unique<Tuner>();

}

JammerNetzAudioEngine::~JammerNetzAudioEngine()
{
	shutdown();
}

void JammerNetzAudioEngine::start()
{
	if (uploadRecorder_ || masterRecorder_) {
		return;
	}
	uploadRecorder_ = std::make_shared<Recorder>(recordingDirectory_, "LocalRecording", RecordingType::WAV);
	masterRecorder_ = std::make_shared<Recorder>(recordingDirectory_, "MasterRecording", RecordingType::FLAC);
	masterRecorder_->setChannelInfo(SAMPLE_RATE, JammerNetzChannelSetup(false, { JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Left), JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Right) }));
}

void JammerNetzAudioEngine::shutdown()
{
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
	playBuffer_.push(std::move(buffer));
}

void JammerNetzAudioEngine::setPlayoutBufferRange(uint64 minimumLength, uint64 maximumLength)
{
	minPlayoutBufferLength_.store(minimumLength, std::memory_order_relaxed);
	maxPlayoutBufferLength_.store(std::max(minimumLength, maximumLength), std::memory_order_relaxed);
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
	return serverBpmUpdate_.readOnce();
}

void JammerNetzAudioEngine::restartClock(std::vector<MidiDeviceInfo> outputs)
{
	// Where to send the Midi Clock signals
	auto retiredMidiSendThread = midiSendThread_.load(std::memory_order_acquire);
	midiSendThread_.store(nullptr, std::memory_order_release);
	if (retiredMidiSendThread) {
		retiredMidiSendThread->shutdown();
	}
	midiSendThread_.store(std::make_shared<MidiSendThread>(outputs), std::memory_order_release);
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
	// Reset counters etc
	PlayoutQualityInfo pqi;
	while (playoutQualityInfo_.try_pop(pqi));
	resetQualityInfo_.store(true, std::memory_order_release);
	isPlaying_ = false;
	std::shared_ptr<JammerNetzAudioData> elem;
	bool isFillIn;
	while (playBuffer_.try_pop(elem, isFillIn));
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

void JammerNetzAudioEngine::calcLocalMonitoring(const AudioBuffer<float>& inputBuffer, AudioBuffer<float>& outputBuffer, const JammerNetzChannelSetup& channelSetup) {

	outputBuffer.clear();
	if (monitorIsLocal_ && inputBuffer.getNumChannels() > 0) {
		auto [monitorVolume, _] = calcMonitorGain(monitorBalance_.load(std::memory_order_relaxed));
		// Apply gain to our channels and do a stereo mixdown
		jassert(inputBuffer.getNumSamples() == outputBuffer.getNumSamples());
		const auto channelsToMix = std::min(static_cast<size_t>(inputBuffer.getNumChannels()), channelSetup.channels.size());
		for (size_t channel = 0; channel < channelsToMix; channel++) {
			const JammerNetzSingleChannelSetup& setup = channelSetup.channels[channel];
			float input_volume = (float) (setup.volume * monitorVolume * masterVolume_);
			switch (setup.target) {
			case Mute:
				// Nothing to be done, ignore this channel
				break;
			case Left:
				// This is a left channel, going into the left.
				if (outputBuffer.getNumChannels() > 0) {
					outputBuffer.addFrom(0, 0, inputBuffer, (int) channel, 0, inputBuffer.getNumSamples(), input_volume);
				}
				break;
			case Right:
				// And the same for the right channel
				if (outputBuffer.getNumChannels() > 1) {
					outputBuffer.addFrom(1, 0, inputBuffer, (int) channel, 0, inputBuffer.getNumSamples(), input_volume);
				}
				break;
			case SendLeft:
			case SendRight:
			case SendMono:
				// Don't include the "send only" channel types into the local monitoring mix, this is what that flag is for!
				break;
			case Mono:
				if (outputBuffer.getNumChannels() > 0) {
					outputBuffer.addFrom(0, 0, inputBuffer, (int) channel, 0, inputBuffer.getNumSamples(), input_volume);
				}
				if (outputBuffer.getNumChannels() > 1) {
					outputBuffer.addFrom(1, 0, inputBuffer, (int) channel, 0, inputBuffer.getNumSamples(), input_volume);
				}
				break;
			}
		}
	}
}

static uint8 sysexMsb(uint16 in)
{
	return (uint8)(in >> 7);
}

static uint8 sysexLsb(uint16 in)
{
	return (uint8)(in & 0x7f);
}

static MidiMessage createBossRC300ClockMessage(double bpm, MidiSignal additionalSignal)
{
	// Boss RC-300 loop pedal is infamous for not being able to slave to MIDI clock. Let's try with tailored sysex messages then
	// See https://www.vguitarforums.com/smf/index.php?topic=7678.50 "RC300- Here's how to slave the RC-300's Tempo to (some) external sources"
	uint16 length = 0x00;
	switch (additionalSignal) {
	case MidiSignal_Start: {
		// We need to calculate the length, which effectively is the bar signature
		// The RC-300 seems to work with 24 pulses per 16th note, or 24*4=96 pulses per quarter note. That is faster than the 24 ppqn we use for the
		// MIDI clock. Anyway, let's just send it a 4 bar of 4/4 signature, which makes 16 quarter notes.
		uint16 quarterNotesLength = 8;
		uint16 pulsesPerQuarterNote = 96;
		length = quarterNotesLength * pulsesPerQuarterNote;
	}
		break;
	case MidiSignal_Stop:
		// The Stop signal just uses the length 0x00 already set above
		break;
	default:
		SimpleLogger::instance()->postMessageOncePerRun("Program error - got unknown MidiSignal to generate in createBossRC300ClockMessage!");
		// fall through
	case MidiSignal_None:
		// Nothing to generate, return empty MidiMessage
		return MidiMessage();
	}

	uint16 tempo = (uint16) round(bpm * 10.0f);
	std::vector<uint8> rc300 { 0x41, 0x10, 0x00, 0x00, 0x5C, 0x12, 0x00, 0x01, 0x00, 0x00, sysexMsb(length), sysexLsb(length), sysexMsb(tempo), sysexLsb(tempo), 0x00,
		0x00, 0x00, 0x00 };
	uint16 checksum = 0;
	for (size_t i = 6; i < rc300.size(); i++) {
		checksum += rc300[i];
	}
	uint8 checksumByte = static_cast<uint8>((0x80 - checksum) & 0x7f);
	rc300.push_back(checksumByte);
	return MidiMessage::createSysExMessage(rc300.data(), (int) rc300.size());
}

static std::vector<MidiMessage> createMidiBeatMessage(double bpm, std::optional<MidiSignal> additionalSignal, bool includeBossRC300)
{
	// For every Midi "Beat" we create a clock message (0xf8)
	std::vector<MidiMessage> result;
	result.push_back(MidiMessage::midiClock());

	// Send a synchronized start/stop additionally
	if (additionalSignal.has_value()) {
		switch (*additionalSignal) {
		case MidiSignal_Start:
			result.push_back(MidiMessage::midiStart());
			break;
		case MidiSignal_Stop:
			result.push_back(MidiMessage::midiStop());
			break;
		case MidiSignal_None:
			// No additional signal requested
			break;
		default:
			jassertfalse;
			SimpleLogger::instance()->postMessageOncePerRun("Program error: Unknown MIDI signal requested for createMidiBeatMessage");
		}
		if (includeBossRC300) {
			result.push_back(createBossRC300ClockMessage(bpm, *additionalSignal));
		}
	}
	return result;
}

void JammerNetzAudioEngine::process(const float* const* inputChannelData, int numInputChannels, float* const* outputChannelData,
    int numOutputChannels, int numSamples)
{
	float* const* constnessCorrection = const_cast<float* const*>(inputChannelData);
	PlayoutQualityInfo qualityInfo = lastPlayoutQualityInfo_;
	if (resetQualityInfo_.exchange(false, std::memory_order_acq_rel)) {
		qualityInfo = PlayoutQualityInfo();
	}
	const auto inputState = inputState_.load(std::memory_order_acquire);

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
	if (numInputChannels > 0 && inputStateMatchesDevice) {
		// Hard disk recording
		if (uploadRecorder_ && uploadRecorder_->isRecording()) {
			uploadRecorder_->saveBlock(inputChannelData, numSamples);
		}

		// Pump the new data into the ingest ring buffer, if it has space. Else that's possibly an assert
		if (numSamples <= inputState->ingestBuffer->getFreeSpace()) {
			inputState->ingestBuffer->write(constnessCorrection, numInputChannels, numSamples);
		}
		else {
			jassertfalse;
		}

		// Ok, now we can exhaust the ring buffer by reading network packet sized chunks and sending them to the server one by one
		while (inputState->ingestBuffer->getNumReady() >= SAMPLE_BUFFER_SIZE) {
			// Allocate an audio buffer and read a buffer full from the ring buffer
			auto audioBuffer = std::make_shared<AudioBuffer<float>>(numInputChannels, SAMPLE_BUFFER_SIZE);
			inputState->ingestBuffer->read(audioBuffer->getArrayOfWritePointers(), numInputChannels, SAMPLE_BUFFER_SIZE);

			// Send it to pitch detection
			tuner_->detectPitch(audioBuffer);

			// Measure the peak values for each channel
			meterSource_.measureBlock(*audioBuffer);

			// Send the MAG, RMS values and the pitch to the server, which will forward it to the other clients so they can show my levels even if they have only the mixed audio
			JammerNetzChannelSetup outgoingSetup = inputState->setup;
			for (size_t c = 0; c < static_cast<size_t>(numInputChannels); c++) {
				outgoingSetup.channels[c].mag = meterSource_.getMaxLevel(static_cast<int>(c));
				outgoingSetup.channels[c].rms = meterSource_.getRMSLevel(static_cast<int>(c));
				outgoingSetup.channels[c].pitch = tuner_->getPitch(c);
			}

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

			ControlData controllers;
			controllers.bpm = clientBpm_.readOnce();
			controllers.midiSignal = midiSignalToSend_.readOnce();
			if (auto* sender = session_.sender()) {
				sender->sendData(outgoingSetup, audioBuffer, controllers); //TODO offload the real sending to a different thread
			}
		}
	}

	// Create a better access structure for the output data
	AudioBuffer<float> outputBuffer(outputChannelData, numOutputChannels, numSamples);
	AudioBuffer<float> inputBufferNotOwned(constnessCorrection, numInputChannels, numSamples);

	// Don't start playing before the desired play-out buffer size is reached
	if (!isPlaying_ && playBuffer_.size() >= minPlayoutBufferLength_) {
		isPlaying_ = true;
	}
	else if (playBuffer_.size() > maxPlayoutBufferLength_) {
		// That's too many packages in our buffer, where did those come from? Did the server deliver too many packets/did our playback stop?
		// Reduce the length of the queue until it is the right size, throuw away audio that is too old to be played out
		std::shared_ptr<JammerNetzAudioData> data;
		while (playBuffer_.size() > CLIENT_PLAYOUT_JITTER_BUFFER) {
			qualityInfo.discardedPackageCounter_++;
			bool isFillIn;
			playBuffer_.try_pop(data, isFillIn);
		}
	}

	// Prepare the output buffer with the local monitoring signal
	if (inputState) {
		calcLocalMonitoring(inputBufferNotOwned, outputBuffer, inputState->setup);
	}
	else {
		outputBuffer.clear();
	}

	// For playout, we have to have enough bytes in the out ringbuffer to fill the output audio block.
	// Let's see if we have enough data from the network!

	while (isPlaying_ && playoutBuffer_->getNumReady() < numSamples) {
		// We need to produce a network package to fill up the playout ring buffer
		std::shared_ptr<JammerNetzAudioData> toPlay;
		bool isFillIn;
		if (playBuffer_.try_pop(toPlay, isFillIn)) {
			qualityInfo.currentPlayQueueLength_ = playBuffer_.size();
			// Ok, we have an Audio buffer to play. Hand over the data to the playback!
			if (toPlay && toPlay->audioBuffer()) {
				// Calculate the to-play latency
				qualityInfo.toPlayLatency_ = Time::getMillisecondCounterHiRes() - toPlay->timestamp();
				playoutBuffer_->write(toPlay->audioBuffer()->getArrayOfReadPointers(),
					toPlay->audioBuffer()->getNumChannels(),
					toPlay->audioBuffer()->getNumSamples());
			}
			else {
				// That would be considered a programming error, I shall not enqueue nullptr
				jassert(false);
				break;
			}

			// Check if we are tasked to generate a MIDI signal
			if (toPlay->midiSignal() != MidiSignal_None) {
				// This might overwrite a signal not yet generated, because the next F8 clock has not been generated
				midiSignalToGenerate_.setValue(toPlay->midiSignal());
			}

			double bpm = toPlay->bpm();
			serverBpm_ = bpm;
			if (auto midiSendThread = midiSendThread_.load(std::memory_order_acquire)) {
				// Play a MIDI clock at the speed given
				constexpr double pulsesPerQuarterNote = 24.0; // This is fairly standard
				double pulsesPerSecond = bpm * pulsesPerQuarterNote / 60.0;
				double samplesPerSecond = static_cast<double>(SAMPLE_RATE);
				double samplesPerPulse = samplesPerSecond / pulsesPerSecond;
				jassert(samplesPerPulse > SAMPLE_BUFFER_SIZE); // Else it gets jitery

				// Determine the server time for the first sample of this package
				uint64 serverTimeinSamples = toPlay->serverTime();
				const auto serverTimeInSamplesAsDouble = static_cast<double>(serverTimeinSamples);
				double bufferStartPulseNumber = floor(serverTimeInSamplesAsDouble / samplesPerPulse);
				double bufferEndPulseNumber = floor((serverTimeInSamplesAsDouble + static_cast<double>(SAMPLE_BUFFER_SIZE)) / samplesPerPulse);
				if (bufferEndPulseNumber - bufferStartPulseNumber > 1e-6) {
					// A Pulse must be sent! When in this buffer is the pulse due?
					double pulseFractionInSamples = bufferEndPulseNumber * samplesPerPulse - serverTimeInSamplesAsDouble;
					jassert(pulseFractionInSamples <= SAMPLE_BUFFER_SIZE);
					auto signalToGenerate = midiSignalToGenerate_.readOnce();
					midiSendThread->enqueue(std::chrono::nanoseconds(int(1e9 * pulseFractionInSamples / SAMPLE_RATE)), createMidiBeatMessage(bpm, signalToGenerate, true));
				}
			}

			// Check if the slider wasn't updated for a while, then take the server value and update the slider
			const auto now = std::chrono::steady_clock::now();
			const auto lastMovedTicks = bpmSliderLastMovedTicks_.load(std::memory_order_acquire);
			const auto lastMoved = std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(lastMovedTicks));
			if (lastMovedTicks == 0 || now - lastMoved > std::chrono::seconds(1)) {
				pendingServerBpm_.store((float) bpm, std::memory_order_release);
				ignoreNextServerBpmChange_.store(true, std::memory_order_release);
				serverBpmUpdate_.setValue(static_cast<float>(bpm));
				bpmSliderLastMovedTicks_.store(now.time_since_epoch().count(), std::memory_order_release);
			}
		}
		else {
			// Buffer underrun
			break;
		}
	}

	if (isPlaying_) {
		if (playoutBuffer_->getNumReady() < numSamples) {
			// This is a serious problem - either the server never started to send data, or we have a buffer underflow.
			qualityInfo.playUnderruns_++;
			isPlaying_ = false;
		}
		else {
			// We have Audio data to play! Make sure it is the correct size
			AudioBuffer<float> sessionAudio(2, numSamples);
			playoutBuffer_->read(sessionAudio.getArrayOfWritePointers(), 2, numSamples);

			auto [_, remoteVolume] = calcMonitorGain(monitorBalance_.load(std::memory_order_relaxed));
			float volume = (float) (remoteVolume * masterVolume_);
			for (int c = 0; c < std::min(2, outputBuffer.getNumChannels()); c++) {
				outputBuffer.addFrom(c, 0, sessionAudio.getReadPointer(c), numSamples, volume);
			}
		}

		// Calculate the RMS and mag displays for the other session participants
		auto session = session_.getCurrentSessionSetup();
		std::vector<float> magnitudes;
		std::vector<float> rmss;
		for (const auto& channel : session.channels) {
			magnitudes.push_back(channel.mag);
			rmss.push_back(channel.rms);
		}
		sessionMeterSource_.setBlockMeasurement(session.channels.size(), magnitudes, rmss);
	}

	outMeterSource_.measureBlock(outputBuffer);
	if (masterRecorder_ && masterRecorder_->isRecording()) {
		masterRecorder_->saveBlock(outputBuffer.getArrayOfReadPointers(), numSamples);
	}

	// Make the calculated quality info available for an interested consumer
	lastPlayoutQualityInfo_ = qualityInfo;
	playoutQualityInfo_.push(qualityInfo);
}

void JammerNetzAudioEngine::prepare(double sampleRate, int maximumBlockSize)
{
	ignoreUnused(sampleRate, maximumBlockSize);
	lastPlayoutQualityInfo_ = PlayoutQualityInfo();
}

void JammerNetzAudioEngine::release()
{
}

void JammerNetzAudioEngine::setChannelSetup(JammerNetzChannelSetup const &channelSetup)
{
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
	inputState_.store(std::make_shared<const InputState>(InputState { channelSetup, std::move(ingestBuffer) }), std::memory_order_release);
	if (channelCountChanged && midiRecorder_) {
		midiRecorder_->startRecording();
	}
}

FFAU::LevelMeterSource* JammerNetzAudioEngine::getMeterSource()
{
	return &meterSource_;
}

FFAU::LevelMeterSource* JammerNetzAudioEngine::getSessionMeterSource()
{
	return &sessionMeterSource_;
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
	// Return the latest QualityInfo
	PlayoutQualityInfo latest;
	while (playoutQualityInfo_.try_pop(latest));
	return latest;
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
	return playBuffer_.qualityStatement();
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
	return tuner_->getPitch(channel);
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
