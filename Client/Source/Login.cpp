/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "Login.h"

#include "DigitalStage/Api/Client.h"

#include "keychain/keychain.h"

#include "DSConfig.h"

#include "LayoutConstants.h"

// used to identify the password in the OS credentials storage
const std::string kKeychainPackage = "org.digital-stage.jammernetz";
const std::string kKeychainServiceUser = "digital-stage-user";
const std::string kKeychainService = "digital-stage-auth";

static std::unique_ptr<LoginDialog> sLoginDialog;
static juce::DialogWindow* sWindow_;

LoginDialog::LoginDialog() : password_("passwordEntry", 0x2022)
{
	setLookAndFeel(&dsLookAndFeel_);

	DigitalStage::Auth::string_t authServer(DS_AUTH_SERVER.begin(), DS_AUTH_SERVER.end());
	authService_ = std::make_unique<DigitalStage::Auth::AuthService>(authServer);

	usernameLabel_.setText("User name", dontSendNotification);
	addAndMakeVisible(usernameLabel_);
	passwordLabel_.setText("Password", dontSendNotification);
	addAndMakeVisible(passwordLabel_);
	addAndMakeVisible(&username_);
	addAndMakeVisible(&password_);
	addAndMakeVisible(loginError_);
	loginError_.setVisible(false);

	register_.setButtonText("Register");
	register_.setURL(juce::URL(DS_REGISTRATION_URL));
	addAndMakeVisible(register_);
	login_.setButtonText("Login");
	login_.onClick = [this]() {
		loginError_.setVisible(false);
		std::string userName = username_.getText().toStdString();
		std::string password = password_.getText().toStdString();
		storeLogin({ userName, password });
		if (tryLogin({ userName, password })) {
			sWindow_->exitModalState(1);
			if (callback_) {
				callback_({ userName, password, apiToken_ });
			}
		}
		else {
			loginError_.setText("Login error - wrong user name or password", dontSendNotification);
			loginError_.setVisible(true);
		}
	};
	addAndMakeVisible(login_);

	//fetchLogin_ = std::make_unique<std::thread>(&LoginDialog::retrieveStoredPasswordFromKeychain, this);

	setSize(400, 300);

	retrieveLogin();
}

LoginDialog::~LoginDialog()
{
	Component::setLookAndFeel(nullptr);
	fetchLogin_.reset();
}

void LoginDialog::release() {
	sLoginDialog.reset();
}

void LoginDialog::storeLogin(LoginData loginData)
{
	Thread::launch([loginData]() {
		keychain::Error error{};
		std::string user = juce::SystemStats::getLogonName().toStdString();
		keychain::setPassword(kKeychainPackage, kKeychainService, user, loginData.password, error);
		keychain::setPassword(kKeychainPackage, kKeychainServiceUser, user, loginData.username, error);
	});
}

void LoginDialog::retrieveLogin()
{
	Thread::launch([]() {
		keychain::Error error{};
		std::string user = juce::SystemStats::getLogonName().toStdString();
		auto password = keychain::getPassword(kKeychainPackage, kKeychainService, user, error);
		if (!error) {
			sLoginDialog->setPassword(password);
		}
		auto username = keychain::getPassword(kKeychainPackage, kKeychainServiceUser, user, error);
		if (!error) {
			sLoginDialog->setUsername(username);
		}
	});
}

void LoginDialog::resized()
{
	auto area = getLocalBounds().reduced(kNormalInset);
	usernameLabel_.setBounds(area.removeFromTop(kLineHeight));
	username_.setBounds(area.removeFromTop(kLineSpacing).withTrimmedTop(kNormalInset));
	passwordLabel_.setBounds(area.removeFromTop(kLineSpacing).withTrimmedTop(kNormalInset));
	password_.setBounds(area.removeFromTop(kLineSpacing).withTrimmedTop(kNormalInset));
	loginError_.setBounds(area.removeFromTop(kLineSpacing).withTrimmedTop(kNormalInset));
	auto buttonrow = area.removeFromBottom(kLineHeight);
	login_.setBounds(buttonrow.removeFromLeft(kLabelWidth));
	register_.setBounds(buttonrow.removeFromRight(kLabelWidth));
}

void LoginDialog::setUsername(std::string const& username)
{
	if (sLoginDialog->username_.getText().isEmpty()) {
		// Do this on the GUI thread
		MessageManager::callAsync([username]() {
			sLoginDialog->username_.setText(username, true);
		});
	}
}

void LoginDialog::setPassword(std::string const& password)
{
	if (sLoginDialog->password_.getText().isEmpty()) {
		// Do this on the GUI thread
		MessageManager::callAsync([password]() {
			sLoginDialog->password_.setText(password, true);
		});
	}
}

void LoginDialog::triggerCallback()
{
	callback_(LoginData{"", "", apiToken_});
}

static void dialogClosed(int modalResult, LoginDialog* dialog)
{
	if (modalResult == 1 && dialog != nullptr) { // (must check that dialog isn't null in case it was deleted..)
		//dialog->triggerCallback();
	}
	else {
		JUCEApplication::quit();
	}
}

void LoginDialog::showDialog(TUserPasswordCallback callback)
{
	if (!sLoginDialog) {
		sLoginDialog = std::make_unique<LoginDialog>();
	}
	sLoginDialog->callback_ = callback;

	DialogWindow::LaunchOptions launcher;
	launcher.content.set(sLoginDialog.get(), false);
	launcher.componentToCentreAround = juce::TopLevelWindow::getTopLevelWindow(0)->getTopLevelComponent();
	launcher.dialogTitle = "Login to Digital Stage";
	launcher.useNativeTitleBar = false;
	launcher.dialogBackgroundColour = Colours::black;
	sWindow_ = launcher.launchAsync();
	ModalComponentManager::getInstance()->attachCallback(sWindow_, ModalCallbackFunction::forComponent(dialogClosed, sLoginDialog.get()));

}

bool LoginDialog::tryLogin(LoginData const& login)
{
	DigitalStage::Auth::string_t userName(login.username.begin(), login.username.end());
	DigitalStage::Auth::string_t passWord(login.password.begin(), login.password.end());

	apiToken_ = authService_->signInSync(userName ,passWord);
	return !apiToken_.empty();
}
