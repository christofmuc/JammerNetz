/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

struct ServerInfo {
	static std::string serverName; //TODO this is no longer read only, should somehow lock access to it!
	static int serverPort;
	static int bufferSize; // Should be the same for all clients
	static int sampleRate; // Should be the same for all clients
};
