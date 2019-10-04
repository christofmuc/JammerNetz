/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "MidiClocker.h"

class BPMDisplay : public Component {
public:
	BPMDisplay(std::weak_ptr<MidiClocker> clocker);

	virtual void resized() override;

private:
	std::unique_ptr<Timer> timer_;	
	Label bpmText_;
};
