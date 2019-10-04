/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ServerSelector.h"

#include "ServerInfo.h"

#include "Data.h"
#include "LayoutConstants.h"

ServerSelector::ServerSelector(std::function<void()> notify) : localhostSelected_(false), lastServer_(ServerInfo::serverName), notify_(notify)
{
	useLocalhost_.setButtonText("Use localhost as server");
	useLocalhost_.addListener(this);
	serverLabel_.setText("Server:", dontSendNotification);	
	ipAddress_.setText(lastServer_, dontSendNotification);
	ipAddress_.addListener(this);

	addAndMakeVisible(useLocalhost_);
	addAndMakeVisible(serverLabel_);
	addAndMakeVisible(ipAddress_);
}

void ServerSelector::resized()
{
	auto area = getLocalBounds();
	serverLabel_.setBounds(area.removeFromLeft(kLabelWidth));
	auto entryArea = area.removeFromLeft(kEntryBoxWidth);
	entryArea.setHeight(kLineHeight);
	ipAddress_.setBounds(entryArea);
	useLocalhost_.setBounds(area);
}

void ServerSelector::fromData()
{
	ValueTree &data = Data::instance().get();
	if (data.hasProperty("ServerName")) {
		ipAddress_.setText(data.getProperty("ServerName"), true);
	}
	if (data.hasProperty("UseLocalhost")) {
		useLocalhost_.setToggleState(data.getProperty("UseLocalhost"), sendNotificationAsync);
	}
}

void ServerSelector::toData() const
{
	ValueTree &data = Data::instance().get();
	data.setProperty("ServerName", ipAddress_.getText(), nullptr);
	data.setProperty("UseLocalhost", useLocalhost_.getToggleState(), nullptr);
}

void ServerSelector::textEditorReturnKeyPressed(TextEditor& editor)
{
	lastServer_ = editor.getText();
	useLocalhost_.unfocusAllComponents();
	ServerInfo::serverName = lastServer_.toStdString();
	notify_();
}

void ServerSelector::textEditorFocusLost(TextEditor&)
{
	ipAddress_.setText(lastServer_, dontSendNotification);
}

void ServerSelector::buttonClicked(Button *button)
{
	localhostSelected_ = button->getToggleState();
	ipAddress_.setEnabled(!localhostSelected_);
	if (localhostSelected_) {
		ServerInfo::serverName = "127.0.0.1";
	}
	else {
		ServerInfo::serverName = lastServer_.toStdString();
	}
	notify_();
}
