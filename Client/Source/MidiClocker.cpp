/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "MidiClocker.h"

#include "StreamLogger.h"

MidiClocker::MidiClocker()
{
}

double MidiClocker::getCurrentBPM()
{
	// Do we have at least one clock source?
	if (clockTimes_.size() == 0) {
		return -1;
	}
	if (clockTimes_.begin()->second.empty()) {
		// This shouldn't be possible
		jassert(false);
		return -1;
	}

	// Then, take the first one who sent us a clock!
	std::deque<double> &list = clockTimes_.begin()->second;
	double delta = 0.0;
	size_t count = 0;
	for (int i = 1; i < list.size(); i++) {
		delta += list[i] - list[i - 1];
		count++;
	}
	double averageSecondsPerBPM = delta / count;
	return 60.0 / averageSecondsPerBPM / 24; // MIDI Clock sends at 24 pulses per quarter note (ppqn)
}

void MidiClocker::processClockMessage(String const & midiSource, MidiMessage const &message)
{
	if (clockTimes_.find(midiSource) == clockTimes_.end()) {
		// First clock signal on this source!
		if (clockTimes_.size() > 0) {
			StreamLogger::instance() << "Warning: More than one MIDI input provides clock signals - what to do with " << midiSource << "?" << std::endl;
		}
		clockTimes_[midiSource] = std::deque<double>();
	}
	clockTimes_[midiSource].push_back(message.getTimeStamp());
	while (clockTimes_[midiSource].size() > kNumAverageClockTicks) clockTimes_[midiSource].pop_front();
}
