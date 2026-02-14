/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ChannelController.h"

#include "Data.h"
#include "LayoutConstants.h"

#include "ApplicationState.h"

#include <utility>

ChannelController::ChannelController(String const &name, String const &id,
	bool hasVolume /*= true*/, bool hasTarget /*= true*/, bool hasPitch /* = false */) :
		id_(id)
        , hasVolume_(hasVolume)
        , hasTarget_(hasTarget)
        , hasPitch_(hasPitch)
        , levelMeter_(name == "Master" ? FFAU::LevelMeter::Default : FFAU::LevelMeter::SingleChannel)
        , meterSource_(nullptr)
        , channelNo_(0)
		, volumeSliderDragged_(false)
{
	channelName_.setText(name, dontSendNotification);
	if (hasVolume) {
		volumeSlider_.setSliderStyle(Slider::LinearVertical);
		volumeSlider_.setTextBoxStyle(Slider::TextBoxBelow, true, 80, 30);
		volumeSlider_.setRange(Range<double>(0.0, 100.0), 0.0);
		volumeSlider_.setTextValueSuffix("%");
		volumeSlider_.setValue(100.0, dontSendNotification);
		volumeSlider_.setNumDecimalPlacesToDisplay(0);
		volumeSlider_.setChangeNotificationOnlyOnRelease(false);
		volumeSlider_.addListener(this);
		addAndMakeVisible(volumeSlider_);
	}

	if (hasTarget) {
		channelType_.addItem("Mute", JammerNetzChannelTarget::Mute + 1);
		channelType_.addItem("Left", JammerNetzChannelTarget::Left + 1);
		channelType_.addItem("Right", JammerNetzChannelTarget::Right + 1);
		channelType_.addItem("Mono", JammerNetzChannelTarget::Mono + 1);
		channelType_.addItem("Send", JammerNetzChannelTarget::SendMono + 1);
		channelType_.addItem("SendLeft", JammerNetzChannelTarget::SendLeft + 1);
		channelType_.addItem("SendRight", JammerNetzChannelTarget::SendRight + 1);
		channelType_.setText("Mono");
		channelType_.addListener(this);
		addAndMakeVisible(channelType_);
	}
	if (hasPitch_) {
		pitchLabel_.setText("-", dontSendNotification);
		pitchLabel_.setJustificationType(Justification::horizontallyCentred);
		addAndMakeVisible(pitchLabel_);
	}

	lnf_ = std::make_unique<FFAU::LevelMeterLookAndFeel>();
	lnf_->setColour(FFAU::LevelMeter::lmOutlineColour, juce::Colours::lightblue);
	lnf_->setColour(FFAU::LevelMeter::lmTicksColour, juce::Colours::lightblue);
	levelMeter_.setLookAndFeel(lnf_.get());
	/*lnf_ = std::make_unique<DSLevelMeterLAF>();
	levelMeter_.setLookAndFeel(lnf_.get());*/

	addAndMakeVisible(channelName_);
	addAndMakeVisible(levelMeter_);
	//addAndMakeVisible(muteButton_);

	bindControls();
}

void ChannelController::resized()
{
	auto area = getLocalBounds();
	channelName_.setBounds(area.removeFromTop(kLineHeight));
	int lowersection = ((hasTarget_ ? 1 : 0) + (hasPitch_ ? 1 : 0)) *  kLineSpacing;
	auto top80 = area.removeFromTop(area.getHeight() - lowersection);
	if (hasVolume_) {
		volumeSlider_.setBounds(top80.removeFromRight(top80.getWidth() / 2));
	}
	levelMeter_.setBounds(top80);
	if (hasTarget_) {
		auto line = area.removeFromTop(kLineSpacing);
		channelType_.setBounds(line.withSizeKeepingCentre(kLabelWidth, kLineHeight));
	}
	if (hasPitch_) {
		auto line = area.removeFromTop(kLineSpacing);
		pitchLabel_.setBounds(line.withSizeKeepingCentre(area.getWidth(), kLineHeight));
	}
	//muteButton_.setBounds(area);
}

void ChannelController::setMeterSource(FFAU::LevelMeterSource *meterSource, int channelNo)
{
	meterSource_ = meterSource;
	channelNo_ = channelNo;
	levelMeter_.setMeterSource(meterSource);
	if (channelNo != -1) {
		levelMeter_.setSelectedChannel(channelNo);
	}
}

void ChannelController::setVolume(float volume)
{
	volumeSlider_.setValue(volume, dontSendNotification);
}

void ChannelController::setTarget(uint8 target)
{
	channelType_.setSelectedId((int)target + 1, dontSendNotification);
}

void ChannelController::setPitchDisplayed(MidiNote note)
{
	if (note.noteNumber() != 0 && geCurrentRMSinDecible() > -40.0) {
		std::stringstream pitch;
		pitch << std::fixed << std::setprecision(1) << std::showpos << note.name() << " " << note.cents() << " ct" << std::endl;
		pitchLabel_.setText(pitch.str(), dontSendNotification);
	}
	else {
		pitchLabel_.setText("", dontSendNotification);
	}
}

void ChannelController::setUpdateHandler(std::function<void(double, JammerNetzChannelTarget)> updateHandler)
{
	updateHandler_ = std::move(updateHandler);
}

bool ChannelController::isVolumeSliderBeingDragged() const
{
	return volumeSliderDragged_;
}

JammerNetzChannelTarget ChannelController::getCurrentTarget() const
{
	JammerNetzChannelTarget target;
	switch (channelType_.getSelectedItemIndex()) {
	case 0: target = Mute; break;
	case 1: target = Left; break;
	case 2: target = Right; break;
	case 3: target = Mono; break;
	case 4: target = SendMono; break;
	case 5: target = SendLeft; break;
	case 6: target = SendRight; break;
	default:
		jassert(false);
		target = Mute;
	}
	return target;
}

float ChannelController::getCurrentVolume() const
{
	jassert(hasVolume_);
	return ((float) volumeSlider_.getValue()) / 100.0f;
}

double ChannelController::geCurrentRMSinDecible() const
{
	if (meterSource_) {
		return 20.0 * std::log10(meterSource_->getRMSLevel(channelNo_));
	}
	return -100.0;
}

void ChannelController::enableVolumeSlider(bool enabled)
{
	volumeSlider_.setEnabled(enabled);
}

void ChannelController::enableTargetSelector(bool enabled)
{
	channelType_.setEnabled(enabled);
}

void ChannelController::bindControls()
{
	ValueTree &data = Data::instance().get();
	auto mixer = data.getOrCreateChildWithName(VALUE_MIXER, nullptr);
	auto channelSettings = mixer.getOrCreateChildWithName(id_, nullptr);
	if (!channelSettings.hasProperty(VALUE_VOLUME)) {
		channelSettings.setProperty(VALUE_VOLUME, 100.0f, nullptr);
	}
	volumeSlider_.getValueObject().referTo(channelSettings.getPropertyAsValue(VALUE_VOLUME, nullptr));

	if (!channelSettings.hasProperty(VALUE_TARGET)) {
		channelSettings.setProperty(VALUE_TARGET, static_cast<int>(JammerNetzChannelTarget::Mono) + 1, nullptr);
	}
	channelType_.getSelectedIdAsValue().referTo(channelSettings.getPropertyAsValue(VALUE_TARGET, nullptr));
}

void ChannelController::comboBoxChanged(ComboBox* comboBoxThatHasChanged)
{
	jassert(hasTarget_);
	if (comboBoxThatHasChanged == &channelType_) {
		if (updateHandler_)
			updateHandler_(volumeSlider_.getValue(), getCurrentTarget());
	}
}

void ChannelController::sliderValueChanged(Slider* slider)
{
	jassert(hasVolume_);
	if (slider == &volumeSlider_) {
		if (updateHandler_)
			updateHandler_(slider->getValue(), getCurrentTarget());
	}
}

void ChannelController::sliderDragStarted(Slider* slider)
{
	if (slider == &volumeSlider_) {
		volumeSliderDragged_ = true;
	}
}

void ChannelController::sliderDragEnded(Slider* slider)
{
	if (slider == &volumeSlider_) {
		volumeSliderDragged_ = false;
		if (updateHandler_) {
			updateHandler_(slider->getValue(), getCurrentTarget());
		}
	}
}
