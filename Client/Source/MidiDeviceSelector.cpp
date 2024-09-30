#include "MidiDeviceSelector.h"

#include "LayoutConstants.h"

#include "MidiController.h"


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
				onSelectionChanged(selectedOutputDevices());
			}
		};
	}
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
