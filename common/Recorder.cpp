/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "Recorder.h"

Recorder::Recorder(File directory, std::string const &baseFileName, RecordingType recordingType) 
	: directory_(directory), baseFileName_(baseFileName), writer_(nullptr), recordingType_(recordingType)
{
	thread_ = std::make_unique<TimeSliceThread>("RecorderDiskWriter");
	thread_->startThread();
}


Recorder::~Recorder()
{
	// Stop writing, make sure to finalize file
	writeThread_.reset();
	// Give it a second to flush and exit
	thread_->stopThread(1000);
}

void Recorder::setRecording(bool recordOn)
{
	if (isRecording()) {
		writeThread_.reset();
	}
	else {
		updateChannelInfo(lastSampleRate_, lastChannelSetup_);
	}
}

bool Recorder::isRecording() const
{
	return writeThread_ != nullptr;
}

juce::Time Recorder::getStartTime() const
{
	return startTime_;
}

juce::String Recorder::getFilename() const
{
	return fileName_;
}

void Recorder::updateChannelInfo(int sampleRate, JammerNetzChannelSetup const &channelSetup) {
	lastSampleRate_ = sampleRate;
	lastChannelSetup_ = channelSetup;

	// We have changed the channel setup - as our output files do like a varying number of channels (you need a DAW project for that)
	// let's close the current file and start a new one
	writeThread_.reset();

	// Create the audio format writer
	std::unique_ptr<AudioFormat> audioFormat;
	std::string fileExtension;
	switch (recordingType_) {
	case RecordingType::WAV: audioFormat = std::make_unique<WavAudioFormat>(); fileExtension = ".wav";  break;
	case RecordingType::FLAC: audioFormat = std::make_unique <FlacAudioFormat>(); fileExtension = ".flac"; break;
	case RecordingType::AIFF: audioFormat = std::make_unique <AiffAudioFormat>(); fileExtension = ".aiff"; break;
	}

	// Need to check that sample rate, bit depth, and channel layout are supported!
	int bitDepthRequested = 16;
	bool bitsOk = false;
	for (auto allowed : audioFormat->getPossibleBitDepths()) if (allowed == bitDepthRequested) bitsOk = true;
	if (!bitsOk) {
		jassert(false);
		std::cerr << "Error: trying to create a file with a bit depth that is not supported by the format: " << bitDepthRequested << std::endl;
		return;
	}

	bool rateOk = false;
	for (auto rate : audioFormat->getPossibleSampleRates()) if (rate == sampleRate) rateOk = true;
	if (!rateOk) {
		jassert(false);
		std::cerr << "Error: trying to create a file with a sample rate that is not supported by the format: " << sampleRate << std::endl;
		return;
	}

	// Setup the channel layout
	AudioChannelSet channels;
	int numChannels = 0;
	for (int c = 0; c < MAXCHANNELSPERCLIENT; c++) {
		switch (channelSetup.channels[c].target) {
		case Unused:
			// Ignore
			break;
		case Left:
			channels.addChannel(AudioChannelSet::left);
			numChannels++;
			break;
		case Right:
			channels.addChannel(AudioChannelSet::right);
			numChannels++;
			break;
		case SendOnly:
			// Fall through
		case Mono:
			channels.addChannel(AudioChannelSet::centre);
			numChannels++;
			break;
		}
	}

	// Check if WAV likes it
	if (!audioFormat->isChannelLayoutSupported(channels)) {
		return;
	}

	// Setup a new audio file to write to
	startTime_ = Time::getCurrentTime();
	File wavFile = directory_.getNonexistentChildFile(String(baseFileName_) + startTime_.formatted("-%Y-%m-%d-%H-%M-%S"), fileExtension, false);
	fileName_ = wavFile.getFileName();
	OutputStream * outStream = new FileOutputStream(wavFile, 16384);

	// Create the writer based on the format and file
	StringPairArray metaData;
	writer_ = audioFormat->createWriterFor(outStream, sampleRate, numChannels, bitDepthRequested, metaData, 1 /* unused by wav */);
	if (!writer_) {
		jassert(false);
		delete outStream;
		std::cerr << "Fatal: Could not create writer for Audio file, can't record to disk" << std::endl;
		return;
	}

	// Finally, create the new writer associating it with the background thread
	writeThread_ = std::make_unique<AudioFormatWriter::ThreadedWriter>(writer_, *thread_, 16384);
}

void Recorder::saveBlock(const float* const* data, int numSamples) {
	// Don't crash on me when there is surprisingly no data in the package
	if (data && data[0]) {
		if (!writeThread_->write(data, numSamples)) {
			//TODO - need a smarter strategy than that
			std::cerr << "Ups, FIFO full and can't write block to disk, lost it!" << std::endl;
		}
	}
}

juce::File Recorder::getDirectory() const
{
	return directory_;
}

void Recorder::setDirectory(File &directory)
{
	// Stop writing if any
	writeThread_.reset();
	directory_ = directory;
}
