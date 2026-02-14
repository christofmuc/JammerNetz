/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once


#include "JuceHeader.h"

#include "ChannelController.h"
#include "DeviceSelector.h"

#include "IncludeFFMeters.h"

#include "AudioService.h"

class ChannelControllerGroup : public Component {
public:
	using SessionVolumeChangedHandler = std::function<void(uint32, uint16, float)>;

	ChannelControllerGroup();

	void setup(std::shared_ptr<ChannelSetup> setup, FFAU::LevelMeterSource*meterSource);
	void setup(std::shared_ptr<JammerNetzChannelSetup> sessionChannels, FFAU::LevelMeterSource*meterSource);
	void setSessionVolumeChangedHandler(SessionVolumeChangedHandler handler);

	JammerNetzChannelTarget getCurrentTarget(int channel) const;
	float getCurrentVolume(int channel) const;
	void setPitchDisplayed(int channel, MidiNote note);
	bool isAnyVolumeSliderBeingDragged() const;

	virtual void resized();

	int numChannels() const;

private:
	void enableClientSideControls(bool enabled);

	OwnedArray<ChannelController> channelControllers_;
	SessionVolumeChangedHandler sessionVolumeChangedHandler_;
};
