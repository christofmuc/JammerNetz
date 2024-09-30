#pragma once

#include "JuceHeader.h"

#include "MidiController.h"

#include "tbb/concurrent_queue.h"

#include <chrono>
#include <deque>


class MidiSendThread : juce::Thread {
public:
	MidiSendThread(std::vector<std::string> const names);
	virtual ~MidiSendThread();

	void enqueue(std::chrono::high_resolution_clock::duration fromNow, MidiMessage const &message);

	void run() override;

private:
	juce::CriticalSection lock_;

	struct MessageQueueItem {
		std::chrono::high_resolution_clock::time_point whenToSend;
		MidiMessage whatToSend;
	};
	tbb::concurrent_queue<MessageQueueItem> midiMessages;
	std::vector<std::shared_ptr<midikraft::SafeMidiOutput>> f8_outputs;
};
