/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "JammerNetzPackage.h"
#include "PacketStreamQueue.h"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic ignored "-Wsign-conversion"
#endif
#include "tbb/concurrent_queue.h"
#include "tbb/concurrent_unordered_map.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#include <string>
#include <set>

class OutgoingPackage {
public:
	OutgoingPackage() : targetAddress(""), audioBlock(), sessionSetup(false) {}

	OutgoingPackage(std::string const &targetAddress_, AudioBlock const &audioBlock_, JammerNetzChannelSetup sessionSetup_) :
		targetAddress(targetAddress_), audioBlock(audioBlock_), sessionSetup(sessionSetup_) {
	}

	std::string targetAddress;
	AudioBlock audioBlock;
    JammerNetzChannelSetup sessionSetup;
};

#if WIN32
#pragma warning( push )
#pragma warning( disable : 4996 ) // Disable deprecated warning for now, as it is inside TBB
#endif
typedef tbb::concurrent_unordered_map<std::string, std::unique_ptr<PacketStreamQueue>> TPacketStreamBundle;
typedef tbb::concurrent_bounded_queue < OutgoingPackage > TOutgoingQueue;
typedef tbb::concurrent_bounded_queue<int> TMessageQueue;
#if WIN32
#pragma warning( pop )
#endif
