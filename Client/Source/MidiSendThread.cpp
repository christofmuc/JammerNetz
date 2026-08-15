#include "MidiSendThread.h"

#include "MidiController.h"

#include <spdlog/spdlog.h>

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

bool MidiSendThread::enqueue(std::chrono::high_resolution_clock::duration fromNow, std::vector<MidiMessage> const &messages)
{
	auto now = std::chrono::high_resolution_clock::now() + fromNow;
	const bool queued = midiMessages.tryWrite([&](MessageQueueItem& item) {
		item.whenToSend = now;
		item.whatToSend = messages;
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
				// Wait until the time has come
				while (std::chrono::high_resolution_clock::now() < item.whenToSend) {
					juce::Thread::yield();
				}
				// Send the F8 MIDI Clock message to all devices registered
				for (auto &out : f8_outputs) {
					out->sendBlockOfMessagesFullSpeed(item.whatToSend);
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
