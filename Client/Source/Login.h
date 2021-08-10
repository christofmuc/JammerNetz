/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "DSLookAndFeel.h"
#include "DigitalStage/Auth/AuthService.h"

struct LoginData {
	std::string username;
	std::string password;
};

class LoginDialog : public Component {
public:	
	typedef std::function<void(LoginData)> TUserPasswordCallback;

	LoginDialog();
	virtual ~LoginDialog();

	virtual void resized() override;

	void setUsername(std::string const& username);
	void setPassword(std::string const& password);

	static void showDialog(TUserPasswordCallback callback);
	static void release();

	static void storeLogin(LoginData);
	static void retrieveLogin();

private:
	bool tryLogin(LoginData const &data);

	std::unique_ptr<DigitalStage::Auth::AuthService> authService_;
	TUserPasswordCallback callback_;

	Label usernameLabel_;
	Label passwordLabel_;
	TextEditor username_;
	TextEditor password_;
	Label loginError_;
	HyperlinkButton register_;
	TextButton login_;
	std::unique_ptr<std::thread> fetchLogin_;

	DSLookAndFeel dsLookAndFeel_;
};