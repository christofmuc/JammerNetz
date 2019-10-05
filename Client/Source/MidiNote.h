/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

class MidiNote {
public:
	MidiNote(double frequency, double frequencyA4 = 440.0);
	MidiNote(int noteNumber, double frequencyA4 = 440.0);

	static int frequencyToNoteNumber(double frequency, double frequencyA4 = 440.0);
	static double noteNumberToFrequency(int noteNumber, double frequencyA4 = 440.0);

	int noteNumber() const;
	double frequency() const;
	double cents();

	std::string name() const;

private:
	int midiNote_;
	double frequency_;
	double frequencyA4_;
};
