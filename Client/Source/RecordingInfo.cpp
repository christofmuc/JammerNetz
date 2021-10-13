/*a
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "RecordingInfo.h"

#include "LayoutConstants.h"
#include "Resources.h"

#include <cmath>

// https://stackoverflow.com/questions/3758606/how-to-convert-byte-size-into-human-readable-format-in-java
//
String humanReadableByteCount(size_t bytes, bool si) {
	size_t unit = si ? 1000 : 1024;
	if (bytes < unit) {
		return String(bytes) + " B";
	}
	size_t exp = (size_t) std::floor(std::log(bytes) / std::log(unit));
	if (exp == 0) {
		jassert(false);
		return "NaN B";
	}
	String pre = String(si ? "kMGTPE" : "KMGTPE")[(int) (exp - 1)] + String(si ? "" : "i"); // Yikes, JUCE String operator[] takes a signed int as index
	std::stringstream str;
	str << std::setprecision(1) << std::fixed << bytes / std::pow(unit, exp) << " " << pre << "B";
	return str.str();
}

class RecordingInfo::UpdateTimer : public Timer {
public:
	UpdateTimer(RecordingInfo *info) : info_(info) {};

	virtual void timerCallback() override
	{
		info_->updateData();
	}

private:
	RecordingInfo *info_;
};

RecordingInfo::RecordingInfo(std::weak_ptr<Recorder> recorder, String explanation) : recorder_(recorder), diskSpace_(diskSpacePercentage_), diskSpacePercentage_(0.0)
{
	explanationText_.setText(explanation, dontSendNotification);
	addAndMakeVisible(explanationText_);

	browse_.setButtonText("Change");
	browse_.addListener(this);
	freeDiskSpace_.setText("Free disk space", dontSendNotification);

	PNGImageFormat reader;
	MemoryInputStream image(live_png, live_png_size, false);
	auto im = reader.decodeImage(image);
	recording_.setClickingTogglesState(true);
	recording_.setImages(false, true, false, im, .9f, Colours::transparentBlack, im, 1.f, Colours::transparentWhite, im, .2f, Colours::transparentBlack);
	recording_.addListener(this);

	reveal_.setButtonText("Show");
	reveal_.addListener(this);

	recordingTime_.setJustificationType(Justification::centred);
	recordingFileName_.setJustificationType(Justification::centred);

	addAndMakeVisible(recordingPath_);
	addAndMakeVisible(browse_);
	addAndMakeVisible(recording_);
	addAndMakeVisible(recordingTime_);
	addAndMakeVisible(recordingFileName_);
	addAndMakeVisible(freeDiskSpace_);
	addAndMakeVisible(diskSpace_);
	addAndMakeVisible(reveal_);

	timer_ = std::make_unique<UpdateTimer>(this);
	timer_->startTimerHz(5);
}

RecordingInfo::~RecordingInfo() = default;

void RecordingInfo::resized()
{
	auto area = getLocalBounds();

	auto row = area.removeFromTop(kLineSpacing + kNormalInset);
	recording_.setBounds(row.withSizeKeepingCentre(kLabelWidth, kLineHeight + kNormalInset));
	explanationText_.setBounds(row.removeFromLeft(recording_.getX()).withTrimmedRight(kSmallInset));
	recordingTime_.setBounds(area.removeFromTop(kLineHeight));
	row = area.removeFromTop(kLineSpacing);
	reveal_.setBounds(row.removeFromRight(kLabelWidth).withHeight(kLineHeight));
	recordingFileName_.setBounds(row.withTrimmedRight(kNormalInset));

	row = area.removeFromTop(kLineSpacing);
	freeDiskSpace_.setBounds(row.removeFromLeft(kLabelWidth));
	diskSpace_.setBounds(row.withSizeKeepingCentre(row.getWidth() - 2 * kNormalInset, kLineHeight - kSmallInset));

	row = area.removeFromTop(kLineSpacing);
	browse_.setBounds(row.removeFromRight(kLabelWidth).withHeight(kLineHeight));
	recordingPath_.setBounds(row.withTrimmedRight(kNormalInset));
}

void RecordingInfo::buttonClicked(Button *clicked)
{
	if (clicked == &browse_ && !recorder_.expired()) {
		FileChooser chooser("Please select the directory to record to...", recorder_.lock()->getDirectory());
		if (chooser.browseForDirectory()) {
			File chosen = chooser.getResult();
			recorder_.lock()->setDirectory(chosen);
		}
	}
	else if (clicked == &recording_) {
		if (!recorder_.expired()) {
			recorder_.lock()->setRecording(!recording_.getToggleState());
		}
	}
	else if (clicked == &reveal_) {
		if (!recorder_.expired()) {
			auto currentFile = recorder_.lock()->getFile();
			currentFile.revealToUser();
		}
	}
}

void RecordingInfo::updateData()
{
	if (recorder_.lock()) {
		auto dir = recorder_.lock()->getDirectory();
		recordingPath_.setText(dir.getFullPathName(), dontSendNotification);
		diskSpacePercentage_ =  1.0 - dir.getBytesFreeOnVolume() / (double)dir.getVolumeTotalSize();
		diskSpace_.setTextToDisplay(humanReadableByteCount(dir.getBytesFreeOnVolume(), false));
		bool isLive = recorder_.lock()->isRecording();
		recording_.setToggleState(!isLive, dontSendNotification);

		auto elapsed = recorder_.lock()->getElapsedTime();
		recordingTime_.setText(elapsed.getDescription(), dontSendNotification);
		recordingTime_.setVisible(isLive && elapsed.inSeconds() > 1);

		recordingFileName_.setText(recorder_.lock()->getFilename(), dontSendNotification);
		recordingFileName_.setVisible(isLive);
	}

}
