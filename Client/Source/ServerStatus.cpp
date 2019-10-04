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
	cloudImage_.setImage(im);
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
