/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "JammerNetzPackage.h"
#include "ClientState.h"

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
#include <mutex>
#include <utility>

struct PeerEndpoint {
	bool updateIfNewer(const String& newAddress, int newPort, std::uint64_t securityCounter)
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (securityCounter <= endpointCounter) return false;
		address = newAddress;
		port = newPort;
		endpointCounter = securityCounter;
		return true;
	}

	std::pair<String, int> snapshot() const
	{
		std::lock_guard<std::mutex> lock(mutex);
		return {address, port};
	}

	mutable std::mutex mutex;
	String address;
	int port{0};
	std::uint64_t endpointCounter{0};
};

class OutgoingPackage {
public:
	OutgoingPackage() : targetAddress(""), audioBlock(), sessionSetup(false), receiverProtocolVersion(JammerNetzProtocol::Current) {}

	OutgoingPackage(std::string const &targetAddress_, AudioBlock const &audioBlock_, JammerNetzChannelSetup sessionSetup_, uint16 receiverProtocolVersion_) :
		targetAddress(targetAddress_), audioBlock(audioBlock_), sessionSetup(sessionSetup_), receiverProtocolVersion(receiverProtocolVersion_) {
	}

	std::string targetAddress;
	AudioBlock audioBlock;
    JammerNetzChannelSetup sessionSetup;
	uint16 receiverProtocolVersion;
};

#if WIN32
#pragma warning( push )
#pragma warning( disable : 4996 ) // Disable deprecated warning for now, as it is inside TBB
#endif
typedef tbb::concurrent_unordered_map<std::string, std::shared_ptr<ClientState>> TPacketStreamBundle;
typedef tbb::concurrent_unordered_map<std::string, std::shared_ptr<PeerEndpoint>> TPeerEndpointMap;
typedef tbb::concurrent_bounded_queue < OutgoingPackage > TOutgoingQueue;
typedef tbb::concurrent_bounded_queue<int> TMessageQueue;
#if WIN32
#pragma warning( pop )
#endif
