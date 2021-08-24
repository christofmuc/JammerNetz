/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

struct ServerInfo {
	std::string serverName; //TODO this is no longer read only, should somehow lock access to it!
	std::string serverPort;
	std::string cryptoKeyfilePath;
	int bufferSize; // Should be the same for all clients
	int sampleRate; // Should be the same for all clients
};

extern ServerInfo globalServerInfo;
