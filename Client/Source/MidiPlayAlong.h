/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

class MidiPlayAlong {
public:
	MidiPlayAlong(String fileName);

	bool isPlaying() const;
	void start();
	void stop();
	String karaoke() const;

	void loadNewPlayalongFile(String fileName);

	void fillNextMidiBuffer(std::vector<MidiMessage> &destBuffer, int numSamples);

private:
	// Use this to collect all text messages from a MIDI file
	MidiFile midiFile_;
	MidiMessageSequence collector_;
	double startTime_;
	bool isPlaying_;

	String karaoke_;
};
