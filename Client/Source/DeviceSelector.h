/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

struct ChannelSetup {
	std::weak_ptr<AudioIODevice> device;
	std::vector<std::string> activeChannelNames;
	std::vector<int> activeChannelIndices;
};

class DeviceSelector : public Component,
	private ComboBox::Listener,
	private ToggleButton::Listener
{
public:
	DeviceSelector(String const &title, bool showTitle, String const &settingsKey, AudioDeviceManager &manager, bool inputInsteadOfOutputDevices, std::function<void(std::shared_ptr<ChannelSetup>)> updateHandler);
	virtual ~DeviceSelector();

	virtual void resized() override;

	std::shared_ptr<AudioIODevice> currentDevice();

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
	OwnedArray<AudioIODeviceType> deviceTypes_;
	OwnedArray<ToggleButton> channelSelectors_;
	OwnedArray<Label> channelNames_;

	std::shared_ptr<AudioIODevice> currentDevice_;
	String title_;
	String settingsKey_;
	bool inputDevices_;
};