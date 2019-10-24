/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "PlayalongDisplay.h"

#include "LayoutConstants.h"

PlayalongDisplay::PlayalongDisplay(MidiPlayAlong &playalong) : playalong_(playalong)
{
	textToDisplay_.setText("None", dontSendNotification);
	addAndMakeVisible(textToDisplay_);
	startStop_.setButtonText("Start");
	addAndMakeVisible(startStop_);
	startStop_.addListener(this);

	startTimer(20);
}

PlayalongDisplay::~PlayalongDisplay()
{
	stopTimer();
}

void PlayalongDisplay::resized()
{
	auto area = getLocalBounds();

	textToDisplay_.setBounds(area.removeFromTop(kLineHeight));
	startStop_.setBounds(area.removeFromTop(kLineHeight).reduced(kSmallInset));
}

void PlayalongDisplay::timerCallback()
{
	auto newText = playalong_.karaoke();
	if (newText != textToDisplay_.getText()) {
		textToDisplay_.setText(newText, dontSendNotification);
	}
}

void PlayalongDisplay::buttonClicked(Button *button)
{
	if (button == &startStop_) {
		if (playalong_.isPlaying()) {
			playalong_.stop();
			startStop_.setButtonText("Start");
		}
		else {
			playalong_.start();
			startStop_.setButtonText("Stop");
		}
	}
}
