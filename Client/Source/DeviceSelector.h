/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

struct ChannelSetup {
	std::string typeName;
	std::string device;
	bool isInputAndOutput; // Flag that this device is input and output and the same time -> if this is the case, the output can be bound to the input
	std::vector<std::string> activeChannelNames;
	std::vector<int> activeChannelIndices;
};

class DeviceSelector : public Component,
	private ComboBox::Listener,
	private ToggleButton::Listener
{
public:
	DeviceSelector(String const &title, bool showTitle, AudioDeviceManager &manager, bool inputInsteadOfOutputDevices, std::function<void(std::shared_ptr<ChannelSetup>)> updateHandler);
	virtual ~DeviceSelector();

	virtual void resized() override;

	AudioIODeviceType *deviceType() const;

	// Store to and load from settings
	void fromData();
	void toData() const;

	// Implement UI logic
	virtual void comboBoxChanged(ComboBox* comboBoxThatHasChanged) override;
	virtual void buttonClicked(Button*) override;

private:
	std::function<void(std::shared_ptr<ChannelSetup>)> updateHandler_;

	Label titleLabel_;
	bool showTitle_;
	ComboBox typeDropdown_;
	ComboBox deviceDropdown_;
	AudioDeviceManager &manager_;
	Viewport scrollList_;
	Component scrollArea_;
	OwnedArray<AudioIODeviceType> deviceTypes_;
	std::unique_ptr<TextButton> controlPanelButton_;
	OwnedArray<ToggleButton> channelSelectors_;
	OwnedArray<Label> channelNames_;

	String title_;
	bool inputDevices_;
};