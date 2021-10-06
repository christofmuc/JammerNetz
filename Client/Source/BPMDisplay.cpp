/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "BPMDisplay.h"

class BPMTimer : public Timer {
public:
	BPMTimer(Label &label, std::weak_ptr<MidiClocker> clocker) : label_(label), clocker_(clocker) {};

	virtual void timerCallback() override
	{
		if (!clocker_.expired()) {
			label_.setText(String(clocker_.lock()->getCurrentBPM()) + String(" bpm"), dontSendNotification);
		}
	}

private:
	Label &label_;
	std::weak_ptr<MidiClocker> clocker_;
};

BPMDisplay::BPMDisplay(std::weak_ptr<MidiClocker> clocker)
{
	addAndMakeVisible(bpmText_);
	timer_ = std::make_unique<BPMTimer>(bpmText_, clocker);
	timer_->startTimerHz(5);
}

void BPMDisplay::resized()
{
	bpmText_.setBounds(getLocalBounds());
}
