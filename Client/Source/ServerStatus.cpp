/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ServerStatus.h"

#include "LayoutConstants.h"

ServerStatus::ServerStatus(std::function<void()> notify) : serverSelector_(notify)
{
	addAndMakeVisible(serverSelector_);
	PNGImageFormat reader;
	MemoryInputStream image(BinaryData::cloud_png, BinaryData::cloud_pngSize, false);
	auto im = reader.decodeImage(image);
	cloudImage_.setClickingTogglesState(false);
	//cloudImage_.setEnabled(false);
	cloudImage_.setImages(false, true, true, im, 1.0f, Colours::white, im, 1.0f, Colours::white, im, 0.8f, Colours::red);
	addAndMakeVisible(cloudImage_);
}

void ServerStatus::resized()
{
	auto area = getLocalBounds().reduced(kNormalInset);

	serverSelector_.setBounds(area.removeFromTop(kLineSpacing));
	cloudImage_.setBounds(area.withSizeKeepingCentre(128, 128));
}

void ServerStatus::fromData()
{
	serverSelector_.fromData();
}

void ServerStatus::toData() const
{
	serverSelector_.toData();
}

void ServerStatus::setConnected(bool isReceiving)
{
	cloudImage_.setToggleState(!isReceiving, dontSendNotification);
}
