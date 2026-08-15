#pragma once

#include "JuceHeader.h"

#include "MidiController.h"
#include "BoundedSpscQueue.h"

#include <chrono>


class MidiSendThread : juce::Thread {
public:
	MidiSendThread(std::vector<juce::MidiDeviceInfo> const outputs);
	virtual ~MidiSendThread() override;
	void shutdown();

	bool enqueue(std::chrono::high_resolution_clock::duration fromNow, std::vector<MidiMessage> const &messages);
	uint64_t droppedMessages() const noexcept;

	void run() override;

private:
	struct MessageQueueItem {
		std::chrono::high_resolution_clock::time_point whenToSend;
		std::vector<MidiMessage> whatToSend;
	};
	// MIDI clock remains timely by dropping new messages after 256 are pending.
	BoundedSpscQueue<MessageQueueItem> midiMessages { 256 };
	std::atomic<uint64_t> droppedMessages_ { 0 };
	std::vector<std::shared_ptr<midikraft::SafeMidiOutput>> f8_outputs;
};
