/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ServerSelector.h"

#include "ServerInfo.h"

#include "Data.h"
#include "LayoutConstants.h"

ServerSelector::ServerSelector(std::function<void()> notify) : localhostSelected_(false), lastServer_(globalServerInfo.serverName), notify_(notify)
{
	useLocalhost_.setButtonText("Use localhost as server");
	useLocalhost_.addListener(this);
	serverLabel_.setText("Server:", dontSendNotification);
	ipAddress_.setText(lastServer_, dontSendNotification);
	ipAddress_.onReturnKey = [this]() { updateServerInfo();  };
	ipAddress_.onEscapeKey = [this]() { ipAddress_.setText(lastServer_, dontSendNotification);  };
	portLabel_.setText("Port:", dontSendNotification);
	port_.setText(lastPort_, dontSendNotification);
	port_.onReturnKey = [this]() { updateServerInfo();  };
	port_.onEscapeKey = [this]() { port_.setText(lastPort_, dontSendNotification);  };
	connectButton_.setButtonText("Connect");
	connectButton_.onClick = [this]() { updateServerInfo(); };

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
	addAndMakeVisible(portLabel_);
	addAndMakeVisible(port_);
	addAndMakeVisible(connectButton_);
	addAndMakeVisible(keyLabel_);
	addAndMakeVisible(keyPath_);
	addAndMakeVisible(browseToKey_);
	addAndMakeVisible(loadKeyButton_);
}

void ServerSelector::reloadCryptoKey() {
	globalServerInfo.cryptoKeyfilePath = cryptoKeyPath_.toStdString();
	notify_();
	if (cryptoKeyPath_.isNotEmpty()) {
		AlertWindow::showMessageBox(AlertWindow::InfoIcon, "New key loaded", "The new crypto key was loaded from " + cryptoKeyPath_);
	}
	else {
		AlertWindow::showMessageBox(AlertWindow::WarningIcon, "No key loaded", "Connecting to the server sending with unencrypted audio");
	}
}

void ServerSelector::resized()
{
	auto area = getLocalBounds();
	auto topRow = area.removeFromTop(kLineHeight);
	serverLabel_.setBounds(topRow.removeFromLeft(kLabelWidth));
	auto entryArea = topRow.removeFromLeft(kEntryBoxWidth);
	ipAddress_.setBounds(entryArea);	
	portLabel_.setBounds(topRow.removeFromLeft(kLabelWidth/2));
	port_.setBounds(topRow.removeFromLeft(kEntryBoxWidth/2));
	connectButton_.setBounds(topRow.removeFromLeft(kLabelWidth).withTrimmedLeft(kNormalInset));

	auto middleRow = area.removeFromTop(kLineSpacing);
	useLocalhost_.setBounds(middleRow.withTrimmedTop(kNormalInset));

	auto lowerRow = area.removeFromTop(kLineSpacing).withTrimmedTop(kNormalInset);
	keyLabel_.setBounds(lowerRow.removeFromLeft(kLabelWidth));
	loadKeyButton_.setBounds(lowerRow.removeFromRight(kLabelWidth).withTrimmedLeft(kNormalInset));
	browseToKey_.setBounds(lowerRow.removeFromRight(kLabelWidth).withTrimmedLeft(kNormalInset));
	keyPath_.setBounds(lowerRow);
}

void ServerSelector::fromServerInfo(ServerInfo const& serverInfo)
{
	// Set the UI for the data from the ServerInfo struct
	ipAddress_.setText(serverInfo.serverName, false);
	lastServer_ = ipAddress_.getText();
	port_.setText(String(serverInfo.serverPort), false);
	lastPort_ = port_.getText();
	useLocalhost_.setToggleState(false, dontSendNotification);
	keyPath_.setText(serverInfo.cryptoKeyfilePath);
	cryptoKeyPath_ = keyPath_.getText();
	globalServerInfo.cryptoKeyfilePath = cryptoKeyPath_.toStdString(); // TODO This is ugly double data
	globalServerInfo.serverName = lastServer_.toStdString();
	globalServerInfo.serverPort = lastPort_.toStdString();
	notify_();
}

void ServerSelector::fromData()
{
	ValueTree &data = Data::instance().get();
	if (data.hasProperty("ServerName")) {
		ipAddress_.setText(data.getProperty("ServerName"), true);
		lastServer_ = ipAddress_.getText();
	}
	if (data.hasProperty("Port")) {
		port_.setText(data.getProperty("Port"), true);
	}
	else {
		port_.setText("7777", true);
	}
	lastPort_ = port_.getText();
	if (data.hasProperty("UseLocalhost")) {
		useLocalhost_.setToggleState(data.getProperty("UseLocalhost"), dontSendNotification);
	}
	if (data.hasProperty("CryptoFilePath")) {
		keyPath_.setText(data.getProperty("CryptoFilePath"), true);
		cryptoKeyPath_ = keyPath_.getText();
		globalServerInfo.cryptoKeyfilePath = cryptoKeyPath_.toStdString(); // TODO This is ugly double data
	}
	buttonClicked(&useLocalhost_);
}

void ServerSelector::toData() const
{
	ValueTree &data = Data::instance().get();
	data.setProperty("ServerName", ipAddress_.getText(), nullptr);
	data.setProperty("Port", port_.getText(), nullptr);
	data.setProperty("UseLocalhost", useLocalhost_.getToggleState(), nullptr);
	data.setProperty("CryptoFilePath", keyPath_.getText(), nullptr);
}

void ServerSelector::updateServerInfo()
{
	lastServer_ = ipAddress_.getText();
	lastPort_ = port_.getText();
	useLocalhost_.unfocusAllComponents();
	globalServerInfo.serverName = lastServer_.toStdString();
	globalServerInfo.serverPort = lastPort_.toStdString();
	notify_();
}

void ServerSelector::buttonClicked(Button *button)
{
	localhostSelected_ = button->getToggleState();
	ipAddress_.setEnabled(!localhostSelected_);
	if (localhostSelected_) {
		globalServerInfo.serverName = "127.0.0.1";
	}
	else {
		globalServerInfo.serverName = lastServer_.toStdString();
		globalServerInfo.serverPort = lastPort_.toStdString();
	}
	notify_();
}
