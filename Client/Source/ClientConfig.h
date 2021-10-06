/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

class ClientConfigurator : public Component {
public:
	ClientConfigurator();

	virtual void resized() override;

private:
	void bindControls();

	Label bufferLabel_;
	Slider bufferLength_;
	Label maxLabel_;
	Slider maxLength_;
	ToggleButton useFEC_;
};
