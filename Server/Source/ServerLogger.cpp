/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ServerLogger.h"


void ServerLogger::errorln(String const &message)
{
	if (message != lastMessage) {
		std::cerr << message << std::endl;
		lastMessage = message;
	}
}

juce::String ServerLogger::lastMessage;

