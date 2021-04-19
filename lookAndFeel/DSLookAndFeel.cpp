#include "DSLookAndFeel.h"

#include "Resources.h"

DSLookAndFeel::DSLookAndFeel() : LookAndFeel_V4(
	  { 0xFF121212, // windowBackground. DS: Level1-Black
		0xFF393939, // widgetBackground, DS: Level4-Grey
		0xFFF4F4F4, // menuBackground, DS: Level 5-White
		0xFF393939, // outline, not used in DS, make it widgetBackground
		0xFFF4F4F4, // defaultText, DS: Text
		0xFF9A9A9A, // defaultFill, not used in JammerNetz (would be Scrollbars e.g.), DS: use TextSoft
		0x99F4F4F4, // highlightedText, DS: 60% TextColor
		0xFF6F92F8, // highlightedFill, DS: Button-active
		0xFF121212 // menuText, DS: Text-inverted
	})
{
	// Load background SVG with grey gradient
	background_ = Drawable::createFromImageData(AppView_BG_Gradient_svg, AppView_BG_Gradient_svg_size);

	// Setup level meters
	setupDefaultMeterColours();
	setColour(FFAU::LevelMeter::lmTicksColour, Colour(0xff9a9a9a));
	setColour(FFAU::LevelMeter::lmMeterGradientLowColour, Colour(0xff012340));
	setColour(FFAU::LevelMeter::lmMeterGradientMidColour, Colour(0xff6e1541)); // Measured myself creating a Photoshop gradient from low to max
	setColour(FFAU::LevelMeter::lmMeterGradientMaxColour, Colour(0xfff20544));
	updateMeterGradients();

	// Load Logo
	PNGImageFormat reader;
	MemoryInputStream image(stage_web_white_png, stage_web_white_png_size, false);
	logo_ = reader.decodeImage(image);
}

juce::Drawable * DSLookAndFeel::backgroundGradient() const
{
	return background_.get();
}

juce::Image DSLookAndFeel::logo() const
{
	return logo_;
}
