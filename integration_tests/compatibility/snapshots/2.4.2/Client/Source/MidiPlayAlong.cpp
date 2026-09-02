/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "MidiPlayAlong.h"

#include "BuffersConfig.h"

MidiPlayAlong::MidiPlayAlong(String fileName) : isPlaying_(false)
{
	loadNewPlayalongFile(fileName);
}

bool MidiPlayAlong::isPlaying() const
{
	return isPlaying_;
}

void MidiPlayAlong::start()
{
	isPlaying_ = true;
	startTime_ = Time::getMillisecondCounterHiRes() * 0.001;
}

void MidiPlayAlong::stop()
{
	isPlaying_ = false;
}

juce::String MidiPlayAlong::karaoke() const
{
	return karaoke_;
}

void MidiPlayAlong::loadNewPlayalongFile(String fileName)
{
	File physicalFile(fileName);
	if (physicalFile.existsAsFile()) {
		FileInputStream inputStream(physicalFile);
		midiFile_.clear();
		collector_.clear();
		if (midiFile_.readFrom(inputStream)) {
			midiFile_.convertTimestampTicksToSeconds();

			// Now loop over all messages in the midiFile and add them into our collector at the proper sample position!
			for (int track = 0; track < midiFile_.getNumTracks(); track++) {
				auto sequence = midiFile_.getTrack(track);
				if (sequence) {
					for (auto event : *sequence) {
						auto &message = event->message;
						if (message.isTextMetaEvent()) {
							auto text = message.getTextFromTextMetaEvent().toStdString();
							ignoreUnused(text);
							int timestamp = (int)(message.getTimeStamp() * 0.001 * SAMPLE_RATE);
							ignoreUnused(timestamp);
							collector_.addEvent(message, 0.0);
						}
					}
				}
			}
		}
		else {
			jassert(false);
		}
	}
}

void MidiPlayAlong::fillNextMidiBuffer(std::vector<MidiMessage> &destBuffer, int numSamples)
{
	if (isPlaying_) {
		double timeElapsed = Time::getMillisecondCounterHiRes() * 0.001 - startTime_;
		double fileStartTime = collector_.getStartTime();
		double endTime = timeElapsed + numSamples / (double)SAMPLE_RATE;

		int index = collector_.getNextIndexAtTime(timeElapsed - fileStartTime);
		while (index < collector_.getNumEvents()) {
			auto message = collector_.getEventPointer(index)->message;
			if (message.getTimeStamp() < endTime) {
				destBuffer.push_back(message);
				index++;
				if (message.isTextMetaEvent()) {
					// Record the last seen text meta event for our Karaoke function!
					karaoke_ = message.getTextFromTextMetaEvent();
				}
			}
			else {
				// this is newer than we need
				return;
			}
		}
	}
}
