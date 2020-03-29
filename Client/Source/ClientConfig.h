/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

class ClientConfigurator : public Component,
	private Slider::Listener {
public:
	ClientConfigurator(std::function<void(int, int)> updateHandler);

	virtual void resized() override;

	// Store to and load from settings
	void fromData();
	void toData() const;

private:
	virtual void sliderValueChanged(Slider* slider) override;

	std::function<void(int, int)> updateHandler_;
	Label bufferLabel_;
	Slider bufferLength_;
	Label maxLabel_;
	Slider maxLength_;
};
