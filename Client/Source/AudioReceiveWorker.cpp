/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "AudioReceiveWorker.h"

#include <cmath>
#include <utility>

namespace {

uint8 sysexMsb(uint16 value) { return static_cast<uint8>(value >> 7); }
uint8 sysexLsb(uint16 value) { return static_cast<uint8>(value & 0x7f); }

MidiMessage createBossClockMessage(double bpm, MidiSignal signal)
{
	uint16 length = 0;
	if (signal == MidiSignal_Start) {
		length = static_cast<uint16>(8 * 96);
	} else if (signal != MidiSignal_Stop) {
		return {};
	}
	const auto tempo = static_cast<uint16>(std::round(bpm * 10.0));
	std::vector<uint8> data { 0x41, 0x10, 0x00, 0x00, 0x5C, 0x12, 0x00, 0x01, 0x00, 0x00,
		sysexMsb(length), sysexLsb(length), sysexMsb(tempo), sysexLsb(tempo), 0x00, 0x00, 0x00, 0x00 };
	uint16 checksum = 0;
	for (size_t index = 6; index < data.size(); ++index) {
		checksum = static_cast<uint16>(checksum + data[index]);
	}
	data.push_back(static_cast<uint8>((0x80 - checksum) & 0x7f));
	return MidiMessage::createSysExMessage(data.data(), static_cast<int>(data.size()));
}

std::vector<MidiMessage> createBeatMessages(double bpm, MidiSignal signal)
{
	std::vector<MidiMessage> messages;
	messages.reserve(3);
	messages.push_back(MidiMessage::midiClock());
	if (signal == MidiSignal_Start) {
		messages.push_back(MidiMessage::midiStart());
		messages.push_back(createBossClockMessage(bpm, signal));
	} else if (signal == MidiSignal_Stop) {
		messages.push_back(MidiMessage::midiStop());
		messages.push_back(createBossClockMessage(bpm, signal));
	}
	return messages;
}

} // namespace

AudioReceiveWorker::AudioReceiveWorker(JammerNetzSession& session)
	: juce::Thread("JammerNetz receive preparation"), session_(session)
{
}

AudioReceiveWorker::~AudioReceiveWorker() { shutdown(); }

void AudioReceiveWorker::start()
{
	if (!isThreadRunning()) {
		startThread(juce::Thread::Priority::high);
	}
}

void AudioReceiveWorker::shutdown()
{
	signalThreadShouldExit();
	stopThread(2000);
	// The owner stops the audio callback and network receive callback before
	// shutdown, so neither queue has a producer or consumer at this point.
	inboundQueue_.reset();
	outputQueue_.reset();
	packetQueue_.reset();
	midiSender_.store(nullptr, std::memory_order_release);
}

void AudioReceiveWorker::enqueue(std::shared_ptr<JammerNetzAudioData> packet)
{
	if (packet && !inboundQueue_.tryWrite([&packet](std::shared_ptr<JammerNetzAudioData>& slot) { slot = std::move(packet); })) {
		inboundOverruns_.fetch_add(1, std::memory_order_relaxed);
	}
}

bool AudioReceiveWorker::tryPop(RemoteAudioFrame& frame)
{
	return outputQueue_.tryRead([&frame](RemoteAudioFrame& queued) { frame = queued; });
}

int AudioReceiveWorker::readyFrames() const noexcept { return outputQueue_.size(); }

void AudioReceiveWorker::setPlayoutRange(uint64_t minimumFrames, uint64_t maximumFrames) noexcept
{
	minimumFrames_.store(minimumFrames, std::memory_order_relaxed);
	maximumFrames_.store(std::max(minimumFrames, maximumFrames), std::memory_order_relaxed);
}

void AudioReceiveWorker::setMidiSender(std::shared_ptr<MidiSendThread> sender)
{
	midiSender_.store(std::move(sender), std::memory_order_release);
}

void AudioReceiveWorker::requestRebuffer() noexcept { rebufferRequested_.store(true, std::memory_order_release); }

uint64_t AudioReceiveWorker::requestReset() noexcept
{
	return requestedGeneration_.fetch_add(1, std::memory_order_acq_rel) + 1;
}

uint64_t AudioReceiveWorker::currentGeneration() const noexcept { return requestedGeneration_.load(std::memory_order_acquire); }

std::optional<float> AudioReceiveWorker::takeServerBpmUpdate() noexcept
{
	if (serverBpmPending_.exchange(false, std::memory_order_acq_rel)) {
		return latestServerBpm_.load(std::memory_order_relaxed);
	}
	return {};
}

uint64_t AudioReceiveWorker::discardedFrames() const noexcept
{
	return discarded_.load(std::memory_order_relaxed) + inboundOverruns_.load(std::memory_order_relaxed);
}
uint64_t AudioReceiveWorker::outputQueueOverruns() const noexcept
{
	return inboundOverruns_.load(std::memory_order_relaxed) + outputOverruns_.load(std::memory_order_relaxed);
}
std::string AudioReceiveWorker::qualityStatement() const { return packetQueue_.qualityStatement(); }
FFAU::LevelMeterSource* AudioReceiveWorker::meterSource() noexcept { return &sessionMeterSource_; }

void AudioReceiveWorker::run()
{
	while (!threadShouldExit()) {
		if (!processNextFrame()) {
			juce::Thread::sleep(1);
		}
	}
}

bool AudioReceiveWorker::processNextPendingFrame()
{
	if (isThreadRunning()) {
		return false;
	}
	return processNextFrame();
}

bool AudioReceiveWorker::processNextFrame()
{
	applyResetIfRequested();
	drainInbound();
	if (rebufferRequested_.exchange(false, std::memory_order_acq_rel)) {
		streamStarted_.store(false, std::memory_order_release);
		recoveringFromOverrun_ = false;
	}

	const auto minimum = minimumFrames_.load(std::memory_order_relaxed);
	const auto maximum = maximumFrames_.load(std::memory_order_relaxed);
	const auto combinedReadyFrames = [this]() {
		return static_cast<uint64_t>(outputQueue_.size()) + static_cast<uint64_t>(packetQueue_.size());
	};

	// The prepared queue is the queue the audio callback actually drains. If
	// the callback stalls, stop feeding it and discard old ordered packets
	// until the prepared queue has played back to the configured minimum.
	if (combinedReadyFrames() > maximum) {
		recoveringFromOverrun_ = true;
	}
	if (recoveringFromOverrun_) {
		std::shared_ptr<JammerNetzAudioData> discardedPacket;
		bool fillIn = false;
		while (combinedReadyFrames() > minimum && packetQueue_.try_pop(discardedPacket, fillIn)) {
			discarded_.fetch_add(1, std::memory_order_relaxed);
		}
		if (combinedReadyFrames() <= minimum) {
			recoveringFromOverrun_ = false;
		}
	}

	if (!streamStarted_.load(std::memory_order_acquire) && packetQueue_.size() >= minimum) {
		streamStarted_.store(true, std::memory_order_release);
	}

	return streamStarted_.load(std::memory_order_acquire)
		&& !recoveringFromOverrun_
		&& static_cast<uint64_t>(outputQueue_.size()) < maximum
		&& prepareOneFrame();
}

void AudioReceiveWorker::applyResetIfRequested()
{
	const auto requested = requestedGeneration_.load(std::memory_order_acquire);
	if (requested == activeGeneration_.load(std::memory_order_relaxed)) {
		return;
	}
	std::shared_ptr<JammerNetzAudioData> packet;
	while (inboundQueue_.tryRead([&packet](std::shared_ptr<JammerNetzAudioData>& queued) { packet = std::move(queued); })) {}
	packetQueue_.reset();
	streamStarted_.store(false, std::memory_order_release);
	recoveringFromOverrun_ = false;
	activeGeneration_.store(requested, std::memory_order_release);
}

void AudioReceiveWorker::drainInbound()
{
	std::shared_ptr<JammerNetzAudioData> packet;
	while (inboundQueue_.tryRead([&packet](std::shared_ptr<JammerNetzAudioData>& queued) { packet = std::move(queued); })) {
		packetQueue_.push(std::move(packet));
	}
}

bool AudioReceiveWorker::prepareOneFrame()
{
	if (outputQueue_.freeSpace() == 0) {
		return false;
	}
	std::shared_ptr<JammerNetzAudioData> packet;
	bool fillIn = false;
	if (!packetQueue_.try_pop(packet, fillIn) || !packet || !packet->audioBuffer()) {
		return false;
	}

	const auto audio = packet->audioBuffer();
	const auto generation = activeGeneration_.load(std::memory_order_acquire);
	const bool written = outputQueue_.tryWrite([&](RemoteAudioFrame& frame) {
		frame.generation = generation;
		frame.sourceTimestamp = packet->timestamp();
		for (int channel = 0; channel < 2; ++channel) {
			auto& destination = frame.samples[static_cast<size_t>(channel)];
			if (channel < audio->getNumChannels()) {
				const auto samplesToCopy = std::min(SAMPLE_BUFFER_SIZE, audio->getNumSamples());
				juce::FloatVectorOperations::copy(destination.data(), audio->getReadPointer(channel), samplesToCopy);
				juce::FloatVectorOperations::clear(destination.data() + samplesToCopy, SAMPLE_BUFFER_SIZE - samplesToCopy);
			} else {
				destination.fill(0.0f);
			}
		}
	});
	if (!written) {
		outputOverruns_.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	latestServerBpm_.store(packet->bpm(), std::memory_order_relaxed);
	serverBpmPending_.store(true, std::memory_order_release);
	if (packet->midiSignal() != MidiSignal_None) {
		pendingMidiSignal_ = packet->midiSignal();
	}
	scheduleMidi(*packet);
	updateSessionMeter();
	return true;
}

void AudioReceiveWorker::scheduleMidi(const JammerNetzAudioData& packet)
{
	const auto sender = midiSender_.load(std::memory_order_acquire);
	const double bpm = packet.bpm();
	if (!sender || bpm <= 0.0) {
		return;
	}
	constexpr double pulsesPerQuarterNote = 24.0;
	const double samplesPerPulse = static_cast<double>(SAMPLE_RATE) / (bpm * pulsesPerQuarterNote / 60.0);
	if (samplesPerPulse <= SAMPLE_BUFFER_SIZE) {
		return;
	}
	const double serverSamples = static_cast<double>(packet.serverTime());
	const double endPulse = std::floor((serverSamples + SAMPLE_BUFFER_SIZE) / samplesPerPulse);
	const double startPulse = std::floor(serverSamples / samplesPerPulse);
	if (endPulse - startPulse < 1.0e-6) {
		return;
	}
	const double offsetSamples = endPulse * samplesPerPulse - serverSamples;
	sender->enqueue(std::chrono::nanoseconds(static_cast<int64_t>(1.0e9 * offsetSamples / SAMPLE_RATE)),
		createBeatMessages(bpm, std::exchange(pendingMidiSignal_, MidiSignal_None)));
}

void AudioReceiveWorker::updateSessionMeter()
{
	const auto setup = session_.getCurrentSessionSetup();
	std::vector<float> magnitudes;
	std::vector<float> rms;
	magnitudes.reserve(setup.channels.size());
	rms.reserve(setup.channels.size());
	for (const auto& channel : setup.channels) {
		magnitudes.push_back(channel.mag);
		rms.push_back(channel.rms);
	}
	sessionMeterSource_.setBlockMeasurement(setup.channels.size(), magnitudes, rms);
}
