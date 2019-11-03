/*a
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "RecordingInfo.h"

#include "LayoutConstants.h"

#include <cmath>

// https://stackoverflow.com/questions/3758606/how-to-convert-byte-size-into-human-readable-format-in-java
String humanReadableByteCount(int64 bytes, bool si) {
	if (bytes < 0) return "";

	int64 unit = si ? 1000 : 1024;
	if (bytes < unit) return bytes + " B";
	auto exp = (int64) std::floor(std::log(bytes) / std::log(unit));
	String pre = String(si ? "kMGTPE" : "KMGTPE")[exp - 1] + String(si ? "" : "i");
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

RecordingInfo::RecordingInfo(std::weak_ptr<Recorder> recorder) : recorder_(recorder), diskSpace_(diskSpacePercentage_), diskSpacePercentage_(0.0)
{
	browse_.setButtonText("Browse");
	browse_.addListener(this);
	freeDiskSpace_.setText("Free disk space", dontSendNotification);

	PNGImageFormat reader;
	MemoryInputStream image(BinaryData::live_png, BinaryData::live_pngSize, false);
	auto im = reader.decodeImage(image);
	recording_.setClickingTogglesState(true);
	recording_.setImages(false, true, false, im, 1.0f, Colours::transparentWhite, im, 1.0f, Colours::transparentWhite, im, .2f, Colours::transparentBlack);
	recording_.addListener(this);

	recordingTime_.setJustificationType(Justification::centred);

	addAndMakeVisible(recordingPath_);
	addAndMakeVisible(browse_);
	addAndMakeVisible(recording_);
	addAndMakeVisible(recordingTime_);
	addAndMakeVisible(freeDiskSpace_);
	addAndMakeVisible(diskSpace_);

	timer_ = std::make_unique<UpdateTimer>(this);
	timer_->startTimerHz(5);
}

RecordingInfo::~RecordingInfo() = default;

void RecordingInfo::resized()
{
	auto area = getLocalBounds();

	auto row2 = area.removeFromTop(kLineSpacing + kNormalInset);
	recording_.setBounds(row2.withSizeKeepingCentre(kLabelWidth, kLineHeight + kNormalInset));
	recordingTime_.setBounds(area.removeFromTop(kLineSpacing));

	auto row3 = area.removeFromTop(kLineSpacing);
	freeDiskSpace_.setBounds(row3.removeFromLeft(kLabelWidth));
	diskSpace_.setBounds(row3.withSizeKeepingCentre(row3.getWidth() - 2 * kNormalInset, kLineHeight));

	auto row1 = area.removeFromTop(kLineSpacing);
	browse_.setBounds(row1.removeFromLeft(kLabelWidth).withHeight(kLineHeight));
	recordingPath_.setBounds(row1.withTrimmedLeft(kNormalInset));
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

		auto elapsed = Time::getCurrentTime() - recorder_.lock()->getStartTime();
		recordingTime_.setText(elapsed.getDescription(), dontSendNotification);
		recordingTime_.setVisible(isLive && elapsed.inSeconds() > 1);
	}
	
}
