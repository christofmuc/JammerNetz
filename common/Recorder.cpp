/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "Recorder.h"

Recorder::Recorder(File directory, std::string const &baseFileName, RecordingType recordingType)
	:
     samplesWritten_(0)
    , directory_(directory)
    , baseFileName_(baseFileName)
    , recordingType_(recordingType)
    , writer_(nullptr)
    , lastChannelSetup_(false)
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
	if (recordOn && !isRecording()) {
		updateChannelInfo(lastSampleRate_, lastChannelSetup_);
		launchWriter();
	} else if (!recordOn && isRecording()) {
		writeThread_.reset();
	}
}

bool Recorder::isRecording() const
{
	return writeThread_ != nullptr;
}

RelativeTime Recorder::getElapsedTime() const
{
	return RelativeTime(samplesWritten_ / (double) lastSampleRate_);
}

juce::String Recorder::getFilename() const
{
	return activeFile_.getFileName();
}

juce::File Recorder::getFile() const
{
	return activeFile_;
}

void Recorder::setChannelInfo(int sampleRate, JammerNetzChannelSetup const &channelSetup)
{
	lastSampleRate_ = sampleRate;
	lastChannelSetup_ = channelSetup;
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
	unsigned numChannels = 0;
	for (auto channel : channelSetup.channels) {
		switch (channel.target) {
		case Mute:
			// Ignore
			//TODO Think if Mute is really part of the recording, but empty. Because you don't want mute/unmute to restart the file recording
			break;
		case Left:
			// Fall through
		case SendLeft:
			channels.addChannel(AudioChannelSet::left);
			numChannels++;
			break;
		case Right:
			// Fall through
		case SendRight:
			channels.addChannel(AudioChannelSet::right);
			numChannels++;
			break;
		case SendMono:
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
	activeFile_ = directory_.getNonexistentChildFile(String(baseFileName_) + startTime_.formatted("-%Y-%m-%d-%H-%M-%S"), fileExtension, false);
	OutputStream * outStream = new FileOutputStream(activeFile_, 16384);

	// Create the writer based on the format and file
	StringPairArray metaData;
	writer_ = audioFormat->createWriterFor(outStream, sampleRate, numChannels, bitDepthRequested, metaData, 1 /* unused by wav */);
	if (!writer_) {
		jassert(false);
		delete outStream;
		std::cerr << "Fatal: Could not create writer for Audio file, can't record to disk" << std::endl;
		return;
	}
}

void Recorder::launchWriter() {
	// Finally, create the new writer associating it with the background thread
	writeThread_ = std::make_unique<AudioFormatWriter::ThreadedWriter>(writer_, *thread_, 16384);
	samplesWritten_ = 0;
}

void Recorder::saveBlock(const float* const* data, int numSamples) {
	// Don't crash on me when there is surprisingly no data in the package
	if (data && data[0] && numSamples > 0) {
		if (!writeThread_->write(data, numSamples)) {
			//TODO - need a smarter strategy than that
			std::cerr << "Ups, FIFO full and can't write block to disk, lost it!" << std::endl;
		}
		samplesWritten_ += (juce::uint64) numSamples;
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
