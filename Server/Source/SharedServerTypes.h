/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "JammerNetzPackage.h"
#include "PacketStreamQueue.h"

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wextra-semi"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif
#include "tbb/concurrent_queue.h"
#include "tbb/concurrent_unordered_map.h"
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
#include <string>
#include <set>
#include <map>
#include <optional>

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

class ClientIdentityRegistry {
public:
	uint32 getOrAssignClientId(const std::string& endpoint)
	{
		const ScopedLock lock(lock_);
		auto found = endpointToClientId_.find(endpoint);
		if (found != endpointToClientId_.end()) {
			return found->second;
		}

		uint32 newId = nextClientId_++;
		if (newId == 0) {
			newId = nextClientId_++;
		}
		endpointToClientId_[endpoint] = newId;
		clientIdToEndpoint_[newId] = endpoint;
		return newId;
	}

	std::optional<std::string> endpointForClientId(uint32 clientId) const
	{
		const ScopedLock lock(lock_);
		auto found = clientIdToEndpoint_.find(clientId);
		if (found == clientIdToEndpoint_.end()) {
			return {};
		}
		return found->second;
	}

private:
	mutable CriticalSection lock_;
	std::map<std::string, uint32> endpointToClientId_;
	std::map<uint32, std::string> clientIdToEndpoint_;
	uint32 nextClientId_ { 1 };
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
