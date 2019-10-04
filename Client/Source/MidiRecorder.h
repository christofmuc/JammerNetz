/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "MidiClocker.h"

class MidiRecorder : private MidiInputCallback {
public:
	MidiRecorder(AudioDeviceManager &deviceManager);
	~MidiRecorder();

	void startRecording();
	void saveToFile(String filename);

	std::weak_ptr<MidiClocker> getClocker();

private:
	// Midi Input Callback
	virtual void handleIncomingMidiMessage(MidiInput* source, const MidiMessage& message) override;
	virtual void handlePartialSysexMessage(MidiInput* source, const uint8* messageData, int numBytesSoFar, double timestamp) override;

	void enableMidiInput(String name);
	void disableMidiInput(String input);

	std::map<String, MidiMessageSequence> recorded_;
	bool isRecording_;
	double recordingStartTime_;

	std::map<String, MidiInputCallback *>  callbacks_;	
	AudioDeviceManager &deviceManager_;

	std::shared_ptr<MidiClocker> clocker_; // To determine the BPM from the Midi Clock signals
};
