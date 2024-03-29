/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "ServerSelector.h"

class ServerStatus : public Component {
public:
	ServerStatus();

	void setConnected(bool isReceivingData);

	virtual void resized() override;

private:
	ServerSelector serverSelector_;
	ImageButton cloudImage_;
};
