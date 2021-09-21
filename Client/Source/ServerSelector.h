/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "ServerInfo.h"

class ServerSelector : public Component
{
public:
	ServerSelector();

	virtual void resized() override;

	// Store to and load from settings
	void bindControls();

private:
	void reloadCryptoKey();

	ToggleButton useLocalhost_;
	Label serverLabel_;
	Label portLabel_;
	TextEditor ipAddress_;
	TextEditor port_;
	TextButton connectButton_;
	TextButton loadKeyButton_;
	Label keyLabel_;
	TextEditor keyPath_;
	TextButton browseToKey_;

	//String lastServer_;
	//String lastPort_;
};
