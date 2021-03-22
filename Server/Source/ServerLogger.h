/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

class ServerLogger {
public:
	static void errorln(String const &message);

private:
	static String lastMessage;
};

