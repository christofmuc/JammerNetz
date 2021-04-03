/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once


#include "JuceHeader.h"

#include "ChannelController.h"
#include "DeviceSelector.h"

#include "IncludeFFMeters.h"

class ChannelControllerGroup : public Component {
public:
	ChannelControllerGroup();

	void setup(std::shared_ptr<ChannelSetup> setup, FFAU::LevelMeterSource*meterSource, std::function<void(std::shared_ptr<ChannelSetup>)> callback);
	void setup(std::shared_ptr<JammerNetzChannelSetup> sessionChannels);

	JammerNetzChannelTarget getCurrentTarget(int channel) const;
	float getCurrentVolume(int channel) const;
	void setPitchDisplayed(int channel, MidiNote note);

	virtual void resized();

	void toData();

private:
	OwnedArray<ChannelController> channelControllers_;
};
