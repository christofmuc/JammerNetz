/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma	once

#include "JuceHeader.h"

#include "JammerNetzPackage.h"
#include "MidiNote.h"

#include "IncludeFFMeters.h"

class ChannelController : public Component,
	private Slider::Listener,
	private ComboBox::Listener
{
public:
	ChannelController(String const &name, String const &id, bool hasVolume = true, bool hasTarget = true, bool hasPitch = false);

	virtual void resized() override;

	void setMeterSource(FFAU::LevelMeterSource *meterSource, int channelNo);
	void setVolume(float volume);
	void setTarget(uint8 target);
	void setPitchDisplayed(MidiNote note);
	void setUpdateHandler(std::function<void(double, JammerNetzChannelTarget)> updateHandler);
	bool isVolumeSliderBeingDragged() const;

	JammerNetzChannelTarget getCurrentTarget() const;
	float getCurrentVolume() const;
	double geCurrentRMSinDecible() const;

	// Enable individual parts
	void enableVolumeSlider(bool enabled);
	void enableTargetSelector(bool enabled);

private:
	void bindControls();

	virtual void sliderValueChanged(Slider* slider) override;
	virtual void sliderDragStarted(Slider* slider) override;
	virtual void sliderDragEnded(Slider* slider) override;
	virtual void comboBoxChanged(ComboBox* comboBoxThatHasChanged) override;

	String id_;
	std::function<void(double, JammerNetzChannelTarget)> updateHandler_;
	bool hasVolume_;
	bool hasTarget_;
	bool hasPitch_;
	Label channelName_;
	Slider volumeSlider_;
	ComboBox channelType_;
	std::unique_ptr<FFAU::LevelMeterLookAndFeel> lnf_;
	FFAU::LevelMeter levelMeter_;
	FFAU::LevelMeterSource *meterSource_;
	int channelNo_;
	Label pitchLabel_;
	bool volumeSliderDragged_;
	//TextButton muteButton_;
};
