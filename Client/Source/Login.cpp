/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "Login.h"

#include "DigitalStage/Api/Client.h"
#include "DigitalStage/Auth/AuthService.h"

#include "DSConfig.h"

LoginDialog::LoginDialog()
{
	DigitalStage::Auth::string_t authServer(DS_AUTH_SERVER.begin(), DS_AUTH_SERVER.end());
	auto authService = DigitalStage::Auth::AuthService(authServer);

}

void LoginDialog::showDialog()
{

}
