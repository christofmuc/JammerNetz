/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "JammerNetzPackage.h"

#include <atomic>

enum class RecordingType {
	WAV,
	FLAC,
	AIFF
};

class Recorder {
public:
	Recorder(File directory, std::string const &baseFileName, RecordingType recordingType);
	~Recorder();

	void setRecording(bool recordOn);
	bool isRecording() const;
	uint64_t recordingGeneration() const noexcept;

	RelativeTime getElapsedTime() const;
	String getFilename() const;
	File getFile() const;

	void setChannelInfo(int sampleRate, JammerNetzChannelSetup const &channelSetup);

	void saveBlock(const float* const* data, int numSamples);

	File getDirectory() const;
	void setDirectory(File &directory);

private:
	bool updateChannelInfo(int sampleRate, JammerNetzChannelSetup const &channelSetup);
	void launchWriter();

	Time startTime_;
	uint64 samplesWritten_;
	File activeFile_;
	File directory_;
	std::string baseFileName_;
	RecordingType recordingType_;
	AudioFormatWriter *writer_;
	std::unique_ptr<TimeSliceThread> thread_;
	std::unique_ptr<AudioFormatWriter::ThreadedWriter> writeThread_;
	std::atomic<bool> recording_ { false };
	std::atomic<uint64_t> recordingGeneration_ { 0 };
	uint64_t nextRecordingGeneration_ { 0 };

	int lastSampleRate_;
	JammerNetzChannelSetup lastChannelSetup_;
	mutable CriticalSection stateLock_;
};
