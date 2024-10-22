/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "Recorder.h"

class RecordingInfo : public Component, private TextButton::Listener {
public:
	RecordingInfo(std::weak_ptr<Recorder> recorder, String explanation);
	virtual ~RecordingInfo() override;

	virtual void resized() override;

	virtual void buttonClicked(Button*) override;

private:
	class UpdateTimer;
	void updateData();

	std::weak_ptr<Recorder> recorder_;
	std::unique_ptr<UpdateTimer> timer_;

	Label explanationText_;
	Label recordingFileName_;
	Label recordingPath_;
	TextButton reveal_;
	TextButton browse_;
	ImageButton recording_;
	Label recordingTime_;
	Label freeDiskSpace_;
	double diskSpacePercentage_;
	ProgressBar diskSpace_;
};
