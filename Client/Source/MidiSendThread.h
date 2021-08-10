#pragma once

#include "JuceHeader.h"

#include "tbb/concurrent_queue.h"

#include <chrono>
#include <deque>

class MidiSendThread : juce::Thread {
public:
	MidiSendThread();
	virtual ~MidiSendThread();

	void setMidiOutput(std::string const &name);
	void enqueue(std::chrono::high_resolution_clock::duration fromNow, MidiMessage const &message);

	void run() override;

private:
	juce::CriticalSection lock_;

	struct MessageQueueItem {
		std::chrono::high_resolution_clock::time_point whenToSend;
		MidiMessage whatToSend;
	};
	tbb::concurrent_queue<MessageQueueItem> midiMessages;
	std::string midiOutput_;
};

