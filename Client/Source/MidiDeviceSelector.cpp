#include "MidiDeviceSelector.h"

#include "LayoutConstants.h"

#include "MidiController.h"
#include "Data.h"

#include "ApplicationState.h"

MidiDeviceSelector::MidiDeviceSelector()
{
	scrollList_.setViewedComponent(&scrollArea_);
	scrollList_.setScrollBarsShown(true, false);
	addAndMakeVisible(scrollList_);
}

void MidiDeviceSelector::refreshList()
{
	deviceSelectors_.clear();
	buttonToDevice_.clear();
	for (auto device : midikraft::MidiController::instance()->currentOutputs(false)) {
		auto button = new ToggleButton(device.name);
		scrollArea_.addAndMakeVisible(button);
		deviceSelectors_.add(button);
		buttonToDevice_[button] = device;
		button->onClick = [this]() {
			if (onSelectionChanged) {
				// Persist in settings structure!
				storeInSettings();
				onSelectionChanged(selectedOutputDevices());
			}
		};
	}
	loadFromSettings();
	resized();
}

std::vector<juce::MidiDeviceInfo> MidiDeviceSelector::selectedOutputDevices() const
{
	std::vector<juce::MidiDeviceInfo> result;
	for (auto selector : deviceSelectors_) {
		if (selector->getToggleState()) {
			auto found = buttonToDevice_.find(selector);
			if (found != buttonToDevice_.end()) {
				result.push_back(found->second);
			} else {
				jassertfalse;
			}
		}
	}
	return result;
}

void MidiDeviceSelector::storeInSettings()
{
	auto settings = Data::instance().get().getOrCreateChildWithName(VALUE_MIDI_CLOCK_DEVICES, nullptr);
	//settings.removeAllChildren(nullptr);
	for (auto selector : deviceSelectors_) {
		if (selector->getToggleState()) {
			auto found = buttonToDevice_.find(selector);
			if (found != buttonToDevice_.end()) {
				auto device = settings.getOrCreateChildWithName(VALUE_ENTRY, nullptr);
				device.setProperty(VALUE_DEVICE_NAME, found->second.name, nullptr);
				device.setProperty(VALUE_DEVICE_ID, found->second.identifier, nullptr); // Not used yet during load
			} else {
				jassertfalse;
			}
		}
	}
}

void MidiDeviceSelector::loadFromSettings()
{
	auto settings = Data::instance().get().getOrCreateChildWithName(VALUE_MIDI_CLOCK_DEVICES, nullptr);
	for (int i = 0; i < settings.getNumChildren(); i++) {
		auto child = settings.getChild(i);
		auto deviceName = child.getProperty(VALUE_DEVICE_NAME).toString();
		// Search for the device
		for (auto buttonEntry : buttonToDevice_) {
			if (buttonEntry.second.name == deviceName) {
				buttonEntry.first->setToggleState(true, dontSendNotification);
			}
		}
	}
}


void MidiDeviceSelector::resized()
{
	auto area = getLocalBounds().reduced(kNormalInset);
	scrollList_.setBounds(area.withTrimmedTop(kNormalInset));

	scrollArea_.setSize(area.getWidth(), deviceSelectors_.size() * kLineHeight);
	auto listArea = scrollArea_.getLocalBounds();
	for (int i = 0; i < deviceSelectors_.size(); i++) {
		auto row = listArea.removeFromTop(kLineHeight);
		deviceSelectors_[i]->setBounds(row.removeFromLeft(row.getWidth()));
	}
}
