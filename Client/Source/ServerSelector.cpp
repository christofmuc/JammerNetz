/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ServerSelector.h"

#include "ServerInfo.h"

#include "Data.h"
#include "LayoutConstants.h"
#include "BuffersConfig.h"
#include "ApplicationState.h"

ServerSelector::ServerSelector() //: localhostSelected_(false), lastServer_(globalServerInfo.serverName)
{
	useLocalhost_.setButtonText("Use localhost as server");
	serverLabel_.setText("Server:", dontSendNotification);
	//ipAddress_.onReturnKey = [this]() { updateServerInfo();  };
	//ipAddress_.onEscapeKey = [this]() { ipAddress_.setText(lastServer_, dontSendNotification);  };
	portLabel_.setText("Port:", dontSendNotification);
	//port_.onReturnKey = [this]() { updateServerInfo();  };
	//port_.onEscapeKey = [this]() { port_.setText(lastPort_, dontSendNotification);  };
	connectButton_.setButtonText("Connect");
	//connectButton_.onClick = [this]() { updateServerInfo(); };

	keyLabel_.setText("Crypto file", dontSendNotification);
	browseToKey_.setButtonText("Browse...");
	browseToKey_.onClick = [this]() {
#ifdef DIGITAL_STAGE
		String cryptoKeyPath = Data::getEphemeralProperty(VALUE_CRYPTOPATH);
#else
		String cryptoKeyPath = Data::getProperty(VALUE_CRYPTOPATH);
#endif
		FileChooser fileChooser("Select crypto file to use", File(cryptoKeyPath).getParentDirectory());
		if (fileChooser.browseForFileToOpen()) {
#ifdef DIGITAL_STAGE
			Data::instance().getEphemeral().setProperty(VALUE_CRYPTOPATH, fileChooser.getResult().getFullPathName(), nullptr);
#else
			Data::instance().get().setProperty(VALUE_CRYPTOPATH, fileChooser.getResult().getFullPathName(), nullptr);
#endif
			reloadCryptoKey();
		}
	};
	//keyPath_.onEscapeKey = [this]() { keyPath_.setText(cryptoKeyPath_, dontSendNotification);  };
	keyPath_.onReturnKey = [this]() { reloadCryptoKey(); };
	loadKeyButton_.setButtonText("Load");
	loadKeyButton_.onClick = [this]() { reloadCryptoKey(); };

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

	bindControls();
}

void ServerSelector::reloadCryptoKey() {
	// This method really is just to open the dialog in the right moment - the cryptokeypath is watched by somebody else and the file is loaded somewhere else
#ifdef DIGITAL_STAGE
	String cryptoKeyPath = Data::instance().getEphemeral().getProperty(VALUE_CRYPTOPATH);
#else
	String cryptoKeyPath = Data::instance().get().getProperty(VALUE_CRYPTOPATH);
#endif
	//notify_();
	if (cryptoKeyPath.isNotEmpty()) {
		AlertWindow::showMessageBox(AlertWindow::InfoIcon, "New key loaded", "The new crypto key was loaded from " + cryptoKeyPath);
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

void ServerSelector::bindControls()
{
#ifdef DIGITAL_STAGE
	ValueTree &data = Data::instance().getEphemeral();
#else
	ValueTree& data = Data::instance().get();
#endif
	if (!data.hasProperty(VALUE_SERVER_NAME)) {
	}
	ipAddress_.getTextValue().referTo(data.getPropertyAsValue(VALUE_SERVER_NAME, nullptr));
	//lastServer_ = ipAddress_.getText();

	if (!data.hasProperty(VALUE_SERVER_PORT)) {
		data.setProperty(VALUE_SERVER_PORT, 7777, nullptr);
	}
	port_.getTextValue().referTo(data.getPropertyAsValue(VALUE_SERVER_PORT, nullptr));
	//lastPort_ = port_.getText();

	if (!data.hasProperty(VALUE_USE_LOCALHOST)) {
		data.setProperty(VALUE_USE_LOCALHOST, false, nullptr);
	}
	useLocalhost_.getToggleStateValue().referTo(data.getPropertyAsValue(VALUE_USE_LOCALHOST, nullptr));

	if (!data.hasProperty(VALUE_CRYPTOPATH)) {
		data.setProperty(VALUE_CRYPTOPATH, "", nullptr);
	}
	keyPath_.getTextValue().referTo(data.getPropertyAsValue(VALUE_CRYPTOPATH, nullptr));
}

/*void ServerSelector::buttonClicked(Button *button)
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
}*/
