/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ChannelController.h"

#include "Data.h"

ChannelController::ChannelController(String const &name, String const &id, std::function<void(double, JammerNetzChannelTarget)> updateHandler, bool hasVolume /*= true*/, bool hasTarget /*= true*/) :
	id_(id), updateHandler_(updateHandler), levelMeter_(name == "Master" ? FFAU::LevelMeter::Default : FFAU::LevelMeter::SingleChannel), hasVolume_(hasVolume), hasTarget_(hasTarget)
{
	channelName_.setText(name, dontSendNotification);
	if (hasVolume) {
		volumeSlider_.setSliderStyle(Slider::LinearVertical);
		volumeSlider_.setTextBoxStyle(Slider::TextBoxBelow, true, 80, 30);
		volumeSlider_.setRange(Range<double>(0.0, 100.0), 0.0);
		volumeSlider_.setTextValueSuffix("%");
		volumeSlider_.setValue(100.0, dontSendNotification);
		volumeSlider_.setNumDecimalPlacesToDisplay(0);
		volumeSlider_.addListener(this);
		addAndMakeVisible(volumeSlider_);
	}

	if (hasTarget) {
		channelType_.addItem("Off", JammerNetzChannelTarget::Unused + 1);
		channelType_.addItem("Left", JammerNetzChannelTarget::Left + 1);
		channelType_.addItem("Right", JammerNetzChannelTarget::Right + 1);
		channelType_.addItem("Mono", JammerNetzChannelTarget::Mono + 1);
		channelType_.addItem("Send", JammerNetzChannelTarget::SendOnly + 1);
		channelType_.setText("Mono");
		channelType_.addListener(this);
		addAndMakeVisible(channelType_);
	}

	lnf_ = new FFAU::LevelMeterLookAndFeel();
	lnf_->setColour(FFAU::LevelMeter::lmOutlineColour, juce::Colours::lightblue);
	lnf_->setColour(FFAU::LevelMeter::lmTicksColour, juce::Colours::lightblue);
	levelMeter_.setLookAndFeel(lnf_);

	addAndMakeVisible(channelName_);
	addAndMakeVisible(levelMeter_);
	//addAndMakeVisible(muteButton_);
}

void ChannelController::resized()
{
	auto area = getLocalBounds();
	channelName_.setBounds(area.removeFromTop(30));
	auto top80 = area.removeFromTop(area.getHeight() * 4 / 5);
	if (hasVolume_) {
		volumeSlider_.setBounds(top80.removeFromRight(top80.getWidth() / 2));
	}
	levelMeter_.setBounds(top80);
	if (hasTarget_) {
		channelType_.setBounds(area.withSizeKeepingCentre(80, 30));
	}
	//muteButton_.setBounds(area);
}

void ChannelController::setMeterSource(FFAU::LevelMeterSource *meterSource, int channelNo)
{
	levelMeter_.setMeterSource(meterSource);
	if (channelNo != -1) {
		levelMeter_.setSelectedChannel(channelNo);
	}
}

JammerNetzChannelTarget ChannelController::getCurrentTarget() const
{
	JammerNetzChannelTarget target;
	switch (channelType_.getSelectedItemIndex()) {
	case 0: target = Unused; break;
	case 1: target = Left; break;
	case 2: target = Right; break;
	case 3: target = Mono; break;
	case 4: target = SendOnly; break;
	default:
		jassert(false);
		target = Unused;
	}
	return target;
}

float ChannelController::getCurrentVolume() const
{
	jassert(hasVolume_);
	return ((float) volumeSlider_.getValue()) / 100.0f;
}

void ChannelController::fromData()
{
	ValueTree &data = Data::instance().get();
	auto channelSettings = data.getChildWithName(id_);
	if (channelSettings.isValid()) {
		if (channelSettings.hasProperty("Volume")) {
			volumeSlider_.setValue(channelSettings.getProperty("Volume").operator float() * 100.0f, dontSendNotification);
		}
		if (channelSettings.hasProperty("Target")) {
			channelType_.setSelectedId(channelSettings.getProperty("Target"), dontSendNotification);
		}
	}
}

void ChannelController::toData() const
{
	ValueTree &data = Data::instance().get();
	auto channelSettings = data.getOrCreateChildWithName(id_, nullptr);
	channelSettings.setProperty("Volume", volumeSlider_.getValue() / 100.0f, nullptr);
	channelSettings.setProperty("Target", channelType_.getSelectedId(), nullptr);
}

void ChannelController::comboBoxChanged(ComboBox* comboBoxThatHasChanged)
{
	jassert(hasTarget_);
	updateHandler_(volumeSlider_.getValue(), getCurrentTarget());
}

void ChannelController::sliderValueChanged(Slider* slider)
{
	jassert(hasVolume_);
	updateHandler_(slider->getValue(), getCurrentTarget());
}
