/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ServerStatus.h"

#include "LayoutConstants.h"
#include "Resources.h"

ServerStatus::ServerStatus()
{
	addAndMakeVisible(serverSelector_);
	PNGImageFormat reader;
	MemoryInputStream image(cloud_png, cloud_png_size, false);
	auto im = reader.decodeImage(image);
	cloudImage_.setClickingTogglesState(false);
	cloudImage_.setEnabled(false);
	cloudImage_.setImages(false, true, true, im, 1.0f, Colours::white, im, 1.0f, Colours::white, im, 0.8f, Colours::red);
	addAndMakeVisible(cloudImage_);
}

void ServerStatus::resized()
{
	auto area = getLocalBounds().reduced(kNormalInset);
	serverSelector_.setBounds(area.removeFromTop((kLineHeight + kNormalInset) *3));
	cloudImage_.setBounds(area.withSizeKeepingCentre(96, 96).withTrimmedTop(kNormalInset));
}

void ServerStatus::setConnected(bool isReceiving)
{
	cloudImage_.setToggleState(!isReceiving, dontSendNotification);
}
