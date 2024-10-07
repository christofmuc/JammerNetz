#pragma once

#include "JuceHeader.h"


class MidiDeviceSelector : public Component
{
public:
	MidiDeviceSelector();

	void refreshList();

	virtual void resized() override;

	std::function<void(std::vector<juce::MidiDeviceInfo>)> onSelectionChanged;

private:
	std::vector<juce::MidiDeviceInfo> selectedOutputDevices() const;
	void loadFromSettings();
	void storeInSettings();

	Viewport scrollList_;
	Component scrollArea_;
	OwnedArray<ToggleButton> deviceSelectors_;
	std::map<ToggleButton *, juce::MidiDeviceInfo> buttonToDevice_;
};
