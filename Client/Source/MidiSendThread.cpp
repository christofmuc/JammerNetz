#include "MidiSendThread.h"

#include "MidiController.h"

#include <spdlog/spdlog.h>

#include <cmath>

namespace {

uint8 sysexMsb(uint16 value) { return static_cast<uint8>(value >> 7); }
uint8 sysexLsb(uint16 value) { return static_cast<uint8>(value & 0x7f); }

MidiMessage createBossClockMessage(double bpm, MidiSignal signal)
{
	uint16 length = 0;
	if (signal == MidiSignal_Start) {
		length = static_cast<uint16>(8 * 96);
	} else if (signal != MidiSignal_Stop || bpm <= 0.0) {
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

std::vector<MidiMessage> createOutputMessages(float bpm, MidiSignal signal, bool sendClock)
{
	std::vector<MidiMessage> messages;
	messages.reserve(3);
	const bool hasValidBossTempo = std::isfinite(bpm) && bpm > 0.0f && bpm <= 6553.5f;
	if (signal == MidiSignal_Start) {
		messages.push_back(MidiMessage::midiStart());
		if (hasValidBossTempo) {
			messages.push_back(createBossClockMessage(bpm, signal));
		}
	} else if (signal == MidiSignal_Stop) {
		messages.push_back(MidiMessage::midiStop());
		if (hasValidBossTempo) {
			messages.push_back(createBossClockMessage(bpm, signal));
		}
	}
	if (sendClock) {
		messages.push_back(MidiMessage::midiClock());
	}
	return messages;
}

} // namespace

MidiSendThread::MidiSendThread(std::vector<juce::MidiDeviceInfo> const outputs) : Thread("MIDI Clock")
{
	// Check if we can find the Midi Outputs requested in the MidiController
	for (auto const &device : outputs) {
		if (device.identifier.isEmpty()) {
			spdlog::error("Failed to find Midi output with the name {}, not sending clock", device.name.toStdString());
		} else {
			auto output = midikraft::MidiController::instance()->getMidiOutput(device);
			if (output->isValid()) {
				f8_outputs.push_back(output);
			} else {
				spdlog::error("Could not open MIDI output device, not sending clock: {}", device.name.toStdString());
			}
		}
	}
	startThread(Thread::Priority::high);
}

MidiSendThread::~MidiSendThread()
{
	shutdown();
}

void MidiSendThread::shutdown()
{
	signalThreadShouldExit();
	stopThread(1000);
}

void MidiSendThread::disableOutput() noexcept
{
	outputEnabled_.store(false, std::memory_order_release);
}

bool MidiSendThread::enqueueAt(std::chrono::steady_clock::time_point whenToSend, float bpm, MidiSignal signal, bool sendClock)
{
	const bool queued = midiMessages.tryWrite([&](MessageQueueItem& item) {
		item.whenToSend = whenToSend;
		item.bpm = bpm;
		item.signal = signal;
		item.sendClock = sendClock;
	});
	if (!queued) {
		droppedMessages_.fetch_add(1, std::memory_order_relaxed);
	}
	return queued;
}

uint64_t MidiSendThread::droppedMessages() const noexcept
{
	return droppedMessages_.load(std::memory_order_relaxed);
}

void MidiSendThread::run()
{

	// New algorithm to be written - use a MidiClocker to calculate the average BPM that should be generated. Then run a high priority thread to generate a stable
	// Midi Clock *and* synchronize it with the clock timestamps coming from the audio clock.
	// The problem is that the Audio thread is only running at e.g. 4 milliseconds clock, and that is about my current jitter - no surprise there.
	while (!threadShouldExit()) {
		try {
			bool sentAny = false;
			while (midiMessages.tryRead([&](MessageQueueItem& item) {
				if (!outputEnabled_.load(std::memory_order_acquire)) {
					return;
				}
				// Wait until the time has come
				while (!threadShouldExit() && outputEnabled_.load(std::memory_order_acquire)
					&& std::chrono::steady_clock::now() < item.whenToSend) {
					juce::Thread::yield();
				}
				if (threadShouldExit() || !outputEnabled_.load(std::memory_order_acquire)) {
					return;
				}
				const auto messages = createOutputMessages(item.bpm, item.signal, item.sendClock);
				for (auto &out : f8_outputs) {
					out->sendBlockOfMessagesFullSpeed(messages);
				}
				sentAny = true;
			}) && !threadShouldExit()) {}
			if (!sentAny) {
				juce::Thread::sleep(1);
			}
		} catch (std::exception &e) {
			spdlog::error("Failed to send MIDI Clock: {}", e.what());
			return;
		}
	}
}
