/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include <deque>

class MidiClocker {
public:
	const size_t kNumAverageClockTicks = 24; // One quarter note average

	MidiClocker();

	int getCurrentBPM();

	void processClockMessage(String const & midiSource, MidiMessage const &message);

private:
	std::map<String, std::deque<double>> clockTimes_;
};
