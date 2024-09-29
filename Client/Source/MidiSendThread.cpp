#include "MidiSendThread.h"

#include "MidiController.h"

MidiSendThread::MidiSendThread() : Thread("MIDI Clock")
{
	startThread(Thread::Priority::high);
}

MidiSendThread::~MidiSendThread()
{
	stopThread(1000);
}

void MidiSendThread::setMidiOutputByName(std::string const &name)
{
	midiOutput_ = midikraft::MidiController::instance()->getMidiOutputByName(name);
}

void MidiSendThread::enqueue(std::chrono::high_resolution_clock::duration fromNow, MidiMessage const &message)
{
	ignoreUnused(fromNow);
	auto now = std::chrono::high_resolution_clock::now();// fromNow;
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
	while (threadShouldExit()) {
		// Wait until the time has come
		do {
			now = std::chrono::high_resolution_clock::now();
		}
		while (now < last + std::chrono::milliseconds(20));
		last = now;
		// Send the F8 MIDI Clock message
		midikraft::MidiController::instance()->getMidiOutput(midiOutput_)->sendMessageNow(MidiMessage::midiClock());
	}
	while (!threadShouldExit()) {
		MessageQueueItem item;
		while (midiMessages.try_pop(item) && !threadShouldExit()) {
			// Wait until the time has come
			while (std::chrono::high_resolution_clock::now() < item.whenToSend);
			// Send the F8 MIDI Clock message
			midikraft::MidiController::instance()->getMidiOutput(midiOutput_)->sendMessageNow(item.whatToSend);
		}
	}
}
