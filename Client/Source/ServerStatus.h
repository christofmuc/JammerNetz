/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "ServerSelector.h"

class ServerStatus : public Component {
public:
	ServerStatus(std::function<void()> notify);

	virtual void resized() override;

	// Store to and load from settings
	void fromData();
	void toData() const;

private:
	ServerSelector serverSelector_;
	ImageComponent cloudImage_;
};
