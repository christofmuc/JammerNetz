/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ClientConfig.h"

#include "BuffersConfig.h"
#include "Data.h"

#include "LayoutConstants.h"

ClientConfigurator::ClientConfigurator(std::function<void(int, int, int)> updateHandler) : updateHandler_(updateHandler)
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
	flareLabel_.setText("Fire Flares: ", dontSendNotification);
	flares_.setSliderStyle(Slider::LinearHorizontal);
	flares_.setTextBoxStyle(Slider::TextBoxRight, true, 50, 30);
	flares_.addListener(this);
	flares_.setRange(Range<double>(0.0, 4.0), 1.0);

	addAndMakeVisible(bufferLabel_);
	addAndMakeVisible(bufferLength_);
	addAndMakeVisible(maxLabel_);
	addAndMakeVisible(maxLength_);
	addAndMakeVisible(flareLabel_);
	addAndMakeVisible(flares_);
}

void ClientConfigurator::resized()
{
	auto area = getLocalBounds().reduced(kNormalInset);
	auto row1 = area.removeFromTop(kLineSpacing);
	bufferLabel_.setBounds(row1.removeFromLeft(kLabelWidth));
	bufferLength_.setBounds(row1.removeFromLeft(kSliderWithBoxWidth));
	auto row2 = area.removeFromTop(kLineSpacing);
	maxLabel_.setBounds(row2.removeFromLeft(kLabelWidth));
	maxLength_.setBounds(row2.removeFromLeft(kSliderWithBoxWidth));
	auto row3 = area.removeFromTop(kLineSpacing);
	flareLabel_.setBounds(row3.removeFromLeft(kLabelWidth));
	flares_.setBounds(row3.removeFromLeft(kSliderWithBoxWidth));
}

void ClientConfigurator::sliderValueChanged(Slider*)
{
	updateHandler_(bufferLength_.getValue(), maxLength_.getValue(), flares_.getValue());
}

void ClientConfigurator::fromData()
{
	ValueTree &data = Data::instance().get();
	ValueTree clientConfig = data.getOrCreateChildWithName(Identifier("BufferConfig"), nullptr);
	bufferLength_.setValue(clientConfig.getProperty("minPlayoutBuffer", CLIENT_PLAYOUT_JITTER_BUFFER), dontSendNotification);
	maxLength_.setValue(clientConfig.getProperty("maxPlayoutBuffer", CLIENT_PLAYOUT_MAX_BUFFER), dontSendNotification);
	flares_.setValue(clientConfig.getProperty("numFlares", 2), dontSendNotification);
	sliderValueChanged(&bufferLength_);
}

void ClientConfigurator::toData() const {
	ValueTree &data = Data::instance().get();
	ValueTree clientConfig = data.getOrCreateChildWithName(Identifier("BufferConfig"), nullptr);
	clientConfig.setProperty("minPlayoutBuffer", bufferLength_.getValue(), nullptr);
	clientConfig.setProperty("maxPlayoutBuffer", maxLength_.getValue(), nullptr);
	clientConfig.setProperty("numFlares", flares_.getValue(), nullptr);
}
