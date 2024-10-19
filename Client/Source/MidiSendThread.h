#pragma once

#include "JuceHeader.h"

#include "MidiController.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wextra-semi"
#include "tbb/concurrent_queue.h"
#pragma clang diagnostic pop

#include <chrono>
#include <deque>


class MidiSendThread : juce::Thread {
public:
	MidiSendThread(std::vector<juce::MidiDeviceInfo> const outputs);
	virtual ~MidiSendThread() override;

	void enqueue(std::chrono::high_resolution_clock::duration fromNow, std::vector<MidiMessage> const &messages);

	void run() override;

private:
	juce::CriticalSection lock_;

	struct MessageQueueItem {
		std::chrono::high_resolution_clock::time_point whenToSend;
		std::vector<MidiMessage> whatToSend;
	};
	tbb::concurrent_queue<MessageQueueItem> midiMessages;
	std::vector<std::shared_ptr<midikraft::SafeMidiOutput>> f8_outputs;
};
