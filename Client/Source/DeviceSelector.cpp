/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "DeviceSelector.h"

#include "StreamLogger.h"
#include "Settings.h"
#include "AudioDeviceDiscovery.h"
#include "ServerInfo.h"
#include "Data.h"
#include "BuffersConfig.h"

#include "LayoutConstants.h"

DeviceSelector::DeviceSelector(String const& title, bool showTitle, bool inputInsteadOfOutputDevices)
	: title_(title), showTitle_(showTitle), inputDevices_(inputInsteadOfOutputDevices)
{
	titleLabel_.setText(title, dontSendNotification);

	typeDropdown_.setTextWhenNoChoicesAvailable("No supported device types on this machine");
	typeDropdown_.setTextWhenNothingSelected("Please select audio device type to open...");
	// Listen to the Value representing the list of available Audio Device types (usually this shouldn't be changing during runtime...)
	auto& ephemeralTree = Data::instance().getEphemeral();
	auto listener = std::make_unique<ValueListener>(ephemeralTree.getPropertyAsValue(EPHEMERAL_VALUE_DEVICE_TYPES_AVAILABLE, nullptr), [this](juce::Value& newValue) {
		var dt = newValue;
		auto a = dt.getArray();
		int index = 1;
		for (auto deviceType = a->begin(); deviceType != a->end(); deviceType++) {
			typeDropdown_.addItem((*deviceType).toString(), index++);
		}
	});
	// Make sure to execute the update at least once
	listener->triggerOnChanged();
	auto setupDefinition = Data::instance().get().getOrCreateChildWithName(title, nullptr);
	listeners_.push_back(std::make_unique<ValueListener>(Data::instance().get().getPropertyAsValue(VALUE_DEVICE_TYPE, nullptr), [this](Value& newValue) {
		deviceDropdown_.clear();
		String typeName = newValue.getValue();
		auto selectedType = AudioDeviceDiscovery::deviceTypeByName(typeName);
		if (selectedType) {
			// Refill the device dropdown
			selectedType->scanForDevices();
			//TODO When we allow other sample rates or buffer sizes, enable more devices
			StringArray items;
			for (auto device : selectedType->getDeviceNames(inputDevices_)) {
#ifdef CHECK_SAMPLE_RATES
				if (AudioDeviceDiscovery::canDeviceDoSampleRate(selectedType, device, inputDevices_, SAMPLE_RATE)) {
#endif
					items.add(device);
#ifdef CHECK_SAMPLE_RATES
				}
				else {
					items.add(device + " (unsupported)");
				}
#endif
			}
			deviceDropdown_.addItemList(items, 1);
		}
	}));
	listeners_.push_back(std::make_unique<ValueListener>(setupDefinition.getPropertyAsValue(VALUE_DEVICE_NAME, nullptr), [this](Value& newValue) {
		ValueTree deviceSelector = Data::instance().get().getOrCreateChildWithName(Identifier(titleLabel_.getText(false)), nullptr);
		channelSelectors_.clear(true);
		channelNames_.clear(true);
		controlPanelButton_.reset();
		String typeName = Data::instance().get().getProperty(VALUE_DEVICE_TYPE, "");
		auto selectedType = AudioDeviceDiscovery::deviceTypeByName(typeName);
		jassert(selectedType);
		if (selectedType) {
			String name = newValue.getValue();
			if (name.isNotEmpty()) {
				std::shared_ptr<AudioIODevice> selectedDevice;
				if (inputDevices_) {
					selectedDevice.reset(selectedType->createDevice("", name));
				}
				else {
					selectedDevice.reset(selectedType->createDevice(name, ""));
				}
				if (selectedDevice) {
					// Does it have a control panel?
					if (selectedDevice->hasControlPanel()) {
						controlPanelButton_ = std::make_unique<TextButton>("Configure");
						controlPanelButton_->onClick = [this, selectedDevice]() {
							selectedDevice->showControlPanel();
						};
						addAndMakeVisible(*controlPanelButton_);
					}

					auto channels = inputDevices_ ? selectedDevice->getInputChannelNames() : selectedDevice->getOutputChannelNames();
					ValueTree channelProps = deviceSelector.getOrCreateChildWithName(VALUE_CHANNELS, nullptr);
					jassert(channelProps.isAChildOf(deviceSelector));
					//channelProps.removeAllChildren(nullptr);
					channelProps.setProperty(VALUE_CHANNEL_COUNT, channels.size(), nullptr);
					int index = 0;
					for (auto channel : channels) {
						ToggleButton* channelButton = new ToggleButton();
						channelButton->setButtonText(channel);
						String channelPropName = "Channel" + String(index++);
						auto channelProp = channelProps.getOrCreateChildWithName(channelPropName, nullptr);
						if (!channelProp.hasProperty(VALUE_CHANNEL_NAME)) {
							channelProp.setProperty(VALUE_CHANNEL_NAME, channel, nullptr);
						}
						if (!channelProp.hasProperty(VALUE_CHANNEL_ACTIVE)) {
							channelProp.setProperty(VALUE_CHANNEL_ACTIVE, false, nullptr);
						}
						channelButton->getToggleStateValue().referTo(channelProp.getPropertyAsValue(VALUE_CHANNEL_ACTIVE, nullptr));
						channelButton->onClick = [this]() {
							Data::instance().get().sendPropertyChangeMessage(title_);
						};
						scrollArea_.addAndMakeVisible(channelButton);
						channelSelectors_.add(channelButton);
						//Label *channelName = new Label("", "unnamed");
						//channelName->setEditable(true);
						//addAndMakeVisible(channelName);
						//channelNames_.add(channelName);
						//channelName->addListener(this);
					}
				}
			}
		}
		resized();
	}));

	deviceDropdown_.setTextWhenNoChoicesAvailable("No compatible devices of this type");
	deviceDropdown_.setTextWhenNothingSelected("Please select audio device to open...");
	if (showTitle_) {
		addAndMakeVisible(titleLabel_);
	}
	addAndMakeVisible(typeDropdown_);
	addAndMakeVisible(deviceDropdown_);
	scrollList_.setViewedComponent(&scrollArea_);
	scrollList_.setScrollBarsShown(true, false);
	addAndMakeVisible(scrollList_);

	startStopButton_.setClickingTogglesState(true);
	addAndMakeVisible(startStopButton_);
	Data::instance().getEphemeral().getOrCreateChildWithName(EPHEMERAL_VALUE_AUDIO_RUNNING, nullptr);
	listeners_.push_back(std::make_unique<ValueListener>(Data::instance().getEphemeral().getPropertyAsValue(EPHEMERAL_VALUE_AUDIO_RUNNING, nullptr),
		[this](Value& newValue) {
			bool audioRunning = newValue.getValue().operator bool();
			startStopButton_.setToggleState(audioRunning, dontSendNotification);
		    startStopButton_.setButtonText(audioRunning ? "Stop audio" : "Start audio");

	}));
	startStopButton_.onClick = []() {
		// Flip the switch
		bool isRunning = Data::instance().getEphemeral().getProperty(EPHEMERAL_VALUE_AUDIO_RUNNING);
		Data::instance().getEphemeral().setProperty(EPHEMERAL_VALUE_AUDIO_SHOULD_RUN, !isRunning, nullptr);
	};

	bindControls();

	// After construction, we need to fire each update trigger to make sure the combo boxes are filled
	MessageManager::callAsync([this]() {
		for_each(listeners_.begin(), listeners_.end(), [](std::unique_ptr<ValueListener>& ptr) { ptr->triggerOnChanged();  });
	});
}

DeviceSelector::~DeviceSelector()
{
}

void DeviceSelector::resized()
{
	auto area = getLocalBounds().reduced(kNormalInset);
	if (showTitle_) {
		titleLabel_.setBounds(area.removeFromTop(30).withSizeKeepingCentre(80, 30));
	}
	typeDropdown_.setBounds(area.removeFromTop(kLineHeight));
	deviceDropdown_.setBounds(area.removeFromTop(kLineHeight));
	if (controlPanelButton_) {
		controlPanelButton_->setBounds(area.removeFromTop(kLineSpacing).withTrimmedTop(kNormalInset).withSizeKeepingCentre(80, kLineHeight));
	}
	auto bottomRow = area.removeFromBottom(kLineHeight);
	startStopButton_.setBounds(bottomRow.withSizeKeepingCentre(80, kLineHeight));
	scrollList_.setBounds(area.withTrimmedTop(kNormalInset));

	scrollArea_.setSize(area.getWidth(), channelSelectors_.size() * kLineHeight);
	auto listArea = scrollArea_.getLocalBounds();
	for (int i = 0; i < channelSelectors_.size(); i++) {
		auto row = listArea.removeFromTop(kLineHeight);
		channelSelectors_[i]->setBounds(row.removeFromLeft(row.getWidth() / 2));
		if (channelNames_[i]) channelNames_[i]->setBounds(row);
	}
}

/*juce::AudioIODeviceType * DeviceSelector::deviceType() const
{
	return deviceTypes_[typeDropdown_.getSelectedItemIndex()];
}*/

bool comboHasText(ComboBox* combo, String text) {
	for (int i = 0; i < combo->getNumItems(); i++) {
		if (combo->getItemText(i) == text)
			return true;
	}
	return false;
}

void DeviceSelector::bindControls()
{
	// Better default device type implementation
	String defaultType;
#ifdef _WIN32
	defaultType = comboHasText(&typeDropdown_, "ASIO") ? "ASIO" : "";
#elif __APPLE__
	defaultType = comboHasText(&typeDropdown_, "CoreAudio") ? "CoreAudio" : "";
#else
	defaultType = comboHasText(&typeDropdown_, "JACK") ? "JACK" : "";
#endif

	ValueTree& data = Data::instance().get();
	ValueTree deviceSelector = data.getOrCreateChildWithName(Identifier(titleLabel_.getText(false)), nullptr);
	if (!Data::instance().get().hasProperty(VALUE_DEVICE_TYPE)) {
		Data::instance().get().setProperty(VALUE_DEVICE_TYPE, defaultType, nullptr);
	}
	typeDropdown_.onChange = [this]() {
		Data::instance().get().setProperty(VALUE_DEVICE_TYPE, typeDropdown_.getText(), nullptr);
	};
	listeners_.push_back(std::make_unique<ValueListener>(Data::instance().get().getPropertyAsValue(VALUE_DEVICE_TYPE, nullptr), [this](Value& newValue) {
		String newType = newValue.getValue();
		for (int i = 0; i < typeDropdown_.getNumItems(); i++) {
			if (typeDropdown_.getItemText(i) == newType) {
				typeDropdown_.setSelectedItemIndex(i, dontSendNotification);
				return;
			}
		}
		typeDropdown_.setSelectedId(0, dontSendNotification);
	}));

	if (!deviceSelector.hasProperty(VALUE_DEVICE_NAME)) {
		deviceSelector.setProperty(VALUE_DEVICE_NAME, "", nullptr);
	}
	deviceDropdown_.onChange = [this]() {
		ValueTree deviceSelector = Data::instance().get().getChildWithName(Identifier(titleLabel_.getText(false)));
		deviceSelector.setProperty(VALUE_DEVICE_NAME, deviceDropdown_.getText(), nullptr);
	};
	listeners_.push_back(std::make_unique<ValueListener>(deviceSelector.getPropertyAsValue(VALUE_DEVICE_NAME, nullptr), [this](Value& newValue) {
		String newType = newValue.getValue();
		for (int i = 0; i < deviceDropdown_.getNumItems(); i++) {
			if (deviceDropdown_.getItemText(i) == newType) {
				deviceDropdown_.setSelectedItemIndex(i, dontSendNotification);
				return;
			}
		}
		deviceDropdown_.setSelectedId(0, dontSendNotification);
	}));
}

/*void DeviceSelector::toData() const
{
	var setupObject(new DynamicObject());
	ValueTree &data = Data::instance().get();
	ValueTree deviceSelector = data.getOrCreateChildWithName(Identifier(titleLabel_.getText(false)), nullptr);
	deviceSelector.setProperty("Type", typeDropdown_.getText(), nullptr);
	deviceSelector.setProperty("Device", deviceDropdown_.getItemText(deviceDropdown_.getSelectedItemIndex()), nullptr);
	ValueTree channels = deviceSelector.getOrCreateChildWithName("Channels", nullptr);
	channels.removeAllChildren(nullptr);
	for (int i = 0; i < channelSelectors_.size(); i++) {
		ValueTree obj("Channel");
		obj.setProperty("Name", channelSelectors_[i]->getButtonText(), nullptr);
		obj.setProperty("Active", channelSelectors_[i]->getToggleState(), nullptr);
		obj.setProperty("Index", String(i), nullptr);
		channels.appendChild(obj, nullptr);
	}
}*/
