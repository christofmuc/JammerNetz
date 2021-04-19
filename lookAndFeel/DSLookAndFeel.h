#pragma once

#include "JuceHeader.h"

#include "DSLevelMeterLAF.h"

class DSLookAndFeel : public LookAndFeel_V4, public FFAU::LevelMeter::LookAndFeelMethods {
public:
	DSLookAndFeel();

	Drawable *backgroundGradient() const;
	Image logo() const;

	#include "ff_meters/ff_meters_LookAndFeelMethods.h"

private:
	std::unique_ptr<Drawable> background_;
	Image logo_;
};
