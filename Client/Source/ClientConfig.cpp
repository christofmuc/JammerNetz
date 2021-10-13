/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ClientConfig.h"

#include "BuffersConfig.h"
#include "Data.h"
#include "ApplicationState.h"

#include "LayoutConstants.h"

ClientConfigurator::ClientConfigurator()
{
	bufferLabel_.setText("Client buffers: ", dontSendNotification);
	bufferLength_.setSliderStyle(Slider::LinearHorizontal);
	bufferLength_.setTextBoxStyle(Slider::TextBoxRight, true, 50, 30);
	bufferLength_.setRange(Range<double>(1.0, 40.0), 1.0);
	maxLabel_.setText("Max buffers: ", dontSendNotification);
	maxLength_.setSliderStyle(Slider::LinearHorizontal);
	maxLength_.setTextBoxStyle(Slider::TextBoxRight, true, 50, 30);
	maxLength_.setRange(Range<double>(1.0, 80.0), 1.0);
	useFEC_.setButtonText("Heal");

	addAndMakeVisible(bufferLabel_);
	addAndMakeVisible(bufferLength_);
	addAndMakeVisible(maxLabel_);
	addAndMakeVisible(maxLength_);
	addAndMakeVisible(useFEC_);

	bindControls();
}

void ClientConfigurator::resized()
{
	auto area = getLocalBounds();
	auto row1 = area.removeFromTop(kLineSpacing).withTrimmedTop(kNormalInset);
	bufferLabel_.setBounds(row1.removeFromLeft(kLabelWidth));
	useFEC_.setBounds(row1.removeFromRight(kLabelWidth));
	bufferLength_.setBounds(row1.removeFromLeft(kSliderWithBoxWidth));
	auto row2 = area.removeFromTop(kLineSpacing).withTrimmedTop(kNormalInset);
	maxLabel_.setBounds(row2.removeFromLeft(kLabelWidth));
	maxLength_.setBounds(row2.removeFromLeft(kSliderWithBoxWidth));
}

void ClientConfigurator::bindControls()
{
	ValueTree &data = Data::instance().get();
	if (!data.hasProperty(VALUE_MIN_PLAYOUT_BUFFER)) {
		data.setProperty(VALUE_MIN_PLAYOUT_BUFFER, CLIENT_PLAYOUT_JITTER_BUFFER, nullptr);
	}
	if (!data.hasProperty(VALUE_MAX_PLAYOUT_BUFFER)) {
		data.setProperty(VALUE_MAX_PLAYOUT_BUFFER, CLIENT_PLAYOUT_MAX_BUFFER, nullptr);
	}
	if (!data.hasProperty(VALUE_USE_FEC)) {
		data.setProperty(VALUE_USE_FEC, false, nullptr);
	}
	bufferLength_.getValueObject().referTo(data.getPropertyAsValue(VALUE_MIN_PLAYOUT_BUFFER, nullptr));
	maxLength_.getValueObject().referTo(data.getPropertyAsValue(VALUE_MAX_PLAYOUT_BUFFER, nullptr));
	useFEC_.getToggleStateValue().referTo(data.getPropertyAsValue(VALUE_USE_FEC, nullptr));
}
