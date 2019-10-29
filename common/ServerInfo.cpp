/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ServerInfo.h"

#include "BuffersConfig.h"

std::string ServerInfo::serverName = "127.0.0.1";
int ServerInfo::serverPort = 7777;

// For now, we keep our life simple by requesting the same sample rate (sane) from all clients
// and also the same buffer size (that would be workable with appropriate ring buffers)
int ServerInfo::bufferSize = SAMPLE_BUFFER_SIZE;
int ServerInfo::sampleRate = SAMPLE_RATE;
	