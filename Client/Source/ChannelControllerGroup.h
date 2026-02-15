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
#include <optional>
#include <vector>

class ChannelControllerGroup : public Component {
public:
	using SessionVolumeChangedHandler = std::function<void(uint32, uint16, float)>;

	ChannelControllerGroup();

	void setup(std::shared_ptr<ChannelSetup> setup, FFAU::LevelMeterSource*meterSource);
	void setup(std::shared_ptr<JammerNetzChannelSetup> sessionChannels, FFAU::LevelMeterSource*meterSource);
	void setSessionVolumeChangedHandler(SessionVolumeChangedHandler handler);
	void setSessionChannelVolume(int channel, float volumePercent);
	bool isSessionVolumeSliderBeingDragged(uint32 sourceClientId, uint16 sourceChannelIndex) const;

	JammerNetzChannelTarget getCurrentTarget(int channel) const;
	float getCurrentVolume(int channel) const;
	void setPitchDisplayed(int channel, MidiNote note);
	bool isAnyVolumeSliderBeingDragged() const;

	virtual void resized();

	int numChannels() const;

private:
	OwnedArray<ChannelController> channelControllers_;
	SessionVolumeChangedHandler sessionVolumeChangedHandler_;
	std::vector<std::pair<uint32, uint16>> sessionChannelIdentities_;

	std::optional<int> findSessionChannelIndex(uint32 sourceClientId, uint16 sourceChannelIndex) const;
};
