/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "JammerNetzClientInfoMessage.h"

class ServerLogger {
public:
	static void init();
	static void deinit();
	static void errorln(String const &message);
	static void printColumnHeader(int row);
	static void printStatistics(int row, std::string const &clientID, JammerNetzStreamQualityInfo quality);

private:
	static String lastMessage;
};

