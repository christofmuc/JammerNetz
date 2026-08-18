#pragma once

#include "JuceHeader.h"

#include "JammerNetzPackage.h"
#include "MidiController.h"
#include "BoundedSpscQueue.h"

#include <chrono>


class MidiSendThread : juce::Thread {
public:
	MidiSendThread(std::vector<juce::MidiDeviceInfo> const outputs);
	virtual ~MidiSendThread() override;
	void shutdown();
	void disableOutput() noexcept;

	// The audio callback only publishes fixed-size event descriptions. JUCE MIDI
	// messages (including the Boss SysEx payload) are constructed on this worker.
	bool enqueueAt(std::chrono::steady_clock::time_point whenToSend, float bpm, MidiSignal signal, bool sendClock);
	uint64_t droppedMessages() const noexcept;

	void run() override;

private:
	struct MessageQueueItem {
		std::chrono::steady_clock::time_point whenToSend;
		float bpm { 0.0f };
		MidiSignal signal { MidiSignal_None };
		bool sendClock { false };
	};
	// MIDI clock remains timely by dropping new messages after 256 are pending.
	BoundedSpscQueue<MessageQueueItem> midiMessages { 256 };
	std::atomic<uint64_t> droppedMessages_ { 0 };
	std::atomic<bool> outputEnabled_ { true };
	std::vector<std::shared_ptr<midikraft::SafeMidiOutput>> f8_outputs;
};
