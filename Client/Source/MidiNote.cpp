/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "MidiNote.h"

#include <cmath>

MidiNote::MidiNote(double frequency, double frequencyA4 /* = 440.0 */) : frequency_(frequency), frequencyA4_(frequencyA4)
{
	midiNote_ = frequencyToNoteNumber(frequency, frequencyA4_);
}

MidiNote::MidiNote(int noteNumber, double frequencyA4 /* = 440.0 */) : midiNote_(noteNumber), frequencyA4_(frequencyA4)
{
	frequency_ = noteNumberToFrequency(noteNumber, frequencyA4_);
}

int MidiNote::frequencyToNoteNumber(double frequency, double frequencyA4 /* = 440.0 */)
{
	// https://en.wikipedia.org/wiki/Piano_key_frequencies
	if (frequency > 0.0) {
		return std::round(12 * std::log2(frequency / frequencyA4) + 69);
	}
	else {
		return 0;
	}
}

double MidiNote::noteNumberToFrequency(int noteNumber, double frequencyA4 /* = 440.0 */)
{
	// 69 is MIDI for A4
	return frequencyA4 * std::pow(2.0, (noteNumber - 69) / 12.0);
}

int MidiNote::noteNumber() const
{
	return midiNote_;
}

double MidiNote::frequency() const
{
	return frequency_;
}

double MidiNote::cents()
{
	// A wild ride: http://www.sengpielaudio.com/calculator-centsratio.htm
	if (frequency_ > 0.0) {
		return 1200.0 * std::log2(frequency_ / noteNumberToFrequency(midiNote_, frequencyA4_));
	}
	else {
		return 0.0;
	}
}

std::string MidiNote::name() const
{
	// Ok, JUCE can do this for us
	if (midiNote_ > 0) {
		return MidiMessage::getMidiNoteName(midiNote_, true, true, 3).toStdString();
	}
	else {
		return "-";
	}
}
