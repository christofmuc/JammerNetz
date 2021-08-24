/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "ServerInfo.h"

class ServerSelector : public Component,
	private Button::Listener
{
public:
	ServerSelector(std::function<void()> notify);

	virtual void resized() override;

	// From ServerInfo struct
	void fromServerInfo(ServerInfo const& serverInfo);

	// Store to and load from settings
	void fromData();
	void toData() const;

private:
	void reloadCryptoKey();

	virtual void buttonClicked(Button*) override;
	virtual void updateServerInfo();

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

	bool localhostSelected_;
	String lastServer_;
	String lastPort_;
	String cryptoKeyPath_;

	std::function<void()> notify_;
};
