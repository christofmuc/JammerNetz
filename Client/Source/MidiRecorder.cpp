/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "MidiRecorder.h"

#include "Settings.h"
#include "StreamLogger.h"

MidiRecorder::MidiRecorder(AudioDeviceManager &deviceManager) : deviceManager_(deviceManager), isRecording_(false)
{
	// Just enable *all* Midi devices. Not sure if that's smart in the long run, but hey...
	auto devices = MidiInput::getDevices();
	for (int i = 0; i < devices.size(); i++) {
		enableMidiInput(devices[i]);
	}
	clocker_ = std::make_shared<MidiClocker>();
}

void MidiRecorder::saveToFile(String filename)
{
	Time now = Time::getCurrentTime();
	File directory = Settings::instance().getSessionStorageDir();
	File midFile = directory.getNonexistentChildFile(String(filename) + now.formatted("-%Y-%m-%d-%H-%M-%S"), ".mid", false);
	MidiFile midiFile;
	for (auto track : recorded_) {
		if (track.second.getNumEvents() > 0) {
			track.second.updateMatchedPairs();
			midiFile.addTrack(track.second);
		}
	}
	FileOutputStream outputStream(midFile, 16384);
	//midiFile.setSmpteTimeFormat(25, 400); // Tenths of milliseconds at 25 fps
	midiFile.setTicksPerQuarterNote(96);
	midiFile.writeTo(outputStream);
}

std::weak_ptr<MidiClocker>  MidiRecorder::getClocker()
{
	return clocker_;
}

MidiRecorder::~MidiRecorder() {
	// Remove all registered callbacks
	for (auto callback : callbacks_) {
		deviceManager_.removeMidiInputCallback(callback.first, callback.second);
	}
	saveToFile("sessionMidi");
}

void MidiRecorder::startRecording()
{
	recordingStartTime_ = Time::getMillisecondCounterHiRes() / 1000.0;
	isRecording_ = true;
}

void MidiRecorder::handleIncomingMidiMessage(MidiInput* source, const MidiMessage& message)
{
	if (!message.isActiveSense() && !message.isMidiClock()) {
		Time now = Time::getCurrentTime();
		StreamLogger::instance() << message.getTimeStamp() << " " << message.getDescription() << std::endl;
		MidiMessage relativeTimestampMessage = message;
		double deltaSeconds = message.getTimeStamp() - recordingStartTime_;
		double deltaTicksPerBeat = deltaSeconds * 120 * 96 / 60; // 96 Ticks per quarter note at 120 bpm
		relativeTimestampMessage.setTimeStamp(deltaTicksPerBeat);
		recorded_[source->getName()].addEvent(relativeTimestampMessage);
	}
	else if (message.isMidiClock()) {
		if (clocker_) {
			clocker_->processClockMessage(source->getName(), message);
		}
	}
}

void MidiRecorder::handlePartialSysexMessage(MidiInput* source, const uint8* messageData, int numBytesSoFar, double timestamp)
{
	 // What to do with this one?
	ignoreUnused(source, messageData, numBytesSoFar, timestamp);
}

void MidiRecorder::enableMidiInput(String name)
{
	// Only enable and register once
	if (!deviceManager_.isMidiInputEnabled(name)) {
		deviceManager_.setMidiInputEnabled(name, true);
	}
	if (callbacks_.find(name) == callbacks_.end()) {
		deviceManager_.addMidiInputCallback(name, this);
		callbacks_[name] = this;
	}
	if (recorded_.find(name) == recorded_.end()) {
		recorded_[name] = MidiMessageSequence();
	}
}

void MidiRecorder::disableMidiInput(String input) {
	if (deviceManager_.isMidiInputEnabled(input)) {
		deviceManager_.setMidiInputEnabled(input, false);
	}
	if (callbacks_.find(input) == callbacks_.end()) {
		deviceManager_.removeMidiInputCallback(input, callbacks_[input]);
		callbacks_.erase(input);
	}
}
