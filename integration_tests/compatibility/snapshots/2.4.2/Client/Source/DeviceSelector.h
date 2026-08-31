/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "ApplicationState.h"

class DeviceSelector : public Component
{
public:
	DeviceSelector(String const &title, bool showTitle, bool inputInsteadOfOutputDevices);
	virtual ~DeviceSelector() override;

	virtual void resized() override;

private:
	void bindControls();

	Label titleLabel_;
	bool showTitle_;
	ComboBox typeDropdown_;
	ComboBox deviceDropdown_;
	Viewport scrollList_;
	Component scrollArea_;

	std::unique_ptr<TextButton> controlPanelButton_;
	OwnedArray<ToggleButton> channelSelectors_;
	OwnedArray<Label> channelNames_;
	TextButton startStopButton_;

	String title_;
	bool inputDevices_;

	// Generic listeners, required to maintain the lifetime of the Values and their listeners
	std::vector<std::unique_ptr<ValueListener>> listeners_;
};
