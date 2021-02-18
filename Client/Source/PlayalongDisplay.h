/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "MidiPlayAlong.h"

class PlayalongDisplay : public Component, private Button::Listener, private Timer {
public:
	PlayalongDisplay(MidiPlayAlong *playalong);
	virtual ~PlayalongDisplay();

	virtual void resized() override;

private:
	virtual void buttonClicked(Button*) override;
	virtual void timerCallback() override;

	Label textToDisplay_;
	TextButton startStop_;

	MidiPlayAlong *playalong_;
};
