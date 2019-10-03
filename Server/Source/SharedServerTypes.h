/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "JammerNetzPackage.h"
#include "PacketStreamQueue.h"

#include "tbb/concurrent_queue.h"
#include "tbb/concurrent_unordered_map.h"

#include <set>

struct OutgoingPackage {
	OutgoingPackage() = default;
	OutgoingPackage(std::string const &targetAddress, AudioBlock const &audioBlock) :
		targetAddress(targetAddress), audioBlock(audioBlock) {
	}

	std::string targetAddress;
	AudioBlock audioBlock;
};

typedef tbb::concurrent_unordered_map<std::string, std::unique_ptr<PacketStreamQueue>> TPacketStreamBundle;
typedef tbb::concurrent_bounded_queue < OutgoingPackage > TOutgoingQueue;
typedef tbb::concurrent_bounded_queue<int> TMessageQueue;

