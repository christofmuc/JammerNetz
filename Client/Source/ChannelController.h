/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma	once

#include "JuceHeader.h"

#include "JammerNetzPackage.h"

class ChannelController : public Component,
	private Slider::Listener,
	private ComboBox::Listener
{
public:
	ChannelController(String const &name, String const &id, std::function<void(double, JammerNetzChannelTarget)> updateHandler, bool hasVolume = true, bool hasTarget = true);

	virtual void resized() override;

	void setMeterSource(FFAU::LevelMeterSource *meterSource, int channelNo);

	JammerNetzChannelTarget getCurrentTarget() const;
	float getCurrentVolume() const;

	// Store to and load from settings
	void fromData();
	void toData() const;

private:
	virtual void sliderValueChanged(Slider* slider) override;
	virtual void comboBoxChanged(ComboBox* comboBoxThatHasChanged) override;

	String id_;
	std::function<void(double, JammerNetzChannelTarget)> updateHandler_;
	bool hasVolume_;
	bool hasTarget_;
	Label channelName_;
	Slider volumeSlider_;
	ComboBox channelType_;
	ScopedPointer <FFAU::LevelMeterLookAndFeel> lnf_;
	FFAU::LevelMeter levelMeter_;
	//TextButton muteButton_;
};
