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

	keyLabel_.setText("Crypto file", dontSendNotification);
	browseToKey_.setButtonText("Browse...");
	browseToKey_.onClick = [this]() {
		FileChooser fileChooser("Select crypto file to use", File(cryptoKeyPath_).getParentDirectory());
		if (fileChooser.browseForFileToOpen()) {
			cryptoKeyPath_ = fileChooser.getResult().getFullPathName();
			ServerInfo::cryptoKeyfilePath = cryptoKeyPath_.toStdString();
			keyPath_.setText(cryptoKeyPath_, dontSendNotification);
			notify_();
		}
	};

	addAndMakeVisible(useLocalhost_);
	addAndMakeVisible(serverLabel_);
	addAndMakeVisible(ipAddress_);
	addAndMakeVisible(keyLabel_);
	addAndMakeVisible(keyPath_);
	addAndMakeVisible(browseToKey_);
}

void ServerSelector::resized()
{
	auto area = getLocalBounds();
	auto topRow = area.removeFromTop(kLineHeight);
	serverLabel_.setBounds(topRow.removeFromLeft(kLabelWidth));
	auto entryArea = topRow.removeFromLeft(kEntryBoxWidth);
	entryArea.setHeight(kLineHeight);
	ipAddress_.setBounds(entryArea);
	useLocalhost_.setBounds(topRow);
	auto lowerRow = area;
	keyLabel_.setBounds(lowerRow.removeFromLeft(kLabelWidth));
	browseToKey_.setBounds(lowerRow.removeFromRight(kLabelWidth).withTrimmedLeft(kNormalInset));
	keyPath_.setBounds(lowerRow);
}

void ServerSelector::fromData()
{
	ValueTree &data = Data::instance().get();
	if (data.hasProperty("ServerName")) {
		ipAddress_.setText(data.getProperty("ServerName"), true);
		lastServer_ = ipAddress_.getText();
	}
	if (data.hasProperty("UseLocalhost")) {
		useLocalhost_.setToggleState(data.getProperty("UseLocalhost"), dontSendNotification);
	}
	if (data.hasProperty("CryptoFilePath")) {
		keyPath_.setText(data.getProperty("CryptoFilePath"), true);
		cryptoKeyPath_ = keyPath_.getText();
		ServerInfo::cryptoKeyfilePath = cryptoKeyPath_.toStdString(); // TODO This is ugly double data
	}
	buttonClicked(&useLocalhost_);
}

void ServerSelector::toData() const
{
	ValueTree &data = Data::instance().get();
	data.setProperty("ServerName", ipAddress_.getText(), nullptr);
	data.setProperty("UseLocalhost", useLocalhost_.getToggleState(), nullptr);
	data.setProperty("CryptoFilePath", keyPath_.getText(), nullptr);
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
