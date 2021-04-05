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
	connectButton_.setButtonText("Connect");
	connectButton_.onClick = [this]() { textEditorReturnKeyPressed(ipAddress_); };

	keyLabel_.setText("Crypto file", dontSendNotification);
	browseToKey_.setButtonText("Browse...");
	browseToKey_.onClick = [this]() {
		FileChooser fileChooser("Select crypto file to use", File(cryptoKeyPath_).getParentDirectory());
		if (fileChooser.browseForFileToOpen()) {
			cryptoKeyPath_ = fileChooser.getResult().getFullPathName();
			keyPath_.setText(cryptoKeyPath_, dontSendNotification);
			reloadCryptoKey();
		}
	};
	keyPath_.onEscapeKey = [this]() { keyPath_.setText(cryptoKeyPath_, dontSendNotification);  };
	keyPath_.onReturnKey = [this]() { cryptoKeyPath_ = keyPath_.getText(); reloadCryptoKey(); };
	loadKeyButton_.setButtonText("Load");
	loadKeyButton_.onClick = [this]() { cryptoKeyPath_ = keyPath_.getText(); reloadCryptoKey(); };

	addAndMakeVisible(useLocalhost_);
	addAndMakeVisible(serverLabel_);
	addAndMakeVisible(ipAddress_);
	addAndMakeVisible(connectButton_);
	addAndMakeVisible(keyLabel_);
	addAndMakeVisible(keyPath_);
	addAndMakeVisible(browseToKey_);
	addAndMakeVisible(loadKeyButton_);
}

void ServerSelector::reloadCryptoKey() {
	ServerInfo::cryptoKeyfilePath = cryptoKeyPath_.toStdString();
	notify_();
	AlertWindow::showMessageBox(AlertWindow::InfoIcon, "New key loaded", "The new crypto key was loaded from " + cryptoKeyPath_);
}

void ServerSelector::resized()
{
	auto area = getLocalBounds();
	auto topRow = area.removeFromTop(kLineHeight);
	serverLabel_.setBounds(topRow.removeFromLeft(kLabelWidth));
	auto entryArea = topRow.removeFromLeft(kEntryBoxWidth);
	ipAddress_.setBounds(entryArea);	
	connectButton_.setBounds(topRow.removeFromLeft(kLabelWidth).withTrimmedLeft(kNormalInset));

	auto middleRow = area.removeFromTop(kLineSpacing);
	useLocalhost_.setBounds(middleRow.withTrimmedTop(kNormalInset));

	auto lowerRow = area.removeFromTop(kLineSpacing).withTrimmedTop(kNormalInset);
	keyLabel_.setBounds(lowerRow.removeFromLeft(kLabelWidth));
	loadKeyButton_.setBounds(lowerRow.removeFromRight(kLabelWidth).withTrimmedLeft(kNormalInset));
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
	 // Nothing to do, the user might click on Connect
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
