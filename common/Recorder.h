/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "JammerNetzPackage.h"

enum class RecordingType {
	WAV,
	FLAC,
	AIFF
};

class Recorder {
public: 
	Recorder(File directory, std::string const &baseFileName, RecordingType recordingType);
	~Recorder();

	bool isRecording() const;
	void updateChannelInfo(int sampleRate, JammerNetzChannelSetup const &channelSetup);
	void saveBlock(const float* const* data, int numSamples);

private:
	File directory_;
	std::string baseFileName_;
	RecordingType recordingType_;
	AudioFormatWriter *writer_;
	std::unique_ptr<TimeSliceThread> thread_;
	std::unique_ptr<AudioFormatWriter::ThreadedWriter> writeThread_;
};
