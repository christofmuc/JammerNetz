#pragma once

#include "JuceHeader.h"


class MidiDeviceSelector : public Component
{
public:
	MidiDeviceSelector();

	void refreshList();

	virtual void resized() override;

private:
	Viewport scrollList_;
	Component scrollArea_;
	OwnedArray<ToggleButton> deviceSelectors_;
};
