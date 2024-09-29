#include "MidiSendThread.h"

#include "MidiController.h"

#include <spdlog/spdlog.h>

MidiSendThread::MidiSendThread() : Thread("MIDI Clock")
{
	startThread(Thread::Priority::highest);
}

MidiSendThread::~MidiSendThread()
{
	stopThread(1000);
}

void MidiSendThread::setMidiOutputByName(std::string const &name)
{
	midiOutput_ = midikraft::MidiController::instance()->getMidiOutputByName(name);
	midikraft::MidiController::instance()->enableMidiOutput(midiOutput_);
}

void MidiSendThread::enqueue(std::chrono::high_resolution_clock::duration fromNow, MidiMessage const &message)
{
	ignoreUnused(fromNow);
	auto now = std::chrono::high_resolution_clock::now() + fromNow;
	midiMessages.push({ now, message });
}

void MidiSendThread::run()
{

	// New algorithm to be written - use a MidiClocker to calculate the average BPM that should be generated. Then run a high priority thread to generate a stable
	// Midi Clock *and* synchronize it with the clock timestamps coming from the audio clock.
	// The problem is that the Audio thread is only running at e.g. 4 milliseconds clock, and that is about my current jitter - no surprise there.

	// Just send a clock every 20 milliseconds
	std::chrono::high_resolution_clock::time_point last = std::chrono::high_resolution_clock::now();
	std::chrono::high_resolution_clock::time_point now;
	double bpm = 150.0;
	double secondsPerPulse = 60.0 / (bpm * 24);
	uint64 wait_nanos = (uint64)round(secondsPerPulse * 1e9);
	while (!threadShouldExit()) {
		// Wait until the time has come
		do {
			now = std::chrono::high_resolution_clock::now();
		}
		while (now < last + std::chrono::nanoseconds(wait_nanos));
		last = now;
		// Send the F8 MIDI Clock message
		midikraft::MidiController::instance()->getMidiOutput(midiOutput_)->sendMessageNow(MidiMessage::midiClock());
	}
	while (!threadShouldExit()) {
		MessageQueueItem item;
		while (midiMessages.try_pop(item) && !threadShouldExit()) {
			// Wait until the time has come
			uint64 waiting = 0;
			while (std::chrono::high_resolution_clock::now() < item.whenToSend) {
				waiting += 1;
			}
			// Send the F8 MIDI Clock message
			if (waiting > 0) {
				spdlog::error("Waited for {}", waiting);
			}
			midikraft::MidiController::instance()->getMidiOutput(midiOutput_)->sendMessageNow(item.whatToSend);
		}
	}
}
