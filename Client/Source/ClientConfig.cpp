/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ClientConfig.h"

#include "BuffersConfig.h"
#include "Data.h"

#include "LayoutConstants.h"

ClientConfigurator::ClientConfigurator(TUpdateHandler updateHandler) : updateHandler_(updateHandler)
{
	bufferLabel_.setText("Client buffers: ", dontSendNotification);
	bufferLength_.setSliderStyle(Slider::LinearHorizontal);
	bufferLength_.setTextBoxStyle(Slider::TextBoxRight, true, 50, 30);
	bufferLength_.addListener(this);
	bufferLength_.setRange(Range<double>(1.0, 40.0), 1.0);
	maxLabel_.setText("Max buffers: ", dontSendNotification);
	maxLength_.setSliderStyle(Slider::LinearHorizontal);
	maxLength_.setTextBoxStyle(Slider::TextBoxRight, true, 50, 30);
	maxLength_.addListener(this);
	maxLength_.setRange(Range<double>(1.0, 80.0), 1.0);
	useFEC_.setButtonText("Heal");
	useFEC_.onStateChange = [this]() { sliderValueChanged(nullptr);  };

	addAndMakeVisible(bufferLabel_);
	addAndMakeVisible(bufferLength_);
	addAndMakeVisible(maxLabel_);
	addAndMakeVisible(maxLength_);
	addAndMakeVisible(useFEC_);
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

void ClientConfigurator::sliderValueChanged(Slider*)
{
	updateHandler_((int) bufferLength_.getValue(), (int) maxLength_.getValue(), useFEC_.getToggleState());
}

void ClientConfigurator::fromData()
{
	ValueTree &data = Data::instance().get();
	ValueTree clientConfig = data.getOrCreateChildWithName(Identifier("BufferConfig"), nullptr);
	bufferLength_.setValue(clientConfig.getProperty("minPlayoutBuffer", CLIENT_PLAYOUT_JITTER_BUFFER), dontSendNotification);
	maxLength_.setValue(clientConfig.getProperty("maxPlayoutBuffer", CLIENT_PLAYOUT_MAX_BUFFER), dontSendNotification);
	useFEC_.setToggleState(clientConfig.getProperty("useFEC", false), dontSendNotification);
	sliderValueChanged(&bufferLength_);
}

void ClientConfigurator::toData() const {
	ValueTree &data = Data::instance().get();
	ValueTree clientConfig = data.getOrCreateChildWithName(Identifier("BufferConfig"), nullptr);
	clientConfig.setProperty("minPlayoutBuffer", bufferLength_.getValue(), nullptr);
	clientConfig.setProperty("maxPlayoutBuffer", maxLength_.getValue(), nullptr);
	clientConfig.setProperty("useFEC", useFEC_.getToggleState(), nullptr);
}
